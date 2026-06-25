#include "RedstoneControlInteraction.h"

#include "../../world/block/BlockStateRegistry.h"
#include "../../world/block/PropIndices.h"

#include <stdexcept>
#include <string>

namespace game::redstone {

namespace {

void requirePoweredProperties() {
    if (PropIndices::POWERED == PropIndices::INVALID ||
        PropIndices::POWERED_TRUE == PropIndices::INVALID ||
        PropIndices::POWERED_FALSE == PropIndices::INVALID) {
        throw std::runtime_error("Lever interaction requires registered powered boolean values");
    }
}

void requireRepeaterDelayProperties() {
    if (PropIndices::DELAY == PropIndices::INVALID ||
        PropIndices::DELAY_1 == PropIndices::INVALID ||
        PropIndices::DELAY_2 == PropIndices::INVALID ||
        PropIndices::DELAY_3 == PropIndices::INVALID ||
        PropIndices::DELAY_4 == PropIndices::INVALID) {
        throw std::runtime_error("Repeater interaction requires registered delay property values");
    }
}

void requireComparatorModeProperties() {
    if (PropIndices::MODE == PropIndices::INVALID ||
        PropIndices::MODE_COMPARE == PropIndices::INVALID ||
        PropIndices::MODE_SUBTRACT == PropIndices::INVALID) {
        throw std::runtime_error("Comparator interaction requires registered mode property values");
    }
}

uint16_t toggledPoweredValue(const uint16_t currentPowered) {
    if (currentPowered == PropIndices::POWERED_FALSE) {
        return PropIndices::POWERED_TRUE;
    }
    if (currentPowered == PropIndices::POWERED_TRUE) {
        return PropIndices::POWERED_FALSE;
    }
    throw std::runtime_error("Lever state contains an unknown powered value");
}

uint16_t nextRepeaterDelayValue(const uint16_t currentDelay) {
    if (currentDelay == PropIndices::DELAY_1) {
        return PropIndices::DELAY_2;
    }
    if (currentDelay == PropIndices::DELAY_2) {
        return PropIndices::DELAY_3;
    }
    if (currentDelay == PropIndices::DELAY_3) {
        return PropIndices::DELAY_4;
    }
    if (currentDelay == PropIndices::DELAY_4) {
        return PropIndices::DELAY_1;
    }
    throw std::runtime_error("Repeater state contains an unknown delay value");
}

uint16_t toggledComparatorModeValue(const uint16_t currentMode) {
    if (currentMode == PropIndices::MODE_COMPARE) {
        return PropIndices::MODE_SUBTRACT;
    }
    if (currentMode == PropIndices::MODE_SUBTRACT) {
        return PropIndices::MODE_COMPARE;
    }
    throw std::runtime_error("Comparator state contains an unknown mode value");
}

StateID withRequiredProperty(const StateID currentState,
                             const uint16_t property,
                             const uint16_t value,
                             const char* transitionName) {
    const StateID updatedState = BlockStateRegistry::withProperty(currentState, property, value);
    if (updatedState == currentState) {
        throw std::runtime_error(std::string(transitionName) + " state transition failed");
    }
    return updatedState;
}

} // namespace

bool isControlBlock(const BlockID blockId) {
    return blockId == BlockIds::LEVER ||
           blockId == BlockIds::REPEATER ||
           blockId == BlockIds::COMPARATOR;
}

StateID nextControlState(const StateID currentState) {
    const BlockID blockId = BlockStateRegistry::getBlockId(currentState);

    if (blockId == BlockIds::LEVER) {
        requirePoweredProperties();
        const uint16_t currentPowered = BlockStateRegistry::getPropertyIndex(currentState, PropIndices::POWERED);
        if (currentPowered == BlockStateRegistry::INVALID_INDEX) {
            throw std::runtime_error("Lever state is missing the powered property");
        }
        return withRequiredProperty(
            currentState,
            PropIndices::POWERED,
            toggledPoweredValue(currentPowered),
            "Lever powered");
    }

    if (blockId == BlockIds::REPEATER) {
        requireRepeaterDelayProperties();
        const uint16_t currentDelay = BlockStateRegistry::getPropertyIndex(currentState, PropIndices::DELAY);
        if (currentDelay == BlockStateRegistry::INVALID_INDEX) {
            throw std::runtime_error("Repeater state is missing the delay property");
        }
        return withRequiredProperty(
            currentState,
            PropIndices::DELAY,
            nextRepeaterDelayValue(currentDelay),
            "Repeater delay");
    }

    if (blockId == BlockIds::COMPARATOR) {
        requireComparatorModeProperties();
        const uint16_t currentMode = BlockStateRegistry::getPropertyIndex(currentState, PropIndices::MODE);
        if (currentMode == BlockStateRegistry::INVALID_INDEX) {
            throw std::runtime_error("Comparator state is missing the mode property");
        }
        return withRequiredProperty(
            currentState,
            PropIndices::MODE,
            toggledComparatorModeValue(currentMode),
            "Comparator mode");
    }

    throw std::runtime_error("Unsupported redstone control block interaction");
}

} // namespace game::redstone
