#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include <glm/vec3.hpp>

#include "../src/ecs/systems/world/RedstoneSystem.h"
#include "../src/world/World.h"
#include "../src/world/block/Block.h"
#include "../src/world/block/BlockStateRegistry.h"
#include "../src/world/block/PropIndices.h"

namespace {

struct BenchmarkStats {
    double medianMs = 0.0;
    double avgMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
    double p95Ms = 0.0;
    uint64_t checksum = 0;
};

struct RedstoneBenchmarkWorld {
    World world;
    int y = 96;
    glm::ivec3 leverPosition{-1, 96, 0};
};

[[noreturn]] void fail(const char* message) {
    std::cerr << "[redstone_performance_benchmark] FAIL: " << message << '\n';
    std::cerr.flush();
    std::_Exit(EXIT_FAILURE);
}

BenchmarkStats computeStats(std::vector<double> timingsMs, const uint64_t checksum) {
    if (timingsMs.empty()) {
        fail("benchmark requires at least one timing sample");
    }
    std::sort(timingsMs.begin(), timingsMs.end());

    BenchmarkStats stats;
    stats.checksum = checksum;
    stats.minMs = timingsMs.front();
    stats.maxMs = timingsMs.back();
    stats.avgMs = std::accumulate(timingsMs.begin(), timingsMs.end(), 0.0) /
                  static_cast<double>(timingsMs.size());
    const size_t mid = timingsMs.size() / 2;
    stats.medianMs = (timingsMs.size() % 2) == 0
        ? (timingsMs[mid - 1] + timingsMs[mid]) * 0.5
        : timingsMs[mid];
    stats.p95Ms = timingsMs[(timingsMs.size() - 1U) * 95U / 100U];
    return stats;
}

void printStats(const std::string& caseName,
                const BenchmarkStats& stats,
                const int warmupRounds,
                const int measureRounds,
                const int wireCount,
                const double targetMs) {
    std::cout << "[redstone_performance_benchmark]"
              << " case=" << caseName
              << " wires=" << wireCount
              << " warmup=" << warmupRounds
              << " rounds=" << measureRounds
              << " target_ms=" << std::fixed << std::setprecision(3) << targetMs
              << " median_ms=" << stats.medianMs
              << " p95_ms=" << stats.p95Ms
              << " avg_ms=" << stats.avgMs
              << " min_ms=" << stats.minMs
              << " max_ms=" << stats.maxMs
              << " checksum=" << std::hex << stats.checksum << std::dec
              << '\n';
}

BlockStateId leverState(const bool powered) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:lever"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_FLOOR},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE}
        });
}

uint8_t powerFromState(const BlockStateId state) {
    static const std::array<uint16_t, 16> kPowerValues = {
        PropIndices::POWER_0,
        PropIndices::POWER_1,
        PropIndices::POWER_2,
        PropIndices::POWER_3,
        PropIndices::POWER_4,
        PropIndices::POWER_5,
        PropIndices::POWER_6,
        PropIndices::POWER_7,
        PropIndices::POWER_8,
        PropIndices::POWER_9,
        PropIndices::POWER_10,
        PropIndices::POWER_11,
        PropIndices::POWER_12,
        PropIndices::POWER_13,
        PropIndices::POWER_14,
        PropIndices::POWER_15,
    };

    const uint16_t value = BlockStateRegistry::getPropertyIndex(state, PropIndices::POWER);
    for (uint8_t power = 0; power < kPowerValues.size(); ++power) {
        if (value == kPowerValues[power]) {
            return power;
        }
    }
    fail("state does not contain a valid redstone power value");
}

uint8_t wirePower(const World& world, const int x, const int y, const int z) {
    return powerFromState(world.getBlockState(x, y, z));
}

bool areaLoaded(const World& world, const int minX, const int maxX, const int y, const int minZ, const int maxZ) {
    for (int x = minX; x <= maxX; x += Chunk::SIZE_X) {
        for (int z = minZ; z <= maxZ; z += Chunk::SIZE_Z) {
            if (!world.isChunkLoadedForBlock(x, y, z)) {
                return false;
            }
        }
    }
    return world.isChunkLoadedForBlock(maxX, y, maxZ);
}

void loadBenchmarkArea(World& world, const int minX, const int maxX, const int y, const int minZ, const int maxZ) {
    world.setRenderDistance(8);
    const glm::vec3 center(
        static_cast<float>(minX + maxX) * 0.5f,
        static_cast<float>(y),
        static_cast<float>(minZ + maxZ) * 0.5f);

    for (int i = 0; i < 512; ++i) {
        world.update(center, 1.0f / 60.0f);
        if (areaLoaded(world, minX, maxX, y, minZ, maxZ)) {
            return;
        }
    }
    fail("benchmark chunks did not finish loading");
}

std::unique_ptr<RedstoneBenchmarkWorld> buildLargeWireGrid() {
    constexpr int minX = 0;
    constexpr int maxX = 99;
    constexpr int minZ = 0;
    constexpr int maxZ = 99;

    auto benchmark = std::make_unique<RedstoneBenchmarkWorld>();
    benchmark->world.init(20260627);
    loadBenchmarkArea(benchmark->world, -1, maxX, benchmark->y, minZ, maxZ);

    const BlockID stone = BlockRegistry::requireIdByName("minecraft:stone");
    const BlockStateId wire = BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire"));

    for (int x = minX; x <= maxX; ++x) {
        for (int z = minZ; z <= maxZ; ++z) {
            benchmark->world.setBlock(x, benchmark->y - 1, z, stone);
            benchmark->world.setBlockState(x, benchmark->y, z, wire);
            benchmark->world.setBlockState(x, benchmark->y + 1, z, NULL_BLOCK_STATE);
        }
    }
    benchmark->world.setBlock(benchmark->leverPosition.x, benchmark->y - 1, benchmark->leverPosition.z, stone);
    benchmark->world.setBlockState(
        benchmark->leverPosition.x,
        benchmark->leverPosition.y,
        benchmark->leverPosition.z,
        leverState(false));

    benchmark->world.redstoneUpdateQueue().clear();
    benchmark->world.redstoneChangedBlockQueue().clear();
    benchmark->world.redstoneScheduledUpdateQueue().clear();
    return benchmark;
}

uint64_t checksumGrid(const World& world, const int y) {
    uint64_t checksum = 14695981039346656037ULL;
    for (int x = 0; x <= 99; ++x) {
        for (int z = 0; z <= 99; ++z) {
            checksum ^= wirePower(world, x, y, z);
            checksum *= 1099511628211ULL;
        }
    }
    return checksum;
}

BenchmarkStats runToggleBenchmark(RedstoneBenchmarkWorld& benchmark,
                                  const int warmupRounds,
                                  const int measureRounds) {
    std::vector<double> timings;
    timings.reserve(static_cast<size_t>(measureRounds));
    uint64_t checksum = 14695981039346656037ULL;
    uint64_t redstoneTick = 0;
    bool powered = false;

    const auto runOne = [&]() {
        powered = !powered;
        benchmark.world.setBlockState(
            benchmark.leverPosition.x,
            benchmark.leverPosition.y,
            benchmark.leverPosition.z,
            leverState(powered));

        const auto start = std::chrono::steady_clock::now();
        const size_t changed = ecs::RedstoneSystem::processWorld(benchmark.world, redstoneTick++, 20000);
        const auto end = std::chrono::steady_clock::now();

        benchmark.world.redstoneUpdateQueue().clear();
        benchmark.world.redstoneChangedBlockQueue().clear();
        benchmark.world.redstoneScheduledUpdateQueue().clear();

        checksum ^= checksumGrid(benchmark.world, benchmark.y) + changed + (powered ? 0x9e3779b97f4a7c15ULL : 0ULL);
        checksum *= 1099511628211ULL;
        return std::chrono::duration<double, std::milli>(end - start).count();
    };

    for (int i = 0; i < warmupRounds; ++i) {
        static_cast<void>(runOne());
    }
    for (int i = 0; i < measureRounds; ++i) {
        timings.push_back(runOne());
    }

    if (wirePower(benchmark.world, 0, benchmark.y, 0) == 0 && powered) {
        fail("powered benchmark grid did not propagate power to the first wire");
    }
    if (wirePower(benchmark.world, 99, benchmark.y, 99) != 0) {
        fail("far benchmark wire should remain outside the redstone power radius");
    }

    return computeStats(std::move(timings), checksum);
}

BenchmarkStats runIdleBenchmark(RedstoneBenchmarkWorld& benchmark,
                                const int warmupRounds,
                                const int measureRounds) {
    std::vector<double> timings;
    timings.reserve(static_cast<size_t>(measureRounds));
    uint64_t checksum = 14695981039346656037ULL;
    uint64_t redstoneTick = 100000;

    const auto runOne = [&]() {
        benchmark.world.redstoneUpdateQueue().clear();
        benchmark.world.redstoneChangedBlockQueue().clear();
        benchmark.world.redstoneScheduledUpdateQueue().clear();

        const auto start = std::chrono::steady_clock::now();
        const size_t changed = ecs::RedstoneSystem::processWorld(benchmark.world, redstoneTick++, 20000);
        const auto end = std::chrono::steady_clock::now();

        checksum ^= changed + benchmark.world.redstoneUpdateQueue().size();
        checksum *= 1099511628211ULL;
        return std::chrono::duration<double, std::milli>(end - start).count();
    };

    for (int i = 0; i < warmupRounds; ++i) {
        static_cast<void>(runOne());
    }
    for (int i = 0; i < measureRounds; ++i) {
        timings.push_back(runOne());
    }

    return computeStats(std::move(timings), checksum);
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    constexpr int warmupRounds = 4;
    constexpr int measureRounds = 12;
    constexpr int wireCount = 10000;
    constexpr double documentedTargetMs = 5.0;
    constexpr double regressionLimitMs = 100.0;

    std::unique_ptr<RedstoneBenchmarkWorld> benchmark = buildLargeWireGrid();

    const BenchmarkStats toggleStats = runToggleBenchmark(*benchmark, warmupRounds, measureRounds);
    printStats("large_wire_grid_toggle", toggleStats, warmupRounds, measureRounds, wireCount, documentedTargetMs);
    if (toggleStats.medianMs > regressionLimitMs) {
        fail("large wire grid redstone update exceeded the regression limit");
    }

    const BenchmarkStats idleStats = runIdleBenchmark(*benchmark, warmupRounds, measureRounds);
    printStats("large_wire_grid_idle", idleStats, warmupRounds, measureRounds, wireCount, 0.1);
    if (idleStats.medianMs > 1.0) {
        fail("idle redstone processing should not scan the loaded benchmark grid");
    }

    std::cout << "[redstone_performance_benchmark] PASS baseline_ready\n";
    return EXIT_SUCCESS;
}
