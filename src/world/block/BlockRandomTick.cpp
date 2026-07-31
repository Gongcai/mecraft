#include "BlockRandomTick.h"

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

#include "../World.h"
#include "BlockStateRegistry.h"
#include "FarmlandRules.h"

namespace {

[[noreturn]] void failBlockRandomTick(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

bool chancePassed(const BlockRandomTickRule& rule, const uint32_t randomBits) {
    constexpr float kUnitScale = 1.0f / 16777216.0f;
    const float sample = static_cast<float>(randomBits & 0x00FFFFFFu) * kUnitScale;
    return sample < rule.chance;
}

bool parseDecimalInt(const std::string& value, int& outValue) {
    const char* begin = value.data();
    const char* end = begin + value.size();
    const std::from_chars_result result = std::from_chars(begin, end, outValue);
    return result.ec == std::errc{} && result.ptr == end;
}

bool incrementNumericProperty(const BlockRandomTickRule& rule, const BlockRandomTickContext& ctx) {
    if (rule.propertyName.empty()) {
        failBlockRandomTick("increment_property random tick requires a property name");
    }
    if (!chancePassed(rule, ctx.randomBits)) {
        return false;
    }

    const uint16_t property = BlockStateRegistry::getPropertyNameIndex(rule.propertyName);
    if (property == BlockStateRegistry::INVALID_INDEX) {
        failBlockRandomTick("increment_property random tick references an unknown property: " + rule.propertyName);
    }

    const uint16_t currentValueIndex = BlockStateRegistry::getPropertyIndex(ctx.state, property);
    if (currentValueIndex == BlockStateRegistry::INVALID_INDEX) {
        failBlockRandomTick("increment_property random tick state does not contain property: " + rule.propertyName);
    }

    int currentValue = 0;
    if (!parseDecimalInt(BlockStateRegistry::getPropertyValue(property, currentValueIndex), currentValue)) {
        failBlockRandomTick("increment_property random tick requires decimal property values: " + rule.propertyName);
    }

    const uint16_t nextValueIndex =
        BlockStateRegistry::getPropertyValueIndex(property, std::to_string(currentValue + 1));
    if (nextValueIndex == BlockStateRegistry::INVALID_INDEX) {
        return false;
    }

    const BlockStateId nextState = BlockStateRegistry::withProperty(ctx.state, property, nextValueIndex);
    if (nextState == ctx.state) {
        return false;
    }

    ctx.world.setBlockState(ctx.pos.x, ctx.pos.y, ctx.pos.z, nextState);
    return true;
}

bool updateFarmlandMoisture(const BlockRandomTickRule& rule, const BlockRandomTickContext& ctx) {
    if (!chancePassed(rule, ctx.randomBits)) {
        return false;
    }

    const uint16_t moisture = BlockStateRegistry::getPropertyNameIndex("moisture");
    if (moisture == BlockStateRegistry::INVALID_INDEX) {
        failBlockRandomTick("farmland_moisture random tick requires a moisture property");
    }

    const uint16_t currentValueIndex = BlockStateRegistry::getPropertyIndex(ctx.state, moisture);
    if (currentValueIndex == BlockStateRegistry::INVALID_INDEX) {
        failBlockRandomTick("farmland_moisture random tick state does not contain moisture");
    }

    if (FarmlandRules::hasHydrationWater(ctx.world, ctx.pos)) {
        const uint16_t moistValue = BlockStateRegistry::getPropertyValueIndex(moisture, "7");
        if (moistValue == BlockStateRegistry::INVALID_INDEX) {
            failBlockRandomTick("farmland_moisture random tick requires moisture value 7");
        }
        const BlockStateId moistState = BlockStateRegistry::withProperty(ctx.state, moisture, moistValue);
        if (moistState != ctx.state) {
            ctx.world.setBlockState(ctx.pos.x, ctx.pos.y, ctx.pos.z, moistState);
            return true;
        }
        return false;
    }

    int currentMoisture = 0;
    if (!parseDecimalInt(BlockStateRegistry::getPropertyValue(moisture, currentValueIndex), currentMoisture)) {
        failBlockRandomTick("farmland_moisture random tick requires decimal moisture values");
    }

    if (currentMoisture > 0) {
        const uint16_t nextValueIndex =
            BlockStateRegistry::getPropertyValueIndex(moisture, std::to_string(currentMoisture - 1));
        if (nextValueIndex == BlockStateRegistry::INVALID_INDEX) {
            failBlockRandomTick("farmland_moisture random tick could not resolve the next moisture value");
        }

        const BlockStateId dryState = BlockStateRegistry::withProperty(ctx.state, moisture, nextValueIndex);
        if (dryState != ctx.state) {
            ctx.world.setBlockState(ctx.pos.x, ctx.pos.y, ctx.pos.z, dryState);
            return true;
        }
        return false;
    }

    if (FarmlandRules::hasCropAbove(ctx.world, ctx.pos)) {
        return false;
    }

    const BlockID dirtBlock = BlockRegistry::requireIdByName("minecraft:dirt");
    ctx.world.setBlockState(ctx.pos.x, ctx.pos.y, ctx.pos.z, BlockStateRegistry::getDefaultState(dirtBlock));
    return true;
}

} // namespace

namespace BlockRandomTick {

bool dispatch(const BlockRandomTickRule& rule, const BlockRandomTickContext& ctx) {
    if (!rule.enabled) {
        return false;
    }

    const std::string_view behavior(rule.behavior);
    if (behavior == "increment_property") {
        return incrementNumericProperty(rule, ctx);
    }
    if (behavior == "farmland_moisture") {
        return updateFarmlandMoisture(rule, ctx);
    }
    failBlockRandomTick("Unknown block random tick behavior: " + rule.behavior);
}

} // namespace BlockRandomTick
