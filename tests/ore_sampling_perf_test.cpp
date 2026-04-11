#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "../src/world/TerrainGenerator.h"
#include "../src/world/Block.h"

class TerrainGeneratorPerfAccess {
public:
    static BlockID sampleOreBlock(const TerrainGenerator& generator,
                                  int worldX,
                                  int y,
                                  int worldZ,
                                  BlockID baseBlock) {
        return generator.sampleOreBlock(worldX, y, worldZ, baseBlock);
    }
};

namespace {

struct OreSample {
    int x = 0;
    int y = 0;
    int z = 0;
    BlockID baseBlock = BlockType::STONE;
};

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

std::vector<OreSample> buildStoneOnlySamples(int sampleCount) {
    std::vector<OreSample> samples;
    samples.reserve(static_cast<size_t>(sampleCount));

    uint32_t state = 0x31415926U;
    for (int i = 0; i < sampleCount; ++i) {
        state = state * 1664525U + 1013904223U;
        const int x = static_cast<int>(state & 8191U) - 4096;
        state = state * 1664525U + 1013904223U;
        const int z = static_cast<int>(state & 8191U) - 4096;
        state = state * 1664525U + 1013904223U;
        const int y = 1 + static_cast<int>(state % 129U);
        samples.push_back(OreSample{x, y, z, BlockType::STONE});
    }

    return samples;
}

std::vector<OreSample> buildMixedSamples(int sampleCount) {
    std::vector<OreSample> samples;
    samples.reserve(static_cast<size_t>(sampleCount));

    uint32_t state = 0x27182818U;
    for (int i = 0; i < sampleCount; ++i) {
        state = state * 1664525U + 1013904223U;
        const int x = static_cast<int>(state & 8191U) - 4096;
        state = state * 1664525U + 1013904223U;
        const int z = static_cast<int>(state & 8191U) - 4096;
        state = state * 1664525U + 1013904223U;
        const int y = 1 + static_cast<int>(state % 129U);

        state = state * 1664525U + 1013904223U;
        const uint32_t bucket = state % 100U;
        BlockID baseBlock = BlockType::STONE;
        if (bucket < 74U) {
            baseBlock = BlockType::STONE;
        } else if (bucket < 84U) {
            baseBlock = BlockType::DIRT;
        } else if (bucket < 93U) {
            baseBlock = BlockType::SAND;
        } else {
            baseBlock = BlockType::AIR;
        }

        samples.push_back(OreSample{x, y, z, baseBlock});
    }

    return samples;
}

BenchmarkStats runBenchmark(const std::string& caseName,
                            const TerrainGenerator& generator,
                            const std::vector<OreSample>& samples,
                            int warmupRounds,
                            int measureRounds) {
    for (int i = 0; i < warmupRounds; ++i) {
        uint64_t warmupChecksum = 0;
        for (const OreSample& sample : samples) {
            warmupChecksum += static_cast<uint64_t>(TerrainGeneratorPerfAccess::sampleOreBlock(
                    generator,
                    sample.x,
                    sample.y,
                    sample.z,
                    sample.baseBlock));
        }
        if (warmupChecksum == 0xFFFFFFFFFFFFFFFFULL) {
            std::cout << "[ore_sampling_perf_test] impossible_warmup_checksum=" << warmupChecksum << "\n";
        }
    }

    std::vector<double> timingsMs;
    timingsMs.reserve(static_cast<size_t>(measureRounds));
    uint64_t checksum = 0;

    for (int i = 0; i < measureRounds; ++i) {
        const auto start = std::chrono::high_resolution_clock::now();
        uint64_t roundChecksum = 0;
        for (const OreSample& sample : samples) {
            roundChecksum += static_cast<uint64_t>(TerrainGeneratorPerfAccess::sampleOreBlock(
                    generator,
                    sample.x,
                    sample.y,
                    sample.z,
                    sample.baseBlock));
        }
        const auto end = std::chrono::high_resolution_clock::now();

        checksum ^= roundChecksum + static_cast<uint64_t>(i + 1);

        const double durationMs = std::chrono::duration<double, std::milli>(end - start).count();
        timingsMs.push_back(durationMs);
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

    const size_t p95Index = (timingsMs.size() - 1U) * 95U / 100U;
    stats.p95Ms = timingsMs[p95Index];

    const double calls = static_cast<double>(samples.size());
    const double medianSec = stats.medianMs / 1000.0;
    if (medianSec > 0.0) {
        stats.callsPerSec = calls / medianSec;
        stats.nsPerCall = (stats.medianMs * 1000000.0) / calls;
    }

    std::cout << "[ore_sampling_perf_test]"
              << " case=" << caseName
              << " samples=" << samples.size()
              << " warmup=" << warmupRounds
              << " rounds=" << measureRounds
              << " median_ms=" << std::fixed << std::setprecision(3) << stats.medianMs
              << " p95_ms=" << stats.p95Ms
              << " avg_ms=" << stats.avgMs
              << " min_ms=" << stats.minMs
              << " max_ms=" << stats.maxMs
              << " calls_per_sec=" << std::setprecision(0) << stats.callsPerSec
              << " ns_per_call=" << std::setprecision(2) << stats.nsPerCall
              << " checksum=" << std::setprecision(0) << stats.checksum
              << "\n";

    return stats;
}

} // namespace

int main() {
    constexpr uint32_t seed = 12345;
    constexpr int seaLevel = 63;
    constexpr int warmupRounds = 4;
    constexpr int measureRounds = 12;
    constexpr int sampleCount = 2'000'000;

    std::cout << "[ore_sampling_perf_test] Starting sampleOreBlock baseline"
              << " seed=" << seed
              << " seaLevel=" << seaLevel
              << " warmup=" << warmupRounds
              << " rounds=" << measureRounds
              << " sample_count=" << sampleCount
              << "\n";

    TerrainGenerator generator;
    generator.init(seed, seaLevel);

    const std::vector<OreSample> stoneOnlySamples = buildStoneOnlySamples(sampleCount);
    const std::vector<OreSample> mixedSamples = buildMixedSamples(sampleCount);

    const BenchmarkStats stoneStats = runBenchmark(
            "stone_only",
            generator,
            stoneOnlySamples,
            warmupRounds,
            measureRounds);

    const BenchmarkStats mixedStats = runBenchmark(
            "mixed_base_blocks",
            generator,
            mixedSamples,
            warmupRounds,
            measureRounds);

    const uint64_t finalChecksum = stoneStats.checksum ^ (mixedStats.checksum << 1U);

    std::cout << "[ore_sampling_perf_test] PASS baseline_ready checksum=" << finalChecksum << "\n";
    return EXIT_SUCCESS;
}

