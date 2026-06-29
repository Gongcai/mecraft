#include "BlockStateRegistry.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "AttachmentFaceGeometry.h"
#include "BlockModelRegistry.h"
#include "PropIndices.h"

std::vector<std::string> BlockStateRegistry::s_propertyNamePool{};
std::unordered_map<std::string, uint16_t> BlockStateRegistry::s_propertyNameLookup{};
std::vector<std::vector<std::string>> BlockStateRegistry::s_propertyValuePool{};
std::vector<std::unordered_map<std::string, uint16_t>> BlockStateRegistry::s_propertyValueLookup{};
std::unordered_map<BlockID, BlockStateRegistry::RegisteredBlockProperties> BlockStateRegistry::s_registeredProperties{};
std::vector<BlockStateEntry> BlockStateRegistry::s_states{};
std::vector<PropertyKey> BlockStateRegistry::s_statePropertiesPool{};
std::vector<StateTextureIndices> BlockStateRegistry::s_stateTextures{};
std::vector<ModelVariant> BlockStateRegistry::s_stateModelVariants{};
std::unordered_map<BlockID, BlockStateId> BlockStateRegistry::s_defaultState{};
std::unordered_map<uint64_t, BlockStateId> BlockStateRegistry::s_stateLookup{};
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

void applyTextureFaces(StateTextureIndices& textures, const BlockTextureFaces& faces) {
    textures.faceTop = faces.faceTop;
    textures.faceBottom = faces.faceBottom;
    textures.faceLeft = faces.faceLeft;
    textures.faceRight = faces.faceRight;
    textures.faceFront = faces.faceFront;
    textures.faceBack = faces.faceBack;
}

void applyStateTextureRules(StateTextureIndices& textures,
                            const BlockDef& def,
                            const std::vector<PropertyKey>& props) {
    if (def.stateTextureRules.empty()) {
        return;
    }

    for (const PropertyKey& prop : props) {
        const std::string& name = BlockStateRegistry::getPropertyName(prop.nameIndex);
        const std::string& value = BlockStateRegistry::getPropertyValue(prop.nameIndex, prop.valueIndex);
        for (const StateTextureRule& rule : def.stateTextureRules) {
            if (rule.propertyName != name) {
                continue;
            }
            const auto textureIt = rule.texturesByValue.find(value);
            if (textureIt != rule.texturesByValue.end()) {
                applyTextureFaces(textures, textureIt->second);
            }
        }
    }
}

void applyFurnaceFacingTextures(StateTextureIndices& textures,
                                const BlockDef& def,
                                const std::vector<PropertyKey>& props) {
    if (def.namespacedId != NamespacedId("minecraft", "furnace")) {
        return;
    }

    uint16_t facingValue = BlockStateRegistry::INVALID_INDEX;
    for (const PropertyKey& prop : props) {
        const std::string& name = BlockStateRegistry::getPropertyName(prop.nameIndex);
        if (name == "facing") {
            facingValue = prop.valueIndex;
            break;
        }
    }

    if (facingValue == BlockStateRegistry::INVALID_INDEX) {
        return;
    }

    const AnimatedTextureRef sideRef = def.faceLeft;
    textures.faceLeft = sideRef;
    textures.faceRight = sideRef;
    textures.faceFront = sideRef;
    textures.faceBack = sideRef;

    const std::string& value = BlockStateRegistry::getPropertyValue(
        BlockStateRegistry::getPropertyNameIndex("facing"),
        facingValue);
    if (value == "north") {
        textures.faceBack = def.faceFront;
    } else if (value == "south") {
        textures.faceFront = def.faceFront;
    } else if (value == "east") {
        textures.faceRight = def.faceFront;
    } else if (value == "west") {
        textures.faceLeft = def.faceFront;
    } else {
        throw std::runtime_error("Furnace facing state requires a horizontal value");
    }
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

    applyFurnaceFacingTextures(textures, def, props);
    applyStateTextureRules(textures, def, props);

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

std::string trimCopy(const std::string& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](const unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](const unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

ModelTransform parseModelTransform(const nlohmann::json& variantJson) {
    ModelTransform transform;
    const auto transformIt = variantJson.find("transform");
    if (transformIt == variantJson.end()) {
        return transform;
    }
    if (!transformIt->is_object()) {
        throw std::runtime_error("Model variant transform must be an object");
    }

    const auto readRotation = [&](const char* key) -> uint16_t {
        const auto it = transformIt->find(key);
        if (it == transformIt->end()) {
            return 0;
        }
        if (!it->is_number_integer()) {
            throw std::runtime_error(std::string("Model variant rotation must be integer: ") + key);
        }
        const int rotation = it->get<int>();
        if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
            throw std::runtime_error(std::string("Model variant rotation must be 0, 90, 180, or 270: ") + key);
        }
        return static_cast<uint16_t>(rotation);
    };

    transform.rotX = readRotation("rotX");
    transform.rotY = readRotation("rotY");
    transform.rotZ = readRotation("rotZ");
    const auto uvLockIt = variantJson.find("uvLock");
    if (uvLockIt != variantJson.end()) {
        if (!uvLockIt->is_boolean()) {
            throw std::runtime_error("Model variant uvLock must be boolean");
        }
        transform.uvLock = uvLockIt->get<bool>();
    }
    return transform;
}

std::vector<std::pair<uint16_t, uint16_t>> parseModelVariantPropertyKey(const std::string& key) {
    const std::string trimmedKey = trimCopy(key);
    if (trimmedKey.empty()) {
        return {};
    }

    std::vector<std::pair<uint16_t, uint16_t>> props;
    std::istringstream input(trimmedKey);
    std::string segment;
    while (std::getline(input, segment, ',')) {
        const std::string trimmedSegment = trimCopy(segment);
        const size_t equals = trimmedSegment.find('=');
        if (equals == std::string::npos) {
            throw std::runtime_error("Model variant state key segment is missing '=': " + trimmedSegment);
        }

        const std::string propName = trimCopy(trimmedSegment.substr(0, equals));
        const std::string propValue = trimCopy(trimmedSegment.substr(equals + 1));
        const uint16_t nameIndex = BlockStateRegistry::getPropertyNameIndex(propName);
        if (nameIndex == BlockStateRegistry::INVALID_INDEX) {
            throw std::runtime_error("Unknown model variant property name: " + propName);
        }
        const uint16_t valueIndex = BlockStateRegistry::getPropertyValueIndex(nameIndex, propValue);
        if (valueIndex == BlockStateRegistry::INVALID_INDEX) {
            throw std::runtime_error("Unknown model variant property value: " + propName + "=" + propValue);
        }
        props.emplace_back(nameIndex, valueIndex);
    }
    return props;
}

bool stateMatchesModelVariantProperties(const BlockStateId stateId,
                                        const std::vector<std::pair<uint16_t, uint16_t>>& props) {
    for (const auto& [property, value] : props) {
        if (BlockStateRegistry::getPropertyIndex(stateId, property) != value) {
            return false;
        }
    }
    return true;
}

BlockStateId makeBlockStateId(const size_t value) {
    return BlockStateId::fromRegistryIndex(value);
}

size_t stateIndex(const BlockStateId stateId) {
    return stateId.registryIndex();
}

glm::ivec3 rotateDirectionX90(const glm::ivec3 direction, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {direction.x, -direction.z, direction.y};
        case 2: return {direction.x, -direction.y, -direction.z};
        case 3: return {direction.x, direction.z, -direction.y};
        case 0:
        default: return direction;
    }
}

glm::ivec3 rotateDirectionY90(const glm::ivec3 direction, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {-direction.z, direction.y, direction.x};
        case 2: return {-direction.x, direction.y, -direction.z};
        case 3: return {direction.z, direction.y, -direction.x};
        case 0:
        default: return direction;
    }
}

glm::ivec3 rotateDirectionZ90(const glm::ivec3 direction, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {-direction.y, direction.x, direction.z};
        case 2: return {-direction.x, -direction.y, direction.z};
        case 3: return {direction.y, -direction.x, direction.z};
        case 0:
        default: return direction;
    }
}

glm::ivec3 applyModelDirectionTransform(glm::ivec3 direction, const ModelTransform& transform) {
    direction = rotateDirectionX90(direction, transform.rotX);
    direction = rotateDirectionY90(direction, transform.rotY);
    direction = rotateDirectionZ90(direction, transform.rotZ);
    return direction;
}

std::optional<ModelTransform> makeFaceOrientedModelTransform(const uint16_t face, const uint16_t facing) {
    if (!AttachmentFaceGeometry::isAttachmentFace(face)) {
        throw std::runtime_error("Face-oriented model state contains an unsupported face value");
    }
    const glm::ivec3 targetNormal = AttachmentFaceGeometry::surfaceNormal(face);
    const glm::ivec3 targetFacing = AttachmentFaceGeometry::directionFromFacing(facing);
    if (!AttachmentFaceGeometry::isDirectionInPlane(face, targetFacing)) {
        return std::nullopt;
    }

    constexpr std::array<uint16_t, 4> rotations = {{0, 90, 180, 270}};
    for (const uint16_t rotX : rotations) {
        for (const uint16_t rotY : rotations) {
            for (const uint16_t rotZ : rotations) {
                ModelTransform transform;
                transform.rotX = rotX;
                transform.rotY = rotY;
                transform.rotZ = rotZ;
                const glm::ivec3 normal = applyModelDirectionTransform({0, 1, 0}, transform);
                const glm::ivec3 facingDirection = applyModelDirectionTransform({0, 0, 1}, transform);
                if (normal == targetNormal && facingDirection == targetFacing) {
                    return transform;
                }
            }
        }
    }

    throw std::runtime_error("Face-oriented model transform table is incomplete");
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
    s_stateModelVariants.clear();
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
        const size_t textureOffset = s_stateTextures.size();
        s_stateTextures.push_back(makeTexturesForBlock(blockId));
        s_states[blockId] = BlockStateEntry{
            makeBlockStateId(blockId),
            blockId,
            0,
            0,
            textureOffset
        };
    }

    size_t nextStateId = blockCount;

    for (BlockID blockId = 0; blockId < blockCount; ++blockId) {
        const auto registeredIt = s_registeredProperties.find(blockId);
        if (registeredIt == s_registeredProperties.end() || registeredIt->second.propertyNameIndices.empty()) {
            s_defaultState[blockId] = makeBlockStateId(blockId);
            continue;
        }

        const RegisteredBlockProperties& registered = registeredIt->second;
        BlockPropertyLayout layout;
        layout.firstStateId = makeBlockStateId(nextStateId);
        layout.propertyCount = static_cast<uint8_t>(registered.propertyNameIndices.size());
        layout.propertyPosition.assign(s_propertyNamePool.size(), std::numeric_limits<uint8_t>::max());
        layout.propertyStride.assign(s_propertyNamePool.size(), 0);
        layout.valueCounts.assign(s_propertyNamePool.size(), 0);
        layout.valueOrdinals.resize(registered.propertyNameIndices.size());

        size_t runningStride = 1;
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
            if (values.size() > 0 && runningStride > std::numeric_limits<size_t>::max() / values.size()) {
                throw std::runtime_error("Block property stride exceeds size_t capacity");
            }
            runningStride *= values.size();
        }

        std::vector<PropertyKey> props(registered.propertyNameIndices.size());
        std::function<void(size_t, bool)> buildStates = [&](const size_t propertyIndex, const bool isDefaultPath) {
            if (propertyIndex == registered.propertyNameIndices.size()) {
                const size_t propertiesOffset = s_statePropertiesPool.size();
                s_statePropertiesPool.insert(s_statePropertiesPool.end(), props.begin(), props.end());

                const size_t textureOffset = s_stateTextures.size();
                s_stateTextures.push_back(makeTexturesForState(blockId, props));

                const BlockStateId stateId = makeBlockStateId(nextStateId);
                ++nextStateId;
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
            s_defaultState[blockId] = makeBlockStateId(blockId);
        }
        s_blockPropertyLayouts[blockId] = std::move(layout);
    }
}

BlockStateId BlockStateRegistry::getDefaultState(const BlockID blockId) {
    const auto it = s_defaultState.find(blockId);
    if (it != s_defaultState.end()) {
        return it->second;
    }
    return makeBlockStateId(blockId);
}

BlockStateId BlockStateRegistry::getState(const BlockID blockId, const uint16_t propKey, const uint16_t propValue) {
    return getState(blockId, {{propKey, propValue}});
}

BlockStateId BlockStateRegistry::getState(const BlockID blockId,
                                          std::initializer_list<std::pair<uint16_t, uint16_t>> props) {
    return getState(blockId, std::vector<std::pair<uint16_t, uint16_t>>(props.begin(), props.end()));
}

BlockStateId BlockStateRegistry::getState(const BlockID blockId,
                                          const std::vector<std::pair<uint16_t, uint16_t>>& props) {
    BlockStateId state = getDefaultState(blockId);
    for (const auto& [propKey, propValue] : props) {
        state = withProperty(state, propKey, propValue);
    }
    return state;
}

BlockID BlockStateRegistry::getBlockId(const BlockStateId stateId) {
    const size_t index = stateIndex(stateId);
    if (index < s_states.size()) {
        return s_states[index].blockId;
    }
    return RUNTIME_ID_NULL;
}

uint16_t BlockStateRegistry::getPropertyIndex(const BlockStateId stateId, const uint16_t nameIndex) {
    const size_t index = stateIndex(stateId);
    if (index >= s_states.size()) {
        return INVALID_INDEX;
    }

    const BlockStateEntry& entry = s_states[index];
    for (uint8_t propertyIndex = 0; propertyIndex < entry.propertyCount; ++propertyIndex) {
        const PropertyKey& prop = s_statePropertiesPool[entry.propertiesOffset + propertyIndex];
        if (prop.nameIndex == nameIndex) {
            return prop.valueIndex;
        }
    }

    return INVALID_INDEX;
}

uint8_t BlockStateRegistry::getPropertyCount(const BlockStateId stateId) {
    const size_t index = stateIndex(stateId);
    if (index < s_states.size()) {
        return s_states[index].propertyCount;
    }
    return 0;
}

BlockStateId BlockStateRegistry::withProperty(const BlockStateId currentState,
                                              const uint16_t propKey,
                                              const uint16_t newValue) {
    const size_t currentIndex = stateIndex(currentState);
    if (propKey == INVALID_INDEX || currentIndex >= s_states.size()) {
        return currentState;
    }

    const BlockID blockId = s_states[currentIndex].blockId;
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

    const int64_t delta = static_cast<int64_t>(newOrdinal) - static_cast<int64_t>(currentOrdinal);
    if (delta == 0) {
        return currentState;
    }

    const int64_t nextState = static_cast<int64_t>(currentIndex) +
                              delta * static_cast<int64_t>(layout.propertyStride[propKey]);
    if (nextState < 0 || static_cast<size_t>(nextState) >= s_states.size()) {
        return currentState;
    }
    return makeBlockStateId(static_cast<size_t>(nextState));
}

BlockStateId BlockStateRegistry::withProperty(const BlockStateId currentState,
                                              const uint16_t propKey,
                                              const std::string& newValue) {
    return withProperty(currentState, propKey, getPropertyValueIndex(propKey, newValue));
}

const StateTextureIndices& BlockStateRegistry::getStateTextures(const BlockStateId stateId) {
    const size_t index = stateIndex(stateId);
    if (index < s_states.size()) {
        const BlockStateEntry& entry = s_states[index];
        if (entry.textureOffset < s_stateTextures.size()) {
            return s_stateTextures[entry.textureOffset];
        }
    }
    return s_fallbackTextures;
}

void BlockStateRegistry::registerBlockModelVariants(const BlockID blockId, const nlohmann::json& variantsJson) {
    if (!variantsJson.is_object()) {
        throw std::runtime_error("modelVariants must be an object for block: " +
                                 BlockRegistry::getNamespacedId(blockId).full());
    }

    if (s_stateModelVariants.size() < s_states.size()) {
        s_stateModelVariants.resize(s_states.size());
    }

    for (auto it = variantsJson.begin(); it != variantsJson.end(); ++it) {
        if (!it.value().is_object()) {
            throw std::runtime_error("Model variant entry must be an object: " + it.key());
        }

        const auto modelIt = it.value().find("model");
        if (modelIt == it.value().end() || !modelIt->is_string()) {
            throw std::runtime_error("Model variant entry requires model string: " + it.key());
        }

        const std::string modelName = modelIt->get<std::string>();
        const BlockModel* model = BlockModelRegistry::get(modelName);
        if (model == nullptr) {
            throw std::runtime_error("Unknown block model referenced by variant: " + modelName);
        }

        ModelVariant variant;
        variant.model = model;
        variant.transform = parseModelTransform(it.value());

        const std::vector<std::pair<uint16_t, uint16_t>> props = parseModelVariantPropertyKey(it.key());
        bool matchedState = false;
        for (const BlockStateId stateId : getStatesForBlock(blockId)) {
            if (!stateMatchesModelVariantProperties(stateId, props)) {
                continue;
            }
            const size_t index = stateIndex(stateId);
            if (index >= s_stateModelVariants.size()) {
                throw std::runtime_error("Model variant state id is outside state registry");
            }
            s_stateModelVariants[index] = variant;
            matchedState = true;
        }
        if (!matchedState) {
            throw std::runtime_error("Model variant state key matched no states: " + it.key());
        }
    }

    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (!def.faceOrientedModel) {
        return;
    }
    if (PropIndices::FACE == PropIndices::INVALID || PropIndices::FACING == PropIndices::INVALID) {
        throw std::runtime_error("Face-oriented model requires registered face and facing properties");
    }

    for (const BlockStateId stateId : getStatesForBlock(blockId)) {
        const uint16_t face = getPropertyIndex(stateId, PropIndices::FACE);
        const uint16_t facing = getPropertyIndex(stateId, PropIndices::FACING);
        if (face == INVALID_INDEX || facing == INVALID_INDEX) {
            throw std::runtime_error("Face-oriented model block is missing face or facing state values: " +
                                     def.namespacedId.full());
        }

        const size_t index = stateIndex(stateId);
        if (index >= s_stateModelVariants.size()) {
            throw std::runtime_error("Face-oriented model state id is outside state registry");
        }
        ModelVariant& variant = s_stateModelVariants[index];
        if (variant.model == nullptr) {
            const BlockStateId baseState = withProperty(stateId, PropIndices::FACING, PropIndices::FACING_SOUTH);
            const ModelVariant* baseVariant = getModelVariant(baseState);
            if (baseVariant == nullptr || baseVariant->model == nullptr) {
                throw std::runtime_error("Face-oriented model state is missing a base south-facing variant: " +
                                         stateToString(stateId));
            }
            variant = *baseVariant;
        }

        const std::optional<ModelTransform> transform = makeFaceOrientedModelTransform(face, facing);
        if (!transform.has_value()) {
            continue;
        }
        variant.transform = *transform;
    }
}

const ModelVariant* BlockStateRegistry::getModelVariant(const BlockStateId stateId) {
    const size_t index = stateIndex(stateId);
    if (index >= s_stateModelVariants.size()) {
        return nullptr;
    }
    const ModelVariant& variant = s_stateModelVariants[index];
    return variant.model != nullptr ? &variant : nullptr;
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

std::string BlockStateRegistry::stateToString(const BlockStateId stateId) {
    const size_t index = stateIndex(stateId);
    if (index >= s_states.size()) {
        return BlockRegistry::getNamespacedId(RUNTIME_ID_NULL).full();
    }

    const BlockStateEntry& entry = s_states[index];
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

std::vector<BlockStateId> BlockStateRegistry::getStatesForBlock(const BlockID blockId) {
    std::vector<BlockStateId> states;
    const bool hasExpandedStates = s_blockPropertyLayouts.find(blockId) != s_blockPropertyLayouts.end();
    for (const BlockStateEntry& entry : s_states) {
        if (entry.blockId == blockId &&
            (!hasExpandedStates || entry.propertyCount > 0)) {
            states.push_back(entry.stateId);
        }
    }
    return states;
}
