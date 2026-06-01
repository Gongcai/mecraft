#include "BlockStateRegistry.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <sstream>

#include "PropIndices.h"

std::vector<std::string> BlockStateRegistry::s_propertyNamePool{};
std::unordered_map<std::string, uint16_t> BlockStateRegistry::s_propertyNameLookup{};
std::vector<std::vector<std::string>> BlockStateRegistry::s_propertyValuePool{};
std::vector<std::unordered_map<std::string, uint16_t>> BlockStateRegistry::s_propertyValueLookup{};
std::unordered_map<BlockID, BlockStateRegistry::RegisteredBlockProperties> BlockStateRegistry::s_registeredProperties{};
std::vector<BlockStateEntry> BlockStateRegistry::s_states{};
std::vector<PropertyKey> BlockStateRegistry::s_statePropertiesPool{};
std::vector<StateTextureIndices> BlockStateRegistry::s_stateTextures{};
std::unordered_map<BlockID, StateID> BlockStateRegistry::s_defaultState{};
std::unordered_map<uint64_t, StateID> BlockStateRegistry::s_stateLookup{};
std::unordered_map<BlockID, BlockStateRegistry::BlockPropertyLayout> BlockStateRegistry::s_blockPropertyLayouts{};
StateTextureIndices BlockStateRegistry::s_fallbackTextures{};

namespace {
AnimatedTextureRef makeStaticWorldRef(const int layer) {
    AnimatedTextureRef ref;
    ref.firstLayer = layer;
    ref.frameCount = 1;
    ref.fps = 0.0f;
    ref.isAnimated = false;
    return ref;
}

AnimatedTextureRef chooseWaterAnimation(const BlockDef& def, const char* alias) {
    const auto it = def.namedTextureAnimations.find(alias);
    if (it != def.namedTextureAnimations.end()) {
        return it->second.ref;
    }
    return makeStaticWorldRef(0);
}

StateTextureIndices makeTexturesForBlock(const BlockID blockId) {
    const BlockDef& def = BlockRegistry::getFast(blockId);
    StateTextureIndices textures;
    textures.faceTop = def.faceTop;
    textures.faceBottom = def.faceBottom;
    textures.faceLeft = def.faceLeft;
    textures.faceRight = def.faceRight;
    textures.faceFront = def.faceFront;
    textures.faceBack = def.faceBack;
    return textures;
}

StateTextureIndices makeTexturesForState(const BlockID blockId,
                                         const std::vector<PropertyKey>& props) {
    StateTextureIndices textures = makeTexturesForBlock(blockId);
    const BlockDef& def = BlockRegistry::getFast(blockId);

    for (const PropertyKey& prop : props) {
        const std::string& name = BlockStateRegistry::getPropertyName(prop.nameIndex);
        const std::string& value = BlockStateRegistry::getPropertyValue(prop.nameIndex, prop.valueIndex);
        if (name != "axis") {
            continue;
        }

        // Determine end (top/bottom) and side texture references.
        // endRef = faceTop (the "end cap" of a log/column)
        // sideRef prefers faceFront, falls back to faceLeft, then faceBottom
        const AnimatedTextureRef endRef = def.faceTop;
        AnimatedTextureRef sideRef = def.faceFront;
        if (def.faceFront.firstLayer == endRef.firstLayer) {
            sideRef = def.faceLeft;
        }
        if (def.faceFront.firstLayer == endRef.firstLayer && def.faceLeft.firstLayer == endRef.firstLayer) {
            sideRef = def.faceBottom;
        }

        if (value == "x") {
            textures.faceTop = sideRef;
            textures.faceBottom = sideRef;
            textures.faceFront = sideRef;
            textures.faceBack = sideRef;
            textures.faceLeft = endRef;
            textures.faceRight = endRef;
        } else if (value == "z") {
            textures.faceTop = sideRef;
            textures.faceBottom = sideRef;
            textures.faceLeft = sideRef;
            textures.faceRight = sideRef;
            textures.faceFront = endRef;
            textures.faceBack = endRef;
        } else {
            // y-axis: keep original orientation
            textures.faceTop = def.faceTop;
            textures.faceBottom = def.faceBottom;
            textures.faceLeft = def.faceLeft;
            textures.faceRight = def.faceRight;
            textures.faceFront = def.faceFront;
            textures.faceBack = def.faceBack;
        }
        break;
    }

    if (def.namespacedId == NamespacedId("minecraft", "water")) {
        bool isSource = true;
        bool isFalling = false;

        for (const PropertyKey& prop : props) {
            const std::string& name = BlockStateRegistry::getPropertyName(prop.nameIndex);
            const std::string& value = BlockStateRegistry::getPropertyValue(prop.nameIndex, prop.valueIndex);
            if (name == "level") {
                isSource = (value == "0");
            } else if (name == "falling") {
                isFalling = (value == "true");
            }
        }

        const AnimatedTextureRef still = chooseWaterAnimation(def, "still");

        static_cast<void>(isSource);
        static_cast<void>(isFalling);
        textures.faceTop = still;
        textures.faceBottom = still;
        textures.faceLeft = still;
        textures.faceRight = still;
        textures.faceFront = still;
        textures.faceBack = still;
    }

    return textures;
}
}

uint16_t BlockStateRegistry::internPropertyName(const std::string& name) {
    const auto it = s_propertyNameLookup.find(name);
    if (it != s_propertyNameLookup.end()) {
        return it->second;
    }

    const uint16_t index = static_cast<uint16_t>(s_propertyNamePool.size());
    s_propertyNamePool.push_back(name);
    s_propertyNameLookup[name] = index;
    s_propertyValuePool.emplace_back();
    s_propertyValueLookup.emplace_back();
    return index;
}

uint16_t BlockStateRegistry::internPropertyValue(const uint16_t nameIndex, const std::string& value) {
    if (nameIndex >= s_propertyValueLookup.size()) {
        return INVALID_INDEX;
    }

    auto& lookup = s_propertyValueLookup[nameIndex];
    const auto it = lookup.find(value);
    if (it != lookup.end()) {
        return it->second;
    }

    auto& pool = s_propertyValuePool[nameIndex];
    const uint16_t index = static_cast<uint16_t>(pool.size());
    pool.push_back(value);
    lookup[value] = index;
    return index;
}

void BlockStateRegistry::registerBlockProperties(
    const BlockID blockId,
    std::vector<std::pair<std::string, std::vector<std::string>>> properties,
    std::map<std::string, std::string> defaultState) {
    RegisteredBlockProperties registered;
    registered.propertyNameIndices.reserve(properties.size());
    registered.propertyValueIndices.reserve(properties.size());
    registered.defaultValueIndices.reserve(properties.size());

    for (auto& [name, values] : properties) {
        if (values.empty()) {
            continue;
        }

        const uint16_t nameIndex = internPropertyName(name);
        std::vector<uint16_t> valueIndices;
        valueIndices.reserve(values.size());
        for (const std::string& value : values) {
            valueIndices.push_back(internPropertyValue(nameIndex, value));
        }

        if (valueIndices.empty()) {
            continue;
        }

        uint16_t defaultValueIndex = valueIndices.front();
        const auto defaultIt = defaultState.find(name);
        if (defaultIt != defaultState.end()) {
            const uint16_t candidate = internPropertyValue(nameIndex, defaultIt->second);
            if (std::find(valueIndices.begin(), valueIndices.end(), candidate) != valueIndices.end()) {
                defaultValueIndex = candidate;
            }
        }

        registered.propertyNameIndices.push_back(nameIndex);
        registered.propertyValueIndices.push_back(std::move(valueIndices));
        registered.defaultValueIndices.push_back(defaultValueIndex);
    }

    if (registered.propertyNameIndices.empty()) {
        s_registeredProperties.erase(blockId);
        return;
    }

    s_registeredProperties[blockId] = std::move(registered);
}

void BlockStateRegistry::clearHotData() {
    s_states.clear();
    s_statePropertiesPool.clear();
    s_stateTextures.clear();
    s_defaultState.clear();
    s_stateLookup.clear();
    s_blockPropertyLayouts.clear();
}

uint64_t BlockStateRegistry::computeStateKey(const BlockID blockId, const std::vector<PropertyKey>& props) {
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&hash](const uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };

    mix(blockId);
    for (const PropertyKey& prop : props) {
        mix((static_cast<uint64_t>(prop.nameIndex) << 16u) | static_cast<uint64_t>(prop.valueIndex));
    }
    return hash;
}

void BlockStateRegistry::explodeAllStates() {
    clearHotData();

    const size_t blockCount = BlockRegistry::getBlockCount();
    s_states.resize(blockCount);
    s_stateTextures.reserve(blockCount);

    for (BlockID blockId = 0; blockId < blockCount; ++blockId) {
        const uint16_t textureOffset = static_cast<uint16_t>(s_stateTextures.size());
        s_stateTextures.push_back(makeTexturesForBlock(blockId));
        s_states[blockId] = BlockStateEntry{
            blockId,
            blockId,
            0,
            0,
            textureOffset
        };
    }

    StateID nextStateId = static_cast<StateID>(blockCount);

    for (BlockID blockId = 0; blockId < blockCount; ++blockId) {
        const auto registeredIt = s_registeredProperties.find(blockId);
        if (registeredIt == s_registeredProperties.end() || registeredIt->second.propertyNameIndices.empty()) {
            s_defaultState[blockId] = blockId;
            continue;
        }

        const RegisteredBlockProperties& registered = registeredIt->second;
        BlockPropertyLayout layout;
        layout.firstStateId = nextStateId;
        layout.propertyCount = static_cast<uint8_t>(registered.propertyNameIndices.size());
        layout.propertyPosition.assign(s_propertyNamePool.size(), std::numeric_limits<uint8_t>::max());
        layout.propertyStride.assign(s_propertyNamePool.size(), 0);
        layout.valueCounts.assign(s_propertyNamePool.size(), 0);
        layout.valueOrdinals.resize(registered.propertyNameIndices.size());

        uint16_t runningStride = 1;
        for (int propertyIndex = static_cast<int>(registered.propertyNameIndices.size()) - 1;
             propertyIndex >= 0;
             --propertyIndex) {
            const uint16_t nameIndex = registered.propertyNameIndices[propertyIndex];
            const auto& values = registered.propertyValueIndices[propertyIndex];
            layout.propertyPosition[nameIndex] = static_cast<uint8_t>(propertyIndex);
            layout.propertyStride[nameIndex] = runningStride;
            layout.valueCounts[nameIndex] = static_cast<uint16_t>(values.size());
            layout.valueOrdinals[propertyIndex].assign(
                s_propertyValuePool[nameIndex].size(),
                INVALID_INDEX);
            for (uint16_t ordinal = 0; ordinal < values.size(); ++ordinal) {
                const uint16_t valueIndex = values[ordinal];
                if (valueIndex < layout.valueOrdinals[propertyIndex].size()) {
                    layout.valueOrdinals[propertyIndex][valueIndex] = ordinal;
                }
            }
            runningStride = static_cast<uint16_t>(runningStride * values.size());
        }

        std::vector<PropertyKey> props(registered.propertyNameIndices.size());
        std::function<void(size_t, bool)> buildStates = [&](const size_t propertyIndex, const bool isDefaultPath) {
            if (propertyIndex == registered.propertyNameIndices.size()) {
                const uint16_t propertiesOffset = static_cast<uint16_t>(s_statePropertiesPool.size());
                s_statePropertiesPool.insert(s_statePropertiesPool.end(), props.begin(), props.end());

                const uint16_t textureOffset = static_cast<uint16_t>(s_stateTextures.size());
                s_stateTextures.push_back(makeTexturesForState(blockId, props));

                const StateID stateId = nextStateId++;
                s_states.push_back(BlockStateEntry{
                    stateId,
                    blockId,
                    static_cast<uint8_t>(props.size()),
                    propertiesOffset,
                    textureOffset
                });
                s_stateLookup[computeStateKey(blockId, props)] = stateId;
                if (isDefaultPath) {
                    s_defaultState[blockId] = stateId;
                }
                return;
            }

            const uint16_t nameIndex = registered.propertyNameIndices[propertyIndex];
            const auto& values = registered.propertyValueIndices[propertyIndex];
            for (const uint16_t valueIndex : values) {
                props[propertyIndex] = PropertyKey{nameIndex, valueIndex};
                buildStates(propertyIndex + 1,
                            isDefaultPath && valueIndex == registered.defaultValueIndices[propertyIndex]);
            }
        };

        buildStates(0, true);
        if (s_defaultState.find(blockId) == s_defaultState.end()) {
            s_defaultState[blockId] = blockId;
        }
        s_blockPropertyLayouts[blockId] = std::move(layout);
    }
}

StateID BlockStateRegistry::getDefaultState(const BlockID blockId) {
    const auto it = s_defaultState.find(blockId);
    if (it != s_defaultState.end()) {
        return it->second;
    }
    return blockId;
}

StateID BlockStateRegistry::getState(const BlockID blockId, const uint16_t propKey, const uint16_t propValue) {
    return getState(blockId, {{propKey, propValue}});
}

StateID BlockStateRegistry::getState(const BlockID blockId,
                                     std::initializer_list<std::pair<uint16_t, uint16_t>> props) {
    return getState(blockId, std::vector<std::pair<uint16_t, uint16_t>>(props.begin(), props.end()));
}

StateID BlockStateRegistry::getState(const BlockID blockId,
                                     const std::vector<std::pair<uint16_t, uint16_t>>& props) {
    StateID state = getDefaultState(blockId);
    for (const auto& [propKey, propValue] : props) {
        state = withProperty(state, propKey, propValue);
    }
    return state;
}

BlockID BlockStateRegistry::getBlockId(const StateID stateId) {
    if (stateId < s_states.size()) {
        return s_states[stateId].blockId;
    }
    return BlockIds::AIR;
}

uint16_t BlockStateRegistry::getPropertyIndex(const StateID stateId, const uint16_t nameIndex) {
    if (stateId >= s_states.size()) {
        return INVALID_INDEX;
    }

    const BlockStateEntry& entry = s_states[stateId];
    for (uint8_t propertyIndex = 0; propertyIndex < entry.propertyCount; ++propertyIndex) {
        const PropertyKey& prop = s_statePropertiesPool[entry.propertiesOffset + propertyIndex];
        if (prop.nameIndex == nameIndex) {
            return prop.valueIndex;
        }
    }

    return INVALID_INDEX;
}

uint8_t BlockStateRegistry::getPropertyCount(const StateID stateId) {
    if (stateId < s_states.size()) {
        return s_states[stateId].propertyCount;
    }
    return 0;
}

StateID BlockStateRegistry::withProperty(const StateID currentState,
                                         const uint16_t propKey,
                                         const uint16_t newValue) {
    if (propKey == INVALID_INDEX || currentState >= s_states.size()) {
        return currentState;
    }

    const BlockID blockId = s_states[currentState].blockId;
    const auto layoutIt = s_blockPropertyLayouts.find(blockId);
    if (layoutIt == s_blockPropertyLayouts.end()) {
        return currentState;
    }

    const BlockPropertyLayout& layout = layoutIt->second;
    if (propKey >= layout.propertyPosition.size()) {
        return currentState;
    }

    const uint8_t propertyPosition = layout.propertyPosition[propKey];
    if (propertyPosition == std::numeric_limits<uint8_t>::max() || propertyPosition >= layout.valueOrdinals.size()) {
        return currentState;
    }

    if (newValue >= layout.valueOrdinals[propertyPosition].size()) {
        return currentState;
    }

    const uint16_t newOrdinal = layout.valueOrdinals[propertyPosition][newValue];
    if (newOrdinal == INVALID_INDEX) {
        return currentState;
    }

    const uint16_t currentValue = getPropertyIndex(currentState, propKey);
    if (currentValue == INVALID_INDEX ||
        currentValue >= layout.valueOrdinals[propertyPosition].size()) {
        return currentState;
    }

    const uint16_t currentOrdinal = layout.valueOrdinals[propertyPosition][currentValue];
    if (currentOrdinal == INVALID_INDEX) {
        return currentState;
    }

    const int delta = static_cast<int>(newOrdinal) - static_cast<int>(currentOrdinal);
    if (delta == 0) {
        return currentState;
    }

    const int nextState = static_cast<int>(currentState) +
                          delta * static_cast<int>(layout.propertyStride[propKey]);
    if (nextState < 0 || nextState >= static_cast<int>(s_states.size())) {
        return currentState;
    }
    return static_cast<StateID>(nextState);
}

StateID BlockStateRegistry::withProperty(const StateID currentState,
                                         const uint16_t propKey,
                                         const std::string& newValue) {
    return withProperty(currentState, propKey, getPropertyValueIndex(propKey, newValue));
}

const StateTextureIndices& BlockStateRegistry::getStateTextures(const StateID stateId) {
    if (stateId < s_states.size()) {
        const BlockStateEntry& entry = s_states[stateId];
        if (entry.textureOffset < s_stateTextures.size()) {
            return s_stateTextures[entry.textureOffset];
        }
    }
    return s_fallbackTextures;
}

uint16_t BlockStateRegistry::getPropertyNameIndex(const std::string& name) {
    const auto it = s_propertyNameLookup.find(name);
    return (it != s_propertyNameLookup.end()) ? it->second : INVALID_INDEX;
}

uint16_t BlockStateRegistry::getPropertyValueIndex(const uint16_t nameIndex, const std::string& value) {
    if (nameIndex == INVALID_INDEX || nameIndex >= s_propertyValueLookup.size()) {
        return INVALID_INDEX;
    }

    const auto& lookup = s_propertyValueLookup[nameIndex];
    const auto it = lookup.find(value);
    return (it != lookup.end()) ? it->second : INVALID_INDEX;
}

const std::string& BlockStateRegistry::getPropertyName(const uint16_t nameIndex) {
    static const std::string empty;
    if (nameIndex < s_propertyNamePool.size()) {
        return s_propertyNamePool[nameIndex];
    }
    return empty;
}

const std::string& BlockStateRegistry::getPropertyValue(const uint16_t nameIndex, const uint16_t valueIndex) {
    static const std::string empty;
    if (nameIndex < s_propertyValuePool.size() && valueIndex < s_propertyValuePool[nameIndex].size()) {
        return s_propertyValuePool[nameIndex][valueIndex];
    }
    return empty;
}

std::string BlockStateRegistry::stateToString(const StateID stateId) {
    if (stateId >= s_states.size()) {
        return BlockRegistry::getNamespacedId(BlockIds::AIR).full();
    }

    const BlockStateEntry& entry = s_states[stateId];
    std::ostringstream out;
    out << BlockRegistry::getNamespacedId(entry.blockId).full();
    if (entry.propertyCount == 0) {
        return out.str();
    }

    out << '[';
    for (uint8_t propertyIndex = 0; propertyIndex < entry.propertyCount; ++propertyIndex) {
        if (propertyIndex > 0) {
            out << ", ";
        }
        const PropertyKey& prop = s_statePropertiesPool[entry.propertiesOffset + propertyIndex];
        out << getPropertyName(prop.nameIndex) << '=' << getPropertyValue(prop.nameIndex, prop.valueIndex);
    }
    out << ']';
    return out.str();
}

size_t BlockStateRegistry::getStateCount() {
    return s_states.size();
}
