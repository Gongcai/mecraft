#ifndef MECRAFT_SAVE_MANAGER_H
#define MECRAFT_SAVE_MANAGER_H

// SaveManager: orchestrates world persistence on the server side.
// Owns the save directory, reads/writes level.json and chunk files,
// and dispatches async chunk saves via the thread pool.
//
// Lifecycle:
//   1. Constructed with a save root path.
//   2. setThreadPool() called before any async operations.
//   3. loadLevelMeta() called on init to restore seed.
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

class Chunk;
class ThreadPool;

namespace save {

class SaveManager {
public:
    explicit SaveManager(std::filesystem::path saveRoot);
    ~SaveManager();

    // Non-copyable, non-movable
    SaveManager(const SaveManager&) = delete;
    SaveManager& operator=(const SaveManager&) = delete;

    // --- Level metadata ---

    /// Load level.json. Returns true if file exists and was read successfully.
    /// On success, outSeed is populated. On failure (new world), outSeed is unchanged.
    bool loadLevelMeta(uint32_t& outSeed);

    /// Save level.json with the given seed. Uses atomic write (tmp + rename).
    void saveLevelMeta(uint32_t seed);

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
