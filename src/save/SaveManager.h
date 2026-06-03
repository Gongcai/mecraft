#ifndef MECRAFT_SAVE_MANAGER_H
#define MECRAFT_SAVE_MANAGER_H

// SaveManager: orchestrates world persistence on the server side.
// Owns the save directory, reads/writes level.json and chunk files,
// and dispatches async chunk saves via the thread pool.
//
// Lifecycle:
//   1. Constructed with a save root path.
//   2. setThreadPool() called before any async operations.
//   3. loadLevelMeta() called on init to restore seed, time, weather.
//   4. tryLoadChunk() called during chunk load (sync or async).
//   5. submitSaveChunk() called when dirty chunks are unloaded or on flush.
//   6. flushPendingSaves() called during shutdown to ensure all data is written.

#include "SavePaths.h"
#include "ChunkSerializer.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

class Chunk;
class ThreadPool;

namespace save {

struct PlayerData;

/// World-level metadata persisted in level.json.
struct LevelMeta {
    uint32_t seed = 0;
    float spawnX = 0.0f;
    float spawnY = 68.0f;
    float spawnZ = 0.0f;
    float timeOfDay = 300.0f;
    double totalGameTime = 300.0;
    int elapsedDays = 0;
    std::string weatherType = "clear";
    float weatherWetness = 0.0f;
    float weatherStorm = 0.0f;
    float weatherAerialReduction = 0.55f;
};

class SaveManager {
public:
    explicit SaveManager(std::filesystem::path saveRoot);
    ~SaveManager();

    // Non-copyable, non-movable
    SaveManager(const SaveManager&) = delete;
    SaveManager& operator=(const SaveManager&) = delete;

    // --- Level metadata ---

    /// Load level.json. Returns true if file exists and was read successfully.
    /// On success, outMeta is populated. On failure (new world), outMeta is unchanged.
    bool loadLevelMeta(LevelMeta& outMeta);

    /// Save level.json with the given metadata. Uses atomic write (tmp + rename).
    void saveLevelMeta(const LevelMeta& meta);

    // --- Chunk I/O ---

    /// Try to load a chunk from disk. Returns nullptr if file doesn't exist or is corrupt.
    /// Can be called from any thread (reads file, creates Chunk).
    [[nodiscard]] std::shared_ptr<Chunk> tryLoadChunk(int cx, int cz);

    /// Submit a chunk for async saving. The chunk data is serialized immediately
    /// on the calling thread (snapshot), then the file write is dispatched to the
    /// thread pool. Must have called setThreadPool() first.
    void submitSaveChunk(int cx, int cz, const Chunk& chunk);

    /// Set the thread pool for async saves.
    void setThreadPool(ThreadPool* pool);

    /// Wait for all pending async saves to complete. Blocking.
    /// Called during shutdown to ensure all data is flushed to disk.
    void flushPendingSaves();

    /// Check if a chunk file exists on disk.
    [[nodiscard]] bool chunkFileExists(int cx, int cz) const;

    /// Save local player data to disk.
    void saveLocalPlayer(const PlayerData& data);

    /// Load local player data from disk. Returns false if file doesn't exist.
    bool loadLocalPlayer(PlayerData& out);

    /// Access the paths helper.
    [[nodiscard]] const SavePaths& paths() const { return m_paths; }

private:
    /// Write a chunk file atomically: write .tmp, flush, rename old to .bak, rename .tmp to final.
    void writeChunkFileAtomic(int cx, int cz, const std::vector<uint8_t>& fileData);

    SavePaths m_paths;
    ThreadPool* m_threadPool = nullptr;

    // Pending save tracking
    std::mutex m_saveMutex;
    std::condition_variable m_saveCv;
    std::atomic<int> m_pendingSaveCount{0};
};

} // namespace save

#endif // MECRAFT_SAVE_MANAGER_H
