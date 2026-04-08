#include <cstdlib>
#include <iostream>

#include <glm/vec3.hpp>

#include "../src/world/World.h"

namespace {
int fail(const char* message) {
    std::cerr << "[world_flat_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main() {
    World world;
    world.init(20260324);
    world.setRenderDistance(1);

    // update() throttles loading to 4 chunks/frame; tick a few frames to fill the radius.
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f, 0.0f, 0.0f));
    }

    const auto& chunks = world.getActiveChunks();
    if (chunks.size() != 9) {
        return fail("unexpected active chunk count for render distance 1");
    }

    const int surfaceY = world.getSurfaceY(0, 0);
    if (surfaceY < 8 || surfaceY > Chunk::SIZE_Y - 8) {
        return fail("surfaceY out of expected generated range");
    }
    if (world.getBlock(0, surfaceY + 1, 0) != BlockType::AIR) {
        return fail("block above surface should be AIR");
    }
    const BlockID topBlock = world.getBlock(0, surfaceY, 0);
    if (topBlock != BlockType::GRASS && topBlock != BlockType::SAND &&
        topBlock != BlockType::DIRT && topBlock != BlockType::STONE &&
        topBlock != BlockType::WATER) {
        return fail("surface block should be a valid generated terrain block");
    }

    const BlockID underBlock = world.getBlock(0, surfaceY - 2, 0);
    if (underBlock == BlockType::AIR) {
        return fail("sub-surface layer should not be AIR");
    }

    if (world.getBlock(0, 1, 0) != BlockType::STONE) {
        return fail("deep layer should include STONE");
    }

    const int negSurfaceY = world.getSurfaceY(-1, -1);
    if (world.getBlock(-1, negSurfaceY, -1) == BlockType::AIR) {
        return fail("negative world coordinate mapping failed");
    }

    const int edgeA = world.getSurfaceY(15, 0);
    const int edgeB = world.getSurfaceY(16, 0);
    if (std::abs(edgeA - edgeB) > 12) {
        return fail("height discontinuity too large across chunk border");
    }


    World world2;
    world2.init(20260324);
    world2.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world2.update(glm::vec3(0.0f, 0.0f, 0.0f));
    }
    if (world2.getSurfaceY(0, 0) != surfaceY) {
        return fail("same seed must produce same surface height");
    }

    world.setBlock(0, surfaceY + 1, 0, BlockType::WOOD);
    if (world.getBlock(0, surfaceY + 1, 0) != BlockType::WOOD) {
        return fail("setBlock/getBlock mismatch");
    }

    std::cout << "[world_flat_test] PASS\n";
    return EXIT_SUCCESS;
}

