#include "SaveManager.h"
#include "../thread/ThreadPool.h"
#include "../world/chunk/Chunk.h"

#include <nlohmann/json.hpp>
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

bool SaveManager::loadLevelMeta(uint32_t& outSeed) {
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

        outSeed = j["seed"].get<uint32_t>();
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[Save] Failed to parse level.json: %s\n", e.what());
        return false;
    }
}

void SaveManager::saveLevelMeta(uint32_t seed) {
    nlohmann::json j;
    j["format"] = "mecraft.level";
    j["version"] = 1;
    j["seed"] = seed;

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

std::shared_ptr<Chunk> SaveManager::tryLoadChunk(int cx, int cz) {
    const auto path = m_paths.chunkPath(cx, cz);

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return nullptr;
    }

    // Read file into memory
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::fprintf(stderr, "[Save] Failed to open chunk file %s\n", path.string().c_str());
        return nullptr;
    }

    const auto fileSize = file.tellg();
    if (fileSize <= 0) return nullptr;
    file.seekg(0);

    std::vector<uint8_t> data(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(fileSize));
    if (!file) {
        std::fprintf(stderr, "[Save] Failed to read chunk file %s\n", path.string().c_str());
        return nullptr;
    }

    // Deserialize
    auto chunk = ChunkSerializer::deserializeFile(data.data(), data.size());
    if (!chunk) {
        std::fprintf(stderr, "[Save] Failed to deserialize chunk (%d, %d)\n", cx, cz);
    }
    return chunk;
}

void SaveManager::submitSaveChunk(int cx, int cz, const Chunk& chunk) {
    if (!m_threadPool) {
        // No thread pool — write synchronously
        std::vector<uint8_t> fileData = ChunkSerializer::serializeFile(chunk);
        writeChunkFileAtomic(cx, cz, fileData);
        return;
    }

    // Serialize snapshot on calling thread (reads chunk data, no mutation)
    auto fileData = std::make_shared<std::vector<uint8_t>>(
        ChunkSerializer::serializeFile(chunk));

    m_pendingSaveCount.fetch_add(1, std::memory_order_relaxed);

    m_threadPool->submit([this, cx, cz, fileData]() {
        writeChunkFileAtomic(cx, cz, *fileData);
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
    return m_paths.chunkFileExists(cx, cz);
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
