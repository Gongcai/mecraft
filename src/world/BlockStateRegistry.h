#pragma once

#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Block.h"

using StateID = uint16_t;

struct PropertyKey {
    uint16_t nameIndex = 0;
    uint16_t valueIndex = 0;
};

struct StateTextureIndices {
    int texTop = 0;
    int texBottom = 0;
    int texLeft = 0;
    int texRight = 0;
    int texFront = 0;
    int texBack = 0;
};

struct BlockStateEntry {
    StateID stateId = 0;
    BlockID blockId = 0;
    uint8_t propertyCount = 0;
    uint16_t propertiesOffset = 0;
    uint16_t textureOffset = 0;
};

class BlockStateRegistry {
public:
    static constexpr uint16_t INVALID_INDEX = UINT16_MAX;

    static void registerBlockProperties(
        BlockID blockId,
        std::vector<std::pair<std::string, std::vector<std::string>>> properties,
        std::map<std::string, std::string> defaultState);

    static void explodeAllStates();

    static StateID getDefaultState(BlockID blockId);
    static StateID getState(BlockID blockId, uint16_t propKey, uint16_t propValue);
    static StateID getState(BlockID blockId,
                            std::initializer_list<std::pair<uint16_t, uint16_t>> props);
    static StateID getState(BlockID blockId,
                            const std::vector<std::pair<uint16_t, uint16_t>>& props);
    static BlockID getBlockId(StateID stateId);
    static uint16_t getPropertyIndex(StateID stateId, uint16_t nameIndex);
    static uint8_t getPropertyCount(StateID stateId);
    static StateID withProperty(StateID currentState, uint16_t propKey, uint16_t newValue);
    static StateID withProperty(StateID currentState, uint16_t propKey, const std::string& newValue);
    static const StateTextureIndices& getStateTextures(StateID stateId);

    static uint16_t getPropertyNameIndex(const std::string& name);
    static uint16_t getPropertyValueIndex(uint16_t nameIndex, const std::string& value);
    static const std::string& getPropertyName(uint16_t nameIndex);
    static const std::string& getPropertyValue(uint16_t nameIndex, uint16_t valueIndex);
    static std::string stateToString(StateID stateId);
    static size_t getStateCount();

private:
    struct RegisteredBlockProperties {
        std::vector<uint16_t> propertyNameIndices;
        std::vector<std::vector<uint16_t>> propertyValueIndices;
        std::vector<uint16_t> defaultValueIndices;
    };

    struct BlockPropertyLayout {
        StateID firstStateId = 0;
        uint8_t propertyCount = 0;
        std::vector<uint8_t> propertyPosition;
        std::vector<uint16_t> propertyStride;
        std::vector<uint16_t> valueCounts;
        std::vector<std::vector<uint16_t>> valueOrdinals;
    };

    static std::vector<std::string> s_propertyNamePool;
    static std::unordered_map<std::string, uint16_t> s_propertyNameLookup;
    static std::vector<std::vector<std::string>> s_propertyValuePool;
    static std::vector<std::unordered_map<std::string, uint16_t>> s_propertyValueLookup;

    static std::unordered_map<BlockID, RegisteredBlockProperties> s_registeredProperties;

    static std::vector<BlockStateEntry> s_states;
    static std::vector<PropertyKey> s_statePropertiesPool;
    static std::vector<StateTextureIndices> s_stateTextures;
    static std::unordered_map<BlockID, StateID> s_defaultState;
    static std::unordered_map<uint64_t, StateID> s_stateLookup;
    static std::unordered_map<BlockID, BlockPropertyLayout> s_blockPropertyLayouts;
    static StateTextureIndices s_fallbackTextures;

    static void clearHotData();
    static uint64_t computeStateKey(BlockID blockId, const std::vector<PropertyKey>& props);
    static uint16_t internPropertyName(const std::string& name);
    static uint16_t internPropertyValue(uint16_t nameIndex, const std::string& value);
};
