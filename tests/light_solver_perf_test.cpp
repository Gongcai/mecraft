#include <algorithm>
#include <array>
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

#include <nlohmann/json.hpp>

#include "../src/world/Chunk.h"
#include "../src/world/LightSolver.h"

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "[light_solver_perf_test] FAIL: " << message << "\n";
    std::cerr.flush();
    std::_Exit(EXIT_FAILURE);
}

struct BenchmarkStats {
    double medianMs = 0.0;
    double avgMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
    double p95Ms = 0.0;
    double callsPerSec = 0.0;
    double nsPerCall = 0.0;
    double avgNodesVisited = 0.0;
    double avgOutgoingNodes = 0.0;
    double avgWorkerMs = 0.0;
    uint64_t checksum = 0;
};

struct PerfCase {
    std::string name;
    std::string focus;
    std::vector<LightJob> jobs;
};

uint32_t mix(uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

int64_t chunkKey(const int cx, const int cz) {
    return (static_cast<int64_t>(cx) << 32) |
           (static_cast<uint32_t>(cz) & 0xFFFFFFFFULL);
}

std::vector<BlockID> snapshotBlocks(const Chunk& chunk) {
    std::vector<BlockID> blocks(Chunk::BLOCK_COUNT, BlockIds::AIR);
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

void applyResultToChunk(const LightResult& result, const std::shared_ptr<Chunk>& chunk) {
    chunk->replacePackedLight(result.selfDelta.packedLight.data(),
                              result.selfDelta.packedLight.size(),
                              nullptr);
}

void carveRoom(Chunk& chunk,
               const int x0,
               const int x1,
               const int y0,
               const int y1,
               const int z0,
               const int z1) {
    for (int y = y0; y <= y1; ++y) {
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                chunk.setBlockFast(x, y, z, BlockIds::AIR);
            }
        }
    }
}

void carveColumn(Chunk& chunk,
                 const int x,
                 const int y0,
                 const int y1,
                 const int z,
                 const BlockID id) {
    for (int y = y0; y <= y1; ++y) {
        chunk.setBlockFast(x, y, z, id);
    }
}

std::shared_ptr<Chunk> makeLightingStressChunk(const int cx,
                                               const int cz,
                                               const uint32_t seed) {
    auto chunk = std::make_shared<Chunk>(cx, cz);

    for (int z = 0; z < Chunk::SIZE_Z; ++z) {
        for (int x = 0; x < Chunk::SIZE_X; ++x) {
            const uint32_t surfaceNoise = mix(seed ^ static_cast<uint32_t>(x * 131 + z * 313));
            const int surfaceY = 74 + static_cast<int>(surfaceNoise % 8U);
            for (int y = 0; y <= surfaceY; ++y) {
                BlockID id = BlockIds::STONE;
                if (y == surfaceY) {
                    id = BlockIds::GRASS;
                } else if (y >= surfaceY - 2) {
                    id = BlockIds::DIRT;
                } else if (y < 3) {
                    id = BlockIds::BEDROCK;
                }
                chunk->setBlockFast(x, y, z, id);
            }
        }
    }

    carveRoom(*chunk, 2, 13, 11, 17, 2, 13);
    carveRoom(*chunk, 3, 12, 20, 24, 3, 12);
    carveRoom(*chunk, 5, 10, 28, 31, 5, 10);
    carveRoom(*chunk, 1, 14, 13, 15, 7, 9);
    carveRoom(*chunk, 7, 9, 13, 15, 1, 14);
    carveRoom(*chunk, 4, 11, 22, 23, 7, 9);
    carveRoom(*chunk, 7, 9, 22, 23, 4, 11);
    carveRoom(*chunk, 4, 4, 16, 31, 4, 4);
    carveRoom(*chunk, 11, 11, 16, 31, 11, 11);

    carveColumn(*chunk, 4, 32, 80, 4, BlockIds::AIR);
    carveColumn(*chunk, 11, 32, 80, 11, BlockIds::AIR);
    carveColumn(*chunk, 8, 25, 80, 8, BlockIds::AIR);

    carveColumn(*chunk, 11, 48, 52, 11, BlockIds::WATER);
    carveColumn(*chunk, 4, 46, 49, 4, BlockIds::GLASS);

    for (int x = 2; x <= 13; ++x) {
        chunk->setBlockFast(x, 18, 2, BlockIds::GLASS);
        chunk->setBlockFast(x, 18, 13, BlockIds::GLASS);
    }
    for (int z = 2; z <= 13; ++z) {
        chunk->setBlockFast(2, 18, z, BlockIds::GLASS);
        chunk->setBlockFast(13, 18, z, BlockIds::GLASS);
    }

    const std::array<std::array<int, 3>, 6> torchPositions = {{
        {4, 14, 8},
        {8, 14, 4},
        {8, 14, 12},
        {12, 14, 8},
        {6, 23, 8},
        {10, 23, 8}
    }};
    for (const auto& pos : torchPositions) {
        chunk->setBlockFast(pos[0], pos[1], pos[2], BlockIds::TORCH);
    }

    const std::array<std::array<int, 2>, 6> floraPositions = {{
        {2, 2},
        {4, 11},
        {7, 8},
        {9, 13},
        {12, 3},
        {13, 12}
    }};
    for (size_t i = 0; i < floraPositions.size(); ++i) {
        const int x = floraPositions[i][0];
        const int z = floraPositions[i][1];
        const BlockID flora = ((seed + static_cast<uint32_t>(i)) & 1U) == 0U
            ? BlockIds::TALL_GRASS
            : BlockIds::ROSE;
        for (int y = Chunk::SIZE_Y - 2; y >= 0; --y) {
            if (chunk->getBlock(x, y, z) != BlockIds::AIR) {
                if (y + 1 < Chunk::SIZE_Y) {
                    chunk->setBlockFast(x, y + 1, z, flora);
                }
                break;
            }
        }
    }

    const std::array<std::array<int, 3>, 10> scatterPositions = {{
        {3, 12, 6},
        {5, 12, 10},
        {6, 21, 6},
        {10, 21, 10},
        {8, 29, 6},
        {6, 29, 8},
        {10, 29, 8},
        {8, 29, 10},
        {5, 15, 5},
        {11, 15, 11}
    }};
    for (size_t i = 0; i < scatterPositions.size(); ++i) {
        const auto& pos = scatterPositions[i];
        const uint32_t value = mix(seed + static_cast<uint32_t>(i * 17U));
        BlockID id = BlockIds::AIR;
        switch (value % 4U) {
            case 0: id = BlockIds::GLASS; break;
            case 1: id = BlockIds::WATER; break;
            case 2: id = BlockIds::STONE; break;
            default: break;
        }
        chunk->setBlockFast(pos[0], pos[1], pos[2], id);
    }

    return chunk;
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

LightJob makeBlockChangedJob(const int cx,
                             const int cz,
                             const uint32_t seed) {
    const std::shared_ptr<Chunk> chunk = makeLightingStressChunk(cx, cz, seed);
    const LightResult initial = LightSolver::solve(makeChunkLoadedJob(chunk));
    applyResultToChunk(initial, chunk);

    std::vector<LocalLightChange> changes;

    const auto pushChange = [&](const int x,
                                const int y,
                                const int z,
                                const BlockID newId) {
        const BlockID oldId = chunk->getBlock(x, y, z);
        if (oldId == newId) {
            return;
        }

        chunk->setBlockFast(x, y, z, newId);
        changes.push_back(LocalLightChange{
            static_cast<uint8_t>(x),
            static_cast<uint8_t>(y),
            static_cast<uint8_t>(z),
            oldId,
            newId
        });
    };

    pushChange(4, 14, 8, BlockIds::AIR);
    pushChange(12, 14, 8, BlockIds::AIR);
    pushChange(8, 23, 8, BlockIds::TORCH);
    pushChange(8, 32, 8, BlockIds::STONE);
    pushChange(11, 49, 11, BlockIds::AIR);
    pushChange(4, 47, 4, BlockIds::STONE);

    LightJob job;
    job.chunkKey = chunkKey(chunk->m_chunkX, chunk->m_chunkZ);
    job.revision = chunk->getLightRevision();
    job.chunk = chunk;
    job.reason = LightDirtyReason::BlockChanged;
    job.blockSnapshot = snapshotBlocks(*chunk);
    job.packedLightSnapshot = snapshotPackedLight(*chunk);
    job.blockChanges = std::move(changes);
    return job;
}

BorderUpdateBatch findOutgoingBatch(const LightResult& result,
                                    const int64_t targetKey,
                                    const uint8_t fromDirection) {
    for (const BorderUpdateBatch& batch : result.outgoing) {
        if (batch.targetChunkKey == targetKey && batch.fromDirection == fromDirection) {
            return batch;
        }
    }
    fail("expected outgoing boundary batch was not produced");
}

void buildBoundaryTunnel(const std::shared_ptr<Chunk>& left,
                         const std::shared_ptr<Chunk>& right,
                         const int y,
                         const int z) {
    for (int iy = 0; iy <= 40; ++iy) {
        for (int iz = 0; iz < Chunk::SIZE_Z; ++iz) {
            for (int ix = 0; ix < Chunk::SIZE_X; ++ix) {
                left->setBlockFast(ix, iy, iz, BlockIds::STONE);
                right->setBlockFast(ix, iy, iz, BlockIds::STONE);
            }
        }
    }

    for (int x = 10; x <= 15; ++x) {
        left->setBlockFast(x, y, z, BlockIds::AIR);
    }
    for (int x = 0; x <= 5; ++x) {
        right->setBlockFast(x, y, z, BlockIds::AIR);
    }

    left->setBlockFast(14, y, z, BlockIds::TORCH);
    left->setBlockFast(12, y + 1, z, BlockIds::GLASS);
    right->setBlockFast(1, y + 1, z, BlockIds::GLASS);
}

LightJob makeNeighborBoundaryJob(const int pairIndex,
                                 const int y,
                                 const int z) {
    const int leftX = pairIndex * 2;
    const int rightX = leftX + 1;
    const std::shared_ptr<Chunk> left = std::make_shared<Chunk>(leftX, 0);
    const std::shared_ptr<Chunk> right = std::make_shared<Chunk>(rightX, 0);

    buildBoundaryTunnel(left, right, y, z);

    LightJob leftLitJob = makeChunkLoadedJob(left);
    leftLitJob.neighborPosX = right;
    const LightResult leftLit = LightSolver::solve(leftLitJob);
    const BorderUpdateBatch litBoundary = findOutgoingBatch(
        leftLit,
        chunkKey(right->m_chunkX, right->m_chunkZ),
        0);

    LightJob rightLitJob = makeChunkLoadedJob(right);
    rightLitJob.reason = LightDirtyReason::NeighborBoundary;
    rightLitJob.changedBoundaryDirections[0] = true;
    rightLitJob.inbox.push_back(litBoundary);
    const LightResult rightLit = LightSolver::solve(rightLitJob);
    applyResultToChunk(rightLit, right);

    left->setBlockFast(14, y, z, BlockIds::AIR);

    LightJob leftRemovedJob = makeChunkLoadedJob(left);
    leftRemovedJob.neighborPosX = right;
    const LightResult leftRemoved = LightSolver::solve(leftRemovedJob);
    const BorderUpdateBatch removedBoundary = findOutgoingBatch(
        leftRemoved,
        chunkKey(right->m_chunkX, right->m_chunkZ),
        0);

    LightJob job;
    job.chunkKey = chunkKey(right->m_chunkX, right->m_chunkZ);
    job.revision = right->getLightRevision();
    job.chunk = right;
    job.reason = LightDirtyReason::NeighborBoundary;
    job.blockSnapshot = snapshotBlocks(*right);
    job.packedLightSnapshot = snapshotPackedLight(*right);
    job.previousInbox.push_back(litBoundary);
    job.inbox.push_back(removedBoundary);
    job.changedBoundaryDirections[0] = true;
    return job;
}

uint64_t checksumResult(const LightResult& result) {
    static constexpr std::array<std::size_t, 8> sampleIndices = {
        0U,
        1U,
        17U,
        255U,
        1024U,
        8191U,
        16384U,
        Chunk::BLOCK_COUNT - 1U
    };

    uint64_t checksum = static_cast<uint64_t>(result.nodesVisited) << 32U;
    checksum ^= static_cast<uint64_t>(result.selfDelta.dirtySubChunkMask);
    checksum ^= static_cast<uint64_t>(result.outgoing.size()) << 48U;
    for (const std::size_t index : sampleIndices) {
        checksum = (checksum * 1315423911ULL) ^
                   static_cast<uint64_t>(result.selfDelta.packedLight[index]);
    }
    for (const BorderUpdateBatch& batch : result.outgoing) {
        checksum ^= static_cast<uint64_t>(batch.dirtySubChunkMask) * 1099511628211ULL;
        checksum ^= static_cast<uint64_t>(batch.nodes.size() + 1U) << (batch.fromDirection + 8U);
    }
    return checksum;
}

BenchmarkStats computeStats(std::vector<double> timingsMs,
                            const uint64_t checksum,
                            const double avgNodesVisited,
                            const double avgOutgoingNodes,
                            const double avgWorkerMs,
                            const size_t solvesPerRound) {
    std::sort(timingsMs.begin(), timingsMs.end());

    BenchmarkStats stats;
    stats.checksum = checksum;
    stats.minMs = timingsMs.front();
    stats.maxMs = timingsMs.back();
    stats.avgMs = std::accumulate(timingsMs.begin(), timingsMs.end(), 0.0) /
                  static_cast<double>(timingsMs.size());

    const size_t mid = timingsMs.size() / 2U;
    if (timingsMs.size() % 2U == 0U) {
        stats.medianMs = (timingsMs[mid - 1U] + timingsMs[mid]) * 0.5;
    } else {
        stats.medianMs = timingsMs[mid];
    }

    const size_t p95Index = (timingsMs.size() - 1U) * 95U / 100U;
    stats.p95Ms = timingsMs[p95Index];
    stats.avgNodesVisited = avgNodesVisited;
    stats.avgOutgoingNodes = avgOutgoingNodes;
    stats.avgWorkerMs = avgWorkerMs;

    if (stats.medianMs > 0.0) {
        const double medianSec = stats.medianMs / 1000.0;
        stats.callsPerSec = static_cast<double>(solvesPerRound) / medianSec;
        stats.nsPerCall = (stats.medianMs * 1'000'000.0) / static_cast<double>(solvesPerRound);
    }

    return stats;
}

BenchmarkStats runBenchmark(const PerfCase& perfCase,
                            const int warmupRounds,
                            const int measureRounds) {
    for (int warmup = 0; warmup < warmupRounds; ++warmup) {
        uint64_t warmupChecksum = 0;
        for (const LightJob& job : perfCase.jobs) {
            warmupChecksum ^= checksumResult(LightSolver::solve(job));
        }
        if (warmupChecksum == 0xFFFFFFFFFFFFFFFFULL) {
            std::cout << "[light_solver_perf_test] impossible_warmup_checksum=" << warmupChecksum << "\n";
        }
    }

    std::vector<double> timingsMs;
    timingsMs.reserve(static_cast<size_t>(measureRounds));
    uint64_t checksum = 0;
    uint64_t totalNodesVisited = 0;
    uint64_t totalOutgoingNodes = 0;
    double totalWorkerMs = 0.0;

    for (int round = 0; round < measureRounds; ++round) {
        const auto start = std::chrono::high_resolution_clock::now();
        uint64_t roundChecksum = 0;
        for (size_t i = 0; i < perfCase.jobs.size(); ++i) {
            const LightResult result = LightSolver::solve(perfCase.jobs[i]);
            roundChecksum ^= checksumResult(result) + static_cast<uint64_t>(i + 1U);
            totalNodesVisited += result.nodesVisited;
            totalWorkerMs += static_cast<double>(result.workerMs);
            for (const BorderUpdateBatch& batch : result.outgoing) {
                totalOutgoingNodes += static_cast<uint64_t>(batch.nodes.size());
            }
        }
        const auto end = std::chrono::high_resolution_clock::now();
        checksum ^= roundChecksum + static_cast<uint64_t>(round + 1U);
        timingsMs.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    const double totalSolves = static_cast<double>(measureRounds) *
                               static_cast<double>(perfCase.jobs.size());
    const double avgNodesVisited = totalSolves > 0.0
        ? static_cast<double>(totalNodesVisited) / totalSolves
        : 0.0;
    const double avgOutgoingNodes = totalSolves > 0.0
        ? static_cast<double>(totalOutgoingNodes) / totalSolves
        : 0.0;
    const double avgWorkerMs = totalSolves > 0.0
        ? totalWorkerMs / totalSolves
        : 0.0;

    return computeStats(std::move(timingsMs),
                        checksum,
                        avgNodesVisited,
                        avgOutgoingNodes,
                        avgWorkerMs,
                        perfCase.jobs.size());
}

void printStats(const PerfCase& perfCase,
                const BenchmarkStats& stats,
                const int warmupRounds,
                const int measureRounds) {
    std::cout << "[light_solver_perf_test]"
              << " case=" << perfCase.name
              << " jobs=" << perfCase.jobs.size()
              << " warmup=" << warmupRounds
              << " rounds=" << measureRounds
              << " median_ms=" << std::fixed << std::setprecision(3) << stats.medianMs
              << " p95_ms=" << stats.p95Ms
              << " avg_ms=" << stats.avgMs
              << " min_ms=" << stats.minMs
              << " max_ms=" << stats.maxMs
              << " calls_per_sec=" << std::setprecision(0) << stats.callsPerSec
              << " ns_per_call=" << std::setprecision(2) << stats.nsPerCall
              << " avg_nodes=" << std::setprecision(2) << stats.avgNodesVisited
              << " avg_outgoing_nodes=" << stats.avgOutgoingNodes
              << " avg_worker_ms=" << stats.avgWorkerMs
              << " checksum=" << std::hex << stats.checksum << std::dec
              << "\n";
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
    const std::string suffix = "tests/light_solver_perf_test.cpp";
    if (file.size() >= suffix.size() &&
        file.substr(file.size() - suffix.size()) == suffix) {
        return file.substr(0, file.size() - suffix.size());
    }

    size_t lastSep = file.find_last_of("/\\");
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
                      const int warmupRounds,
                      const int measureRounds,
                      const std::vector<std::pair<PerfCase, BenchmarkStats>>& results) {
    using json = nlohmann::json;

    json cases = json::array();
    for (const auto& entry : results) {
        const PerfCase& perfCase = entry.first;
        const BenchmarkStats& stats = entry.second;
        cases.push_back({
            {"name", perfCase.name},
            {"focus", perfCase.focus},
            {"jobs", perfCase.jobs.size()},
            {"median_ms", stats.medianMs},
            {"p95_ms", stats.p95Ms},
            {"avg_ms", stats.avgMs},
            {"min_ms", stats.minMs},
            {"max_ms", stats.maxMs},
            {"calls_per_sec", stats.callsPerSec},
            {"ns_per_call", stats.nsPerCall},
            {"avg_nodes_visited", stats.avgNodesVisited},
            {"avg_outgoing_nodes", stats.avgOutgoingNodes},
            {"avg_worker_ms", stats.avgWorkerMs},
            {"checksum", stats.checksum}
        });
    }

    json root;
    root["benchmark"] = "light_solver_perf_test";
    root["focus"] = "LightSolver solve()/propagateLevel()/getOpacity() baseline";
    root["captured_at"] = currentDate();
    root["build"] = {{"config", buildConfig}};
    root["settings"] = {
        {"warmup_rounds", warmupRounds},
        {"measure_rounds", measureRounds}
    };
    root["cases"] = std::move(cases);

    std::ofstream out(filePath);
    if (!out.is_open()) {
        std::cerr << "[light_solver_perf_test] ERROR: cannot write results to "
                  << filePath << "\n";
        return false;
    }

    out << root.dump(2) << "\n";
    return true;
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    constexpr int warmupRounds = 3;
    constexpr int measureRounds = 10;
    constexpr int jobsPerCase = 12;

#ifdef NDEBUG
    const std::string buildConfig = "Release";
#elif defined(_DEBUG)
    const std::string buildConfig = "Debug";
#else
    const std::string buildConfig = "RelWithDebInfo";
#endif

    PerfCase chunkLoadedCase;
    chunkLoadedCase.name = "chunk_loaded_rebuild";
    chunkLoadedCase.focus = "Full rebuild path dominated by sky/block add propagation";
    chunkLoadedCase.jobs.reserve(jobsPerCase);
    for (int i = 0; i < jobsPerCase; ++i) {
        chunkLoadedCase.jobs.push_back(makeChunkLoadedJob(
            makeLightingStressChunk(i, 0, 0xC0FFEE00U + static_cast<uint32_t>(i))));
    }

    PerfCase blockChangedCase;
    blockChangedCase.name = "block_changed_mixed";
    blockChangedCase.focus = "Local block updates with remove/add passes and dirty sky columns";
    blockChangedCase.jobs.reserve(jobsPerCase);
    for (int i = 0; i < jobsPerCase; ++i) {
        blockChangedCase.jobs.push_back(makeBlockChangedJob(
            i,
            4,
            0xBADC0DE0U + static_cast<uint32_t>(i)));
    }

    PerfCase neighborBoundaryCase;
    neighborBoundaryCase.name = "neighbor_boundary_removal";
    neighborBoundaryCase.focus = "Cross-chunk boundary diff with propagated light removal";
    neighborBoundaryCase.jobs.reserve(jobsPerCase);
    for (int i = 0; i < jobsPerCase; ++i) {
        neighborBoundaryCase.jobs.push_back(makeNeighborBoundaryJob(
            i,
            18 + (i % 6),
            4 + (i % 8)));
    }

    const std::vector<PerfCase> perfCases = {
        chunkLoadedCase,
        blockChangedCase,
        neighborBoundaryCase
    };

    std::vector<std::pair<PerfCase, BenchmarkStats>> results;
    results.reserve(perfCases.size());

    std::cout << "[light_solver_perf_test] Starting LightSolver baseline"
              << " build=" << buildConfig
              << " warmup=" << warmupRounds
              << " rounds=" << measureRounds
              << " jobs_per_case=" << jobsPerCase
              << "\n";

    for (const PerfCase& perfCase : perfCases) {
        const BenchmarkStats stats = runBenchmark(perfCase, warmupRounds, measureRounds);
        printStats(perfCase, stats, warmupRounds, measureRounds);
        results.push_back({perfCase, stats});
    }

    const std::string outputPath =
        resolveProjectRoot() + "tests/perf_baselines/light_solver_perf_baseline.json";
    if (writeResultsJson(outputPath, buildConfig, warmupRounds, measureRounds, results)) {
        std::cout << "[light_solver_perf_test] Results written to " << outputPath << "\n";
    }

    uint64_t finalChecksum = 0;
    for (const auto& entry : results) {
        finalChecksum ^= entry.second.checksum;
    }

    std::cout << "[light_solver_perf_test] PASS baseline_ready checksum="
              << std::hex << finalChecksum << std::dec << "\n";
    return EXIT_SUCCESS;
}
