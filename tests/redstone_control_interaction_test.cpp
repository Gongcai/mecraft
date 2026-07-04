#include <cstdlib>
#include <iostream>
#include <vector>

#include "../src/game/redstone/RedstoneControlInteraction.h"
#include "../src/world/block/BlockStateRegistry.h"
#include "../src/world/block/PropIndices.h"

namespace {

int fail(const char* message) {
    std::cerr << "[redstone_control_interaction_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

BlockStateId leverState(const bool powered) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:lever"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_NORTH},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE}
        });
}

BlockStateId buttonState(const BlockID blockId, const bool powered) {
    return BlockStateRegistry::getState(
        blockId,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_NORTH},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE}
        });
}

BlockStateId openableState(const BlockID blockId, const bool open, const bool powered) {
    return BlockStateRegistry::getState(
        blockId,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::OPEN, open ? PropIndices::OPEN_TRUE : PropIndices::OPEN_FALSE},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE}
        });
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    if (!game::redstone::isControlBlock(BlockRegistry::requireIdByName("minecraft:lever")) ||
        !game::redstone::isControlBlock(BlockRegistry::requireIdByName("minecraft:stone_button")) ||
        !game::redstone::isControlBlock(BlockRegistry::requireIdByName("minecraft:oak_button")) ||
        !game::redstone::isControlBlock(BlockRegistry::requireIdByName("minecraft:repeater")) ||
        !game::redstone::isControlBlock(BlockRegistry::requireIdByName("minecraft:comparator")) ||
        !game::redstone::isControlBlock(BlockRegistry::requireIdByName("minecraft:oak_door")) ||
        !game::redstone::isControlBlock(BlockRegistry::requireIdByName("minecraft:oak_trapdoor")) ||
        !game::redstone::isControlBlock(BlockRegistry::requireIdByName("minecraft:oak_fence_gate"))) {
        return fail("lever, buttons, repeater, comparator, and openable blocks should be right-click redstone controls");
    }
    if (game::redstone::isControlBlock(BlockRegistry::requireIdByName("minecraft:redstone_lamp")) ||
        game::redstone::isControlBlock(BlockRegistry::requireIdByName("minecraft:oak_planks"))) {
        return fail("non-control blocks should not be treated as direct right-click controls");
    }

    const BlockStateId leverOff = leverState(false);
    const BlockStateId leverOn = game::redstone::nextControlState(leverOff);
    if (BlockStateRegistry::getPropertyIndex(leverOn, PropIndices::POWERED) != PropIndices::POWERED_TRUE) {
        return fail("right-clicking an unpowered lever should switch powered to true");
    }
    const ModelVariant* leverOnVariant = BlockStateRegistry::getModelVariant(leverOn);
    if (leverOnVariant == nullptr ||
        leverOnVariant->model == nullptr ||
        leverOnVariant->model->name != "block/lever_wall_powered") {
        return fail("powered lever state should resolve to the powered model variant");
    }

    const BlockStateId leverOffAgain = game::redstone::nextControlState(leverOn);
    if (BlockStateRegistry::getPropertyIndex(leverOffAgain, PropIndices::POWERED) != PropIndices::POWERED_FALSE) {
        return fail("right-clicking a powered lever should switch powered to false");
    }
    const ModelVariant* leverOffVariant = BlockStateRegistry::getModelVariant(leverOffAgain);
    if (leverOffVariant == nullptr ||
        leverOffVariant->model == nullptr ||
        leverOffVariant->model->name != "block/lever_wall") {
        return fail("unpowered lever state should resolve to the unpowered model variant");
    }

    const BlockStateId stoneButtonOff = buttonState(BlockRegistry::requireIdByName("minecraft:stone_button"), false);
    const BlockStateId stoneButtonOn = game::redstone::nextControlState(stoneButtonOff);
    if (BlockStateRegistry::getPropertyIndex(stoneButtonOn, PropIndices::POWERED) != PropIndices::POWERED_TRUE) {
        return fail("right-clicking an unpowered stone button should switch powered to true");
    }
    const ModelVariant* buttonOnVariant = BlockStateRegistry::getModelVariant(stoneButtonOn);
    if (buttonOnVariant == nullptr ||
        buttonOnVariant->model == nullptr ||
        buttonOnVariant->model->name != "block/stone_button_wall_pressed") {
        return fail("powered stone button state should resolve to the pressed model variant");
    }
    if (game::redstone::nextControlState(stoneButtonOn) != stoneButtonOn) {
        return fail("right-clicking an already powered button should keep the active pulse state");
    }

    const BlockStateId repeaterDelay1 = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:repeater"),
        PropIndices::DELAY,
        PropIndices::DELAY_1);
    const BlockStateId repeaterDelay2 = game::redstone::nextControlState(repeaterDelay1);
    if (BlockStateRegistry::getPropertyIndex(repeaterDelay2, PropIndices::DELAY) != PropIndices::DELAY_2) {
        return fail("right-clicking a repeater should advance its delay");
    }

    const BlockStateId comparatorCompare = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:comparator"),
        PropIndices::MODE,
        PropIndices::MODE_COMPARE);
    const BlockStateId comparatorSubtract = game::redstone::nextControlState(comparatorCompare);
    if (BlockStateRegistry::getPropertyIndex(comparatorSubtract, PropIndices::MODE) != PropIndices::MODE_SUBTRACT) {
        return fail("right-clicking a comparator should toggle subtract mode");
    }

    const BlockStateId trapdoorClosedPowered = openableState(BlockRegistry::requireIdByName("minecraft:oak_trapdoor"), false, true);
    const BlockStateId trapdoorOpenPowered = game::redstone::nextControlState(trapdoorClosedPowered);
    if (BlockStateRegistry::getPropertyIndex(trapdoorOpenPowered, PropIndices::OPEN) != PropIndices::OPEN_TRUE ||
        BlockStateRegistry::getPropertyIndex(trapdoorOpenPowered, PropIndices::POWERED) != PropIndices::POWERED_TRUE) {
        return fail("right-clicking an openable block should toggle open without changing powered");
    }
    const BlockStateId trapdoorClosedAgain = game::redstone::nextControlState(trapdoorOpenPowered);
    if (BlockStateRegistry::getPropertyIndex(trapdoorClosedAgain, PropIndices::OPEN) != PropIndices::OPEN_FALSE ||
        BlockStateRegistry::getPropertyIndex(trapdoorClosedAgain, PropIndices::POWERED) != PropIndices::POWERED_TRUE) {
        return fail("right-clicking an open openable block should close it without changing powered");
    }

    const BlockStateId fenceGateClosed = openableState(BlockRegistry::requireIdByName("minecraft:oak_fence_gate"), false, false);
    const BlockStateId fenceGateOpen = game::redstone::nextControlState(fenceGateClosed);
    if (BlockStateRegistry::getPropertyIndex(fenceGateOpen, PropIndices::OPEN) != PropIndices::OPEN_TRUE ||
        BlockStateRegistry::getPropertyIndex(fenceGateOpen, PropIndices::POWERED) != PropIndices::POWERED_FALSE) {
        return fail("right-clicking a fence gate should use the generic open property interaction");
    }

    const BlockStateId doorClosed = BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:oak_door"));
    const BlockStateId doorOpen = game::redstone::nextControlState(doorClosed);
    if (BlockStateRegistry::getPropertyIndex(doorOpen, PropIndices::OPEN) != PropIndices::OPEN_TRUE ||
        BlockStateRegistry::getPropertyIndex(doorOpen, PropIndices::POWERED) != PropIndices::POWERED_FALSE ||
        BlockStateRegistry::getPropertyIndex(doorOpen, PropIndices::HALF) != PropIndices::HALF_LOWER) {
        return fail("right-clicking a door state should toggle open while preserving powered and half");
    }

    std::cout << "[redstone_control_interaction_test] PASS\n";
    return EXIT_SUCCESS;
}
