#include "ModelSceneSerializer.h"

#include "app/AppSettings.h"
#include "renderer/contracts/LocalShadowContract.h"
#include "renderer/contracts/ReflectionProbeContract.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace scene {
namespace {

using json = nlohmann::json;

constexpr float kMinimumScaleMagnitude = 1e-8f;
constexpr float kWorldDaySeconds = 1200.0f;

[[nodiscard]] const char* reflectionProbeFieldName(const renderer::contracts::ReflectionProbeField field) {
    using renderer::contracts::ReflectionProbeField;
    switch (field) {
    case ReflectionProbeField::StableId: return "id";
    case ReflectionProbeField::Position: return "position";
    case ReflectionProbeField::Exposure: return "exposureScale";
    case ReflectionProbeField::InfluenceBounds: return "influenceBounds";
    case ReflectionProbeField::BlendDistance: return "blendDistance";
    case ReflectionProbeField::BoxProjectionBounds: return "boxProjectionBounds";
    case ReflectionProbeField::Validity:
    case ReflectionProbeField::PrefilteredCubemapIndex:
    case ReflectionProbeField::CaptureRevision:
    case ReflectionProbeField::ContractVersion:
    case ReflectionProbeField::ReservedValue:
    case ReflectionProbeField::SurfacePosition:
    case ReflectionProbeField::SurfaceNormal:
    case ReflectionProbeField::None: return "contract";
    default: std::abort();
    }
}

[[nodiscard]] bool isValidUtf8(const std::string& value) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    std::size_t index = 0u;
    while (index < value.size()) {
        const unsigned char lead = bytes[index];
        if (lead <= 0x7fu) {
            ++index;
            continue;
        }
        std::size_t count = 0u;
        uint32_t codePoint = 0u;
        if ((lead & 0xe0u) == 0xc0u) {
            count = 2u;
            codePoint = lead & 0x1fu;
        } else if ((lead & 0xf0u) == 0xe0u) {
            count = 3u;
            codePoint = lead & 0x0fu;
        } else if ((lead & 0xf8u) == 0xf0u) {
            count = 4u;
            codePoint = lead & 0x07u;
        } else {
            return false;
        }
        if (index + count > value.size()) {
            return false;
        }
        for (std::size_t offset = 1u; offset < count; ++offset) {
            const unsigned char continuation = bytes[index + offset];
            if ((continuation & 0xc0u) != 0x80u) {
                return false;
            }
            codePoint = (codePoint << 6u) | (continuation & 0x3fu);
        }
        const uint32_t minimum = count == 2u ? 0x80u : count == 3u ? 0x800u : 0x10000u;
        if (codePoint < minimum || codePoint > 0x10ffffu || (codePoint >= 0xd800u && codePoint <= 0xdfffu)) {
            return false;
        }
        index += count;
    }
    return true;
}

[[nodiscard]] bool isValidDocumentString(const std::string& value) {
    return isValidUtf8(value) && value.find('\0') == std::string::npos;
}

[[nodiscard]] bool finiteVec3(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool manualPointLightShadowPolicyValid(const renderer::contracts::GpuLightShadowPolicy policy) {
    using renderer::contracts::GpuLightShadowPolicy;
    return policy == GpuLightShadowPolicy::None || policy == GpuLightShadowPolicy::RasterDynamic ||
           policy == GpuLightShadowPolicy::RasterCached;
}

[[nodiscard]] json vec3ToJson(const glm::vec3& value) {
    return json::array({value.x, value.y, value.z});
}

[[nodiscard]] json manualPointLightToJson(const SceneManualPointLightDocument& light) {
    return {
        {"colorLinear", vec3ToJson(light.colorLinear)},
        {"intensityCandela", light.intensityCandela},
        {"rangeMeters", light.rangeMeters},
        {"emitterRadiusMeters", light.emitterRadiusMeters},
        {"selfShadowRadiusMeters", light.selfShadowRadiusMeters},
        {"shadowPolicy", static_cast<uint32_t>(light.shadowPolicy)},
    };
}

[[nodiscard]] bool readObject(const json& owner, const char* key, const json*& value, const std::string& context,
                              std::string& error) {
    const auto it = owner.find(key);
    if (it == owner.end()) {
        error = context + "." + key + " is required";
        return false;
    }
    if (!it->is_object()) {
        error = context + "." + key + " must be an object";
        return false;
    }
    value = &*it;
    return true;
}

[[nodiscard]] bool readArray(const json& owner, const char* key, const json*& value, const std::string& context,
                             std::string& error) {
    const auto it = owner.find(key);
    if (it == owner.end()) {
        error = context + "." + key + " is required";
        return false;
    }
    if (!it->is_array()) {
        error = context + "." + key + " must be an array";
        return false;
    }
    value = &*it;
    return true;
}

[[nodiscard]] bool readString(const json& owner, const char* key, std::string& value, const std::string& context,
                              std::string& error) {
    const auto it = owner.find(key);
    if (it == owner.end()) {
        error = context + "." + key + " is required";
        return false;
    }
    if (!it->is_string()) {
        error = context + "." + key + " must be a string";
        return false;
    }
    value = it->get<std::string>();
    if (!isValidUtf8(value)) {
        error = context + "." + key + " must contain valid UTF-8";
        return false;
    }
    return true;
}

template <typename UInt>
[[nodiscard]] bool readUnsigned(const json& owner, const char* key, UInt& value, const std::string& context,
                                std::string& error) {
    const auto it = owner.find(key);
    if (it == owner.end()) {
        error = context + "." + key + " is required";
        return false;
    }
    if (!it->is_number_unsigned()) {
        error = context + "." + key + " must be an unsigned integer";
        return false;
    }
    const uint64_t raw = it->get<uint64_t>();
    if (raw > static_cast<uint64_t>(std::numeric_limits<UInt>::max())) {
        error = context + "." + key + " exceeds the supported range";
        return false;
    }
    value = static_cast<UInt>(raw);
    return true;
}

[[nodiscard]] bool readFloat(const json& owner, const char* key, float& value, const std::string& context,
                             std::string& error) {
    const auto it = owner.find(key);
    if (it == owner.end()) {
        error = context + "." + key + " is required";
        return false;
    }
    if (!it->is_number()) {
        error = context + "." + key + " must be a number";
        return false;
    }
    const double raw = it->get<double>();
    if (!std::isfinite(raw) || std::abs(raw) > static_cast<double>(std::numeric_limits<float>::max())) {
        error = context + "." + key + " must be a finite 32-bit float";
        return false;
    }
    value = static_cast<float>(raw);
    return true;
}

[[nodiscard]] bool readBool(const json& owner, const char* key, bool& value, const std::string& context,
                            std::string& error) {
    const auto it = owner.find(key);
    if (it == owner.end()) {
        error = context + "." + key + " is required";
        return false;
    }
    if (!it->is_boolean()) {
        error = context + "." + key + " must be a boolean";
        return false;
    }
    value = it->get<bool>();
    return true;
}

[[nodiscard]] bool readVec3(const json& owner, const char* key, glm::vec3& value, const std::string& context,
                            std::string& error) {
    const auto it = owner.find(key);
    if (it == owner.end()) {
        error = context + "." + key + " is required";
        return false;
    }
    if (!it->is_array() || it->size() != 3u) {
        error = context + "." + key + " must be a 3-number array";
        return false;
    }
    glm::vec3 parsed;
    for (std::size_t index = 0u; index < 3u; ++index) {
        const json& item = (*it)[index];
        if (!item.is_number()) {
            error = context + "." + key + " must contain only numbers";
            return false;
        }
        const double raw = item.get<double>();
        if (!std::isfinite(raw) || std::abs(raw) > static_cast<double>(std::numeric_limits<float>::max())) {
            error = context + "." + key + " must contain finite 32-bit floats";
            return false;
        }
        parsed[index] = static_cast<float>(raw);
    }
    value = parsed;
    return true;
}

template <typename Id>
[[nodiscard]] bool readOptionalId(const json& owner, const char* key, std::optional<Id>& value,
                                  const std::string& context, std::string& error) {
    const auto it = owner.find(key);
    if (it == owner.end()) {
        error = context + "." + key + " is required";
        return false;
    }
    if (it->is_null()) {
        value.reset();
        return true;
    }
    if (!it->is_number_unsigned()) {
        error = context + "." + key + " must be null or an unsigned integer";
        return false;
    }
    const uint64_t raw = it->get<uint64_t>();
    if (raw == 0u || raw > static_cast<uint64_t>(std::numeric_limits<Id>::max())) {
        error = context + "." + key + " contains an invalid ID";
        return false;
    }
    value = static_cast<Id>(raw);
    return true;
}

[[nodiscard]] bool validateHierarchy(const std::unordered_map<SceneEntityId, SceneEntityId>& parents,
                                     std::string& error) {
    std::unordered_map<SceneEntityId, uint8_t> states;
    states.reserve(parents.size());
    for (const auto& pair : parents) {
        SceneEntityId current = pair.first;
        std::vector<SceneEntityId> path;
        while (current != kInvalidSceneEntityId) {
            const uint8_t state = states[current];
            if (state == 2u) {
                break;
            }
            if (state == 1u) {
                error = "scene entity hierarchy contains a cycle at ID " + std::to_string(current);
                return false;
            }
            states[current] = 1u;
            path.push_back(current);
            const auto parentIt = parents.find(current);
            current = parentIt != parents.end() ? parentIt->second : kInvalidSceneEntityId;
        }
        for (const SceneEntityId id : path) {
            states[id] = 2u;
        }
    }
    return true;
}

} // namespace

bool ModelSceneSerializer::validate(const ModelSceneDocument& document, std::string& error) {
    error.clear();
    if (document.format != ModelSceneDocument::kFormat) {
        error = "scene format must be mecraft.scene";
        return false;
    }
    if (document.version != ModelSceneDocument::kCurrentVersion) {
        error = "scene version is not supported";
        return false;
    }

    std::unordered_set<SceneAssetId> assetIds;
    assetIds.reserve(document.assets.size());
    for (std::size_t index = 0u; index < document.assets.size(); ++index) {
        const SceneAssetDocument& asset = document.assets[index];
        const std::string context = "assets[" + std::to_string(index) + "]";
        if (asset.id == kInvalidSceneAssetId || asset.id == std::numeric_limits<SceneAssetId>::max()) {
            error = context + ".id is invalid";
            return false;
        }
        if (!assetIds.insert(asset.id).second) {
            error = context + ".id is duplicated";
            return false;
        }
        if (asset.name.empty() || !isValidDocumentString(asset.name)) {
            error = context + ".name must be non-empty valid UTF-8";
            return false;
        }
        if (asset.path.empty() || !isValidDocumentString(asset.path)) {
            error = context + ".path must be non-empty valid UTF-8";
            return false;
        }
    }

    std::unordered_set<SceneEntityId> entityIds;
    entityIds.reserve(document.entities.size());
    std::unordered_map<SceneEntityId, SceneEntityId> parents;
    parents.reserve(document.entities.size());
    for (std::size_t index = 0u; index < document.entities.size(); ++index) {
        const SceneEntityDocument& entity = document.entities[index];
        const std::string context = "entities[" + std::to_string(index) + "]";
        if (entity.id == kInvalidSceneEntityId || entity.id == std::numeric_limits<SceneEntityId>::max()) {
            error = context + ".id is invalid";
            return false;
        }
        if (!entityIds.insert(entity.id).second) {
            error = context + ".id is duplicated";
            return false;
        }
        if (entity.name.empty() || !isValidDocumentString(entity.name)) {
            error = context + ".name must be non-empty valid UTF-8";
            return false;
        }
        if (!finiteVec3(entity.transform.position) || !finiteVec3(entity.transform.rotation) ||
            !finiteVec3(entity.transform.scale)) {
            error = context + ".transform must contain finite values";
            return false;
        }
        if (std::abs(entity.transform.scale.x) <= kMinimumScaleMagnitude ||
            std::abs(entity.transform.scale.y) <= kMinimumScaleMagnitude ||
            std::abs(entity.transform.scale.z) <= kMinimumScaleMagnitude) {
            error = context + ".transform.scale must be non-singular";
            return false;
        }
        parents.emplace(entity.id, entity.parentId.value_or(kInvalidSceneEntityId));
    }
    for (std::size_t index = 0u; index < document.entities.size(); ++index) {
        const SceneEntityDocument& entity = document.entities[index];
        const std::string context = "entities[" + std::to_string(index) + "]";
        if (entity.parentId.has_value() && entityIds.find(*entity.parentId) == entityIds.end()) {
            error = context + ".parentId references an unknown entity";
            return false;
        }
        if (entity.parentId == entity.id) {
            error = context + ".parentId cannot reference itself";
            return false;
        }
        if (entity.assetId.has_value() && assetIds.find(*entity.assetId) == assetIds.end()) {
            error = context + ".assetId references an unknown asset";
            return false;
        }
        if (entity.assetId.has_value() && entity.manualPointLight.has_value()) {
            error = context + " cannot combine a mesh asset and manual Point light";
            return false;
        }
        if (entity.manualPointLight.has_value() && !validateManualPointLight(*entity.manualPointLight, error)) {
            error = context + ".manualPointLight." + error;
            return false;
        }
    }
    if (!validateHierarchy(parents, error)) {
        return false;
    }

    if (document.reflectionProbes.size() > renderer::contracts::kReflectionProbeCaptureMaxProbeCount) {
        error = "reflectionProbes exceeds the capture capacity";
        return false;
    }
    std::unordered_set<SceneReflectionProbeId> reflectionProbeIds;
    reflectionProbeIds.reserve(document.reflectionProbes.size());
    for (std::size_t index = 0u; index < document.reflectionProbes.size(); ++index) {
        const SceneReflectionProbeDocument& probe = document.reflectionProbes[index];
        const std::string context = "reflectionProbes[" + std::to_string(index) + "]";
        if (!validateReflectionProbe(probe, error)) {
            error = context + "." + error;
            return false;
        }
        if (!reflectionProbeIds.insert(probe.id).second) {
            error = context + ".id is duplicated";
            return false;
        }
    }

    const std::size_t manualPointLightCount = std::count_if(
        document.entities.begin(), document.entities.end(),
        [](const SceneEntityDocument& entity) { return entity.manualPointLight.has_value(); });
    if (manualPointLightCount > renderer::contracts::kLocalShadowMaxPointLightCount) {
        error = "entities exceeds the supported Point-light capacity";
        return false;
    }

    if (!std::isfinite(document.environment.timeOfDay) || document.environment.timeOfDay < 0.0f ||
        document.environment.timeOfDay >= kWorldDaySeconds) {
        error = "environment.timeOfDay must be within [0, 1200)";
        return false;
    }
    if (!std::isfinite(document.environment.timeScale) || document.environment.timeScale <= 0.0f ||
        document.environment.timeScale > 100.0f) {
        error = "environment.timeScale must be within (0, 100]";
        return false;
    }
    if (document.environment.weather < WeatherType::Clear || document.environment.weather > WeatherType::Snow) {
        error = "environment.weather is invalid";
        return false;
    }
    RenderSettings verifiedSettings;
    if (!app::deserializeRenderSettings(app::serializeRenderSettings(document.environment.renderSettings),
                                        verifiedSettings, error)) {
        error = "environment." + error;
        return false;
    }
    if (!finiteVec3(document.editorCamera.target) || !std::isfinite(document.editorCamera.distance) ||
        !std::isfinite(document.editorCamera.yaw) || !std::isfinite(document.editorCamera.pitch) ||
        !std::isfinite(document.editorCamera.nearPlane) || !std::isfinite(document.editorCamera.farPlane) ||
        document.editorCamera.distance <= 0.0f || document.editorCamera.nearPlane <= 0.0f ||
        document.editorCamera.farPlane <= document.editorCamera.nearPlane || document.editorCamera.pitch < -89.9f ||
        document.editorCamera.pitch > 89.9f) {
        error = "editorCamera contains invalid navigation values";
        return false;
    }
    return true;
}

bool ModelSceneSerializer::validateReflectionProbe(const SceneReflectionProbeDocument& probe, std::string& error) {
    error.clear();
    if (probe.id == kInvalidSceneReflectionProbeId || probe.id == std::numeric_limits<SceneReflectionProbeId>::max()) {
        error = "id is invalid";
        return false;
    }
    renderer::contracts::ReflectionProbeNormalizationInput input;
    input.probeId = renderer::contracts::StableReflectionProbeId{probe.id};
    input.positionMeters = probe.position;
    input.exposureScale = probe.exposureScale;
    input.influenceMinMeters = probe.influenceMin;
    input.influenceMaxMeters = probe.influenceMax;
    input.blendDistanceMeters = probe.blendDistance;
    input.boxProjectionMinMeters = probe.boxProjectionMin;
    input.boxProjectionMaxMeters = probe.boxProjectionMax;
    const renderer::contracts::ReflectionProbeNormalizationResult normalized =
        renderer::contracts::normalizeReflectionProbe(input);
    if (!normalized.succeeded()) {
        error = std::string(reflectionProbeFieldName(normalized.field)) + " violates the reflection-probe contract";
        return false;
    }
    return true;
}

bool ModelSceneSerializer::validateManualPointLight(const SceneManualPointLightDocument& light, std::string& error) {
    error.clear();
    if (!manualPointLightShadowPolicyValid(light.shadowPolicy)) {
        error = "shadowPolicy is invalid";
        return false;
    }
    renderer::contracts::GpuLightNormalizationInput input;
    input.lightId = renderer::contracts::StableLightId{1u};
    input.type = renderer::contracts::GpuLightType::Point;
    input.positionMeters = glm::vec3(0.0f);
    input.rangeMeters = light.rangeMeters;
    input.pointEmitterRadiusMeters = light.emitterRadiusMeters;
    input.pointSelfShadowRadiusMeters = light.selfShadowRadiusMeters;
    input.colorLinear = light.colorLinear;
    input.intensity = light.intensityCandela;
    input.intensityUnit = renderer::contracts::GpuLightIntensityUnit::Candela;
    input.contributionFlags =
        renderer::contracts::gpuLightContributionFlagBit(renderer::contracts::GpuLightContributionFlag::Diffuse) |
        renderer::contracts::gpuLightContributionFlagBit(renderer::contracts::GpuLightContributionFlag::Specular);
    const renderer::contracts::GpuLightNormalizationResult normalized = renderer::contracts::normalizeGpuLight(input);
    if (!normalized.succeeded()) {
        error = std::string(renderer::contracts::gpuLightFieldStableId(normalized.field)) +
                " violates the Point-light contract";
        return false;
    }
    return true;
}

nlohmann::json ModelSceneSerializer::serialize(const ModelSceneDocument& document) {
    json assets = json::array();
    for (const SceneAssetDocument& asset : document.assets) {
        assets.push_back({
            {"id", asset.id},
            {"name", asset.name},
            {"path", asset.path},
        });
    }

    json entities = json::array();
    for (const SceneEntityDocument& entity : document.entities) {
        entities.push_back({
            {"id", entity.id},
            {"name", entity.name},
            {"parentId", entity.parentId.has_value() ? json(*entity.parentId) : json(nullptr)},
            {"assetId", entity.assetId.has_value() ? json(*entity.assetId) : json(nullptr)},
            {"manualPointLight", entity.manualPointLight.has_value() ? manualPointLightToJson(*entity.manualPointLight)
                                                                       : json(nullptr)},
            {"transform",
             {
                 {"position", vec3ToJson(entity.transform.position)},
                 {"rotation", vec3ToJson(entity.transform.rotation)},
                 {"scale", vec3ToJson(entity.transform.scale)},
             }},
        });
    }

    json reflectionProbes = json::array();
    for (const SceneReflectionProbeDocument& probe : document.reflectionProbes) {
        reflectionProbes.push_back({
            {"id", probe.id},
            {"position", vec3ToJson(probe.position)},
            {"influenceMin", vec3ToJson(probe.influenceMin)},
            {"influenceMax", vec3ToJson(probe.influenceMax)},
            {"boxProjectionMin", vec3ToJson(probe.boxProjectionMin)},
            {"boxProjectionMax", vec3ToJson(probe.boxProjectionMax)},
            {"blendDistance", probe.blendDistance},
            {"exposureScale", probe.exposureScale},
        });
    }

    return {
        {"format", document.format},
        {"version", document.version},
        {"assets", std::move(assets)},
        {"entities", std::move(entities)},
        {"reflectionProbes", std::move(reflectionProbes)},
        {"environment",
         {
             {"timeOfDay", document.environment.timeOfDay},
             {"timePaused", document.environment.timePaused},
             {"timeScale", document.environment.timeScale},
             {"weather", static_cast<uint32_t>(document.environment.weather)},
             {"weatherTransitionInstant", document.environment.weatherTransitionInstant},
             {"renderSettings", app::serializeRenderSettings(document.environment.renderSettings)},
         }},
        {"editorCamera",
         {
             {"target", vec3ToJson(document.editorCamera.target)},
             {"distance", document.editorCamera.distance},
             {"yaw", document.editorCamera.yaw},
             {"pitch", document.editorCamera.pitch},
             {"nearPlane", document.editorCamera.nearPlane},
             {"farPlane", document.editorCamera.farPlane},
         }},
    };
}

bool ModelSceneSerializer::deserialize(const nlohmann::json& value, ModelSceneDocument& document, std::string& error) {
    error.clear();
    if (!value.is_object()) {
        error = "scene root must be an object";
        return false;
    }

    ModelSceneDocument parsed;
    if (!readString(value, "format", parsed.format, "scene", error) ||
        !readUnsigned(value, "version", parsed.version, "scene", error)) {
        return false;
    }
    if (parsed.version != ModelSceneDocument::kCurrentVersion) {
        error = "scene version is not supported";
        return false;
    }
    const json* assets = nullptr;
    const json* entities = nullptr;
    const json* reflectionProbes = nullptr;
    const json* environment = nullptr;
    const json* editorCamera = nullptr;
    if (!readArray(value, "assets", assets, "scene", error) ||
        !readArray(value, "entities", entities, "scene", error) ||
        !readArray(value, "reflectionProbes", reflectionProbes, "scene", error) ||
        !readObject(value, "environment", environment, "scene", error) ||
        !readObject(value, "editorCamera", editorCamera, "scene", error)) {
        return false;
    }

    parsed.assets.reserve(assets->size());
    for (std::size_t index = 0u; index < assets->size(); ++index) {
        const json& item = (*assets)[index];
        const std::string context = "scene.assets[" + std::to_string(index) + "]";
        if (!item.is_object()) {
            error = context + " must be an object";
            return false;
        }
        SceneAssetDocument asset;
        if (!readUnsigned(item, "id", asset.id, context, error) ||
            !readString(item, "name", asset.name, context, error) ||
            !readString(item, "path", asset.path, context, error)) {
            return false;
        }
        parsed.assets.push_back(std::move(asset));
    }

    parsed.entities.reserve(entities->size());
    for (std::size_t index = 0u; index < entities->size(); ++index) {
        const json& item = (*entities)[index];
        const std::string context = "scene.entities[" + std::to_string(index) + "]";
        if (!item.is_object()) {
            error = context + " must be an object";
            return false;
        }
        SceneEntityDocument entity;
        const json* transform = nullptr;
        if (!readUnsigned(item, "id", entity.id, context, error) ||
            !readString(item, "name", entity.name, context, error) ||
            !readOptionalId(item, "parentId", entity.parentId, context, error) ||
            !readOptionalId(item, "assetId", entity.assetId, context, error) ||
            !readObject(item, "transform", transform, context, error) ||
            !readVec3(*transform, "position", entity.transform.position, context + ".transform", error) ||
            !readVec3(*transform, "rotation", entity.transform.rotation, context + ".transform", error) ||
            !readVec3(*transform, "scale", entity.transform.scale, context + ".transform", error)) {
            return false;
        }
        const auto manualPointLight = item.find("manualPointLight");
        if (manualPointLight == item.end()) {
            error = context + ".manualPointLight is required";
            return false;
        }
        if (!manualPointLight->is_null()) {
            if (!manualPointLight->is_object()) {
                error = context + ".manualPointLight must be null or an object";
                return false;
            }
            SceneManualPointLightDocument light;
            uint32_t shadowPolicy = 0u;
            const std::string lightContext = context + ".manualPointLight";
            if (!readVec3(*manualPointLight, "colorLinear", light.colorLinear, lightContext, error) ||
                !readFloat(*manualPointLight, "intensityCandela", light.intensityCandela, lightContext, error) ||
                !readFloat(*manualPointLight, "rangeMeters", light.rangeMeters, lightContext, error) ||
                !readFloat(*manualPointLight, "emitterRadiusMeters", light.emitterRadiusMeters, lightContext, error) ||
                !readFloat(*manualPointLight, "selfShadowRadiusMeters", light.selfShadowRadiusMeters, lightContext,
                           error) ||
                !readUnsigned(*manualPointLight, "shadowPolicy", shadowPolicy, lightContext, error)) {
                return false;
            }
            if (shadowPolicy > static_cast<uint32_t>(renderer::contracts::GpuLightShadowPolicy::RayQuery)) {
                error = lightContext + ".shadowPolicy is invalid";
                return false;
            }
            light.shadowPolicy = static_cast<renderer::contracts::GpuLightShadowPolicy>(shadowPolicy);
            entity.manualPointLight = light;
        }
        parsed.entities.push_back(std::move(entity));
    }

    parsed.reflectionProbes.reserve(reflectionProbes->size());
    for (std::size_t index = 0u; index < reflectionProbes->size(); ++index) {
        const json& item = (*reflectionProbes)[index];
        const std::string context = "scene.reflectionProbes[" + std::to_string(index) + "]";
        if (!item.is_object()) {
            error = context + " must be an object";
            return false;
        }
        SceneReflectionProbeDocument probe;
        if (!readUnsigned(item, "id", probe.id, context, error) ||
            !readVec3(item, "position", probe.position, context, error) ||
            !readVec3(item, "influenceMin", probe.influenceMin, context, error) ||
            !readVec3(item, "influenceMax", probe.influenceMax, context, error) ||
            !readVec3(item, "boxProjectionMin", probe.boxProjectionMin, context, error) ||
            !readVec3(item, "boxProjectionMax", probe.boxProjectionMax, context, error) ||
            !readFloat(item, "blendDistance", probe.blendDistance, context, error) ||
            !readFloat(item, "exposureScale", probe.exposureScale, context, error)) {
            return false;
        }
        parsed.reflectionProbes.push_back(probe);
    }

    const json* renderSettings = nullptr;
    uint32_t weather = 0u;
    if (!readFloat(*environment, "timeOfDay", parsed.environment.timeOfDay, "scene.environment", error) ||
        !readBool(*environment, "timePaused", parsed.environment.timePaused, "scene.environment", error) ||
        !readFloat(*environment, "timeScale", parsed.environment.timeScale, "scene.environment", error) ||
        !readUnsigned(*environment, "weather", weather, "scene.environment", error) ||
        !readBool(*environment, "weatherTransitionInstant", parsed.environment.weatherTransitionInstant,
                  "scene.environment", error) ||
        !readObject(*environment, "renderSettings", renderSettings, "scene.environment", error) ||
        !app::deserializeRenderSettings(*renderSettings, parsed.environment.renderSettings, error)) {
        return false;
    }
    if (weather > static_cast<uint32_t>(WeatherType::Snow)) {
        error = "scene.environment.weather is invalid";
        return false;
    }
    parsed.environment.weather = static_cast<WeatherType>(weather);
    if (!readVec3(*editorCamera, "target", parsed.editorCamera.target, "scene.editorCamera", error) ||
        !readFloat(*editorCamera, "distance", parsed.editorCamera.distance, "scene.editorCamera", error) ||
        !readFloat(*editorCamera, "yaw", parsed.editorCamera.yaw, "scene.editorCamera", error) ||
        !readFloat(*editorCamera, "pitch", parsed.editorCamera.pitch, "scene.editorCamera", error) ||
        !readFloat(*editorCamera, "nearPlane", parsed.editorCamera.nearPlane, "scene.editorCamera", error) ||
        !readFloat(*editorCamera, "farPlane", parsed.editorCamera.farPlane, "scene.editorCamera", error)) {
        return false;
    }
    if (!validate(parsed, error)) {
        return false;
    }
    document = std::move(parsed);
    return true;
}

bool ModelSceneSerializer::saveToFile(const std::string& path, const ModelSceneDocument& document, std::string& error) {
    error.clear();
    if (path.empty()) {
        error = "scene save path must not be empty";
        return false;
    }
    if (!validate(document, error)) {
        return false;
    }

    std::error_code filesystemError;
    std::filesystem::path finalPath = std::filesystem::absolute(std::filesystem::u8path(path), filesystemError);
    if (filesystemError) {
        error = "failed to resolve scene save path: " + filesystemError.message();
        return false;
    }
    finalPath = std::filesystem::weakly_canonical(finalPath, filesystemError);
    if (filesystemError) {
        error = "failed to normalize scene save path: " + filesystemError.message();
        return false;
    }
    ModelSceneDocument storedDocument = document;
    for (SceneAssetDocument& asset : storedDocument.assets) {
        std::filesystem::path assetPath =
            std::filesystem::absolute(std::filesystem::u8path(asset.path), filesystemError);
        if (filesystemError) {
            error = "failed to resolve scene asset path for saving: " + filesystemError.message();
            return false;
        }
        assetPath = std::filesystem::weakly_canonical(assetPath, filesystemError);
        if (filesystemError) {
            error = "failed to normalize scene asset path for saving: " + filesystemError.message();
            return false;
        }
        const std::filesystem::path relativePath =
            std::filesystem::relative(assetPath, finalPath.parent_path(), filesystemError);
        if (filesystemError || relativePath.empty()) {
            error = "failed to make scene asset path relative to the scene file";
            return false;
        }
        asset.path = relativePath.generic_u8string();
    }
    std::filesystem::path tempPath = finalPath;
    tempPath += ".tmp";
    std::filesystem::path backupPath = finalPath;
    backupPath += ".bak";
    if (finalPath.has_parent_path()) {
        std::filesystem::create_directories(finalPath.parent_path(), filesystemError);
        if (filesystemError) {
            error = "failed to create scene directory: " + filesystemError.message();
            return false;
        }
    }

    {
        std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            error = "failed to open scene temporary file for writing";
            return false;
        }
        output << serialize(storedDocument).dump(2) << '\n';
        output.flush();
        if (!output.good()) {
            error = "failed while writing scene temporary file";
            output.close();
            std::filesystem::remove(tempPath, filesystemError);
            return false;
        }
    }

    bool movedOriginal = false;
    if (std::filesystem::exists(finalPath, filesystemError)) {
        if (filesystemError) {
            error = "failed to inspect existing scene file: " + filesystemError.message();
            std::filesystem::remove(tempPath, filesystemError);
            return false;
        }
        std::filesystem::remove(backupPath, filesystemError);
        if (filesystemError) {
            error = "failed to replace previous scene backup: " + filesystemError.message();
            std::filesystem::remove(tempPath, filesystemError);
            return false;
        }
        std::filesystem::rename(finalPath, backupPath, filesystemError);
        if (filesystemError) {
            error = "failed to preserve existing scene file: " + filesystemError.message();
            std::filesystem::remove(tempPath, filesystemError);
            return false;
        }
        movedOriginal = true;
    } else if (filesystemError) {
        error = "failed to inspect scene save path: " + filesystemError.message();
        std::filesystem::remove(tempPath, filesystemError);
        return false;
    }

    std::filesystem::rename(tempPath, finalPath, filesystemError);
    if (!filesystemError) {
        return true;
    }
    const std::string renameError = filesystemError.message();
    if (movedOriginal) {
        filesystemError.clear();
        std::filesystem::rename(backupPath, finalPath, filesystemError);
        if (filesystemError) {
            error = "failed to install scene file and restore the original: " + renameError +
                    "; restore error: " + filesystemError.message();
            return false;
        }
    }
    filesystemError.clear();
    std::filesystem::remove(tempPath, filesystemError);
    error = "failed to install scene file: " + renameError;
    return false;
}

bool ModelSceneSerializer::loadFromFile(const std::string& path, ModelSceneDocument& document, std::string& error) {
    error.clear();
    if (path.empty()) {
        error = "scene load path must not be empty";
        return false;
    }
    std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
    if (!input.is_open()) {
        error = "failed to open scene file";
        return false;
    }
    json root = json::parse(input, nullptr, false);
    if (root.is_discarded()) {
        error = "scene file contains invalid JSON";
        return false;
    }
    ModelSceneDocument parsed;
    if (!deserialize(root, parsed, error)) {
        return false;
    }

    std::error_code filesystemError;
    std::filesystem::path scenePath = std::filesystem::absolute(std::filesystem::u8path(path), filesystemError);
    if (filesystemError) {
        error = "failed to resolve scene file path: " + filesystemError.message();
        return false;
    }
    scenePath = std::filesystem::weakly_canonical(scenePath, filesystemError);
    if (filesystemError) {
        error = "failed to normalize scene file path: " + filesystemError.message();
        return false;
    }
    for (SceneAssetDocument& asset : parsed.assets) {
        std::filesystem::path assetPath = std::filesystem::u8path(asset.path);
        if (assetPath.is_relative()) {
            assetPath = scenePath.parent_path() / assetPath;
        }
        assetPath = std::filesystem::weakly_canonical(assetPath, filesystemError);
        if (filesystemError) {
            error = "failed to normalize scene asset path: " + filesystemError.message();
            return false;
        }
        asset.path = assetPath.generic_u8string();
    }
    document = std::move(parsed);
    return true;
}

} // namespace scene
