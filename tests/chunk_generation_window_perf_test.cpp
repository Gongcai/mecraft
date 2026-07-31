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
#include "../src/world/block/BlockStateRegistry.h"
#include "../src/world/gen/TerrainGenerator.h"
#include "../src/world/light/LightSolver.h"

namespace {

struct BenchmarkStats {
    double medianMs = 0.0;
    double avgMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
    double p95Ms = 0.0;
    double chunksPerSec = 0.0;
    uint64_t checksum = 0;
};

struct WindowCase {
    std::string name;
    int radius = 1;
    int chunkCount = 0;
    BenchmarkStats stats;
};

int64_t chunkKey(const int cx, const int cz) {
    return (static_cast<int64_t>(cx) << 32) | (static_cast<uint32_t>(cz) & 0xFFFFFFFFULL);
}

std::vector<BlockID> snapshotBlocks(const Chunk& chunk) {
    std::vector<BlockID> blocks(Chunk::BLOCK_COUNT, RUNTIME_ID_NULL);
    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                blocks[Chunk::toIndex(x, y, z)] = BlockStateRegistry::getBlockId(chunk.getBlock(x, y, z));
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
    if (chunk->neighbors[0]) {
        job.neighborPosX = std::shared_ptr<const Chunk>(chunk, chunk->neighbors[0]);
    }
    if (chunk->neighbors[1]) {
        job.neighborNegX = std::shared_ptr<const Chunk>(chunk, chunk->neighbors[1]);
    }
    if (chunk->neighbors[2]) {
        job.neighborPosZ = std::shared_ptr<const Chunk>(chunk, chunk->neighbors[2]);
    }
    if (chunk->neighbors[3]) {
        job.neighborNegZ = std::shared_ptr<const Chunk>(chunk, chunk->neighbors[3]);
    }
    job.forceOutgoingBoundaryMask = 0x0F;
    return job;
}

void applyLightResult(const LightResult& result, const std::shared_ptr<Chunk>& chunk) {
    chunk->replacePackedLight(result.selfDelta.packedLight.data(), result.selfDelta.packedLight.size(), nullptr);
}

uint64_t checksumLightResult(const LightResult& result) {
    uint64_t checksum = static_cast<uint64_t>(result.nodesVisited) << 32U;
    checksum ^= static_cast<uint64_t>(result.selfDelta.dirtySubChunkMask);
    checksum ^= static_cast<uint64_t>(result.outgoing.size()) << 48U;
    static constexpr std::size_t sampleIndices[] = {0U, 255U, 4096U, 16384U, Chunk::BLOCK_COUNT - 1U};
    for (const std::size_t index : sampleIndices) {
        checksum = (checksum * 1099511628211ULL) ^ static_cast<uint64_t>(result.selfDelta.packedLight[index]);
    }
    for (const BorderUpdateBatch& batch : result.outgoing) {
        checksum ^= static_cast<uint64_t>(batch.nodes.size() + 1U) << (batch.fromDirection + 8U);
        checksum ^= static_cast<uint64_t>(batch.dirtySubChunkMask) * 1315423911ULL;
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
        for (BlockVertex& vertex : subChunkMesh.opaqueVertices) {
            vertex.y += yOffset;
        }
        for (BlockVertex& vertex : subChunkMesh.cutoutVertices) {
            vertex.y += yOffset;
        }
        for (BlockVertex& vertex : subChunkMesh.cutoutDistanceVertices) {
            vertex.y += yOffset;
        }
        for (BlockVertex& vertex : subChunkMesh.transparentVertices) {
            vertex.y += yOffset;
        }
        for (BlockVertex& vertex : subChunkMesh.waterVertices) {
            vertex.y += yOffset;
        }

        merged.opaqueVertices.insert(merged.opaqueVertices.end(), subChunkMesh.opaqueVertices.begin(),
                                     subChunkMesh.opaqueVertices.end());
        merged.cutoutVertices.insert(merged.cutoutVertices.end(), subChunkMesh.cutoutVertices.begin(),
                                     subChunkMesh.cutoutVertices.end());
        merged.cutoutDistanceVertices.insert(merged.cutoutDistanceVertices.end(),
                                             subChunkMesh.cutoutDistanceVertices.begin(),
                                             subChunkMesh.cutoutDistanceVertices.end());
        merged.transparentVertices.insert(merged.transparentVertices.end(), subChunkMesh.transparentVertices.begin(),
                                          subChunkMesh.transparentVertices.end());
        merged.waterVertices.insert(merged.waterVertices.end(), subChunkMesh.waterVertices.begin(),
                                    subChunkMesh.waterVertices.end());
        merged.opaqueFaceCountBeforeGreedy += subChunkMesh.opaqueFaceCountBeforeGreedy;
        merged.opaqueFaceCountAfterGreedy += subChunkMesh.opaqueFaceCountAfterGreedy;
        merged.transparentFaceCountBeforeGreedy += subChunkMesh.transparentFaceCountBeforeGreedy;
        merged.transparentFaceCountAfterGreedy += subChunkMesh.transparentFaceCountAfterGreedy;
        if (subChunkMesh.hasBounds) {
            expandBounds(merged, subChunkMesh.boundsMin + glm::vec3(0.0f, yOffset, 0.0f),
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

struct ChunkWindow {
    int radius = 0;
    int side = 0;
    std::vector<std::shared_ptr<Chunk>> chunks;

    std::shared_ptr<Chunk>& at(const int gx, const int gz) { return chunks[static_cast<std::size_t>(gz * side + gx)]; }

    const std::shared_ptr<Chunk>& at(const int gx, const int gz) const {
        return chunks[static_cast<std::size_t>(gz * side + gx)];
    }
};

void linkNeighbors(ChunkWindow& window) {
    for (int gz = 0; gz < window.side; ++gz) {
        for (int gx = 0; gx < window.side; ++gx) {
            Chunk& chunk = *window.at(gx, gz);
            chunk.neighbors[0] = (gx + 1 < window.side) ? window.at(gx + 1, gz).get() : nullptr;
            chunk.neighbors[1] = (gx > 0) ? window.at(gx - 1, gz).get() : nullptr;
            chunk.neighbors[2] = (gz + 1 < window.side) ? window.at(gx, gz + 1).get() : nullptr;
            chunk.neighbors[3] = (gz > 0) ? window.at(gx, gz - 1).get() : nullptr;
            chunk.linkExistingSubChunksWithNeighbor(0);
            chunk.linkExistingSubChunksWithNeighbor(1);
            chunk.linkExistingSubChunksWithNeighbor(2);
            chunk.linkExistingSubChunksWithNeighbor(3);
        }
    }
}

ChunkWindow buildWindow(const TerrainGenerator& generator, const int radius) {
    ChunkWindow window;
    window.radius = radius;
    window.side = radius * 2 + 1;
    window.chunks.reserve(static_cast<std::size_t>(window.side * window.side));

    for (int gz = 0; gz < window.side; ++gz) {
        for (int gx = 0; gx < window.side; ++gx) {
            const int cx = gx - radius;
            const int cz = gz - radius;
            std::shared_ptr<Chunk> chunk = std::make_shared<Chunk>(cx, cz);
            generator.generateChunk(*chunk);
            window.chunks.push_back(std::move(chunk));
        }
    }

    linkNeighbors(window);
    return window;
}

uint64_t runFullWindowPipeline(const TerrainGenerator& generator, const int radius) {
    ChunkWindow window = buildWindow(generator, radius);
    uint64_t checksum = 0;

    std::vector<LightResult> lightResults;
    lightResults.reserve(window.chunks.size());
    for (const std::shared_ptr<Chunk>& chunk : window.chunks) {
        LightResult light = LightSolver::solve(makeChunkLoadedJob(chunk));
        checksum ^= checksumLightResult(light);
        lightResults.push_back(std::move(light));
    }

    for (std::size_t i = 0; i < window.chunks.size(); ++i) {
        applyLightResult(lightResults[i], window.chunks[i]);
    }

    for (std::size_t i = 0; i < window.chunks.size(); ++i) {
        const ChunkMeshData meshData = buildChunkMeshData(*window.chunks[i]);
        checksum ^= checksumMeshData(meshData) + static_cast<uint64_t>(i + 1U);
    }

    return checksum;
}

BenchmarkStats computeStats(std::vector<double> timingsMs, const uint64_t checksum, const int chunkCount) {
    std::sort(timingsMs.begin(), timingsMs.end());

    BenchmarkStats stats;
    stats.checksum = checksum;
    stats.minMs = timingsMs.front();
    stats.maxMs = timingsMs.back();
    stats.avgMs = std::accumulate(timingsMs.begin(), timingsMs.end(), 0.0) / static_cast<double>(timingsMs.size());

    const std::size_t mid = timingsMs.size() / 2U;
    stats.medianMs = timingsMs.size() % 2U == 0U ? (timingsMs[mid - 1U] + timingsMs[mid]) * 0.5 : timingsMs[mid];
    stats.p95Ms = timingsMs[(timingsMs.size() - 1U) * 95U / 100U];

    if (stats.medianMs > 0.0 && chunkCount > 0) {
        stats.chunksPerSec = static_cast<double>(chunkCount) / (stats.medianMs / 1000.0);
    }
    return stats;
}

BenchmarkStats runBenchmark(const std::string& caseName, const TerrainGenerator& generator, const int radius,
                            const int warmupRounds, const int measureRounds) {
    const int side = radius * 2 + 1;
    const int chunkCount = side * side;

    for (int warmup = 0; warmup < warmupRounds; ++warmup) {
        (void)runFullWindowPipeline(generator, radius);
    }

    std::vector<double> timingsMs;
    timingsMs.reserve(static_cast<std::size_t>(measureRounds));
    uint64_t checksum = 0;

    for (int round = 0; round < measureRounds; ++round) {
        const auto start = std::chrono::high_resolution_clock::now();
        checksum ^= runFullWindowPipeline(generator, radius) + static_cast<uint64_t>(round + 1);
        const auto end = std::chrono::high_resolution_clock::now();
        timingsMs.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    BenchmarkStats stats = computeStats(std::move(timingsMs), checksum, chunkCount);
    std::cout << "[chunk_generation_window_perf_test]"
              << " case=" << caseName << " radius=" << radius << " chunks=" << chunkCount << " warmup=" << warmupRounds
              << " rounds=" << measureRounds << " median_ms=" << std::fixed << std::setprecision(3) << stats.medianMs
              << " p95_ms=" << stats.p95Ms << " avg_ms=" << stats.avgMs << " min_ms=" << stats.minMs
              << " max_ms=" << stats.maxMs << " chunks_per_sec=" << std::setprecision(0) << stats.chunksPerSec
              << " checksum=" << std::hex << stats.checksum << std::dec << "\n";
    return stats;
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
    const std::string suffix = "tests/chunk_generation_window_perf_test.cpp";
    if (file.size() >= suffix.size() && file.substr(file.size() - suffix.size()) == suffix) {
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

bool writeResultsJson(const std::string& filePath, const std::string& buildConfig, const int seed, const int seaLevel,
                      const int warmupRounds, const int measureRounds, const std::vector<WindowCase>& cases) {
    using json = nlohmann::json;

    json caseJson = json::array();
    for (const WindowCase& c : cases) {
        caseJson.push_back({{"name", c.name},
                            {"radius", c.radius},
                            {"chunks", c.chunkCount},
                            {"median_ms", c.stats.medianMs},
                            {"p95_ms", c.stats.p95Ms},
                            {"avg_ms", c.stats.avgMs},
                            {"min_ms", c.stats.minMs},
                            {"max_ms", c.stats.maxMs},
                            {"chunks_per_sec", c.stats.chunksPerSec},
                            {"checksum", c.stats.checksum}});
    }

    json root;
    root["benchmark"] = "chunk_generation_window_perf_test";
    root["focus"] = "End-to-end generated chunk window: terrain generation, neighbor linking, lighting, meshing";
    root["captured_at"] = currentDate();
    root["build"] = {{"config", buildConfig}};
    root["settings"] = {
        {"seed", seed}, {"sea_level", seaLevel}, {"warmup_rounds", warmupRounds}, {"measure_rounds", measureRounds}};
    root["cases"] = std::move(caseJson);

    std::ofstream out(filePath);
    if (!out.is_open()) {
        std::cerr << "[chunk_generation_window_perf_test] ERROR: cannot write results to " << filePath << "\n";
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
    constexpr int warmupRounds = 2;
    constexpr int measureRounds = 6;

#ifdef NDEBUG
    const std::string buildConfig = "Release";
#elif defined(_DEBUG)
    const std::string buildConfig = "Debug";
#else
    const std::string buildConfig = "RelWithDebInfo";
#endif

    TerrainGenerator generator;
    generator.init(seed, seaLevel);

    std::cout << "[chunk_generation_window_perf_test] Starting end-to-end chunk window baseline"
              << " build=" << buildConfig << " seed=" << seed << " sea_level=" << seaLevel << " warmup=" << warmupRounds
              << " rounds=" << measureRounds << "\n";

    std::vector<WindowCase> cases = {{"window_3x3", 1, 9, {}}, {"window_5x5", 2, 25, {}}};

    for (WindowCase& c : cases) {
        c.stats = runBenchmark(c.name, generator, c.radius, warmupRounds, measureRounds);
    }

    const std::string outputPath =
        resolveProjectRoot() + "tests/perf_baselines/chunk_generation_window_perf_baseline.json";
    if (writeResultsJson(outputPath, buildConfig, static_cast<int>(seed), seaLevel, warmupRounds, measureRounds,
                         cases)) {
        std::cout << "[chunk_generation_window_perf_test] Results written to " << outputPath << "\n";
    }

    uint64_t finalChecksum = 0;
    for (const WindowCase& c : cases) {
        finalChecksum ^= c.stats.checksum;
    }

    std::cout << "[chunk_generation_window_perf_test] PASS baseline_ready checksum=" << std::hex << finalChecksum
              << std::dec << "\n";
    return EXIT_SUCCESS;
}
