#include <cstdlib>
#include <iostream>

#include <glm/vec3.hpp>

#include "../src/world/FluidState.h"
#include "../src/world/World.h"

namespace {
int fail(const char* message) {
    std::cerr << "[world_flat_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

bool hasDanglingNeighbor(const World& world) {
    const auto& chunks = world.getActiveChunks();
    for (const auto& entry : chunks) {
        const Chunk* chunk = entry.second.get();
        for (int neighborIndex = 0; neighborIndex < 4; ++neighborIndex) {
            const Chunk* neighbor = chunk->neighbors[neighborIndex];
            if (neighbor == nullptr) {
                continue;
            }

            const int64_t neighborKey = World::chunkKey(neighbor->m_chunkX, neighbor->m_chunkZ);
            const auto neighborIt = chunks.find(neighborKey);
            if (neighborIt == chunks.end() || neighborIt->second.get() != neighbor) {
                return true;
            }
        }
    }

    return false;
}
}

int main() {
    BlockRegistry::init(nullptr);

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
    if (world.getBlock(0, surfaceY + 1, 0) != BlockIds::AIR) {
        return fail("block above surface should be AIR");
    }
    const BlockID topBlock = world.getBlock(0, surfaceY, 0);
    if (topBlock != BlockIds::GRASS && topBlock != BlockIds::SAND &&
        topBlock != BlockIds::DIRT && topBlock != BlockIds::STONE &&
        !FluidState::isWater(topBlock) && topBlock != BlockIds::TALL_GRASS &&
        topBlock != BlockIds::ROSE) {
        return fail("surface block should be a valid generated terrain block");
    }

    if ((topBlock == BlockIds::TALL_GRASS || topBlock == BlockIds::ROSE) &&
        world.getBlock(0, surfaceY - 1, 0) != BlockIds::GRASS) {
        return fail("vegetation surface block should be rooted on grass");
    }

    const BlockID underBlock = world.getBlock(0, surfaceY - 2, 0);
    if (underBlock == BlockIds::AIR) {
        return fail("sub-surface layer should not be AIR");
    }

    if (world.getBlock(0, 1, 0) != BlockIds::STONE) {
        return fail("deep layer should include STONE");
    }

    const int negSurfaceY = world.getSurfaceY(-1, -1);
    if (world.getBlock(-1, negSurfaceY, -1) == BlockIds::AIR) {
        return fail("negative world coordinate mapping failed");
    }

    const int edgeA = world.getSurfaceY(15, 0);
    const int edgeB = world.getSurfaceY(16, 0);
    if (std::abs(edgeA - edgeB) > 12) {
        return fail("height discontinuity too large across chunk border");
    }

    if (hasDanglingNeighbor(world)) {
        return fail("freshly loaded chunks should not contain dangling neighbor pointers");
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

    world.setBlock(0, surfaceY + 1, 0, BlockIds::WOOD);
    if (world.getBlock(0, surfaceY + 1, 0) != BlockIds::WOOD) {
        return fail("setBlock/getBlock mismatch");
    }

    world.update(glm::vec3(static_cast<float>(Chunk::SIZE_X), 0.0f, 0.0f));
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(static_cast<float>(Chunk::SIZE_X), 0.0f, 0.0f));
    }

    if (hasDanglingNeighbor(world)) {
        return fail("unloading chunks should clear neighbor back-pointers on survivors");
    }

    const auto& shiftedChunks = world.getActiveChunks();
    for (int z = -1; z <= 1; ++z) {
        const auto it = shiftedChunks.find(World::chunkKey(0, z));
        if (it != shiftedChunks.end() && it->second->neighbors[1] != nullptr) {
            return fail("chunks on the new west edge should not retain an unloaded -X neighbor");
        }
    }

    std::cout << "[world_flat_test] PASS\n";
    return EXIT_SUCCESS;
}

