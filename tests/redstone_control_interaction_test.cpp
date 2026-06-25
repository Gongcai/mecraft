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

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    if (!game::redstone::isControlBlock(BlockIds::LEVER) ||
        !game::redstone::isControlBlock(BlockIds::REPEATER) ||
        !game::redstone::isControlBlock(BlockIds::COMPARATOR)) {
        return fail("lever, repeater, and comparator should be right-click redstone controls");
    }
    if (game::redstone::isControlBlock(BlockIds::REDSTONE_LAMP)) {
        return fail("redstone lamp should not be treated as a direct right-click control");
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
