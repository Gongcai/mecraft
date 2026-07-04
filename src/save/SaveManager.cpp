#include "SaveManager.h"
#include "../Diagnostics.h"
#include "PlayerSerializer.h"
#include "RegionFile.h"
#include "../thread/ThreadPool.h"
#include "../world/chunk/Chunk.h"

#include <nlohmann/json.hpp>
#include <stb/stb_image_write.h>
#include <fstream>
#include <cstdio>
#include <limits>

namespace save {

namespace {

const nlohmann::json* findField(const nlohmann::json& object, const char* key) {
    if (!object.is_object()) {
        return nullptr;
    }
    const auto it = object.find(key);
    return it != object.end() ? &(*it) : nullptr;
}

bool readIntField(const nlohmann::json& object, const char* key, int& out, const bool required = false) {
    const nlohmann::json* value = findField(object, key);
    if (value == nullptr) {
        if (required) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Missing integer field: %s\n", key);
            return false;
        }
        return true;
    }
    if (!value->is_number_integer()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid integer field: %s\n", key);
        return false;
    }
    const auto raw = value->get<int64_t>();
    if (raw < std::numeric_limits<int>::min() || raw > std::numeric_limits<int>::max()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Integer field out of range: %s\n", key);
        return false;
    }
    out = static_cast<int>(raw);
    return true;
}

bool readUnsignedRaw(const nlohmann::json& object, const char* key, uint64_t& out, const bool required) {
    const nlohmann::json* value = findField(object, key);
    if (value == nullptr) {
        if (required) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Missing unsigned integer field: %s\n", key);
            return false;
        }
        return true;
    }
    if (value->is_number_unsigned()) {
        out = value->get<uint64_t>();
        return true;
    }
    if (value->is_number_integer()) {
        const auto raw = value->get<int64_t>();
        if (raw < 0) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Negative value for unsigned field: %s\n", key);
            return false;
        }
        out = static_cast<uint64_t>(raw);
        return true;
    }
    MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid unsigned integer field: %s\n", key);
    return false;
}

bool readUint32Field(const nlohmann::json& object, const char* key, uint32_t& out, const bool required = false) {
    uint64_t raw = out;
    if (!readUnsignedRaw(object, key, raw, required)) {
        return false;
    }
    if (raw > std::numeric_limits<uint32_t>::max()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Unsigned integer field out of range: %s\n", key);
        return false;
    }
    out = static_cast<uint32_t>(raw);
    return true;
}

bool readUint64Field(const nlohmann::json& object, const char* key, uint64_t& out, const bool required = false) {
    uint64_t raw = out;
    if (!readUnsignedRaw(object, key, raw, required)) {
        return false;
    }
    out = raw;
    return true;
}

bool readFloatField(const nlohmann::json& object, const char* key, float& out, const bool required = false) {
    const nlohmann::json* value = findField(object, key);
    if (value == nullptr) {
        if (required) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Missing number field: %s\n", key);
            return false;
        }
        return true;
    }
    if (!value->is_number()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid number field: %s\n", key);
        return false;
    }
    out = value->get<float>();
    return true;
}

bool readDoubleField(const nlohmann::json& object, const char* key, double& out, const bool required = false) {
    const nlohmann::json* value = findField(object, key);
    if (value == nullptr) {
        if (required) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Missing number field: %s\n", key);
            return false;
        }
        return true;
    }
    if (!value->is_number()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid number field: %s\n", key);
        return false;
    }
    out = value->get<double>();
    return true;
}

bool readBoolField(const nlohmann::json& object, const char* key, bool& out, const bool required = false) {
    const nlohmann::json* value = findField(object, key);
    if (value == nullptr) {
        if (required) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Missing boolean field: %s\n", key);
            return false;
        }
        return true;
    }
    if (!value->is_boolean()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid boolean field: %s\n", key);
        return false;
    }
    out = value->get<bool>();
    return true;
}

bool readStringField(const nlohmann::json& object,
                     const char* key,
                     std::string& out,
                     const bool required = false) {
    const nlohmann::json* value = findField(object, key);
    if (value == nullptr) {
        if (required) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Missing string field: %s\n", key);
            return false;
        }
        return true;
    }
    if (!value->is_string()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid string field: %s\n", key);
        return false;
    }
    out = value->get<std::string>();
    return true;
}

bool readFloat3Field(const nlohmann::json& object,
                     const char* key,
                     float& x,
                     float& y,
                     float& z,
                     const bool required = false) {
    const nlohmann::json* value = findField(object, key);
    if (value == nullptr) {
        if (required) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Missing vec3 field: %s\n", key);
            return false;
        }
        return true;
    }
    if (!value->is_array() || value->size() < 3 ||
        !(*value)[0].is_number() || !(*value)[1].is_number() || !(*value)[2].is_number()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid vec3 field: %s\n", key);
        return false;
    }
    x = (*value)[0].get<float>();
    y = (*value)[1].get<float>();
    z = (*value)[2].get<float>();
    return true;
}

bool readInt3Field(const nlohmann::json& object,
                   const char* key,
                   int& x,
                   int& y,
                   int& z,
                   const bool required = false) {
    const nlohmann::json* value = findField(object, key);
    if (value == nullptr) {
        if (required) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Missing ivec3 field: %s\n", key);
            return false;
        }
        return true;
    }
    if (!value->is_array() || value->size() < 3 ||
        !(*value)[0].is_number_integer() ||
        !(*value)[1].is_number_integer() ||
        !(*value)[2].is_number_integer()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid ivec3 field: %s\n", key);
        return false;
    }
    const auto rawX = (*value)[0].get<int64_t>();
    const auto rawY = (*value)[1].get<int64_t>();
    const auto rawZ = (*value)[2].get<int64_t>();
    if (rawX < std::numeric_limits<int>::min() || rawX > std::numeric_limits<int>::max() ||
        rawY < std::numeric_limits<int>::min() || rawY > std::numeric_limits<int>::max() ||
        rawZ < std::numeric_limits<int>::min() || rawZ > std::numeric_limits<int>::max()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] ivec3 field out of range: %s\n", key);
        return false;
    }
    x = static_cast<int>(rawX);
    y = static_cast<int>(rawY);
    z = static_cast<int>(rawZ);
    return true;
}

bool parseJsonFile(std::ifstream& file, const char* label, nlohmann::json& out) {
    out = nlohmann::json::parse(file, nullptr, false);
    if (out.is_discarded()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to parse %s: invalid JSON\n", label);
        return false;
    }
    if (!out.is_object()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to parse %s: root must be an object\n", label);
        return false;
    }
    return true;
}

} // namespace

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
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to open %s\n", path.string().c_str());
        return false;
    }

    nlohmann::json j;
    if (!parseJsonFile(file, "level.json", j)) {
        return false;
    }

    int version = 0;
    if (!readIntField(j, "version", version)) {
        return false;
    }
    if (version != 1) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Unsupported level.json version: %d\n", version);
        return false;
    }

    if (!readUint32Field(j, "seed", outMeta.seed, true) ||
        !readFloat3Field(j, "spawn", outMeta.spawnX, outMeta.spawnY, outMeta.spawnZ)) {
        return false;
    }

    if (const nlohmann::json* time = findField(j, "time")) {
        if (!time->is_object() ||
            !readFloatField(*time, "timeOfDay", outMeta.timeOfDay) ||
            !readDoubleField(*time, "totalGameTime", outMeta.totalGameTime) ||
            !readIntField(*time, "elapsedDays", outMeta.elapsedDays)) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid time object in level.json\n");
            return false;
        }
    }

    if (const nlohmann::json* weather = findField(j, "weather")) {
        if (!weather->is_object() ||
            !readStringField(*weather, "type", outMeta.weatherType) ||
            !readFloatField(*weather, "wetness", outMeta.weatherWetness) ||
            !readFloatField(*weather, "storm", outMeta.weatherStorm) ||
            !readFloatField(*weather, "aerialReduction", outMeta.weatherAerialReduction)) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid weather object in level.json\n");
            return false;
        }
    }

    return readStringField(j, "displayName", outMeta.displayName) &&
           readStringField(j, "createdUtc", outMeta.createdUtc) &&
           readStringField(j, "lastSavedUtc", outMeta.lastSavedUtc) &&
           readStringField(j, "screenshotPath", outMeta.screenshotPath) &&
           readStringField(j, "gameMode", outMeta.gameMode);
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
    j["gameMode"] = meta.gameMode;

    const auto path = m_paths.levelPath();
    const auto tmpPath = path.string() + ".tmp";

    // Write to tmp file
    {
        std::ofstream file(tmpPath);
        if (!file.is_open()) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to write %s\n", tmpPath.c_str());
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
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to rename old level.json to .bak: %s\n",
                         ec.message().c_str());
        }
    }
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to rename tmp to level.json: %s\n",
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

    std::lock_guard<std::mutex> lock(m_regionCacheMutex);
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
    ChunkLoadData data = tryLoadChunkData(cx, cz);
    return std::move(data.chunk);
}

ChunkLoadData SaveManager::tryLoadChunkData(int cx, int cz) {
    ChunkLoadData loadData;
    // Try region file first
    RegionFile* region = getOrCreateRegion(cx, cz);
    if (region && region->hasChunk(cx, cz)) {
        return region->readChunkWithData(cx, cz);
    }

    // Read the single-file chunk path after the region-file path.
    const auto path = m_paths.chunkPath(cx, cz);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return loadData;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return loadData;

    const auto fileSize = file.tellg();
    if (fileSize <= 0) return loadData;
    file.seekg(0);

    std::vector<uint8_t> data(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(fileSize));
    if (!file) return loadData;

    return ChunkSerializer::deserializeFileData(data.data(), data.size());
}

void SaveManager::submitSaveChunk(int cx, int cz, const Chunk& chunk) {
    static const std::vector<WireContainerSaveEntry> kNoWireContainers;
    submitSaveChunk(cx, cz, chunk, kNoWireContainers);
}

void SaveManager::submitSaveChunk(int cx,
                                  int cz,
                                  const Chunk& chunk,
                                  const std::vector<WireContainerSaveEntry>& wireContainers) {
    // Serialize snapshot on calling thread (reads chunk data, no mutation)
    auto fileData = std::make_shared<std::vector<uint8_t>>(
        ChunkSerializer::serializeFile(chunk, wireContainers));
    if (fileData->empty()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to serialize chunk (%d, %d)\n", cx, cz);
        return;
    }
    const int64_t key = makeChunkKey(cx, cz);
    const uint64_t saveSequence = registerSaveSequence(key);

    if (!m_threadPool || !m_threadPool->isRunning()) {
        writeChunkSnapshotIfCurrent(cx, cz, key, saveSequence, *fileData);
        return;
    }

    m_pendingSaveCount.fetch_add(1, std::memory_order_relaxed);

    m_threadPool->submit([this, cx, cz, key, saveSequence, fileData]() {
        writeChunkSnapshotIfCurrent(cx, cz, key, saveSequence, *fileData);
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
    const int rx = RegionFile::toRegionCoord(cx);
    const int rz = RegionFile::toRegionCoord(cz);
    const int64_t key = (static_cast<int64_t>(rx) << 32) | (static_cast<int64_t>(rz) & 0xFFFFFFFF);

    RegionFile* cachedRegion = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_regionCacheMutex);
        auto it = m_regionCache.find(key);
        if (it != m_regionCache.end()) {
            cachedRegion = it->second.get();
        }
    }
    if (cachedRegion && cachedRegion->hasChunk(cx, cz)) {
        return true;
    }

    const auto regionPath = RegionFile::regionPath(m_paths.chunksDir(), rx, rz);
    std::error_code ec;
    if (std::filesystem::exists(regionPath, ec)) {
        RegionFile* region = getOrCreateRegion(cx, cz);
        if (region && region->hasChunk(cx, cz)) {
            return true;
        }
    }

    // Single-file chunk files are still part of the supported on-disk layout.
    return m_paths.chunkFileExists(cx, cz);
}

int64_t SaveManager::makeChunkKey(int cx, int cz) {
    return (static_cast<int64_t>(cx) << 32) | (static_cast<int64_t>(cz) & 0xFFFFFFFF);
}

uint64_t SaveManager::registerSaveSequence(const int64_t chunkKey) {
    const uint64_t sequence = m_nextSaveSequence.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(m_latestSaveMutex);
    m_latestSaveSequence[chunkKey] = sequence;
    return sequence;
}

bool SaveManager::isSaveSequenceCurrent(const int64_t chunkKey, const uint64_t saveSequence) const {
    std::lock_guard<std::mutex> lock(m_latestSaveMutex);
    const auto it = m_latestSaveSequence.find(chunkKey);
    return it != m_latestSaveSequence.end() && it->second == saveSequence;
}

void SaveManager::clearSaveSequence(const int64_t chunkKey, const uint64_t saveSequence) {
    std::lock_guard<std::mutex> lock(m_latestSaveMutex);
    const auto it = m_latestSaveSequence.find(chunkKey);
    if (it != m_latestSaveSequence.end() && it->second == saveSequence) {
        m_latestSaveSequence.erase(it);
    }
}

void SaveManager::writeChunkSnapshot(int cx, int cz, const std::vector<uint8_t>& fileData) {
    RegionFile* region = getOrCreateRegion(cx, cz);
    if (region) {
        if (!region->writeChunkRaw(cx, cz, fileData)) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to write chunk (%d, %d) to region\n", cx, cz);
        }
        return;
    }

    writeChunkFileAtomic(cx, cz, fileData);
}

void SaveManager::writeChunkSnapshotIfCurrent(const int cx,
                                              const int cz,
                                              const int64_t chunkKey,
                                              const uint64_t saveSequence,
                                              const std::vector<uint8_t>& fileData) {
    std::lock_guard<std::mutex> writeLock(m_chunkWriteMutex);
    if (!isSaveSequenceCurrent(chunkKey, saveSequence)) {
        return;
    }

    writeChunkSnapshot(cx, cz, fileData);
    clearSaveSequence(chunkKey, saveSequence);
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
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to write %s\n", tmpPath.c_str());
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
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to rename entity file: %s\n", ec.message().c_str());
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

    nlohmann::json root;
    if (!parseJsonFile(file, "entity save file", root)) {
        out.clear();
        return false;
    }

    int version = 0;
    const nlohmann::json* entities = findField(root, "entities");
    if (!readIntField(root, "version", version) ||
        version != 1 ||
        entities == nullptr ||
        !entities->is_array()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Unsupported entity save file\n");
        out.clear();
        return false;
    }

    for (const auto& j : *entities) {
        if (!j.is_object()) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid persistent entity entry\n");
            out.clear();
            return false;
        }

        PersistentEntityData entity;
        if (!readStringField(j, "type", entity.type, true) ||
            entity.type.empty() ||
            !readFloat3Field(j, "position", entity.posX, entity.posY, entity.posZ) ||
            !readFloat3Field(j, "velocity", entity.velX, entity.velY, entity.velZ) ||
            !readFloatField(j, "yaw", entity.yaw) ||
            !readFloatField(j, "pitch", entity.pitch) ||
            !readUint64Field(j, "dropId", entity.dropId) ||
            !readBoolField(j, "grounded", entity.grounded)) {
            out.clear();
            return false;
        }

        if (const nlohmann::json* health = findField(j, "health")) {
            if (!health->is_object() ||
                !readIntField(*health, "current", entity.health) ||
                !readIntField(*health, "max", entity.healthMax)) {
                MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid persistent entity health object\n");
                out.clear();
                return false;
            }
        }

        if (const nlohmann::json* item = findField(j, "item")) {
            if (!item->is_object() ||
                !readUint32Field(*item, "id", entity.itemId) ||
                !readUint32Field(*item, "count", entity.stackCount)) {
                MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid persistent entity item object\n");
                out.clear();
                return false;
            }
        } else if (!readUint32Field(j, "itemId", entity.itemId) ||
                   !readUint32Field(j, "stackCount", entity.stackCount)) {
            out.clear();
            return false;
        }

        if (!readFloat3Field(j, "bounds", entity.halfExtentX, entity.halfExtentY, entity.halfExtentZ)) {
            out.clear();
            return false;
        }

        if (const nlohmann::json* spin = findField(j, "spin")) {
            if (!spin->is_object() ||
                !readFloatField(*spin, "yaw", entity.yaw) ||
                !readFloatField(*spin, "speed", entity.spinSpeed)) {
                MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid persistent entity spin object\n");
                out.clear();
                return false;
            }
        }

        if (const nlohmann::json* lifetime = findField(j, "lifetime")) {
            if (!lifetime->is_object() ||
                !readFloatField(*lifetime, "age", entity.ageSeconds) ||
                !readFloatField(*lifetime, "max", entity.lifeTimeSeconds)) {
                MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid persistent entity lifetime object\n");
                out.clear();
                return false;
            }
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
}

void SaveManager::saveBlockEntities(const std::vector<BlockEntityData>& entities) {
    nlohmann::json root;
    root["version"] = 1;
    root["blockEntities"] = nlohmann::json::array();

    for (const BlockEntityData& entity : entities) {
        if (entity.type.empty()) {
            continue;
        }

        nlohmann::json slots = nlohmann::json::array();
        for (const BlockEntitySlotData& slot : entity.slots) {
            if (slot.slot < 0 || slot.itemId == 0 || slot.count == 0) {
                continue;
            }
            slots.push_back({
                {"slot", slot.slot},
                {"item", {
                    {"id", slot.itemId},
                    {"count", slot.count},
                    {"durability", slot.durability}
                }}
            });
        }

        nlohmann::json j;
        j["type"] = entity.type;
        j["position"] = {entity.x, entity.y, entity.z};
        j["slots"] = std::move(slots);
        if (entity.type == "minecraft:furnace") {
            j["burnSecondsRemaining"] = entity.burnSecondsRemaining;
            j["burnSecondsTotal"] = entity.burnSecondsTotal;
            j["cookSeconds"] = entity.cookSeconds;
            j["cookTargetSeconds"] = entity.cookTargetSeconds;
        }
        root["blockEntities"].push_back(std::move(j));
    }

    const auto path = m_paths.overworldBlockEntitiesPath();
    const auto tmpPath = path.string() + ".tmp";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    {
        std::ofstream file(tmpPath);
        if (!file.is_open()) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to write %s\n", tmpPath.c_str());
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
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to rename block entity file: %s\n", ec.message().c_str());
    }
}

bool SaveManager::loadBlockEntities(std::vector<BlockEntityData>& out) {
    out.clear();
    const auto path = m_paths.overworldBlockEntitiesPath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    nlohmann::json root;
    if (!parseJsonFile(file, "block entity save file", root)) {
        out.clear();
        return false;
    }

    int version = 0;
    const nlohmann::json* blockEntities = findField(root, "blockEntities");
    if (!readIntField(root, "version", version) ||
        version != 1 ||
        blockEntities == nullptr ||
        !blockEntities->is_array()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Unsupported block entity save file\n");
        out.clear();
        return false;
    }

    for (const auto& j : *blockEntities) {
        if (!j.is_object()) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid block entity entry\n");
            out.clear();
            return false;
        }

        BlockEntityData entity;
        if (!readStringField(j, "type", entity.type, true) ||
            entity.type.empty() ||
            !readInt3Field(j, "position", entity.x, entity.y, entity.z, true)) {
            out.clear();
            return false;
        }

        const nlohmann::json* slots = findField(j, "slots");
        if (slots == nullptr || !slots->is_array()) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid block entity slots array\n");
            out.clear();
            return false;
        }

        if (entity.type == "minecraft:furnace" &&
            (!readFloatField(j, "burnSecondsRemaining", entity.burnSecondsRemaining) ||
             !readFloatField(j, "burnSecondsTotal", entity.burnSecondsTotal) ||
             !readFloatField(j, "cookSeconds", entity.cookSeconds) ||
             !readFloatField(j, "cookTargetSeconds", entity.cookTargetSeconds))) {
            out.clear();
            return false;
        }

        for (const auto& slotJson : *slots) {
            if (!slotJson.is_object()) {
                MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid block entity slot entry\n");
                out.clear();
                return false;
            }

            BlockEntitySlotData slot;
            if (!readIntField(slotJson, "slot", slot.slot, true)) {
                out.clear();
                return false;
            }

            if (const nlohmann::json* item = findField(slotJson, "item")) {
                if (!item->is_object() ||
                    !readUint32Field(*item, "id", slot.itemId) ||
                    !readUint32Field(*item, "count", slot.count) ||
                    !readUint32Field(*item, "durability", slot.durability)) {
                    MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid block entity slot item object\n");
                    out.clear();
                    return false;
                }
            } else if (!readUint32Field(slotJson, "itemId", slot.itemId) ||
                       !readUint32Field(slotJson, "count", slot.count) ||
                       !readUint32Field(slotJson, "durability", slot.durability)) {
                out.clear();
                return false;
            }

            if (slot.slot < 0 || slot.itemId == 0 || slot.count == 0) {
                continue;
            }
            entity.slots.push_back(slot);
        }

        out.push_back(std::move(entity));
    }

    return true;
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
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to write screenshot PNG\n");
        return;
    }

    // Atomic rename
    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to rename screenshot: %s\n", ec.message().c_str());
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
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to create tmp file %s\n",
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
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to rename old chunk to .bak: %s\n",
                         ec.message().c_str());
            // Continue anyway — the tmp rename below will overwrite
        }
    }

    // Rename .tmp to final
    std::filesystem::rename(tmpPath, finalPath, ec);
    if (ec) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to rename tmp to final: %s\n",
                     ec.message().c_str());
    }
}

} // namespace save
