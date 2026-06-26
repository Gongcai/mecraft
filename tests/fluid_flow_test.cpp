#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include <glm/vec3.hpp>

#include "../src/world/fluid/FluidFlow.h"
#include "../src/world/fluid/FluidState.h"
#include "../src/world/World.h"

namespace {

int fail(const std::string& message) {
    std::cerr << "[fluid_flow_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void loadChunks(World& world, const uint32_t seed) {
    world.init(seed);
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f, 0.0f, 0.0f));
    }
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

bool approxEqual(const float lhs, const float rhs, const float epsilon = 0.001f) {
    return std::fabs(lhs - rhs) <= epsilon;
}

const char* testSymmetricFlowReturnsZero() {
    World world;
    loadChunks(world, 20260410);

    const int y = 96;
    fillBox(world, -2, 2, y - 1, y - 1, -2, 2, BlockRegistry::requireIdByName("minecraft:stone"));
    fillBox(world, -2, 2, y, y + 1, -2, 2, RUNTIME_ID_NULL);

    world.setBlock(0, y, 0, FluidState::makeWater(3, false));
    world.setBlock(1, y, 0, FluidState::makeWater(3, false));
    world.setBlock(-1, y, 0, FluidState::makeWater(3, false));
    world.setBlock(0, y, 1, FluidState::makeWater(3, false));
    world.setBlock(0, y, -1, FluidState::makeWater(3, false));

    const glm::vec3 flow = computeFluidFlowVector(world, glm::ivec3(0, y, 0), FluidKind::Water);
    if (!approxEqual(flow.x, 0.0f) || !approxEqual(flow.y, 0.0f) || !approxEqual(flow.z, 0.0f)) {
        return "symmetric water heights should cancel to a zero flow vector";
    }
    return nullptr;
}

const char* testLowerNeighborBiasesDirection() {
    World world;
    loadChunks(world, 20260411);

    const int y = 100;
    fillBox(world, -2, 2, y - 1, y - 1, -1, 1, BlockRegistry::requireIdByName("minecraft:stone"));
    fillBox(world, -2, 2, y, y + 1, -1, 1, RUNTIME_ID_NULL);

    world.setBlock(0, y, 0, FluidState::makeWater(0, false));
    world.setBlock(1, y, 0, FluidState::makeWater(3, false));

    const glm::vec3 flow = computeFluidFlowVector(world, glm::ivec3(0, y, 0), FluidKind::Water);
    if (flow.x < 0.9f || !approxEqual(flow.y, 0.0f) || !approxEqual(flow.z, 0.0f)) {
        return "lower east neighbor should push the flow vector toward +X";
    }
    return nullptr;
}

const char* testFallingWaterAddsVerticalBias() {
    World world;
    loadChunks(world, 20260412);

    const int y = 104;
    fillBox(world, -1, 1, y - 2, y + 1, -1, 1, RUNTIME_ID_NULL);
    world.setBlock(0, y, 0, FluidState::makeWater(0, true));
    world.setBlock(1, y, 0, FluidState::makeWater(0, true));
    world.setBlock(-1, y, 0, FluidState::makeWater(0, true));

    const glm::vec3 flow = computeFluidFlowVector(world, glm::ivec3(0, y, 0), FluidKind::Water);
    if (flow.y > -0.9f || !approxEqual(flow.x, 0.0f) || !approxEqual(flow.z, 0.0f)) {
        return "falling water should inject a strong downward component into the flow vector";
    }
    return nullptr;
}

const char* testCappedColumnStaysStable() {
    World world;
    loadChunks(world, 20260413);

    const int y = 108;
    fillBox(world, -2, 2, y - 1, y - 1, -2, 2, BlockRegistry::requireIdByName("minecraft:stone"));
    fillBox(world, -2, 2, y, y + 2, -2, 2, RUNTIME_ID_NULL);

    world.setBlock(0, y, 0, FluidState::makeWater(3, false));
    world.setBlock(1, y, 0, FluidState::makeWater(3, false));
    world.setBlock(-1, y, 0, FluidState::makeWater(3, false));
    world.setBlock(0, y, 1, FluidState::makeWater(3, false));
    world.setBlock(0, y, -1, FluidState::makeWater(3, false));

    world.setBlock(0, y + 1, 0, FluidState::makeWater(0, false));
    world.setBlock(1, y + 1, 0, FluidState::makeWater(0, false));
    world.setBlock(-1, y + 1, 0, FluidState::makeWater(0, false));
    world.setBlock(0, y + 1, 1, FluidState::makeWater(0, false));
    world.setBlock(0, y + 1, -1, FluidState::makeWater(0, false));

    const glm::vec3 flow = computeFluidFlowVector(world, glm::ivec3(0, y, 0), FluidKind::Water);
    if (!approxEqual(flow.x, 0.0f) || !approxEqual(flow.y, 0.0f) || !approxEqual(flow.z, 0.0f)) {
        return "capped water columns with symmetric neighbors should stay directionally stable";
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
        {"symmetric_zero", testSymmetricFlowReturnsZero},
        {"single_side_bias", testLowerNeighborBiasesDirection},
        {"falling_bias", testFallingWaterAddsVerticalBias},
        {"capped_column", testCappedColumnStaysStable},
    };

    for (const TestCase& testCase : cases) {
        if (const char* error = testCase.run()) {
            return fail(std::string(testCase.name) + ": " + error);
        }
    }

    std::cout << "[fluid_flow_test] PASS\n";
    return EXIT_SUCCESS;
}
