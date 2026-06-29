#include <cstdlib>
#include <iostream>
#include <vector>

#include <glm/vec3.hpp>

#include "../src/world/World.h"
#include "../src/world/block/Block.h"
#include "../src/world/block/BlockStateRegistry.h"
#include "../src/world/block/PropIndices.h"
#include "../src/world/redstone/WireContainerPlacement.h"

namespace {

int fail(const char* message) {
    std::cerr << "[wire_container_placement_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void loadOriginChunks(World& world) {
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f, 0.0f, 0.0f));
    }
}

BlockStateId wireState(const char* blockName,
                       const uint16_t facing,
                       const uint16_t power = PropIndices::POWER_0) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName(blockName),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, facing},
            {PropIndices::POWER, power},
            {PropIndices::NORTH, PropIndices::NORTH_NONE},
            {PropIndices::SOUTH, PropIndices::SOUTH_NONE},
            {PropIndices::EAST, PropIndices::EAST_NONE},
            {PropIndices::WEST, PropIndices::WEST_NONE},
        });
}

BlockStateId redFloorWithEastConnection() {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:redstone_wire"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_FLOOR},
            {PropIndices::POWER, PropIndices::POWER_7},
            {PropIndices::NORTH, PropIndices::NORTH_NONE},
            {PropIndices::SOUTH, PropIndices::SOUTH_NONE},
            {PropIndices::EAST, PropIndices::EAST_SIDE},
            {PropIndices::WEST, PropIndices::WEST_NONE},
        });
}

uint16_t channelId(const char* blockName) {
    return BlockRegistry::get(BlockRegistry::requireIdByName(blockName)).redstoneWireChannelId;
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    World world;
    world.init(20260629);
    loadOriginChunks(world);

    const glm::ivec3 pos(0, 120, 0);
    const glm::ivec3 eastNeighbor = pos + glm::ivec3(1, 0, 0);
    world.setBlockState(pos.x, pos.y, pos.z, NULL_BLOCK_STATE);
    world.setBlockState(eastNeighbor.x, eastNeighbor.y, eastNeighbor.z, NULL_BLOCK_STATE);
    world.setBlockState(pos.x,
                        pos.y - 1,
                        pos.z,
                        BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
    world.setBlockState(eastNeighbor.x,
                        eastNeighbor.y - 1,
                        eastNeighbor.z,
                        BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));

    const uint16_t redChannel = channelId("minecraft:redstone_wire");
    const uint16_t blueChannel = channelId("minecraft:blue_redstone_wire");
    const BlockStateId redFloor = redFloorWithEastConnection();
    const BlockStateId blueFloor = wireState("minecraft:blue_redstone_wire", PropIndices::FACING_FLOOR);
    const BlockStateId blueNorth = wireState("minecraft:blue_redstone_wire", PropIndices::FACING_NORTH);

    world.setBlockState(pos.x, pos.y, pos.z, redFloor);
    world.setBlockState(eastNeighbor.x, eastNeighbor.y, eastNeighbor.z, wireState("minecraft:redstone_wire",
                                                                                  PropIndices::FACING_FLOOR));
    if (!WireContainerPlacement::canApply(world, pos, blueFloor)) {
        return fail("blue floor wire should be addable to a red floor wire cell");
    }
    if (WireContainerPlacement::apply(world, pos, blueFloor) != WireContainerPlacement::ApplyResult::Applied) {
        return fail("red plus blue floor wires should upgrade the cell to a wire container");
    }

    const BlockStateId containerState = world.getBlockState(pos.x, pos.y, pos.z);
    if (BlockStateRegistry::getBlockId(containerState) != BlockRegistry::requireIdByName("minecraft:wire_container")) {
        return fail("wire placement should replace the plain wire block with wire_container");
    }

    const WireContainerParts* parts = world.wireContainerParts().find(pos);
    if (parts == nullptr ||
        parts->find(redChannel, PropIndices::FACING_FLOOR) == nullptr ||
        parts->find(blueChannel, PropIndices::FACING_FLOOR) == nullptr ||
        parts->size() != 2) {
        return fail("wire container should store the migrated red part and incoming blue part");
    }
    const WirePart* migratedRed = parts->find(redChannel, PropIndices::FACING_FLOOR);
    if (migratedRed->power != 7 || migratedRed->connections != WireConnectionBits::AXIS1_POS) {
        return fail("wire container should preserve the migrated wire power and visual connection bits");
    }

    const uint64_t revisionBeforeAppend = world.getBlockContentRevision();
    if (WireContainerPlacement::apply(world, pos, blueNorth) != WireContainerPlacement::ApplyResult::Applied) {
        return fail("wire container should accept another face for an existing channel");
    }
    if (world.getBlockContentRevision() <= revisionBeforeAppend) {
        return fail("wire container part append should mark world block content dirty");
    }
    parts = world.wireContainerParts().find(pos);
    if (parts == nullptr ||
        parts->find(blueChannel, PropIndices::FACING_NORTH) == nullptr ||
        parts->size() != 3) {
        return fail("wire container should store appended wall-face wire parts");
    }

    if (WireContainerPlacement::apply(world, pos, blueFloor) != WireContainerPlacement::ApplyResult::Rejected ||
        world.wireContainerParts().find(pos)->size() != 3) {
        return fail("wire container should reject duplicate channel/facing parts without mutation");
    }

    const glm::ivec3 duplicatePos(1, 120, 0);
    world.setBlockState(duplicatePos.x,
                        duplicatePos.y - 1,
                        duplicatePos.z,
                        BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
    world.setBlockState(duplicatePos.x, duplicatePos.y, duplicatePos.z, redFloor);
    if (WireContainerPlacement::canApply(world, duplicatePos, redFloor) ||
        WireContainerPlacement::apply(world, duplicatePos, redFloor) != WireContainerPlacement::ApplyResult::Rejected ||
        world.wireContainerParts().find(duplicatePos) != nullptr) {
        return fail("same channel and same face should not upgrade a plain wire into a container");
    }

    std::cout << "[wire_container_placement_test] PASS\n";
    return EXIT_SUCCESS;
}
