#include <cstdlib>
#include <iostream>

#include <glm/vec3.hpp>

#include "../src/world/fluid/FluidState.h"
#include "../src/world/World.h"

namespace {

int fail(const char* message) {
    std::cerr << "[water_spread_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void advanceTicks(World& world, uint64_t& currentTick, const uint64_t tickCount) {
    for (uint64_t i = 0; i < tickCount; ++i) {
        ++currentTick;
        world.fluidSystem().processScheduledBlockTicks(currentTick);
    }
}

bool advanceUntilFluidsSettle(World& world, uint64_t& currentTick, const uint64_t maxTicks) {
    for (uint64_t i = 0; i < maxTicks; ++i) {
        if (world.fluidSystem().pendingTickCount() == 0) {
            return true;
        }
        ++currentTick;
        world.fluidSystem().processScheduledBlockTicks(currentTick);
    }
    return world.fluidSystem().pendingTickCount() == 0;
}

template <typename Predicate>
bool advanceUntil(World& world, uint64_t& currentTick, const uint64_t maxTicks, Predicate&& predicate) {
    for (uint64_t i = 0; i < maxTicks; ++i) {
        if (predicate()) {
            return true;
        }
        ++currentTick;
        world.fluidSystem().processScheduledBlockTicks(currentTick);
    }
    return predicate();
}

void fillBox(World& world,
             const int minX, const int maxX,
             const int minY, const int maxY,
             const int minZ, const int maxZ,
             const BlockID block) {
    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                world.setBlock(x, y, z, block);
            }
        }
    }
}

}

int main() {
    BlockRegistry::init(nullptr);

    World world;
    world.init(20260324);
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f, 0.0f, 0.0f));
    }
    uint64_t currentTick = 0;

    const int baseY = 96;
    fillBox(world, -6, 7, baseY, baseY + 2, -6, 6, RUNTIME_ID_NULL);
    fillBox(world, -6, 7, baseY - 1, baseY - 1, -6, 6, BlockRegistry::requireIdByName("minecraft:stone"));
    advanceTicks(world, currentTick, 12);

    const BlockStateId source = FluidState::makeWater(0, false);
    world.setBlockState(0, baseY, 0, source);
    if (world.fluidSystem().pendingTickCount() == 0) {
        return fail("placing a block should enqueue fluid updates");
    }

    world.fluidSystem().scheduleBlockTick(glm::ivec3(0, baseY, 0), currentTick + 1);
    world.fluidSystem().scheduleBlockTick(glm::ivec3(0, baseY, 0), currentTick + 4);
    world.fluidSystem().scheduleBlockTick(glm::ivec3(1, baseY, 0), currentTick + 1);
    if (world.fluidSystem().pendingTickCount() != 7) {
        return fail("scheduled fluid updates should deduplicate by block position");
    }

    ++currentTick;
    world.fluidSystem().processScheduledBlockTicks(currentTick, 1);
    if (world.fluidSystem().pendingTickCount() == 0) {
        return fail("budgeted processing should leave queued fluid updates for later ticks");
    }

    advanceTicks(world, currentTick, 8);
    if (!FluidState::isWater(world.getBlock(1, baseY, 0)) || FluidState::level(world.getBlock(1, baseY, 0)) > 1) {
        return fail("water should spread horizontally with level decay");
    }

    world.setBlock(1, baseY - 1, 0, RUNTIME_ID_NULL);
    advanceTicks(world, currentTick, 6);
    const BlockStateId fallingBelow = world.getBlock(1, baseY - 1, 0);
    if (!FluidState::isWater(fallingBelow) || !FluidState::isFalling(fallingBelow)) {
        return fail("water should prioritize falling into newly opened space");
    }
    world.setBlock(2, baseY - 1, 0, RUNTIME_ID_NULL);
    world.setBlock(1, baseY - 2, 0, BlockRegistry::requireIdByName("minecraft:stone"));
    world.setBlock(2, baseY - 2, 0, BlockRegistry::requireIdByName("minecraft:stone"));
    advanceTicks(world, currentTick, 12);
    if (!FluidState::isWater(world.getBlock(2, baseY - 1, 0))) {
        return fail("falling water should spread outward after landing on support");
    }

    world.setBlock(0, baseY, 0, RUNTIME_ID_NULL);
    const auto baseWaterRetracted = [&]() {
        return world.fluidSystem().pendingTickCount() == 0 &&
               world.getBlock(1, baseY, 0) == NULL_BLOCK_STATE &&
               world.getBlock(1, baseY - 1, 0) == NULL_BLOCK_STATE;
    };
    if (!advanceUntil(world, currentTick, 128, baseWaterRetracted)) {
        return fail("fluid update wave should settle after the source is removed");
    }

    {
        const int seekY = 104;
        fillBox(world, -4, 4, seekY, seekY + 2, -2, 2, RUNTIME_ID_NULL);
        fillBox(world, -4, 4, seekY - 1, seekY - 1, -2, 2, BlockRegistry::requireIdByName("minecraft:stone"));
        world.setBlock(1, seekY - 1, 0, RUNTIME_ID_NULL);
        advanceTicks(world, currentTick, 4);

        world.setBlockState(0, seekY, 0, source);
        const auto waterFoundNearestHole = [&]() {
            return FluidState::isWater(world.getBlock(1, seekY, 0)) &&
                   world.getBlock(-1, seekY, 0) == NULL_BLOCK_STATE &&
                   world.getBlock(0, seekY, 1) == NULL_BLOCK_STATE &&
                   world.getBlock(0, seekY, -1) == NULL_BLOCK_STATE;
        };
        if (!advanceUntil(world, currentTick, 128, waterFoundNearestHole)) {
            return fail("water should seek the nearest hole instead of spreading evenly across flat ground");
        }
    }

    {
        const int rerouteY = 160;
        fillBox(world, -6, 6, rerouteY, rerouteY + 2, -6, 6, RUNTIME_ID_NULL);
        fillBox(world, -6, 6, rerouteY - 1, rerouteY - 1, -6, 6, BlockRegistry::requireIdByName("minecraft:stone"));
        world.setBlock(3, rerouteY - 1, 0, RUNTIME_ID_NULL);
        world.setBlock(0, rerouteY - 1, 4, RUNTIME_ID_NULL);
        advanceTicks(world, currentTick, 4);

        world.setBlockState(0, rerouteY, 0, source);
        advanceTicks(world, currentTick, 12);
        if (!FluidState::isWater(world.getBlock(1, rerouteY, 0)) ||
            FluidState::isWater(world.getBlock(0, rerouteY, 1))) {
            return fail("water should initially prefer the closest downhill hole");
        }

        world.setBlock(3, rerouteY - 1, 0, BlockRegistry::requireIdByName("minecraft:stone"));
        advanceTicks(world, currentTick, 24);
        if (!FluidState::isWater(world.getBlock(0, rerouteY, 1)) ||
            world.getBlock(1, rerouteY, 0) != NULL_BLOCK_STATE) {
            return fail("water should reroute when the closest downhill hole is blocked");
        }
    }

    {
        const int forkY = 120;
        fillBox(world, 8, 16, forkY, forkY + 2, -2, 2, RUNTIME_ID_NULL);
        fillBox(world, 8, 16, forkY - 1, forkY - 1, -2, 2, BlockRegistry::requireIdByName("minecraft:stone"));
        world.setBlock(10, forkY - 1, 0, RUNTIME_ID_NULL);
        world.setBlock(12, forkY - 1, 0, RUNTIME_ID_NULL);
        advanceTicks(world, currentTick, 4);

        world.setBlockState(11, forkY, 0, source);
        const auto waterSplitAcrossBestDirections = [&]() {
            return FluidState::isWater(world.getBlock(10, forkY, 0)) &&
                   FluidState::isWater(world.getBlock(12, forkY, 0)) &&
                   world.getBlock(11, forkY, 1) == NULL_BLOCK_STATE &&
                   world.getBlock(11, forkY, -1) == NULL_BLOCK_STATE;
        };
        if (!advanceUntil(world, currentTick, 128, waterSplitAcrossBestDirections)) {
            return fail("equal-distance holes should allow water to split across the best directions only");
        }
    }

    {
        const int flatY = 136;
        fillBox(world, 15, 27, flatY, flatY + 2, -6, 6, RUNTIME_ID_NULL);
        fillBox(world, 15, 27, flatY - 1, flatY - 1, -6, 6, BlockRegistry::requireIdByName("minecraft:stone"));
        advanceTicks(world, currentTick, 4);

        world.setBlockState(21, flatY, 0, source);
        advanceTicks(world, currentTick, 8);
        if (!FluidState::isWater(world.getBlock(22, flatY, 0)) ||
            !FluidState::isWater(world.getBlock(20, flatY, 0)) ||
            !FluidState::isWater(world.getBlock(21, flatY, 1))) {
            return fail("water should still spread normally when no downhill hole is found");
        }
    }

    {
        const int naturalPoolY = 168;
        fillBox(world, -4, 4, naturalPoolY - 1, naturalPoolY + 2, -2, 2, RUNTIME_ID_NULL);
        fillBox(world, -4, 4, naturalPoolY - 2, naturalPoolY - 2, -2, 2, BlockRegistry::requireIdByName("minecraft:stone"));
        fillBox(world, 1, 1, naturalPoolY, naturalPoolY + 1, 0, 0, BlockRegistry::requireIdByName("minecraft:stone"));
        advanceTicks(world, currentTick, 4);

        world.setBlockState(-1, naturalPoolY - 1, 0, source);
        world.setBlockState(-1, naturalPoolY, 0, source);
        world.setBlockState(0, naturalPoolY, 0, source);
        advanceTicks(world, currentTick, 8);

        world.setBlock(1, naturalPoolY, 0, RUNTIME_ID_NULL);
        const auto naturalPoolFilledExposedAir = [&]() {
            return FluidState::isWater(world.getBlock(1, naturalPoolY, 0));
        };
        if (!advanceUntil(world, currentTick, 128, naturalPoolFilledExposedAir)) {
            return fail("natural water should spread into newly exposed air instead of seeking filled water columns");
        }
    }

    {
        const int infiniteY = 144;
        fillBox(world, 19, 31, infiniteY, infiniteY + 2, -6, 6, RUNTIME_ID_NULL);
        fillBox(world, 19, 31, infiniteY - 1, infiniteY - 1, -6, 6, BlockRegistry::requireIdByName("minecraft:stone"));
        advanceTicks(world, currentTick, 4);

        world.setBlockState(24, infiniteY, 0, source);
        world.setBlockState(26, infiniteY, 0, source);
        advanceTicks(world, currentTick, 12);
        if (!FluidState::isSource(world.getBlock(25, infiniteY, 0))) {
            return fail("two adjacent sources with support below should regenerate the middle source");
        }
    }

    {
        const int unsupportedInfiniteY = 152;
        fillBox(world, 19, 31, unsupportedInfiniteY, unsupportedInfiniteY + 2, -6, 6, RUNTIME_ID_NULL);
        fillBox(world, 19, 31, unsupportedInfiniteY - 1, unsupportedInfiniteY - 1, -6, 6, BlockRegistry::requireIdByName("minecraft:stone"));
        world.setBlock(25, unsupportedInfiniteY - 1, 0, RUNTIME_ID_NULL);
        advanceTicks(world, currentTick, 4);

        world.setBlockState(24, unsupportedInfiniteY, 0, source);
        world.setBlockState(26, unsupportedInfiniteY, 0, source);
        advanceTicks(world, currentTick, 12);
        if (FluidState::isSource(world.getBlock(25, unsupportedInfiniteY, 0))) {
            return fail("unsupported middle cells should not become infinite water sources");
        }
    }

    const int edgeY = 112;
    for (int x = 14; x <= 17; ++x) {
        for (int y = edgeY; y <= edgeY + 1; ++y) {
            world.setBlock(x, y, 0, RUNTIME_ID_NULL);
        }
        world.setBlock(x, edgeY - 1, 0, BlockRegistry::requireIdByName("minecraft:stone"));
    }

    world.setBlockState(15, edgeY, 0, source);
    const auto waterCrossedLoadedChunkBorder = [&]() {
        return FluidState::isWater(world.getBlock(16, edgeY, 0));
    };
    if (!advanceUntil(world, currentTick, 128, waterCrossedLoadedChunkBorder)) {
        return fail("water should spread across loaded chunk borders");
    }

    const int pillarY = 128;
    for (int x = -2; x <= 2; ++x) {
        for (int z = -2; z <= 2; ++z) {
            for (int y = pillarY - 2; y <= pillarY + 1; ++y) {
                world.setBlock(x, y, z, RUNTIME_ID_NULL);
            }
        }
    }
    world.setBlock(0, pillarY - 1, 0, BlockRegistry::requireIdByName("minecraft:stone"));
    advanceTicks(world, currentTick, 8);

    world.setBlockState(0, pillarY, 0, source);
    advanceTicks(world, currentTick, 12);
    const BlockStateId unsupportedEdge = world.getBlock(1, pillarY, 0);
    if (!FluidState::isWater(unsupportedEdge) || !FluidState::isFalling(unsupportedEdge)) {
        return fail("unsupported horizontal spill should become falling water");
    }
    if (FluidState::surfaceHeight(unsupportedEdge) >= 0.999f) {
        return fail("unsupported horizontal spill should still lower its top surface by level");
    }
    if (world.getBlock(2, pillarY, 0) != NULL_BLOCK_STATE) {
        return fail("water on top of a single pillar should not keep spreading sideways without support");
    }

    std::cout << "[water_spread_test] PASS\n";
    return EXIT_SUCCESS;
}
