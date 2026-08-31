#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

#include "../src/world/fluid/FluidState.h"
#include "../src/world/World.h"

namespace {

int fail(const std::string& message) {
    std::cerr << "[fluid_stability_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void loadChunks(World& world, const uint32_t seed) {
    world.init(seed);
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f, 0.0f, 0.0f));
    }
}

void fillBox(World& world, const int minX, const int maxX, const int minY, const int maxY, const int minZ,
             const int maxZ, const BlockID block) {
    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                world.setBlock(x, y, z, block);
            }
        }
    }
}

void advanceTicks(World& world, uint64_t& currentTick, const uint64_t tickCount) {
    for (uint64_t i = 0; i < tickCount; ++i) {
        ++currentTick;
        world.fluidSystem().processScheduledBlockTicks(currentTick);
    }
}

bool advanceUntilSettled(World& world, uint64_t& currentTick, const uint64_t maxTicks) {
    for (uint64_t i = 0; i < maxTicks; ++i) {
        if (world.fluidSystem().pendingTickCount() == 0) {
            return true;
        }
        ++currentTick;
        world.fluidSystem().processScheduledBlockTicks(currentTick);
    }
    return world.fluidSystem().pendingTickCount() == 0;
}

struct Box {
    int minX = 0;
    int maxX = 0;
    int minY = 0;
    int maxY = 0;
    int minZ = 0;
    int maxZ = 0;
};

std::vector<BlockStateId> snapshotBox(const World& world, const Box& box) {
    std::vector<BlockStateId> states;
    for (int x = box.minX; x <= box.maxX; ++x) {
        for (int y = box.minY; y <= box.maxY; ++y) {
            for (int z = box.minZ; z <= box.maxZ; ++z) {
                states.push_back(world.getBlock(x, y, z));
            }
        }
    }
    return states;
}

bool boxIsStableAcrossTicks(World& world, uint64_t& currentTick, const Box& box, const uint64_t observeTicks) {
    const std::vector<BlockStateId> before = snapshotBox(world, box);
    advanceTicks(world, currentTick, observeTicks);
    const std::vector<BlockStateId> after = snapshotBox(world, box);
    return before == after;
}

const BlockID stoneId() { return BlockRegistry::requireIdByName("minecraft:stone"); }

// Builds a stone-floored basin with an optional sealed pit column beneath the floor.
void buildBasin(World& world, const int y) {
    fillBox(world, -6, 6, y, y + 2, -6, 6, RUNTIME_ID_NULL);
    fillBox(world, -6, 6, y - 3, y - 1, -6, 6, stoneId());
}

void openPit(World& world, const int pitX, const int y, const int pitZ) {
    world.setBlock(pitX, y - 1, pitZ, RUNTIME_ID_NULL);
    world.setBlock(pitX, y - 2, pitZ, RUNTIME_ID_NULL);
}

void sealPit(World& world, const int pitX, const int y, const int pitZ) {
    world.setBlock(pitX, y - 1, pitZ, stoneId());
    world.setBlock(pitX, y - 2, pitZ, stoneId());
}

const char* testPitFillReachesSteadyState() {
    World world;
    loadChunks(world, 20260830);
    uint64_t currentTick = 0;

    const int y = 96;
    buildBasin(world, y);
    openPit(world, 2, y, 0);
    advanceTicks(world, currentTick, 8);

    world.setBlockState(0, y, 0, FluidState::makeWater(0, false));
    if (!advanceUntilSettled(world, currentTick, 2000)) {
        return "water filling a sealed pit should reach a settled state";
    }

    const Box box{-6, 6, y - 3, y + 1, -6, 6};
    if (!boxIsStableAcrossTicks(world, currentTick, box, 40)) {
        return "settled water around a filled pit must not flicker between ticks";
    }
    return nullptr;
}

const char* testDrainConvergesAfterSourceRemoved() {
    World world;
    loadChunks(world, 20260831);
    uint64_t currentTick = 0;

    const int y = 104;
    buildBasin(world, y);
    advanceTicks(world, currentTick, 8);

    world.setBlockState(0, y, 0, FluidState::makeWater(0, false));
    advanceTicks(world, currentTick, 60);
    if (!FluidState::isWater(world.getBlock(1, y, 0))) {
        return "water should spread before the drain phase of this test starts";
    }

    world.setBlock(0, y, 0, RUNTIME_ID_NULL);
    if (!advanceUntilSettled(world, currentTick, 2000)) {
        return "draining water should settle instead of oscillating forever";
    }

    for (int x = -6; x <= 6; ++x) {
        for (int z = -6; z <= 6; ++z) {
            const BlockStateId state = world.getBlock(x, y, z);
            if (FluidState::isWater(state)) {
                return "all water should be gone after the source is removed and the system settles";
            }
        }
    }
    return nullptr;
}

const char* testDirectionToggleSettlesWithoutFlicker() {
    World world;
    loadChunks(world, 20260832);
    uint64_t currentTick = 0;

    const int y = 112;
    buildBasin(world, y);
    openPit(world, 2, y, 0);
    openPit(world, 0, y, 3);
    advanceTicks(world, currentTick, 8);

    world.setBlockState(0, y, 0, FluidState::makeWater(0, false));
    if (!advanceUntilSettled(world, currentTick, 2000)) {
        return "initial two-pit flow should settle";
    }

    const Box box{-6, 6, y - 3, y + 1, -6, 6};

    // Block the preferred pit; water must reroute to the other pit and settle.
    sealPit(world, 2, y, 0);
    if (!advanceUntilSettled(world, currentTick, 2000)) {
        return "rerouted water should settle after the preferred pit is sealed";
    }
    if (!FluidState::isWater(world.getBlock(0, y - 1, 3))) {
        return "water should drain into the remaining open pit after the reroute";
    }
    if (!boxIsStableAcrossTicks(world, currentTick, box, 40)) {
        return "rerouted flow must not flicker between ticks";
    }

    // Reopen the original pit; flow should move back and settle again.
    openPit(world, 2, y, 0);
    if (!advanceUntilSettled(world, currentTick, 2000)) {
        return "flow should settle after the original pit reopens";
    }
    if (!boxIsStableAcrossTicks(world, currentTick, box, 40)) {
        return "flow must not flicker after the original pit reopens";
    }
    return nullptr;
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    struct TestCase {
        const char* name;
        const char* (*run)();
    };

    const TestCase cases[] = {
        {"pit_fill_steady_state", testPitFillReachesSteadyState},
        {"drain_converges", testDrainConvergesAfterSourceRemoved},
        {"direction_toggle_no_flicker", testDirectionToggleSettlesWithoutFlicker},
    };

    for (const TestCase& testCase : cases) {
        if (const char* error = testCase.run()) {
            return fail(std::string(testCase.name) + ": " + error);
        }
    }

    std::cout << "[fluid_stability_test] PASS\n";
    return EXIT_SUCCESS;
}
