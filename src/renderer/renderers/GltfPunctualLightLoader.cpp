#include "GltfPunctualLightLoader.h"

#include <glm/gtc/type_ptr.hpp>

#include <nlohmann/json.hpp>

#include "cgltf/cgltf.h"

namespace renderer::assets {

bool GltfPunctualLightDecodeResult::succeeded() const {
    return error == GltfPunctualLightDecodeError::None;
}

GltfPunctualLightDecodeResult decodeGltfPunctualLight(
    const cgltf_node& node) {
    using namespace renderer::contracts;

    GltfPunctualLightDecodeResult result;
    if (node.light == nullptr) {
        result.error = GltfPunctualLightDecodeError::MissingLight;
        return result;
    }

    switch (node.light->type) {
        case cgltf_light_type_directional:
            result.source.type = GpuLightType::Directional;
            result.source.intensityUnit = GpuLightIntensityUnit::Lux;
            break;
        case cgltf_light_type_point:
            result.source.type = GpuLightType::Point;
            result.source.intensityUnit = GpuLightIntensityUnit::Candela;
            break;
        case cgltf_light_type_spot:
            result.source.type = GpuLightType::Spot;
            result.source.intensityUnit = GpuLightIntensityUnit::Candela;
            break;
        case cgltf_light_type_invalid:
        case cgltf_light_type_max_enum:
            result.error = GltfPunctualLightDecodeError::InvalidType;
            return result;
    }

    const bool point = result.source.type == GpuLightType::Point;
    if (!point) {
        result.source.localEmissionDirection = {0.0f, 0.0f, -1.0f};
    }
    result.source.rangeMeters =
        result.source.type == GpuLightType::Directional
            ? 0.0f
            : node.light->range;
    result.source.colorLinear = glm::make_vec3(node.light->color);
    result.source.intensity = node.light->intensity;
    if (result.source.type == GpuLightType::Spot) {
        result.source.innerConeAngleRadians =
            node.light->spot_inner_cone_angle;
        result.source.outerConeAngleRadians =
            node.light->spot_outer_cone_angle;
    }
    if (node.light->extras.data == nullptr) {
        result.error = GltfPunctualLightDecodeError::InvalidShadowPolicy;
        return result;
    }
    const nlohmann::json extras = nlohmann::json::parse(
        node.light->extras.data, nullptr, false);
    if (extras.is_discarded() || !extras.is_object() ||
        !extras.contains("mecraftShadowPolicy") ||
        !extras["mecraftShadowPolicy"].is_string()) {
        result.error = GltfPunctualLightDecodeError::InvalidShadowPolicy;
        return result;
    }
    const std::string policy =
        extras["mecraftShadowPolicy"].get<std::string>();
    if (policy == "none") {
        result.source.shadowPolicy = GpuLightShadowPolicy::None;
    } else if (policy == "raster_dynamic") {
        result.source.shadowPolicy = GpuLightShadowPolicy::RasterDynamic;
    } else if (policy == "raster_cached") {
        result.source.shadowPolicy = GpuLightShadowPolicy::RasterCached;
    } else if (policy == "ray_query") {
        result.source.shadowPolicy = GpuLightShadowPolicy::RayQuery;
    } else {
        result.error = GltfPunctualLightDecodeError::InvalidShadowPolicy;
        return result;
    }

    cgltf_float worldValues[16];
    cgltf_node_transform_world(&node, worldValues);
    const glm::mat4 world = glm::make_mat4(worldValues);
    const AnalyticLightInstantiationResult validation =
        instantiateAnalyticLight(
            result.source, StableLightId{1u}, world,
            glm::vec3(0.0f));
    if (!validation.succeeded()) {
        result.instantiationError = validation.error;
        if (validation.error ==
            AnalyticLightInstantiationError::NormalizationFailed) {
            result.error =
                GltfPunctualLightDecodeError::InvalidPhysicalValue;
            result.normalizationError = validation.normalizationError;
            result.normalizationField = validation.normalizationField;
        } else {
            result.error = GltfPunctualLightDecodeError::InvalidTransform;
        }
        return result;
    }
    if (!point) {
        result.source.localEmissionDirection =
            glm::vec3(validation.sceneLight.light.direction);
    }
    if (result.source.type != GpuLightType::Directional) {
        result.source.localPositionMeters =
            glm::vec3(validation.sceneLight.light.positionAndRange);
    }
    return result;
}

const char* gltfPunctualLightDecodeErrorStableId(
    const GltfPunctualLightDecodeError error) {
    switch (error) {
        case GltfPunctualLightDecodeError::None: return "None";
        case GltfPunctualLightDecodeError::MissingLight:
            return "MissingLight";
        case GltfPunctualLightDecodeError::InvalidType:
            return "InvalidType";
        case GltfPunctualLightDecodeError::InvalidShadowPolicy:
            return "InvalidShadowPolicy";
        case GltfPunctualLightDecodeError::InvalidTransform:
            return "InvalidTransform";
        case GltfPunctualLightDecodeError::InvalidPhysicalValue:
            return "InvalidPhysicalValue";
    }
    return "InvalidGltfPunctualLightDecodeError";
}

} // namespace renderer::assets
