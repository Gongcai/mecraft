#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Block.h"
#include "BlockModel.h"

// Opaque handle into the global BlockState table.
// The registry owns this index space; callers should treat the stored value as
// an implementation detail of the current registry encoding.
struct BlockStateId {
    using Index = size_t;

    constexpr BlockStateId() = default;
    [[nodiscard]] static constexpr BlockStateId fromRegistryIndex(const Index index) { return BlockStateId(index); }

    [[nodiscard]] constexpr Index registryIndex() const { return m_index; }
    [[nodiscard]] constexpr bool operator==(const BlockStateId& other) const { return m_index == other.m_index; }
    [[nodiscard]] constexpr bool operator!=(const BlockStateId& other) const { return m_index != other.m_index; }
    [[nodiscard]] constexpr bool operator<(const BlockStateId& other) const { return m_index < other.m_index; }

    constexpr BlockStateId& operator++() {
        ++m_index;
        return *this;
    }

    constexpr BlockStateId operator++(int) {
        const BlockStateId previous = *this;
        ++(*this);
        return previous;
    }

private:
    explicit constexpr BlockStateId(const Index index) : m_index(index) {}

    Index m_index = 0;
};

namespace std {
template <> struct hash<BlockStateId> {
    size_t operator()(const BlockStateId& stateId) const noexcept {
        return hash<BlockStateId::Index>{}(stateId.registryIndex());
    }
};
} // namespace std

constexpr BlockStateId NULL_BLOCK_STATE = BlockStateId::fromRegistryIndex(0);

struct PropertyKey {
    uint16_t nameIndex = 0;
    uint16_t valueIndex = 0;
};

struct StateTextureIndices {
    AnimatedTextureRef faceTop;
    AnimatedTextureRef faceBottom;
    AnimatedTextureRef faceLeft;
    AnimatedTextureRef faceRight;
    AnimatedTextureRef faceFront;
    AnimatedTextureRef faceBack;

    // Convenience: return the TextureArray first layer for a given face (0=top,1=bottom,2=front,3=back,4=left,5=right)
    [[nodiscard]] int getFaceLayer(int face) const {
        switch (face) {
        case 0: return faceTop.firstLayer;
        case 1: return faceBottom.firstLayer;
        case 2: return faceFront.firstLayer;
        case 3: return faceBack.firstLayer;
        case 4: return faceLeft.firstLayer;
        case 5: return faceRight.firstLayer;
        default: return faceTop.firstLayer;
        }
    }
};

struct BlockStateEntry {
    BlockStateId stateId{};
    BlockID blockId = 0;
    uint8_t propertyCount = 0;
    size_t propertiesOffset = 0;
    size_t textureOffset = 0;
};

class BlockStateRegistry {
public:
    static constexpr uint16_t INVALID_INDEX = UINT16_MAX;

    static void registerBlockProperties(BlockID blockId,
                                        std::vector<std::pair<std::string, std::vector<std::string>>> properties,
                                        std::map<std::string, std::string> defaultState);

    static void explodeAllStates();

    static BlockStateId getDefaultState(BlockID blockId);
    static BlockStateId getState(BlockID blockId, uint16_t propKey, uint16_t propValue);
    static BlockStateId getState(BlockID blockId, std::initializer_list<std::pair<uint16_t, uint16_t>> props);
    static BlockStateId getState(BlockID blockId, const std::vector<std::pair<uint16_t, uint16_t>>& props);
    static BlockID getBlockId(BlockStateId stateId);
    static uint16_t getPropertyIndex(BlockStateId stateId, uint16_t nameIndex);
    static uint8_t getPropertyCount(BlockStateId stateId);
    static BlockStateId withProperty(BlockStateId currentState, uint16_t propKey, uint16_t newValue);
    static BlockStateId withProperty(BlockStateId currentState, uint16_t propKey, const std::string& newValue);
    static const StateTextureIndices& getStateTextures(BlockStateId stateId);
    static void registerBlockModelVariants(BlockID blockId, const nlohmann::json& variantsJson);
    static const ModelVariant* getModelVariant(BlockStateId stateId);

    static uint16_t getPropertyNameIndex(const std::string& name);
    static uint16_t getPropertyValueIndex(uint16_t nameIndex, const std::string& value);
    static const std::string& getPropertyName(uint16_t nameIndex);
    static const std::string& getPropertyValue(uint16_t nameIndex, uint16_t valueIndex);
    static std::string stateToString(BlockStateId stateId);
    static size_t getStateCount();
    static std::vector<BlockStateId> getStatesForBlock(BlockID blockId);

private:
    struct RegisteredBlockProperties {
        std::vector<uint16_t> propertyNameIndices;
        std::vector<std::vector<uint16_t>> propertyValueIndices;
        std::vector<uint16_t> defaultValueIndices;
    };

    struct BlockPropertyLayout {
        BlockStateId firstStateId{};
        uint8_t propertyCount = 0;
        std::vector<uint8_t> propertyPosition;
        std::vector<size_t> propertyStride;
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
    static std::vector<ModelVariant> s_stateModelVariants;
    static std::unordered_map<BlockID, BlockStateId> s_defaultState;
    static std::unordered_map<uint64_t, BlockStateId> s_stateLookup;
    static std::unordered_map<BlockID, BlockPropertyLayout> s_blockPropertyLayouts;
    static StateTextureIndices s_fallbackTextures;

    static void clearHotData();
    static uint64_t computeStateKey(BlockID blockId, const std::vector<PropertyKey>& props);
    static uint16_t internPropertyName(const std::string& name);
    static uint16_t internPropertyValue(uint16_t nameIndex, const std::string& value);
};
