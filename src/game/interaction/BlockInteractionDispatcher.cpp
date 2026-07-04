#include "BlockInteractionDispatcher.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "BlockInteractionRegistry.h"
#include "../../world/World.h"
#include "../../world/block/DoorBlock.h"

namespace game::interaction {

namespace {

[[noreturn]] void failBlockInteractionDispatcher(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

const BlockInteractionDef& requireInteractionForBlock(const BlockID blockId) {
    if (blockId == RUNTIME_ID_NULL) {
        failBlockInteractionDispatcher("Air cannot declare a block interaction");
    }
    const BlockDef& blockDef = BlockRegistry::getFast(blockId);
    if (blockDef.interaction.empty()) {
        failBlockInteractionDispatcher("Block has no interaction binding: " + blockDef.namespacedId.full());
    }
    return BlockInteractionRegistry::require(blockDef.interaction);
}

uint16_t requirePropertyIndex(const BlockInteractionDef& def) {
    const uint16_t property = BlockStateRegistry::getPropertyNameIndex(def.property);
    if (property == BlockStateRegistry::INVALID_INDEX) {
        failBlockInteractionDispatcher(def.id + " references unknown property: " + def.property);
    }
    return property;
}

uint16_t requirePropertyValue(const BlockInteractionDef& def,
                              const uint16_t property,
                              const std::string& value) {
    const uint16_t valueIndex = BlockStateRegistry::getPropertyValueIndex(property, value);
    if (valueIndex == BlockStateRegistry::INVALID_INDEX) {
        failBlockInteractionDispatcher(def.id + " references unknown value '" + value +
                                 "' for property: " + def.property);
    }
    return valueIndex;
}

uint16_t requireCurrentValue(const BlockStateId currentState,
                             const BlockInteractionDef& def,
                             const uint16_t property) {
    const uint16_t currentValue = BlockStateRegistry::getPropertyIndex(currentState, property);
    if (currentValue == BlockStateRegistry::INVALID_INDEX) {
        failBlockInteractionDispatcher(def.id + " target state is missing property: " + def.property);
    }
    return currentValue;
}

BlockStateId withInteractionProperty(const BlockStateId currentState,
                                const BlockInteractionDef& def,
                                const uint16_t property,
                                const uint16_t value) {
    const BlockStateId updated = BlockStateRegistry::withProperty(currentState, property, value);
    if (BlockStateRegistry::getPropertyIndex(updated, property) != value) {
        failBlockInteractionDispatcher(def.id + " failed to update property: " + def.property);
    }
    return updated;
}

BlockStateId nextToggleBooleanState(const BlockStateId currentState,
                               const BlockInteractionDef& def,
                               const uint16_t property) {
    const uint16_t falseValue = requirePropertyValue(def, property, def.falseValue);
    const uint16_t trueValue = requirePropertyValue(def, property, def.trueValue);
    const uint16_t currentValue = requireCurrentValue(currentState, def, property);

    if (currentValue == falseValue) {
        return withInteractionProperty(currentState, def, property, trueValue);
    }
    if (currentValue == trueValue) {
        return withInteractionProperty(currentState, def, property, falseValue);
    }
    failBlockInteractionDispatcher(def.id + " target state contains an unknown boolean value");
}

BlockStateId nextSetPropertyOnceState(const BlockStateId currentState,
                                 const BlockInteractionDef& def,
                                 const uint16_t property) {
    const uint16_t value = requirePropertyValue(def, property, def.setValue);
    static_cast<void>(requireCurrentValue(currentState, def, property));
    return withInteractionProperty(currentState, def, property, value);
}

BlockStateId nextCyclePropertyState(const BlockStateId currentState,
                               const BlockInteractionDef& def,
                               const uint16_t property) {
    std::vector<uint16_t> values;
    values.reserve(def.cycleValues.size());
    for (const std::string& value : def.cycleValues) {
        values.push_back(requirePropertyValue(def, property, value));
    }

    const uint16_t currentValue = requireCurrentValue(currentState, def, property);
    const auto it = std::find(values.begin(), values.end(), currentValue);
    if (it == values.end()) {
        failBlockInteractionDispatcher(def.id + " target state contains a value outside the configured cycle");
    }

    const auto nextIt = std::next(it) == values.end() ? values.begin() : std::next(it);
    return withInteractionProperty(currentState, def, property, *nextIt);
}

BlockStateId nextStateForDef(const BlockStateId currentState, const BlockInteractionDef& def) {
    const uint16_t property = requirePropertyIndex(def);
    switch (def.action) {
        case BlockInteractionActionKind::ToggleBooleanProperty:
            return nextToggleBooleanState(currentState, def, property);
        case BlockInteractionActionKind::SetPropertyOnce:
            return nextSetPropertyOnceState(currentState, def, property);
        case BlockInteractionActionKind::CycleProperty:
            return nextCyclePropertyState(currentState, def, property);
    }
    failBlockInteractionDispatcher(def.id + " has an unsupported action");
}

void applyPartnerSync(World& world,
                      const glm::ivec3& position,
                      const BlockStateId currentState,
                      const BlockStateId updatedState,
                      const BlockInteractionDef& def) {
    if (def.partnerSync == BlockInteractionPartnerSync::None) {
        if (updatedState != currentState) {
            world.setBlockState(position.x, position.y, position.z, updatedState);
        }
        return;
    }

    if (def.partnerSync == BlockInteractionPartnerSync::DoorOpen) {
        if (!DoorBlockLogic::isDoorState(currentState)) {
            failBlockInteractionDispatcher(def.id + " door partner sync requires a door state");
        }
        const uint16_t property = requirePropertyIndex(def);
        const uint16_t trueValue = requirePropertyValue(def, property, def.trueValue);
        const uint16_t updatedValue = BlockStateRegistry::getPropertyIndex(updatedState, property);
        if (updatedValue == BlockStateRegistry::INVALID_INDEX) {
            failBlockInteractionDispatcher(def.id + " updated door state is missing property: " + def.property);
        }
        DoorBlockLogic::setDoorOpen(world, position, updatedValue == trueValue);
        return;
    }

    failBlockInteractionDispatcher(def.id + " has an unsupported partner sync mode");
}

} // namespace

bool hasBlockInteraction(const BlockID blockId) {
    if (blockId == RUNTIME_ID_NULL) {
        return false;
    }
    const BlockDef& blockDef = BlockRegistry::getFast(blockId);
    if (blockDef.interaction.empty()) {
        return false;
    }
    static_cast<void>(BlockInteractionRegistry::require(blockDef.interaction));
    return true;
}

BlockStateId nextBlockInteractionState(const BlockStateId currentState) {
    const BlockID blockId = BlockStateRegistry::getBlockId(currentState);
    const BlockInteractionDef& def = requireInteractionForBlock(blockId);
    return nextStateForDef(currentState, def);
}

bool applyBlockInteraction(World& world, const glm::ivec3& position) {
    const BlockStateId currentState = world.getBlockState(position.x, position.y, position.z);
    const BlockID blockId = BlockStateRegistry::getBlockId(currentState);
    if (!hasBlockInteraction(blockId)) {
        return false;
    }

    const BlockInteractionDef& def = requireInteractionForBlock(blockId);
    const BlockStateId updatedState = nextStateForDef(currentState, def);
    applyPartnerSync(world, position, currentState, updatedState, def);
    return true;
}

} // namespace game::interaction
