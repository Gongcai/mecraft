#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "../src/world/Block.h"
#include "../src/world/BlockStateRegistry.h"
#include "../src/world/Placement.h"
#include "../src/world/PropIndices.h"
#include "../src/world/SubChunk.h"

namespace {
int fail(const char* message) {
    std::cerr << "[block_state_registry_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main() {
    BlockRegistry::init(nullptr);

    if (PropIndices::FACING == PropIndices::INVALID) {
        return fail("facing property should be registered from blocks.json");
    }
    if (PropIndices::AXIS == PropIndices::INVALID) {
        return fail("axis property should be registered from blocks.json");
    }

    const StateID torchDefault = BlockStateRegistry::getDefaultState(BlockIds::TORCH);
    if (torchDefault == BlockIds::TORCH) {
        return fail("torch should expand into dedicated state ids");
    }
    if (BlockStateRegistry::getBlockId(torchDefault) != BlockIds::TORCH) {
        return fail("expanded torch state should resolve back to torch block id");
    }
    if (BlockStateRegistry::getPropertyIndex(torchDefault, PropIndices::FACING) != PropIndices::FACING_FLOOR) {
        return fail("torch default state should face floor");
    }

    const StateID torchNorth = BlockStateRegistry::withProperty(
        torchDefault,
        PropIndices::FACING,
        PropIndices::FACING_NORTH);
    if (torchNorth == torchDefault) {
        return fail("withProperty should produce a distinct torch north-facing state");
    }
    if (BlockStateRegistry::getPropertyIndex(torchNorth, PropIndices::FACING) != PropIndices::FACING_NORTH) {
        return fail("torch north-facing state should report facing=north");
    }
    if (!BlockRegistry::get(torchNorth).isLightSource) {
        return fail("state ids should resolve through BlockRegistry to the owning block definition");
    }
    if (BlockRegistry::getBlockDropId(torchNorth) != BlockRegistry::getBlockDropId(BlockIds::TORCH)) {
        return fail("state ids should reuse the owning block drop table entry");
    }

    const StateID birchLogDefault = BlockStateRegistry::getDefaultState(BlockIds::BIRCH_LOG);
    if (BlockStateRegistry::getPropertyIndex(birchLogDefault, PropIndices::AXIS) != PropIndices::AXIS_Y) {
        return fail("birch log default state should be axis=y");
    }

    const StateID birchLogX = BlockStateRegistry::getState(
        BlockIds::BIRCH_LOG,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::AXIS, PropIndices::AXIS_X}
        });
    if (birchLogX == birchLogDefault) {
        return fail("birch log x-axis state should differ from the default state");
    }
    if (BlockStateRegistry::getPropertyIndex(birchLogX, PropIndices::AXIS) != PropIndices::AXIS_X) {
        return fail("birch log x-axis state should report axis=x");
    }

    const StateTextureIndices& birchLogDefaultTextures = BlockStateRegistry::getStateTextures(birchLogDefault);
    const StateTextureIndices& birchLogXTextures = BlockStateRegistry::getStateTextures(birchLogX);
    if (birchLogXTextures.texLeft != birchLogDefaultTextures.texTop ||
        birchLogXTextures.texRight != birchLogDefaultTextures.texTop) {
        return fail("birch log x-axis state should rotate end-grain textures onto left/right faces");
    }
    if (birchLogXTextures.texTop != birchLogDefaultTextures.texFront ||
        birchLogXTextures.texBottom != birchLogDefaultTextures.texFront) {
        return fail("birch log x-axis state should rotate bark textures onto top/bottom faces");
    }

    const std::string torchStateString = BlockStateRegistry::stateToString(torchNorth);
    if (torchStateString.find("facing=north") == std::string::npos) {
        return fail("stateToString should include the resolved torch facing");
    }

    const BlockDef& torchDef = BlockRegistry::get(BlockIds::TORCH);
    if (torchDef.placementStrategy != "attach_wall") {
        return fail("torch should parse placementStrategy from blocks.json");
    }
    if (torchDef.supportRule != "attached_face") {
        return fail("torch should parse supportRule from blocks.json");
    }
    if (torchDef.renderShapeName != "torch") {
        return fail("torch should parse its registered render shape name from blocks.json");
    }

    const BlockDef& birchLogDef = BlockRegistry::get(BlockIds::BIRCH_LOG);
    if (birchLogDef.placementStrategy != "axis_oriented") {
        return fail("birch log should parse axis-oriented placement strategy");
    }

    PlacementStrategyFn torchStrategy = PlacementStrategyRegistry::getStrategy(torchDef.placementStrategy);
    if (torchStrategy == nullptr) {
        return fail("torch placement strategy should be registered");
    }

    PlacementContext torchPlacement;
    torchPlacement.blockId = BlockIds::TORCH;
    torchPlacement.hitNormal = {0, 0, -1};
    const StateID torchPlacedNorth = torchStrategy(torchPlacement);
    if (BlockStateRegistry::getPropertyIndex(torchPlacedNorth, PropIndices::FACING) != PropIndices::FACING_NORTH) {
        return fail("attach_wall placement should derive torch facing from hit normal");
    }

    PlacementStrategyFn logStrategy = PlacementStrategyRegistry::getStrategy(birchLogDef.placementStrategy);
    if (logStrategy == nullptr) {
        return fail("birch log placement strategy should be registered");
    }

    PlacementContext logPlacement;
    logPlacement.blockId = BlockIds::BIRCH_LOG;
    logPlacement.hitNormal = {1, 0, 0};
    const StateID birchLogPlacedX = logStrategy(logPlacement);
    if (BlockStateRegistry::getPropertyIndex(birchLogPlacedX, PropIndices::AXIS) != PropIndices::AXIS_X) {
        return fail("axis_oriented placement should derive log axis from hit normal");
    }

    if (BlockStateRegistry::getStateCount() <= BlockRegistry::getBlockCount()) {
        return fail("state registry should contain expanded states beyond raw block ids");
    }

    {
        SubChunk subChunk;
        std::vector<BlockID> ids;
        ids.reserve(300);
        for (int i = 0; i < 300; ++i) {
            const BlockID id = BlockRegistry::registerBlock(
                NamespacedId("test", "palette_" + std::to_string(i)),
                BlockDef{});
            ids.push_back(id);

            const int x = i % SubChunk::SIZE;
            const int z = (i / SubChunk::SIZE) % SubChunk::SIZE;
            const int y = i / (SubChunk::SIZE * SubChunk::SIZE);
            subChunk.setBlockFast(x, y, z, id);
        }

        for (int i = 0; i < 300; ++i) {
            const int x = i % SubChunk::SIZE;
            const int z = (i / SubChunk::SIZE) % SubChunk::SIZE;
            const int y = i / (SubChunk::SIZE * SubChunk::SIZE);
            if (subChunk.getBlock(x, y, z) != ids[i]) {
                return fail("sub-chunk palette should preserve block ids past 256 unique entries");
            }
        }
    }

    std::cout << "[block_state_registry_test] PASS\n";
    return EXIT_SUCCESS;
}
