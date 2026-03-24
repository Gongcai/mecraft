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

    const int surfaceY = world.getFlatSurfaceY();
    if (world.getBlock(0, surfaceY + 1, 0) != BlockType::AIR) {
        return fail("block above surface should be AIR");
    }
    if (world.getBlock(0, surfaceY, 0) != BlockType::GRASS) {
        return fail("surface block should be GRASS");
    }
    if (world.getBlock(0, surfaceY - 2, 0) != BlockType::DIRT) {
        return fail("sub-surface layer should be DIRT");
    }
    if (world.getBlock(0, surfaceY - 6, 0) != BlockType::STONE) {
        return fail("deep layer should be STONE");
    }

    if (world.getBlock(-1, surfaceY, -1) != BlockType::GRASS) {
        return fail("negative world coordinate mapping failed");
    }

    world.setBlock(0, surfaceY + 1, 0, BlockType::WOOD);
    if (world.getBlock(0, surfaceY + 1, 0) != BlockType::WOOD) {
        return fail("setBlock/getBlock mismatch");
    }

    std::cout << "[world_flat_test] PASS\n";
    return EXIT_SUCCESS;
}

