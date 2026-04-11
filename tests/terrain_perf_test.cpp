#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "../src/world/TerrainGenerator.h"
#include "../src/world/Chunk.h"

namespace {
struct BenchmarkStats {
    double medianMs = 0.0;
    double avgMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
    uint64_t checksum = 0;
};

BenchmarkStats runBenchmark(const std::string& caseName,
                            int warmupRounds,
                            int measureRounds,
                            const std::string& workloadLabel,
                            uint32_t seed,
                            const std::function<uint64_t()>& workload) {
    for (int i = 0; i < warmupRounds; ++i) {
        (void)workload();
    }

    std::vector<double> timingsMs;
    timingsMs.reserve(static_cast<size_t>(measureRounds));
    uint64_t checksum = 0;

    for (int i = 0; i < measureRounds; ++i) {
        const auto start = std::chrono::high_resolution_clock::now();
        checksum ^= workload() + static_cast<uint64_t>(i + 1);
        const auto end = std::chrono::high_resolution_clock::now();
        const auto duration = std::chrono::duration<double, std::milli>(end - start).count();
        timingsMs.push_back(duration);
    }

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

    std::cout << "[terrain_perf_test] "
              << "case=" << caseName
              << " seed=" << seed
              << " warmup=" << warmupRounds
              << " rounds=" << measureRounds
              << " workload=" << workloadLabel
              << " median_ms=" << std::fixed << std::setprecision(3) << stats.medianMs
              << " avg_ms=" << stats.avgMs
              << " min_ms=" << stats.minMs
              << " max_ms=" << stats.maxMs
              << " checksum=" << stats.checksum
              << "\n";

    return stats;
}

struct SurfaceRun {
    int startX = 0;
    int z = 0;
    int count = 0;
};

std::vector<SurfaceRun> buildSurfaceRuns(int sampleCount, int batchSize) {
    std::vector<SurfaceRun> runs;
    runs.reserve(static_cast<size_t>((sampleCount + batchSize - 1) / batchSize));

    uint32_t state = 0x12345678U;
    int processed = 0;
    while (processed < sampleCount) {
        state = state * 1664525U + 1013904223U;
        const int z = static_cast<int>(state & 1023U) - 512;
        state = state * 1664525U + 1013904223U;
        const int startX = static_cast<int>(state & 2047U) - 1024;
        const int count = std::min(batchSize, sampleCount - processed);

        runs.push_back(SurfaceRun{startX, z, count});
        processed += count;
    }

    return runs;
}
}

int main() {
    constexpr uint32_t seed = 12345;
    constexpr int seaLevel = 63;
    constexpr int warmupRounds = 3;
    constexpr int measureRounds = 10;

    std::cout << "[terrain_perf_test] Starting terrain generation performance tests"
              << " (seed=" << seed
              << ", seaLevel=" << seaLevel
              << ", warmup=" << warmupRounds
              << ", rounds=" << measureRounds << ")\n";

    TerrainGenerator generator;
    generator.init(seed, seaLevel);

    uint64_t globalChecksum = 0;

    // Case 1: single chunk generation (stability-focused)
    {
        const BenchmarkStats stats = runBenchmark(
                "single_chunk",
                warmupRounds,
                measureRounds,
                "1_chunk",
                seed,
                [&generator]() -> uint64_t {
                    Chunk chunk(0, 0);
                    generator.generateChunk(chunk);
                    uint64_t checksum = 0;
                    checksum ^= static_cast<uint64_t>(chunk.getBlock(0, 0, 0));
                    checksum ^= static_cast<uint64_t>(chunk.getBlock(0, Chunk::SIZE_Y / 2, 0)) << 8U;
                    checksum ^= static_cast<uint64_t>(chunk.getBlock(Chunk::SIZE_X - 1, 0, Chunk::SIZE_Z - 1)) << 16U;
                    return checksum;
                });
        globalChecksum ^= stats.checksum;
    }

    // Case 2: batch chunk generation (throughput-focused)
    {
        constexpr int chunkCount = 100;
        const BenchmarkStats stats = runBenchmark(
                "batch_chunks",
                warmupRounds,
                measureRounds,
                "100_chunks",
                seed,
                [&generator, chunkCount]() -> uint64_t {
                    uint64_t checksum = 0;
                    for (int i = 0; i < chunkCount; ++i) {
                        Chunk chunk(i, i / 10);
                        generator.generateChunk(chunk);
                        checksum ^= static_cast<uint64_t>(chunk.getBlock(i % Chunk::SIZE_X, 0, (i * 3) % Chunk::SIZE_Z));
                    }
                    return checksum;
                });
        globalChecksum ^= stats.checksum;
    }

    // Case 3: scalar vs batch surface sampling with same input sequences.
    {
        constexpr int sampleCount = 1'000'000;
        const int batchSizes[] = {8, 16, 32, 64};

        for (const int batchSize : batchSizes) {
            const std::vector<SurfaceRun> runs = buildSurfaceRuns(sampleCount, batchSize);
            std::vector<int> sampledSurface(static_cast<size_t>(batchSize));

            const BenchmarkStats scalarStats = runBenchmark(
                    "surface_sampling_scalar",
                    warmupRounds,
                    measureRounds,
                    "1,000,000_samples_shared_input",
                    seed,
                    [&generator, &runs]() -> uint64_t {
                        uint64_t checksum = 0;
                        for (const SurfaceRun& run : runs) {
                            for (int i = 0; i < run.count; ++i) {
                                checksum += static_cast<uint64_t>(generator.sampleSurfaceY(run.startX + i, run.z));
                            }
                        }
                        return checksum;
                    });
            globalChecksum ^= scalarStats.checksum;

            const std::string workload = "1,000,000_samples_batch" + std::to_string(batchSize);
            const BenchmarkStats batchStats = runBenchmark(
                    "surface_sampling_batch",
                    warmupRounds,
                    measureRounds,
                    workload,
                    seed,
                    [&generator, &runs, &sampledSurface]() -> uint64_t {
                        uint64_t checksum = 0;
                        for (const SurfaceRun& run : runs) {
                            generator.sampleSurfaceYBatch(run.startX, run.z, run.count, sampledSurface.data());
                            for (int i = 0; i < run.count; ++i) {
                                checksum += static_cast<uint64_t>(sampledSurface[static_cast<size_t>(i)]);
                            }
                        }
                        return checksum;
                    });
            globalChecksum ^= batchStats.checksum;
        }
    }

    std::cout << "[terrain_perf_test] PASS - Stable baseline captured"
              << " checksum=" << globalChecksum << "\n";
    return EXIT_SUCCESS;
}
