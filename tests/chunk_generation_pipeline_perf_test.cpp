#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include "../src/renderer/mesh/ChunkMesher.h"
#include "../src/world/gen/TerrainGenerator.h"
#include "../src/world/light/LightSolver.h"

namespace {

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

struct StageResult {
    std::string name;
    std::string focus;
    int workItems = 0;
    BenchmarkStats stats;
};

uint32_t nextRand(uint32_t& state) {
    state = state * 1664525U + 1013904223U;
    return state;
}

int64_t chunkKey(const int cx, const int cz) {
    return (static_cast<int64_t>(cx) << 32) |
           (static_cast<uint32_t>(cz) & 0xFFFFFFFFULL);
}

std::vector<BlockID> snapshotBlocks(const Chunk& chunk) {
    std::vector<BlockID> blocks(Chunk::BLOCK_COUNT, RUNTIME_ID_NULL);
    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                blocks[Chunk::toIndex(x, y, z)] = chunk.getBlock(x, y, z);
            }
        }
    }
    return blocks;
}

std::vector<uint8_t> snapshotPackedLight(const Chunk& chunk) {
    std::vector<uint8_t> packed(Chunk::BLOCK_COUNT, 0);
    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                packed[Chunk::toIndex(x, y, z)] = chunk.getPackedLight(x, y, z);
            }
        }
    }
    return packed;
}

LightJob makeChunkLoadedJob(const std::shared_ptr<Chunk>& chunk) {
    LightJob job;
    job.chunkKey = chunkKey(chunk->m_chunkX, chunk->m_chunkZ);
    job.revision = chunk->getLightRevision();
    job.chunk = chunk;
    job.reason = LightDirtyReason::ChunkLoaded;
    job.blockSnapshot = snapshotBlocks(*chunk);
    job.packedLightSnapshot = snapshotPackedLight(*chunk);
    return job;
}

void applyLightResult(const LightResult& result, const std::shared_ptr<Chunk>& chunk) {
    chunk->replacePackedLight(result.selfDelta.packedLight.data(),
                              result.selfDelta.packedLight.size(),
                              nullptr);
}

uint64_t checksumLightResult(const LightResult& result) {
    uint64_t checksum = static_cast<uint64_t>(result.nodesVisited) << 32U;
    checksum ^= static_cast<uint64_t>(result.selfDelta.dirtySubChunkMask);
    checksum ^= static_cast<uint64_t>(result.outgoing.size()) << 48U;

    static constexpr std::size_t sampleIndices[] = {
        0U,
        1U,
        17U,
        4096U,
        16384U,
        Chunk::BLOCK_COUNT - 1U
    };
    for (const std::size_t index : sampleIndices) {
        checksum = (checksum * 1315423911ULL) ^
                   static_cast<uint64_t>(result.selfDelta.packedLight[index]);
    }
    return checksum;
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

ChunkMeshData buildChunkMeshData(const Chunk& chunk) {
    ChunkMeshData merged;
    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        if (ChunkMesher::shouldSkipSubChunk(chunk, scy)) {
            continue;
        }

        const SubChunkMeshingSnapshotPtr snapshot = ChunkMesher::captureSubChunkSnapshot(chunk, scy);
        if (!snapshot) {
            continue;
        }

        ChunkMeshData subChunkMesh = ChunkMesher::buildSubChunkMeshData(*snapshot);
        const float yOffset = static_cast<float>(scy * SubChunk::SIZE);
        for (BlockVertex& vertex : subChunkMesh.opaqueVertices) { vertex.y += yOffset; }
        for (BlockVertex& vertex : subChunkMesh.cutoutVertices) { vertex.y += yOffset; }
        for (BlockVertex& vertex : subChunkMesh.cutoutDistanceVertices) { vertex.y += yOffset; }
        for (BlockVertex& vertex : subChunkMesh.transparentVertices) { vertex.y += yOffset; }
        for (BlockVertex& vertex : subChunkMesh.waterVertices) { vertex.y += yOffset; }

        merged.opaqueVertices.insert(merged.opaqueVertices.end(),
                                     subChunkMesh.opaqueVertices.begin(),
                                     subChunkMesh.opaqueVertices.end());
        merged.cutoutVertices.insert(merged.cutoutVertices.end(),
                                     subChunkMesh.cutoutVertices.begin(),
                                     subChunkMesh.cutoutVertices.end());
        merged.cutoutDistanceVertices.insert(merged.cutoutDistanceVertices.end(),
                                             subChunkMesh.cutoutDistanceVertices.begin(),
                                             subChunkMesh.cutoutDistanceVertices.end());
        merged.transparentVertices.insert(merged.transparentVertices.end(),
                                          subChunkMesh.transparentVertices.begin(),
                                          subChunkMesh.transparentVertices.end());
        merged.waterVertices.insert(merged.waterVertices.end(),
                                    subChunkMesh.waterVertices.begin(),
                                    subChunkMesh.waterVertices.end());
        merged.opaqueFaceCountBeforeGreedy += subChunkMesh.opaqueFaceCountBeforeGreedy;
        merged.opaqueFaceCountAfterGreedy += subChunkMesh.opaqueFaceCountAfterGreedy;
        merged.transparentFaceCountBeforeGreedy += subChunkMesh.transparentFaceCountBeforeGreedy;
        merged.transparentFaceCountAfterGreedy += subChunkMesh.transparentFaceCountAfterGreedy;
        if (subChunkMesh.hasBounds) {
            expandBounds(merged,
                         subChunkMesh.boundsMin + glm::vec3(0.0f, yOffset, 0.0f),
                         subChunkMesh.boundsMax + glm::vec3(0.0f, yOffset, 0.0f));
        }
    }

    merged.opaqueVertexCount = static_cast<uint32_t>(merged.opaqueVertices.size());
    return merged;
}

uint64_t checksumMeshData(const ChunkMeshData& meshData) {
    uint64_t checksum = static_cast<uint64_t>(meshData.opaqueVertices.size()) * 0x9E3779B97F4A7C15ULL;
    checksum ^= static_cast<uint64_t>(meshData.cutoutVertices.size()) << 1U;
    checksum ^= static_cast<uint64_t>(meshData.cutoutDistanceVertices.size()) << 3U;
    checksum ^= static_cast<uint64_t>(meshData.transparentVertices.size()) << 5U;
    checksum ^= static_cast<uint64_t>(meshData.waterVertices.size()) << 7U;
    checksum ^= static_cast<uint64_t>(meshData.opaqueFaceCountAfterGreedy) << 16U;
    checksum ^= static_cast<uint64_t>(meshData.transparentFaceCountAfterGreedy) << 32U;
    return checksum;
}

BenchmarkStats computeStats(std::vector<double> timingsMs, const uint64_t checksum, const int workItems) {
    std::sort(timingsMs.begin(), timingsMs.end());

    BenchmarkStats stats;
    stats.checksum = checksum;
    stats.minMs = timingsMs.front();
    stats.maxMs = timingsMs.back();
    stats.avgMs = std::accumulate(timingsMs.begin(), timingsMs.end(), 0.0) /
                  static_cast<double>(timingsMs.size());

    const std::size_t mid = timingsMs.size() / 2U;
    stats.medianMs = timingsMs.size() % 2U == 0U
        ? (timingsMs[mid - 1U] + timingsMs[mid]) * 0.5
        : timingsMs[mid];
    stats.p95Ms = timingsMs[(timingsMs.size() - 1U) * 95U / 100U];

    if (stats.medianMs > 0.0 && workItems > 0) {
        stats.callsPerSec = static_cast<double>(workItems) / (stats.medianMs / 1000.0);
        stats.nsPerCall = (stats.medianMs * 1'000'000.0) / static_cast<double>(workItems);
    }
    return stats;
}

template <typename Workload>
BenchmarkStats runBenchmark(const std::string& tag,
                            const std::string& caseName,
                            const int warmupRounds,
                            const int measureRounds,
                            const int workItems,
                            Workload&& workload) {
    for (int i = 0; i < warmupRounds; ++i) {
        (void)workload();
    }

    std::vector<double> timingsMs;
    timingsMs.reserve(static_cast<std::size_t>(measureRounds));
    uint64_t checksum = 0;

    for (int round = 0; round < measureRounds; ++round) {
        const auto start = std::chrono::high_resolution_clock::now();
        checksum ^= workload() + static_cast<uint64_t>(round + 1);
        const auto end = std::chrono::high_resolution_clock::now();
        timingsMs.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    BenchmarkStats stats = computeStats(std::move(timingsMs), checksum, workItems);
    std::cout << "[" << tag << "]"
              << " case=" << caseName
              << " work_items=" << workItems
              << " warmup=" << warmupRounds
              << " rounds=" << measureRounds
              << " median_ms=" << std::fixed << std::setprecision(3) << stats.medianMs
              << " p95_ms=" << stats.p95Ms
              << " avg_ms=" << stats.avgMs
              << " min_ms=" << stats.minMs
              << " max_ms=" << stats.maxMs
              << " calls_per_sec=" << std::setprecision(0) << stats.callsPerSec
              << " ns_per_call=" << std::setprecision(2) << stats.nsPerCall
              << " checksum=" << std::hex << stats.checksum << std::dec
              << "\n";
    return stats;
}

std::vector<std::pair<int, int>> buildChunkCoords(const int chunkCount) {
    std::vector<std::pair<int, int>> coords;
    coords.reserve(static_cast<std::size_t>(chunkCount));
    int ring = 0;
    while (static_cast<int>(coords.size()) < chunkCount) {
        for (int z = -ring; z <= ring && static_cast<int>(coords.size()) < chunkCount; ++z) {
            for (int x = -ring; x <= ring && static_cast<int>(coords.size()) < chunkCount; ++x) {
                if (ring > 0 && std::max(std::abs(x), std::abs(z)) != ring) {
                    continue;
                }
                coords.push_back({x, z});
            }
        }
        ++ring;
    }
    return coords;
}

std::shared_ptr<Chunk> generateLitChunk(const TerrainGenerator& generator, const int cx, const int cz) {
    std::shared_ptr<Chunk> chunk = std::make_shared<Chunk>(cx, cz);
    generator.generateChunk(*chunk);
    const LightResult light = LightSolver::solve(makeChunkLoadedJob(chunk));
    applyLightResult(light, chunk);
    return chunk;
}

std::string currentDate() {
    std::time_t now = std::time(nullptr);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &localTime);
    return buffer;
}

std::string resolveProjectRoot() {
    std::string file = __FILE__;
    const std::string suffix = "tests/chunk_generation_pipeline_perf_test.cpp";
    if (file.size() >= suffix.size() &&
        file.substr(file.size() - suffix.size()) == suffix) {
        return file.substr(0, file.size() - suffix.size());
    }

    std::size_t lastSep = file.find_last_of("/\\");
    if (lastSep != std::string::npos) {
        file = file.substr(0, lastSep);
        lastSep = file.find_last_of("/\\");
        if (lastSep != std::string::npos) {
            return file.substr(0, lastSep + 1U);
        }
    }
    return "./";
}

bool writeResultsJson(const std::string& filePath,
                      const std::string& buildConfig,
                      const int seed,
                      const int seaLevel,
                      const int warmupRounds,
                      const int measureRounds,
                      const std::vector<StageResult>& results) {
    using json = nlohmann::json;

    json cases = json::array();
    for (const StageResult& result : results) {
        cases.push_back({
            {"name", result.name},
            {"focus", result.focus},
            {"work_items", result.workItems},
            {"median_ms", result.stats.medianMs},
            {"p95_ms", result.stats.p95Ms},
            {"avg_ms", result.stats.avgMs},
            {"min_ms", result.stats.minMs},
            {"max_ms", result.stats.maxMs},
            {"calls_per_sec", result.stats.callsPerSec},
            {"ns_per_call", result.stats.nsPerCall},
            {"checksum", result.stats.checksum}
        });
    }

    json root;
    root["benchmark"] = "chunk_generation_pipeline_perf_test";
    root["focus"] = "Chunk generation stage breakdown: terrain sampling, chunk fill, lighting, meshing, full pipeline";
    root["captured_at"] = currentDate();
    root["build"] = {{"config", buildConfig}};
    root["settings"] = {
        {"seed", seed},
        {"sea_level", seaLevel},
        {"warmup_rounds", warmupRounds},
        {"measure_rounds", measureRounds}
    };
    root["cases"] = std::move(cases);

    std::ofstream out(filePath);
    if (!out.is_open()) {
        std::cerr << "[chunk_generation_pipeline_perf_test] ERROR: cannot write results to "
                  << filePath << "\n";
        return false;
    }

    out << root.dump(2) << "\n";
    return true;
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    constexpr uint32_t seed = 20260601U;
    constexpr int seaLevel = 63;
    constexpr int warmupRounds = 3;
    constexpr int measureRounds = 10;
    constexpr int sampleCount = 131072;
    constexpr int chunkBatchSize = 32;

#ifdef NDEBUG
    const std::string buildConfig = "Release";
#elif defined(_DEBUG)
    const std::string buildConfig = "Debug";
#else
    const std::string buildConfig = "RelWithDebInfo";
#endif

    TerrainGenerator generator;
    generator.init(seed, seaLevel);

    std::cout << "[chunk_generation_pipeline_perf_test] Starting stage pipeline baseline"
              << " build=" << buildConfig
              << " seed=" << seed
              << " sea_level=" << seaLevel
              << " warmup=" << warmupRounds
              << " rounds=" << measureRounds
              << "\n";

    std::vector<StageResult> results;

    StageResult surfaceSampling;
    surfaceSampling.name = "surface_sampling_batch";
    surfaceSampling.focus = "TerrainGenerator::sampleSurfaceYBatch over deterministic world columns";
    surfaceSampling.workItems = sampleCount;
    surfaceSampling.stats = runBenchmark(
        "chunk_generation_pipeline_perf_test",
        surfaceSampling.name,
        warmupRounds,
        measureRounds,
        sampleCount,
        [&generator, sampleCount]() -> uint64_t {
            const int localSampleCount = sampleCount;
            constexpr int batchWidth = 64;
            std::vector<int> samples(batchWidth, 0);
            uint32_t state = 0x12345678U;
            uint64_t checksum = 0;
            int sampled = 0;
            while (sampled < localSampleCount) {
                const int count = std::min(batchWidth, localSampleCount - sampled);
                const int startX = static_cast<int>(nextRand(state) & 4095U) - 2048;
                const int z = static_cast<int>(nextRand(state) & 4095U) - 2048;
                generator.sampleSurfaceYBatch(startX, z, count, samples.data());
                for (int i = 0; i < count; ++i) {
                    checksum += static_cast<uint64_t>(samples[static_cast<std::size_t>(i)] + i + 1);
                }
                sampled += count;
            }
            return checksum;
        });
    results.push_back(surfaceSampling);

    const std::vector<std::pair<int, int>> coords = buildChunkCoords(chunkBatchSize);

    StageResult terrainGeneration;
    terrainGeneration.name = "terrain_generate_chunks";
    terrainGeneration.focus = "TerrainGenerator::generateChunk full column fill including caves, ores, trees, vegetation";
    terrainGeneration.workItems = chunkBatchSize;
    terrainGeneration.stats = runBenchmark(
        "chunk_generation_pipeline_perf_test",
        terrainGeneration.name,
        warmupRounds,
        measureRounds,
        chunkBatchSize,
        [&generator, &coords]() -> uint64_t {
            uint64_t checksum = 0;
            for (std::size_t i = 0; i < coords.size(); ++i) {
                Chunk chunk(coords[i].first, coords[i].second);
                generator.generateChunk(chunk);
                checksum ^= static_cast<uint64_t>(chunk.getHeightMap(static_cast<int>(i) % Chunk::SIZE_X,
                                                                      static_cast<int>(i * 3U) % Chunk::SIZE_Z))
                            << (i % 17U);
                checksum ^= static_cast<uint64_t>(chunk.getBlock(static_cast<int>(i) % Chunk::SIZE_X,
                                                                 32 + static_cast<int>(i % 48U),
                                                                 static_cast<int>(i * 5U) % Chunk::SIZE_Z));
            }
            return checksum;
        });
    results.push_back(terrainGeneration);

    std::vector<std::shared_ptr<Chunk>> litInputChunks;
    litInputChunks.reserve(coords.size());
    for (const auto& coord : coords) {
        std::shared_ptr<Chunk> chunk = std::make_shared<Chunk>(coord.first, coord.second);
        generator.generateChunk(*chunk);
        litInputChunks.push_back(std::move(chunk));
    }

    StageResult lighting;
    lighting.name = "light_solver_chunk_loaded";
    lighting.focus = "LightSolver::solve chunk-loaded rebuild over generated terrain snapshots";
    lighting.workItems = chunkBatchSize;
    lighting.stats = runBenchmark(
        "chunk_generation_pipeline_perf_test",
        lighting.name,
        warmupRounds,
        measureRounds,
        chunkBatchSize,
        [&litInputChunks]() -> uint64_t {
            uint64_t checksum = 0;
            for (std::size_t i = 0; i < litInputChunks.size(); ++i) {
                const LightResult light = LightSolver::solve(makeChunkLoadedJob(litInputChunks[i]));
                checksum ^= checksumLightResult(light) + static_cast<uint64_t>(i + 1U);
            }
            return checksum;
        });
    results.push_back(lighting);

    std::vector<std::shared_ptr<Chunk>> meshingInputChunks;
    meshingInputChunks.reserve(coords.size());
    for (const auto& coord : coords) {
        meshingInputChunks.push_back(generateLitChunk(generator, coord.first, coord.second));
    }

    StageResult meshing;
    meshing.name = "chunk_meshing_after_light";
    meshing.focus = "ChunkMesher snapshot capture and sub-chunk mesh building over generated lit chunks";
    meshing.workItems = chunkBatchSize;
    meshing.stats = runBenchmark(
        "chunk_generation_pipeline_perf_test",
        meshing.name,
        warmupRounds,
        measureRounds,
        chunkBatchSize,
        [&meshingInputChunks]() -> uint64_t {
            uint64_t checksum = 0;
            for (std::size_t i = 0; i < meshingInputChunks.size(); ++i) {
                const ChunkMeshData meshData = buildChunkMeshData(*meshingInputChunks[i]);
                checksum ^= checksumMeshData(meshData) + static_cast<uint64_t>(i + 1U);
            }
            return checksum;
        });
    results.push_back(meshing);

    StageResult fullPipeline;
    fullPipeline.name = "terrain_light_mesh_full_pipeline";
    fullPipeline.focus = "Generate terrain, solve initial light, apply light, and build chunk meshes";
    fullPipeline.workItems = chunkBatchSize;
    fullPipeline.stats = runBenchmark(
        "chunk_generation_pipeline_perf_test",
        fullPipeline.name,
        warmupRounds,
        measureRounds,
        chunkBatchSize,
        [&generator, &coords]() -> uint64_t {
            uint64_t checksum = 0;
            for (std::size_t i = 0; i < coords.size(); ++i) {
                std::shared_ptr<Chunk> chunk = std::make_shared<Chunk>(coords[i].first, coords[i].second);
                generator.generateChunk(*chunk);
                const LightResult light = LightSolver::solve(makeChunkLoadedJob(chunk));
                applyLightResult(light, chunk);
                const ChunkMeshData meshData = buildChunkMeshData(*chunk);
                checksum ^= checksumLightResult(light);
                checksum ^= checksumMeshData(meshData) + static_cast<uint64_t>(i + 1U);
            }
            return checksum;
        });
    results.push_back(fullPipeline);

    const std::string outputPath =
        resolveProjectRoot() + "tests/perf_baselines/chunk_generation_pipeline_perf_baseline.json";
    if (writeResultsJson(outputPath,
                         buildConfig,
                         static_cast<int>(seed),
                         seaLevel,
                         warmupRounds,
                         measureRounds,
                         results)) {
        std::cout << "[chunk_generation_pipeline_perf_test] Results written to "
                  << outputPath << "\n";
    }

    uint64_t finalChecksum = 0;
    for (const StageResult& result : results) {
        finalChecksum ^= result.stats.checksum;
    }

    std::cout << "[chunk_generation_pipeline_perf_test] PASS baseline_ready checksum="
              << std::hex << finalChecksum << std::dec << "\n";
    return EXIT_SUCCESS;
}
