#include <cstdlib>
#include <iostream>

#include <glm/vec3.hpp>

#include "../src/world/fluid/FluidState.h"
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
    if (chunks.size() != 5) {
        return fail("unexpected active chunk count for circular render distance 1");
    }

    const int surfaceY = world.getSurfaceY(0, 0);
    if (surfaceY < 8 || surfaceY > Chunk::SIZE_Y - 8) {
        return fail("surfaceY out of expected generated range");
    }
    if (world.getBlock(0, surfaceY + 1, 0) != RUNTIME_ID_NULL) {
        return fail("block above surface should be AIR");
    }
    const BlockID topBlock = world.getBlock(0, surfaceY, 0);
    if (topBlock != BlockRegistry::requireIdByName("minecraft:grass_block") && topBlock != BlockRegistry::requireIdByName("minecraft:sand") &&
        topBlock != BlockRegistry::requireIdByName("minecraft:dirt") && topBlock != BlockRegistry::requireIdByName("minecraft:stone") &&
        topBlock != BlockRegistry::requireIdByName("minecraft:oak_log") && topBlock != BlockRegistry::requireIdByName("minecraft:birch_log") &&
        topBlock != BlockRegistry::requireIdByName("minecraft:oak_leaves") && topBlock != BlockRegistry::requireIdByName("minecraft:birch_leaves") &&
        !FluidState::isWater(topBlock) && topBlock != BlockRegistry::requireIdByName("minecraft:tall_grass") &&
        topBlock != BlockRegistry::requireIdByName("minecraft:rose")) {
        return fail("surface block should be a valid generated terrain block");
    }

    if ((topBlock == BlockRegistry::requireIdByName("minecraft:tall_grass") || topBlock == BlockRegistry::requireIdByName("minecraft:rose")) &&
        world.getBlock(0, surfaceY - 1, 0) != BlockRegistry::requireIdByName("minecraft:grass_block")) {
        return fail("vegetation surface block should be rooted on grass");
    }

    const BlockID underBlock = world.getBlock(0, surfaceY - 2, 0);
    if (underBlock == RUNTIME_ID_NULL) {
        return fail("sub-surface layer should not be AIR");
    }

    if (world.getBlock(0, 1, 0) != BlockRegistry::requireIdByName("minecraft:stone")) {
        return fail("deep layer should include STONE");
    }

    const int negSurfaceY = world.getSurfaceY(-1, 0);
    if (world.getBlock(-1, negSurfaceY, 0) == RUNTIME_ID_NULL) {
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

    world.setBlock(0, surfaceY + 1, 0, BlockRegistry::requireIdByName("minecraft:oak_log"));
    if (world.getBlock(0, surfaceY + 1, 0) != BlockRegistry::requireIdByName("minecraft:oak_log")) {
        return fail("setBlock/getBlock mismatch");
    }

    const int waterloggedY = surfaceY + 2;
    world.setBlock(0, waterloggedY, 0, BlockRegistry::requireIdByName("minecraft:torch"));
    world.setFluidState(0, waterloggedY, 0, FluidState::makeWater(0, false));
    world.setBlock(0, waterloggedY, 0, RUNTIME_ID_NULL);
    if (!FluidState::isWater(world.getBlock(0, waterloggedY, 0))) {
        return fail("breaking a waterlogged block should restore pure water in the block layer");
    }
    if (world.getFluidState(0, waterloggedY, 0) != world.getBlock(0, waterloggedY, 0)) {
        return fail("restored water should not remain only in the dedicated fluid layer");
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

