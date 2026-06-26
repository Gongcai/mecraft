#include <cstdlib>
#include <exception>
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

StateID leverState(const bool powered) {
    return BlockStateRegistry::getState(
        BlockIds::LEVER,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_NORTH},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE}
        });
}

StateID buttonState(const BlockID blockId, const bool powered) {
    return BlockStateRegistry::getState(
        blockId,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_NORTH},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE}
        });
}

StateID openableState(const BlockID blockId, const bool open, const bool powered) {
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

    if (!game::redstone::isControlBlock(BlockIds::LEVER) ||
        !game::redstone::isControlBlock(BlockIds::STONE_BUTTON) ||
        !game::redstone::isControlBlock(BlockIds::OAK_BUTTON) ||
        !game::redstone::isControlBlock(BlockIds::REPEATER) ||
        !game::redstone::isControlBlock(BlockIds::COMPARATOR) ||
        !game::redstone::isControlBlock(BlockIds::OAK_DOOR) ||
        !game::redstone::isControlBlock(BlockIds::OAK_TRAPDOOR) ||
        !game::redstone::isControlBlock(BlockIds::OAK_FENCE_GATE)) {
        return fail("lever, buttons, repeater, comparator, and openable blocks should be right-click redstone controls");
    }
    if (game::redstone::isControlBlock(BlockIds::REDSTONE_LAMP) ||
        game::redstone::isControlBlock(BlockIds::OAK_PLANKS)) {
        return fail("non-control blocks should not be treated as direct right-click controls");
    }

    const StateID leverOff = leverState(false);
    const StateID leverOn = game::redstone::nextControlState(leverOff);
    if (BlockStateRegistry::getPropertyIndex(leverOn, PropIndices::POWERED) != PropIndices::POWERED_TRUE) {
        return fail("right-clicking an unpowered lever should switch powered to true");
    }
    const ModelVariant* leverOnVariant = BlockStateRegistry::getModelVariant(leverOn);
    if (leverOnVariant == nullptr ||
        leverOnVariant->model == nullptr ||
        leverOnVariant->model->name != "block/lever_wall_powered") {
        return fail("powered lever state should resolve to the powered model variant");
    }

    const StateID leverOffAgain = game::redstone::nextControlState(leverOn);
    if (BlockStateRegistry::getPropertyIndex(leverOffAgain, PropIndices::POWERED) != PropIndices::POWERED_FALSE) {
        return fail("right-clicking a powered lever should switch powered to false");
    }
    const ModelVariant* leverOffVariant = BlockStateRegistry::getModelVariant(leverOffAgain);
    if (leverOffVariant == nullptr ||
        leverOffVariant->model == nullptr ||
        leverOffVariant->model->name != "block/lever_wall") {
        return fail("unpowered lever state should resolve to the unpowered model variant");
    }

    const StateID stoneButtonOff = buttonState(BlockIds::STONE_BUTTON, false);
    const StateID stoneButtonOn = game::redstone::nextControlState(stoneButtonOff);
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

    const StateID repeaterDelay1 = BlockStateRegistry::getState(
        BlockIds::REPEATER,
        PropIndices::DELAY,
        PropIndices::DELAY_1);
    const StateID repeaterDelay2 = game::redstone::nextControlState(repeaterDelay1);
    if (BlockStateRegistry::getPropertyIndex(repeaterDelay2, PropIndices::DELAY) != PropIndices::DELAY_2) {
        return fail("right-clicking a repeater should advance its delay");
    }

    const StateID comparatorCompare = BlockStateRegistry::getState(
        BlockIds::COMPARATOR,
        PropIndices::MODE,
        PropIndices::MODE_COMPARE);
    const StateID comparatorSubtract = game::redstone::nextControlState(comparatorCompare);
    if (BlockStateRegistry::getPropertyIndex(comparatorSubtract, PropIndices::MODE) != PropIndices::MODE_SUBTRACT) {
        return fail("right-clicking a comparator should toggle subtract mode");
    }

    const StateID trapdoorClosedPowered = openableState(BlockIds::OAK_TRAPDOOR, false, true);
    const StateID trapdoorOpenPowered = game::redstone::nextControlState(trapdoorClosedPowered);
    if (BlockStateRegistry::getPropertyIndex(trapdoorOpenPowered, PropIndices::OPEN) != PropIndices::OPEN_TRUE ||
        BlockStateRegistry::getPropertyIndex(trapdoorOpenPowered, PropIndices::POWERED) != PropIndices::POWERED_TRUE) {
        return fail("right-clicking an openable block should toggle open without changing powered");
    }
    const StateID trapdoorClosedAgain = game::redstone::nextControlState(trapdoorOpenPowered);
    if (BlockStateRegistry::getPropertyIndex(trapdoorClosedAgain, PropIndices::OPEN) != PropIndices::OPEN_FALSE ||
        BlockStateRegistry::getPropertyIndex(trapdoorClosedAgain, PropIndices::POWERED) != PropIndices::POWERED_TRUE) {
        return fail("right-clicking an open openable block should close it without changing powered");
    }

    const StateID fenceGateClosed = openableState(BlockIds::OAK_FENCE_GATE, false, false);
    const StateID fenceGateOpen = game::redstone::nextControlState(fenceGateClosed);
    if (BlockStateRegistry::getPropertyIndex(fenceGateOpen, PropIndices::OPEN) != PropIndices::OPEN_TRUE ||
        BlockStateRegistry::getPropertyIndex(fenceGateOpen, PropIndices::POWERED) != PropIndices::POWERED_FALSE) {
        return fail("right-clicking a fence gate should use the generic open property interaction");
    }

    const StateID doorClosed = BlockStateRegistry::getDefaultState(BlockIds::OAK_DOOR);
    const StateID doorOpen = game::redstone::nextControlState(doorClosed);
    if (BlockStateRegistry::getPropertyIndex(doorOpen, PropIndices::OPEN) != PropIndices::OPEN_TRUE ||
        BlockStateRegistry::getPropertyIndex(doorOpen, PropIndices::POWERED) != PropIndices::POWERED_FALSE ||
        BlockStateRegistry::getPropertyIndex(doorOpen, PropIndices::HALF) != PropIndices::HALF_LOWER) {
        return fail("right-clicking a door state should toggle open while preserving powered and half");
    }

    bool unsupportedThrew = false;
    try {
        static_cast<void>(game::redstone::nextControlState(
            BlockStateRegistry::getDefaultState(BlockIds::REDSTONE_LAMP)));
    } catch (const std::exception&) {
        unsupportedThrew = true;
    }
    if (!unsupportedThrew) {
        return fail("unsupported redstone controls should fail loudly");
    }

    std::cout << "[redstone_control_interaction_test] PASS\n";
    return EXIT_SUCCESS;
}
