#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "../src/renderer/mesh/ChunkMesher.h"
#include "../src/renderer/mesh/ChunkMeshingService.h"
#include "../src/thread/ThreadPool.h"
#include "../src/world/fluid/FluidState.h"

namespace {

// ── Benchmark statistics ──────────────────────────────────────────────
struct BenchmarkStats {
    double medianMs = 0.0;
    double avgMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
    double p95Ms = 0.0;
    double callsPerSec = 0.0;
    double nsPerCall = 0.0;
    uint64_t checksum = 0;
};

BenchmarkStats computeStats(std::vector<double> timingsMs, uint64_t checksum) {
    std::sort(timingsMs.begin(), timingsMs.end());

    BenchmarkStats stats;
    stats.checksum = checksum;
    stats.minMs = timingsMs.front();
    stats.maxMs = timingsMs.back();
    stats.avgMs = std::accumulate(timingsMs.begin(), timingsMs.end(), 0.0) / static_cast<double>(timingsMs.size());

    const size_t mid = timingsMs.size() / 2;
    if (timingsMs.size() % 2 == 0) {
        stats.medianMs = (timingsMs[mid - 1] + timingsMs[mid]) * 0.5;
    } else {
        stats.medianMs = timingsMs[mid];
    }

    const size_t p95Index = (timingsMs.size() - 1U) * 95U / 100U;
    stats.p95Ms = timingsMs[p95Index];

    const double medianSec = stats.medianMs / 1000.0;
    if (medianSec > 0.0) {
        stats.callsPerSec = 1.0 / medianSec;
        stats.nsPerCall = stats.medianMs * 1'000'000.0;
    }
    return stats;
}

void printStats(const std::string& tag, const std::string& caseName, const BenchmarkStats& stats, int warmupRounds,
                int measureRounds, int batchSize = 1) {
    std::cout << "[" << tag << "]"
              << " case=" << caseName << " batch=" << batchSize << " warmup=" << warmupRounds
              << " rounds=" << measureRounds << " median_ms=" << std::fixed << std::setprecision(3) << stats.medianMs
              << " p95_ms=" << stats.p95Ms << " avg_ms=" << stats.avgMs << " min_ms=" << stats.minMs
              << " max_ms=" << stats.maxMs << " calls_per_sec=" << std::setprecision(0) << stats.callsPerSec
              << " ns_per_call=" << std::setprecision(2) << stats.nsPerCall << " checksum=" << std::hex
              << std::setprecision(0) << stats.checksum << std::dec << "\n";
}

// ── Test data builders ────────────────────────────────────────────────

// Deterministic PRNG for reproducible terrain
uint32_t nextRand(uint32_t& state) {
    state = state * 1664525U + 1013904223U;
    return state;
}

BlockStateId stateForBlockId(const BlockID blockId) {
    return blockId == RUNTIME_ID_NULL ? NULL_BLOCK_STATE : BlockStateRegistry::getDefaultState(blockId);
}

BlockStateId stateForBlockName(const char* name) {
    return stateForBlockId(BlockRegistry::requireIdByName(name));
}

/// Small: ~20% fill, only top layers (low block density)
std::shared_ptr<Chunk> makeSmallChunk(int cx, int cz, uint32_t seed) {
    auto chunk = std::make_shared<Chunk>(cx, cz);
    uint32_t state = seed;
    for (int z = 0; z < Chunk::SIZE_Z; ++z) {
        for (int x = 0; x < Chunk::SIZE_X; ++x) {
            nextRand(state);
            const int height = 4 + static_cast<int>(state % 8); // 4..11
            for (int y = 0; y < height; ++y) {
                nextRand(state);
                const uint32_t bucket = state % 100U;
                BlockID id;
                if (bucket < 70U)
                    id = BlockRegistry::requireIdByName("minecraft:stone");
                else if (bucket < 90U)
                    id = BlockRegistry::requireIdByName("minecraft:dirt");
                else
                    id = BlockRegistry::requireIdByName("minecraft:grass_block");
                chunk->setBlock(x, y, z, stateForBlockId(id));
            }
        }
    }
    return chunk;
}

/// Medium: ~40% fill, typical terrain (grass/dirt/stone/water/sand)
std::shared_ptr<Chunk> makeMediumChunk(int cx, int cz, uint32_t seed) {
    auto chunk = std::make_shared<Chunk>(cx, cz);
    uint32_t state = seed;
    for (int z = 0; z < Chunk::SIZE_Z; ++z) {
        for (int x = 0; x < Chunk::SIZE_X; ++x) {
            nextRand(state);
            const int height = 40 + static_cast<int>(state % 40); // 40..79
            for (int y = 0; y < height; ++y) {
                nextRand(state);
                const uint32_t bucket = state % 100U;
                BlockID id;
                if (y == height - 1 && bucket < 50U)
                    id = BlockRegistry::requireIdByName("minecraft:grass_block");
                else if (y >= height - 3)
                    id = BlockRegistry::requireIdByName("minecraft:dirt");
                else if (bucket < 80U)
                    id = BlockRegistry::requireIdByName("minecraft:stone");
                else if (bucket < 90U)
                    id = BlockRegistry::requireIdByName("minecraft:sand");
                else
                    id = BlockRegistry::requireIdByName("minecraft:dirt");
                chunk->setBlock(x, y, z, stateForBlockId(id));
            }
            // Add water in low areas
            for (int y = height; y < 63; ++y) {
                chunk->setBlock(x, y, z, stateForBlockName("minecraft:water"));
            }
        }
    }
    // Sprinkle some ores and glass
    for (int i = 0; i < 32; ++i) {
        nextRand(state);
        const int ox = static_cast<int>(state % Chunk::SIZE_X);
        nextRand(state);
        const int oy = static_cast<int>(state % 60) + 5;
        nextRand(state);
        const int oz = static_cast<int>(state % Chunk::SIZE_Z);
        nextRand(state);
        const uint32_t bucket = state % 4U;
        BlockID id = BlockRegistry::requireIdByName("minecraft:coal_ore");
        if (bucket == 1)
            id = BlockRegistry::requireIdByName("minecraft:iron_ore");
        else if (bucket == 2)
            id = BlockRegistry::requireIdByName("minecraft:glass");
        else if (bucket == 3)
            id = BlockRegistry::requireIdByName("minecraft:gold_ore");
        chunk->setBlock(ox, oy, oz, stateForBlockId(id));
    }
    // Add some tall grass / flowers on surface
    for (int i = 0; i < 16; ++i) {
        nextRand(state);
        const int fx = static_cast<int>(state % Chunk::SIZE_X);
        nextRand(state);
        const int fz = static_cast<int>(state % Chunk::SIZE_Z);
        nextRand(state);
        const int baseY = 40 + static_cast<int>(state % 40);
        // Find top solid block
        for (int y = Chunk::SIZE_Y - 1; y >= 0; --y) {
            if (chunk->getBlock(fx, y, fz) != NULL_BLOCK_STATE) {
                if (y + 1 < Chunk::SIZE_Y) {
                    nextRand(state);
                    chunk->setBlock(fx, y + 1, fz,
                                    stateForBlockId((state % 2 == 0)
                                                        ? BlockRegistry::requireIdByName("minecraft:tall_grass")
                                                        : BlockRegistry::requireIdByName("minecraft:rose")));
                }
                break;
            }
        }
    }
    return chunk;
}

/// Large: ~50% fill, dense terrain with water, ores, cross blocks, glass
std::shared_ptr<Chunk> makeLargeChunk(int cx, int cz, uint32_t seed) {
    auto chunk = std::make_shared<Chunk>(cx, cz);
    uint32_t state = seed;
    for (int z = 0; z < Chunk::SIZE_Z; ++z) {
        for (int x = 0; x < Chunk::SIZE_X; ++x) {
            nextRand(state);
            const int height = 60 + static_cast<int>(state % 50); // 60..109
            for (int y = 0; y < height; ++y) {
                nextRand(state);
                const uint32_t bucket = state % 100U;
                BlockID id;
                if (y == height - 1 && bucket < 40U)
                    id = BlockRegistry::requireIdByName("minecraft:grass_block");
                else if (y >= height - 4)
                    id = BlockRegistry::requireIdByName("minecraft:dirt");
                else if (bucket < 75U)
                    id = BlockRegistry::requireIdByName("minecraft:stone");
                else if (bucket < 85U)
                    id = BlockRegistry::requireIdByName("minecraft:sand");
                else if (bucket < 92U)
                    id = BlockRegistry::requireIdByName("minecraft:coal_ore");
                else if (bucket < 96U)
                    id = BlockRegistry::requireIdByName("minecraft:iron_ore");
                else if (bucket < 98U)
                    id = BlockRegistry::requireIdByName("minecraft:gold_ore");
                else
                    id = BlockRegistry::requireIdByName("minecraft:diamond_ore");
                chunk->setBlock(x, y, z, stateForBlockId(id));
            }
            // Fill water up to sea level
            for (int y = height; y < 63; ++y) {
                chunk->setBlock(x, y, z, stateForBlockName("minecraft:water"));
            }
            // Glass panes scattered in upper layers
            if (height > 80) {
                for (int y = 75; y < 80; ++y) {
                    nextRand(state);
                    if (state % 5 == 0) {
                        chunk->setBlock(x, y, z, stateForBlockName("minecraft:glass"));
                    }
                }
            }
        }
    }
    // Dense cross-geometry layer
    for (int i = 0; i < 64; ++i) {
        nextRand(state);
        const int fx = static_cast<int>(state % Chunk::SIZE_X);
        nextRand(state);
        const int fz = static_cast<int>(state % Chunk::SIZE_Z);
        nextRand(state);
        const int baseY = 60 + static_cast<int>(state % 50);
        for (int y = Chunk::SIZE_Y - 1; y >= 0; --y) {
            if (chunk->getBlock(fx, y, fz) != NULL_BLOCK_STATE && !FluidState::isWater(chunk->getBlock(fx, y, fz))) {
                if (y + 1 < Chunk::SIZE_Y) {
                    nextRand(state);
                    const uint32_t t = state % 3U;
                    chunk->setBlock(fx, y + 1, fz,
                                    stateForBlockId(t == 0 ? BlockRegistry::requireIdByName("minecraft:tall_grass")
                                                           : (t == 1 ? BlockRegistry::requireIdByName("minecraft:rose")
                                                                     : BlockRegistry::requireIdByName(
                                                                           "minecraft:brown_mushroom"))));
                }
                break;
            }
        }
    }
    return chunk;
}

// ── Scenario 1: Single-thread sub-chunk meshing baseline ─────────────

struct TestCase {
    std::string name;
    std::vector<std::shared_ptr<Chunk>> chunks;
};

uint64_t checksumMeshData(const ChunkMeshData& data) {
    uint64_t h = 0;
    h ^= static_cast<uint64_t>(data.opaqueVertices.size()) * 0x9E3779B97F4A7C15ULL;
    h ^= static_cast<uint64_t>(data.cutoutVertices.size()) * 0x9E3779B97F4A7C15ULL >> 1;
    h ^= static_cast<uint64_t>(data.transparentVertices.size()) * 0x9E3779B97F4A7C15ULL >> 2;
    h ^= static_cast<uint64_t>(data.opaqueFaceCountAfterGreedy) << 16;
    h ^= static_cast<uint64_t>(data.transparentFaceCountAfterGreedy) << 8;
    return h;
}

void expandBounds(ChunkMeshData& merged, const glm::vec3& candidateMin, const glm::vec3& candidateMax) {
    if (!merged.hasBounds) {
        merged.hasBounds = true;
        merged.boundsMin = candidateMin;
        merged.boundsMax = candidateMax;
        return;
    }

    merged.boundsMin.x = std::min(merged.boundsMin.x, candidateMin.x);
    merged.boundsMin.y = std::min(merged.boundsMin.y, candidateMin.y);
    merged.boundsMin.z = std::min(merged.boundsMin.z, candidateMin.z);
    merged.boundsMax.x = std::max(merged.boundsMax.x, candidateMax.x);
    merged.boundsMax.y = std::max(merged.boundsMax.y, candidateMax.y);
    merged.boundsMax.z = std::max(merged.boundsMax.z, candidateMax.z);
}

ChunkMeshData buildMeshDataFor(const Chunk& chunk) {
    ChunkMeshData merged;
    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        if (ChunkMesher::shouldSkipSubChunk(chunk, scy)) {
            continue;
        }

        const SubChunkMeshingSnapshotPtr snapshot = ChunkMesher::captureSubChunkSnapshot(chunk, scy);
        if (!snapshot) {
            continue;
        }

        ChunkMeshData scMeshData = ChunkMesher::buildSubChunkMeshData(*snapshot);
        const float yOffset = static_cast<float>(scy * SubChunk::SIZE);
        for (auto& vertex : scMeshData.opaqueVertices) {
            vertex.y += yOffset;
        }
        for (auto& vertex : scMeshData.cutoutVertices) {
            vertex.y += yOffset;
        }
        for (auto& vertex : scMeshData.transparentVertices) {
            vertex.y += yOffset;
        }

        merged.opaqueVertices.insert(merged.opaqueVertices.end(), scMeshData.opaqueVertices.begin(),
                                     scMeshData.opaqueVertices.end());
        merged.cutoutVertices.insert(merged.cutoutVertices.end(), scMeshData.cutoutVertices.begin(),
                                     scMeshData.cutoutVertices.end());
        merged.transparentVertices.insert(merged.transparentVertices.end(), scMeshData.transparentVertices.begin(),
                                          scMeshData.transparentVertices.end());
        merged.opaqueFaceCountBeforeGreedy += scMeshData.opaqueFaceCountBeforeGreedy;
        merged.opaqueFaceCountAfterGreedy += scMeshData.opaqueFaceCountAfterGreedy;
        merged.transparentFaceCountBeforeGreedy += scMeshData.transparentFaceCountBeforeGreedy;
        merged.transparentFaceCountAfterGreedy += scMeshData.transparentFaceCountAfterGreedy;
        if (scMeshData.hasBounds) {
            expandBounds(merged, scMeshData.boundsMin + glm::vec3(0.0f, yOffset, 0.0f),
                         scMeshData.boundsMax + glm::vec3(0.0f, yOffset, 0.0f));
        }
    }

    merged.opaqueVertexCount = static_cast<uint32_t>(merged.opaqueVertices.size());
    return merged;
}

std::vector<SubChunkMeshingJob> collectMeshingJobs(const std::vector<std::shared_ptr<Chunk>>& chunks) {
    std::vector<SubChunkMeshingJob> jobs;
    for (size_t chunkIndex = 0; chunkIndex < chunks.size(); ++chunkIndex) {
        const std::shared_ptr<Chunk>& chunk = chunks[chunkIndex];
        if (!chunk) {
            continue;
        }

        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            if (ChunkMesher::shouldSkipSubChunk(*chunk, scy)) {
                continue;
            }

            SubChunkMeshingJob job;
            job.chunkKey = static_cast<int64_t>(chunkIndex);
            job.scy = scy;
            job.revision = chunk->getSubChunkMeshRevision(scy);
            job.snapshot = ChunkMesher::captureSubChunkSnapshot(*chunk, scy);
            jobs.push_back(std::move(job));
        }
    }
    return jobs;
}

BenchmarkStats runSingleThreadBenchmark(const TestCase& testCase, int warmupRounds, int measureRounds) {
    const size_t batchCount = testCase.chunks.size();

    for (int r = 0; r < warmupRounds; ++r) {
        for (size_t i = 0; i < batchCount; ++i) {
            ChunkMeshData data = buildMeshDataFor(*testCase.chunks[i]);
            if (checksumMeshData(data) == 0xFFFFFFFFFFFFFFFFULL) {
                std::cout << "[meshing_perf_test] impossible_warmup_checksum\n";
            }
        }
    }

    std::vector<double> timingsMs;
    timingsMs.reserve(static_cast<size_t>(measureRounds));
    uint64_t checksum = 0;

    for (int r = 0; r < measureRounds; ++r) {
        const auto start = std::chrono::high_resolution_clock::now();
        uint64_t roundChecksum = 0;
        for (size_t i = 0; i < batchCount; ++i) {
            ChunkMeshData data = buildMeshDataFor(*testCase.chunks[i]);
            roundChecksum ^= checksumMeshData(data) + static_cast<uint64_t>(i + 1);
        }
        const auto end = std::chrono::high_resolution_clock::now();

        checksum ^= roundChecksum + static_cast<uint64_t>(r + 1);
        const double durationMs = std::chrono::duration<double, std::milli>(end - start).count();
        timingsMs.push_back(durationMs);
    }

    auto stats = computeStats(std::move(timingsMs), checksum);
    // Normalize to per-chunk stats
    stats.callsPerSec = static_cast<double>(batchCount) / (stats.medianMs / 1000.0);
    stats.nsPerCall = stats.medianMs * 1'000'000.0 / static_cast<double>(batchCount);
    return stats;
}

// ── Scenario 2: ThreadPool + ChunkMeshingService throughput ───────────

struct ThroughputStats {
    double totalMs = 0.0;
    double chunksPerSec = 0.0;
    int totalChunks = 0;
    uint64_t checksum = 0;
};

ThroughputStats runServiceThroughput(const std::vector<std::shared_ptr<Chunk>>& chunks, int numThreads,
                                     int warmupRounds, int measureRounds) {
    const std::vector<SubChunkMeshingJob> jobs = collectMeshingJobs(chunks);
    const int totalChunks = static_cast<int>(jobs.size());

    for (int w = 0; w < warmupRounds; ++w) {
        ThreadPool pool(numThreads);
        ChunkMeshingService service;
        pool.start();
        service.start(&pool);

        for (const SubChunkMeshingJob& job : jobs) {
            service.submit(job, 0);
        }

        int completedCount = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (completedCount < totalChunks && std::chrono::steady_clock::now() < deadline) {
            SubChunkMeshingResult result;
            while (service.tryPopCompleted(result)) {
                service.recycleMeshData(std::move(result.meshData));
                ++completedCount;
            }
            if (completedCount < totalChunks) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        service.shutdown();
        pool.shutdown();
    }

    double totalTimeMs = 0.0;
    uint64_t checksum = 0;

    for (int r = 0; r < measureRounds; ++r) {
        ThreadPool pool(numThreads);
        ChunkMeshingService service;
        pool.start();
        service.start(&pool);

        const auto submitStart = std::chrono::high_resolution_clock::now();

        for (const SubChunkMeshingJob& job : jobs) {
            service.submit(job, 0);
        }

        int completedCount = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (completedCount < totalChunks && std::chrono::steady_clock::now() < deadline) {
            SubChunkMeshingResult result;
            while (service.tryPopCompleted(result)) {
                checksum ^= checksumMeshData(result.meshData) + static_cast<uint64_t>(completedCount + 1);
                service.recycleMeshData(std::move(result.meshData));
                ++completedCount;
            }
            if (completedCount < totalChunks) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        const auto endTime = std::chrono::high_resolution_clock::now();
        totalTimeMs += std::chrono::duration<double, std::milli>(endTime - submitStart).count();

        service.shutdown();
        pool.shutdown();
    }

    ThroughputStats stats;
    stats.totalMs = totalTimeMs / static_cast<double>(measureRounds);
    stats.totalChunks = totalChunks;
    stats.checksum = checksum;
    if (stats.totalMs > 0.0) {
        stats.chunksPerSec = static_cast<double>(totalChunks) / (stats.totalMs / 1000.0);
    }
    return stats;
}

void printThroughput(const std::string& caseName, int numThreads, const ThroughputStats& stats, int warmupRounds,
                     int measureRounds) {
    std::cout << "[meshing_perf_test]"
              << " case=" << caseName << "_throughput"
              << " threads=" << numThreads << " chunks=" << stats.totalChunks << " warmup=" << warmupRounds
              << " rounds=" << measureRounds << " avg_total_ms=" << std::fixed << std::setprecision(3) << stats.totalMs
              << " chunks_per_sec=" << std::setprecision(0) << stats.chunksPerSec << " checksum=" << std::hex
              << stats.checksum << std::dec << "\n";
}

// ── JSON result writer ────────────────────────────────────────────────

struct CaseResult {
    std::string name;
    double medianMs = 0.0;
    double p95Ms = 0.0;
    double avgMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
    double callsPerSec = 0.0;
    double nsPerCall = 0.0;
    uint64_t checksum = 0;
    // Throughput-specific fields (0 for single-thread cases)
    int totalChunks = 0;
    int threads = 0;
    double totalMs = 0.0;
    double chunksPerSec = 0.0;
};

std::tm currentLocalTime() {
    std::time_t now = std::time(nullptr);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    return tm;
}

std::string getCurrentDate() {
    std::tm tm = currentLocalTime();
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

std::string getCurrentTimestampSuffix() {
    std::tm tm = currentLocalTime();
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    return buf;
}

/// Resolve the project source root from __FILE__ so the output path works
/// regardless of the working directory (e.g. when running from x64-Debug/).
std::string resolveProjectRoot() {
    // __FILE__ = "<root>/tests/meshing_perf_test.cpp"
    std::string file = __FILE__;
    // Strip "tests/meshing_perf_test.cpp" to get the project root
    const std::string suffix = "tests/meshing_perf_test.cpp";
    if (file.size() >= suffix.size() && file.substr(file.size() - suffix.size()) == suffix) {
        return file.substr(0, file.size() - suffix.size());
    }
    // Resolve paths produced by unusual compiler configurations.
    auto lastSep = file.find_last_of("/\\");
    if (lastSep != std::string::npos) {
        file = file.substr(0, lastSep);
        lastSep = file.find_last_of("/\\");
        if (lastSep != std::string::npos) {
            return file.substr(0, lastSep + 1);
        }
    }
    return "./";
}

bool writeResultsJson(const std::string& filePath, const std::string& buildConfig, int warmupRounds, int measureRounds,
                      int numThreads, const std::vector<CaseResult>& singleThreadCases,
                      const std::vector<CaseResult>& throughputCases) {
    using json = nlohmann::json;

    json singleThreadJson = json::array();
    for (const auto& c : singleThreadCases) {
        singleThreadJson.push_back({{"name", c.name},
                                    {"median_ms", c.medianMs},
                                    {"p95_ms", c.p95Ms},
                                    {"avg_ms", c.avgMs},
                                    {"min_ms", c.minMs},
                                    {"max_ms", c.maxMs},
                                    {"calls_per_sec", c.callsPerSec},
                                    {"ns_per_call", c.nsPerCall},
                                    {"checksum", c.checksum}});
    }

    json throughputJson = json::array();
    for (const auto& c : throughputCases) {
        throughputJson.push_back({{"name", c.name},
                                  {"threads", c.threads},
                                  {"chunks", c.totalChunks},
                                  {"avg_total_ms", c.totalMs},
                                  {"chunks_per_sec", c.chunksPerSec},
                                  {"median_ms", c.medianMs},
                                  {"p95_ms", c.p95Ms},
                                  {"checksum", c.checksum}});
    }

    json root;
    root["benchmark"] = "meshing_perf_test";
    root["focus"] = "SubChunk meshing + ChunkMeshingService throughput";

    root["captured_at"] = getCurrentDate();
    root["build"] = {{"config", buildConfig}};
    root["settings"] = {{"warmup_rounds", warmupRounds}, {"measure_rounds", measureRounds},
                        {"num_threads", numThreads},     {"small_chunks", 8},
                        {"medium_chunks", 16},           {"large_chunks", 32}};
    root["scenario_1_single_thread"] = singleThreadJson;
    root["scenario_2_throughput"] = throughputJson;

    std::ofstream out(filePath);
    if (!out.is_open()) {
        std::cerr << "[meshing_perf_test] ERROR: cannot write results to " << filePath << "\n";
        return false;
    }

    out << root.dump(2) << "\n";
    out.close();
    return true;
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    constexpr int warmupRounds = 3;
    constexpr int measureRounds = 10;
    constexpr int numThreads = 4;

    // Build config detection (best-effort)
#ifdef NDEBUG
    const std::string buildConfig = "Release";
#elif defined(_DEBUG)
    const std::string buildConfig = "Debug";
#else
    const std::string buildConfig = "RelWithDebInfo";
#endif

    // ── Build test data ───────────────────────────────────────────────
    // Small: 8 chunks
    std::vector<std::shared_ptr<Chunk>> smallChunks;
    for (int i = 0; i < 8; ++i) {
        smallChunks.push_back(makeSmallChunk(i, 0, 0xDEADBEEFU + static_cast<uint32_t>(i)));
    }

    // Medium: 16 chunks (1 full render-distance ring)
    std::vector<std::shared_ptr<Chunk>> mediumChunks;
    for (int i = 0; i < 16; ++i) {
        mediumChunks.push_back(makeMediumChunk(i, 0, 0xCAFEBABEU + static_cast<uint32_t>(i)));
    }

    // Large: 32 chunks (high density, high concurrency)
    std::vector<std::shared_ptr<Chunk>> largeChunks;
    for (int i = 0; i < 32; ++i) {
        largeChunks.push_back(makeLargeChunk(i, 0, 0xBAADF00DU + static_cast<uint32_t>(i)));
    }

    std::vector<TestCase> testCases = {{"small", smallChunks}, {"medium", mediumChunks}, {"large", largeChunks}};

    std::vector<CaseResult> singleThreadResults;
    std::vector<CaseResult> throughputResults;

    // ── Scenario 1: Single-thread sub-chunk meshing baseline ─────────
    std::cout << "[meshing_perf_test] === Scenario 1: sub-chunk meshing single-thread baseline ===\n";

    for (const auto& tc : testCases) {
        const BenchmarkStats stats = runSingleThreadBenchmark(tc, warmupRounds, measureRounds);
        printStats("meshing_perf_test", tc.name, stats, warmupRounds, measureRounds,
                   static_cast<int>(tc.chunks.size()));

        CaseResult cr;
        cr.name = tc.name;
        cr.medianMs = stats.medianMs;
        cr.p95Ms = stats.p95Ms;
        cr.avgMs = stats.avgMs;
        cr.minMs = stats.minMs;
        cr.maxMs = stats.maxMs;
        cr.callsPerSec = stats.callsPerSec;
        cr.nsPerCall = stats.nsPerCall;
        cr.checksum = stats.checksum;
        singleThreadResults.push_back(cr);
    }

    // ── Scenario 2: ThreadPool + ChunkMeshingService throughput ───────
    std::cout << "[meshing_perf_test] === Scenario 2: ChunkMeshingService throughput (" << numThreads
              << " threads) ===\n";
    for (const auto& tc : testCases) {
        const ThroughputStats stats = runServiceThroughput(tc.chunks, numThreads, warmupRounds, measureRounds);
        printThroughput(tc.name, numThreads, stats, warmupRounds, measureRounds);

        CaseResult cr;
        cr.name = tc.name;
        cr.totalChunks = stats.totalChunks;
        cr.threads = numThreads;
        cr.totalMs = stats.totalMs;
        cr.chunksPerSec = stats.chunksPerSec;
        cr.checksum = stats.checksum;
        throughputResults.push_back(cr);
    }

    // ── Write results to JSON ─────────────────────────────────────────
    const std::string projectRoot = resolveProjectRoot();
    const std::string outputPath =
        projectRoot + "tests/perf_baselines/meshing_perf_baseline_" + getCurrentTimestampSuffix() + ".json";
    if (writeResultsJson(outputPath, buildConfig, warmupRounds, measureRounds, numThreads, singleThreadResults,
                         throughputResults)) {
        std::cout << "[meshing_perf_test] Results written to " << outputPath << "\n";
    }

    std::cout << "[meshing_perf_test] PASS baseline_ready\n";
    return EXIT_SUCCESS;
}
