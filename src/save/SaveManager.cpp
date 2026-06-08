#include "SaveManager.h"
#include "PlayerSerializer.h"
#include "RegionFile.h"
#include "../thread/ThreadPool.h"
#include "../world/chunk/Chunk.h"

#include <nlohmann/json.hpp>
#include <stb/stb_image_write.h>
#include <fstream>
#include <cstdio>

namespace save {

SaveManager::SaveManager(std::filesystem::path saveRoot)
    : m_paths(std::move(saveRoot)) {}

SaveManager::~SaveManager() {
    // Ensure all pending saves are completed before destruction
    flushPendingSaves();
}

// ---------------------------------------------------------------------------
// Level metadata
// ---------------------------------------------------------------------------

bool SaveManager::loadLevelMeta(LevelMeta& outMeta) {
    const auto path = m_paths.levelPath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        std::fprintf(stderr, "[Save] Failed to open %s\n", path.string().c_str());
        return false;
    }

    try {
        nlohmann::json j;
        file >> j;

        const int version = j.value("version", 0);
        if (version != 1) {
            std::fprintf(stderr, "[Save] Unsupported level.json version: %d\n", version);
            return false;
        }

        outMeta.seed = j["seed"].get<uint32_t>();

        // Spawn position (optional, defaults preserved)
        if (j.contains("spawn") && j["spawn"].is_array() && j["spawn"].size() >= 3) {
            outMeta.spawnX = j["spawn"][0].get<float>();
            outMeta.spawnY = j["spawn"][1].get<float>();
            outMeta.spawnZ = j["spawn"][2].get<float>();
        }

        // Time (optional)
        if (j.contains("time") && j["time"].is_object()) {
            outMeta.timeOfDay = j["time"].value("timeOfDay", 300.0f);
            outMeta.totalGameTime = j["time"].value("totalGameTime", 300.0);
            outMeta.elapsedDays = j["time"].value("elapsedDays", 0);
        }

        // Weather (optional)
        if (j.contains("weather") && j["weather"].is_object()) {
            outMeta.weatherType = j["weather"].value("type", "clear");
            outMeta.weatherWetness = j["weather"].value("wetness", 0.0f);
            outMeta.weatherStorm = j["weather"].value("storm", 0.0f);
            outMeta.weatherAerialReduction = j["weather"].value("aerialReduction", 0.55f);
        }

        // Extended metadata (optional)
        outMeta.displayName = j.value("displayName", "");
        outMeta.createdUtc = j.value("createdUtc", "");
        outMeta.lastSavedUtc = j.value("lastSavedUtc", "");
        outMeta.screenshotPath = j.value("screenshotPath", "");

        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[Save] Failed to parse level.json: %s\n", e.what());
        return false;
    }
}

void SaveManager::saveLevelMeta(const LevelMeta& meta) {
    nlohmann::json j;
    j["format"] = "mecraft.level";
    j["version"] = 1;
    j["seed"] = meta.seed;
    j["displayName"] = meta.displayName;
    j["spawn"] = {meta.spawnX, meta.spawnY, meta.spawnZ};
    j["time"] = {
        {"timeOfDay", meta.timeOfDay},
        {"totalGameTime", meta.totalGameTime},
        {"elapsedDays", meta.elapsedDays}
    };
    j["weather"] = {
        {"type", meta.weatherType},
        {"wetness", meta.weatherWetness},
        {"storm", meta.weatherStorm},
        {"aerialReduction", meta.weatherAerialReduction}
    };
    j["createdUtc"] = meta.createdUtc;
    j["lastSavedUtc"] = meta.lastSavedUtc;
    j["screenshotPath"] = meta.screenshotPath;

    const auto path = m_paths.levelPath();
    const auto tmpPath = path.string() + ".tmp";

    // Write to tmp file
    {
        std::ofstream file(tmpPath);
        if (!file.is_open()) {
            std::fprintf(stderr, "[Save] Failed to write %s\n", tmpPath.c_str());
            return;
        }
        file << j.dump(2) << '\n';
        file.flush();
        file.close();
    }

    // Atomic rename
    std::error_code ec;
    const auto bakPath = path.string() + ".bak";
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::rename(path, bakPath, ec);
        if (ec) {
            std::fprintf(stderr, "[Save] Failed to rename old level.json to .bak: %s\n",
                         ec.message().c_str());
        }
    }
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        std::fprintf(stderr, "[Save] Failed to rename tmp to level.json: %s\n",
                     ec.message().c_str());
    }
}

// ---------------------------------------------------------------------------
// Chunk I/O
// ---------------------------------------------------------------------------

RegionFile* SaveManager::getOrCreateRegion(int cx, int cz) const {
    const int rx = RegionFile::toRegionCoord(cx);
    const int rz = RegionFile::toRegionCoord(cz);
    const int64_t key = (static_cast<int64_t>(rx) << 32) | (static_cast<int64_t>(rz) & 0xFFFFFFFF);

    auto it = m_regionCache.find(key);
    if (it != m_regionCache.end()) {
        return it->second.get();
    }

    const auto path = RegionFile::regionPath(m_paths.chunksDir(), rx, rz);
    auto rf = RegionFile::open(path, rx, rz);
    if (!rf) return nullptr;

    RegionFile* ptr = rf.get();
    m_regionCache[key] = std::move(rf);
    return ptr;
}

std::shared_ptr<Chunk> SaveManager::tryLoadChunk(int cx, int cz) {
    // Try region file first
    RegionFile* region = getOrCreateRegion(cx, cz);
    if (region && region->hasChunk(cx, cz)) {
        return region->readChunk(cx, cz);
    }

    // Fall back to legacy single-file format
    const auto path = m_paths.chunkPath(cx, cz);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return nullptr;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return nullptr;

    const auto fileSize = file.tellg();
    if (fileSize <= 0) return nullptr;
    file.seekg(0);

    std::vector<uint8_t> data(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(fileSize));
    if (!file) return nullptr;

    return ChunkSerializer::deserializeFile(data.data(), data.size());
}

void SaveManager::submitSaveChunk(int cx, int cz, const Chunk& chunk) {
    // Serialize snapshot on calling thread (reads chunk data, no mutation)
    auto fileData = std::make_shared<std::vector<uint8_t>>(
        ChunkSerializer::serializeFile(chunk));

    if (!m_threadPool) {
        // No thread pool — write synchronously
        RegionFile* region = getOrCreateRegion(cx, cz);
        if (region) {
            region->writeChunk(cx, cz, chunk);
        } else {
            writeChunkFileAtomic(cx, cz, *fileData);
        }
        return;
    }

    m_pendingSaveCount.fetch_add(1, std::memory_order_relaxed);

    m_threadPool->submit([this, cx, cz, fileData]() {
        RegionFile* region = getOrCreateRegion(cx, cz);
        if (region) {
            region->writeChunkRaw(cx, cz, *fileData);
        } else {
            writeChunkFileAtomic(cx, cz, *fileData);
        }
        m_pendingSaveCount.fetch_sub(1, std::memory_order_release);
        m_saveCv.notify_all();
    }, 0);
}

void SaveManager::setThreadPool(ThreadPool* pool) {
    m_threadPool = pool;
}

void SaveManager::flushPendingSaves() {
    std::unique_lock<std::mutex> lock(m_saveMutex);
    m_saveCv.wait(lock, [this]() {
        return m_pendingSaveCount.load(std::memory_order_acquire) == 0;
    });
}

bool SaveManager::chunkFileExists(int cx, int cz) const {
    // Check region file cache
    const int rx = RegionFile::toRegionCoord(cx);
    const int rz = RegionFile::toRegionCoord(cz);
    const int64_t key = (static_cast<int64_t>(rx) << 32) | (static_cast<int64_t>(rz) & 0xFFFFFFFF);
    auto it = m_regionCache.find(key);
    if (it != m_regionCache.end() && it->second && it->second->hasChunk(cx, cz)) {
        return true;
    }

    // Check region file on disk
    const auto regionPath = RegionFile::regionPath(m_paths.chunksDir(), rx, rz);
    std::error_code ec;
    if (std::filesystem::exists(regionPath, ec)) {
        return true; // Conservative: assume chunk might be in region
    }

    // Fall back to legacy single-file check
    return m_paths.chunkFileExists(cx, cz);
}

// ---------------------------------------------------------------------------
// Player save/load
// ---------------------------------------------------------------------------

void SaveManager::saveLocalPlayer(const PlayerData& data) {
    PlayerSerializer::saveToFile(m_paths.localPlayerPath().string(), data);
}

bool SaveManager::loadLocalPlayer(PlayerData& out) {
    return PlayerSerializer::loadFromFile(m_paths.localPlayerPath().string(), out);
}

void SaveManager::savePlayer(uint32_t clientId, const PlayerData& data) {
    PlayerSerializer::saveToFile(
        (m_paths.playersDir() / (std::to_string(clientId) + ".json")).string(), data);
}

bool SaveManager::loadPlayer(uint32_t clientId, PlayerData& out) {
    return PlayerSerializer::loadFromFile(
        (m_paths.playersDir() / (std::to_string(clientId) + ".json")).string(), out);
}

void SaveManager::savePersistentEntities(const std::vector<PersistentEntityData>& entities) {
    nlohmann::json root;
    root["version"] = 1;
    root["entities"] = nlohmann::json::array();

    for (const PersistentEntityData& entity : entities) {
        if (entity.type.empty()) {
            continue;
        }
        if (entity.type != "minecraft:item" && entity.health <= 0) {
            continue;
        }
        if (entity.type == "minecraft:item" && (entity.itemId == 0 || entity.stackCount == 0)) {
            continue;
        }

        nlohmann::json j;
        j["type"] = entity.type;
        j["position"] = {entity.posX, entity.posY, entity.posZ};
        j["velocity"] = {entity.velX, entity.velY, entity.velZ};
        if (entity.type == "minecraft:item") {
            j["item"] = {{"id", entity.itemId}, {"count", entity.stackCount}};
            j["dropId"] = entity.dropId;
            j["bounds"] = {entity.halfExtentX, entity.halfExtentY, entity.halfExtentZ};
            j["spin"] = {{"yaw", entity.yaw}, {"speed", entity.spinSpeed}};
            j["lifetime"] = {{"age", entity.ageSeconds}, {"max", entity.lifeTimeSeconds}};
            j["grounded"] = entity.grounded;
        } else {
            j["yaw"] = entity.yaw;
            j["pitch"] = entity.pitch;
            j["health"] = {{"current", entity.health}, {"max", entity.healthMax}};
        }
        root["entities"].push_back(std::move(j));
    }

    const auto path = m_paths.overworldEntitiesPath();
    const auto tmpPath = path.string() + ".tmp";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    {
        std::ofstream file(tmpPath);
        if (!file.is_open()) {
            std::fprintf(stderr, "[Save] Failed to write %s\n", tmpPath.c_str());
            return;
        }
        file << root.dump(2) << '\n';
        file.flush();
    }

    const auto bakPath = path.string() + ".bak";
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::rename(path, bakPath, ec);
        ec.clear();
    }
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        std::fprintf(stderr, "[Save] Failed to rename entity file: %s\n", ec.message().c_str());
    }
}

bool SaveManager::loadPersistentEntities(std::vector<PersistentEntityData>& out) {
    out.clear();
    const auto path = m_paths.overworldEntitiesPath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    try {
        nlohmann::json root;
        file >> root;

        const int version = root.value("version", 0);
        if (version != 1 || !root.contains("entities") || !root["entities"].is_array()) {
            std::fprintf(stderr, "[Save] Unsupported entity save file\n");
            return false;
        }

        for (const auto& j : root["entities"]) {
            if (!j.is_object()) {
                continue;
            }

            PersistentEntityData entity;
            entity.type = j.value("type", "");
            if (entity.type.empty()) {
                continue;
            }

            if (j.contains("position") && j["position"].is_array() && j["position"].size() >= 3) {
                entity.posX = j["position"][0].get<float>();
                entity.posY = j["position"][1].get<float>();
                entity.posZ = j["position"][2].get<float>();
            }
            if (j.contains("velocity") && j["velocity"].is_array() && j["velocity"].size() >= 3) {
                entity.velX = j["velocity"][0].get<float>();
                entity.velY = j["velocity"][1].get<float>();
                entity.velZ = j["velocity"][2].get<float>();
            }
            entity.yaw = j.value("yaw", entity.yaw);
            entity.pitch = j.value("pitch", entity.pitch);
            entity.dropId = j.value("dropId", entity.dropId);
            entity.grounded = j.value("grounded", entity.grounded);

            if (j.contains("health") && j["health"].is_object()) {
                entity.health = j["health"].value("current", entity.health);
                entity.healthMax = j["health"].value("max", entity.healthMax);
            }
            if (j.contains("item") && j["item"].is_object()) {
                entity.itemId = j["item"].value("id", entity.itemId);
                entity.stackCount = j["item"].value("count", entity.stackCount);
            } else {
                entity.itemId = j.value("itemId", entity.itemId);
                entity.stackCount = j.value("stackCount", entity.stackCount);
            }
            if (j.contains("bounds") && j["bounds"].is_array() && j["bounds"].size() >= 3) {
                entity.halfExtentX = j["bounds"][0].get<float>();
                entity.halfExtentY = j["bounds"][1].get<float>();
                entity.halfExtentZ = j["bounds"][2].get<float>();
            }
            if (j.contains("spin") && j["spin"].is_object()) {
                entity.yaw = j["spin"].value("yaw", entity.yaw);
                entity.spinSpeed = j["spin"].value("speed", entity.spinSpeed);
            }
            if (j.contains("lifetime") && j["lifetime"].is_object()) {
                entity.ageSeconds = j["lifetime"].value("age", entity.ageSeconds);
                entity.lifeTimeSeconds = j["lifetime"].value("max", entity.lifeTimeSeconds);
            }

            if (entity.type == "minecraft:item") {
                if (entity.itemId != 0 && entity.stackCount != 0) {
                    out.push_back(entity);
                }
            } else if (entity.health > 0) {
                out.push_back(entity);
            }
        }

        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[Save] Failed to read entity file: %s\n", e.what());
        out.clear();
        return false;
    }
}

// ---------------------------------------------------------------------------
// Timestamp and screenshot
// ---------------------------------------------------------------------------

std::string SaveManager::currentUtcTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                  utc.tm_hour, utc.tm_min, utc.tm_sec);
    return buf;
}

void SaveManager::saveScreenshot(const uint8_t* rgbData, int width, int height) {
    const auto path = m_paths.screenshotPath();
    const auto tmpPath = path.string() + ".tmp";

    // Convert RGB to RGBA (alpha = 255)
    const int pixelCount = width * height;
    std::vector<uint8_t> rgba(pixelCount * 4);
    for (int i = 0; i < pixelCount; ++i) {
        rgba[i * 4 + 0] = rgbData[i * 3 + 0]; // R
        rgba[i * 4 + 1] = rgbData[i * 3 + 1]; // G
        rgba[i * 4 + 2] = rgbData[i * 3 + 2]; // B
        rgba[i * 4 + 3] = 255;                 // A
    }

    // Write PNG using stb_image_write
    const int stride = width * 4;
    const int result = stbi_write_png(tmpPath.c_str(), width, height, 4, rgba.data(), stride);
    if (result == 0) {
        std::fprintf(stderr, "[Save] Failed to write screenshot PNG\n");
        return;
    }

    // Atomic rename
    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        std::fprintf(stderr, "[Save] Failed to rename screenshot: %s\n", ec.message().c_str());
    }
}

// ---------------------------------------------------------------------------
// Atomic file write
// ---------------------------------------------------------------------------

void SaveManager::writeChunkFileAtomic(int cx, int cz, const std::vector<uint8_t>& fileData) {
    const auto finalPath = m_paths.chunkPath(cx, cz);
    const auto tmpPath = m_paths.chunkTmpPath(cx, cz);
    const auto bakPath = m_paths.chunkBakPath(cx, cz);

    // Ensure chunks directory exists
    std::error_code ec;
    std::filesystem::create_directories(m_paths.chunksDir(), ec);

    // Write to .tmp
    {
        std::ofstream file(tmpPath, std::ios::binary);
        if (!file.is_open()) {
            std::fprintf(stderr, "[Save] Failed to create tmp file %s\n",
                         tmpPath.string().c_str());
            return;
        }
        file.write(reinterpret_cast<const char*>(fileData.data()),
                   static_cast<std::streamsize>(fileData.size()));
        file.flush();
        file.close();
    }

    // Rename old file to .bak (if exists)
    if (std::filesystem::exists(finalPath, ec)) {
        std::filesystem::rename(finalPath, bakPath, ec);
        if (ec) {
            std::fprintf(stderr, "[Save] Failed to rename old chunk to .bak: %s\n",
                         ec.message().c_str());
            // Continue anyway — the tmp rename below will overwrite
        }
    }

    // Rename .tmp to final
    std::filesystem::rename(tmpPath, finalPath, ec);
    if (ec) {
        std::fprintf(stderr, "[Save] Failed to rename tmp to final: %s\n",
                     ec.message().c_str());
    }
}

} // namespace save
