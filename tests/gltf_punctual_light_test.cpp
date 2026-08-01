#include "renderer/renderers/GltfPunctualLightLoader.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "cgltf/cgltf.h"
#include <glm/gtc/matrix_transform.hpp>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[gltf_punctual_light_test] FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool near(const float actual, const float expected, const float tolerance = 1.0e-5f) {
    return std::abs(actual - expected) <= tolerance;
}

bool testParsedPunctualLights() {
    const std::string assetPath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/tests/assets/punctual_lights.gltf";
    cgltf_options options{};
    cgltf_data* rawData = nullptr;
    cgltf_result parseResult = cgltf_parse_file(&options, assetPath.c_str(), &rawData);
    if (!requireTrue(parseResult == cgltf_result_success && rawData != nullptr,
                     "the punctual-light glTF asset must parse")) {
        return false;
    }
    const auto freeData = [](cgltf_data* data) {
        cgltf_free(data);
    };
    std::unique_ptr<cgltf_data, decltype(freeData)> data(rawData, freeData);
    if (!requireTrue(cgltf_load_buffers(&options, data.get(), assetPath.c_str()) == cgltf_result_success &&
                         cgltf_validate(data.get()) == cgltf_result_success,
                     "the punctual-light glTF asset must satisfy cgltf validation") ||
        !requireTrue(data->extensions_required_count == 1u &&
                         std::strcmp(data->extensions_required[0], "KHR_lights_punctual") == 0,
                     "the test asset must require KHR_lights_punctual")) {
        return false;
    }

    std::vector<const cgltf_node*> nodes;
    for (cgltf_size index = 0u; index < data->scene->nodes_count; ++index) {
        nodes.push_back(data->scene->nodes[index]);
    }
    std::vector<renderer::contracts::AnalyticLightSourceDefinition> sources;
    for (std::size_t index = 0u; index < nodes.size(); ++index) {
        const cgltf_node* node = nodes[index];
        if (node->light != nullptr) {
            const renderer::assets::GltfPunctualLightDecodeResult decoded =
                renderer::assets::decodeGltfPunctualLight(*node);
            if (!requireTrue(decoded.succeeded(), "every default-scene light node must decode")) {
                return false;
            }
            sources.push_back(decoded.source);
        }
        for (cgltf_size child = 0u; child < node->children_count; ++child) {
            nodes.push_back(node->children[child]);
        }
    }

    using namespace renderer::contracts;
    if (!requireTrue(sources.size() == 3u, "point, spot, and directional nodes must all decode") ||
        !requireTrue(
            sources[0].type == GpuLightType::Point && sources[0].intensityUnit == GpuLightIntensityUnit::Candela &&
                sources[0].shadowPolicy == GpuLightShadowPolicy::RasterDynamic &&
                sources[0].localPositionMeters == glm::vec3(1.0f, 2.0f, 3.0f) && sources[0].rangeMeters == 8.0f,
            "point lights must preserve glTF position, candela, and range") ||
        !requireTrue(
            sources[1].type == GpuLightType::Spot && sources[1].shadowPolicy == GpuLightShadowPolicy::RasterDynamic &&
                near(sources[1].localEmissionDirection.x, -1.0f) && near(sources[1].localEmissionDirection.y, 0.0f) &&
                near(sources[1].localEmissionDirection.z, 0.0f) && near(sources[1].innerConeAngleRadians, 0.2f) &&
                near(sources[1].outerConeAngleRadians, 0.6f),
            "spot lights must transform local minus-Z and preserve cone angles") ||
        !requireTrue(
            sources[2].type == GpuLightType::Directional && sources[2].intensityUnit == GpuLightIntensityUnit::Lux &&
                sources[2].shadowPolicy == GpuLightShadowPolicy::None &&
                near(sources[2].localEmissionDirection.x, 0.0f) && near(sources[2].localEmissionDirection.y, 1.0f) &&
                near(sources[2].localEmissionDirection.z, 0.0f),
            "directional lights must use lux and transformed local minus-Z")) {
        return false;
    }

    const AnalyticLightInstantiationResult instantiated = instantiateAnalyticLight(
        sources[0], StableLightId{101u}, glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 0.0f, -2.0f)),
        glm::vec3(1.0f, 1.0f, 1.0f));
    return requireTrue(instantiated.succeeded(), "decoded glTF lights must instantiate into GPU records") &&
           requireTrue(glm::vec3(instantiated.sceneLight.light.positionAndRange) == glm::vec3(4.0f, 1.0f, 0.0f),
                       "glTF instance lights must become camera-relative") &&
           requireTrue(instantiated.sceneLight.light.classificationAndIdentity.y == 101u,
                       "glTF instance lights must preserve their stable identity") &&
           requireTrue(instantiated.sceneLight.requestedShadowPolicy == GpuLightShadowPolicy::RasterDynamic &&
                           instantiated.sceneLight.light.classificationAndIdentity.z ==
                               static_cast<uint32_t>(GpuLightShadowPolicy::None),
                       "glTF shadow requests must remain unallocated scene metadata");
}

bool testValidationAtriumAsset() {
    const std::string assetPath =
        std::string(MECRAFT_TEST_SOURCE_DIR) + "/assets/models/validation/M03SponzaAtrium.gltf";
    cgltf_options options{};
    cgltf_data* rawData = nullptr;
    const cgltf_result parseResult = cgltf_parse_file(&options, assetPath.c_str(), &rawData);
    if (!requireTrue(parseResult == cgltf_result_success && rawData != nullptr,
                     "the M03 validation atrium must parse")) {
        return false;
    }
    const auto freeData = [](cgltf_data* data) {
        cgltf_free(data);
    };
    std::unique_ptr<cgltf_data, decltype(freeData)> data(rawData, freeData);
    if (!requireTrue(cgltf_load_buffers(&options, data.get(), assetPath.c_str()) == cgltf_result_success &&
                         cgltf_validate(data.get()) == cgltf_result_success && data->scene != nullptr,
                     "the M03 validation atrium must satisfy cgltf validation")) {
        return false;
    }

    bool requiresPunctualLights = false;
    bool requiresEmissiveStrength = false;
    for (cgltf_size index = 0u; index < data->extensions_required_count; ++index) {
        const char* extension = data->extensions_required[index];
        requiresPunctualLights = requiresPunctualLights || std::strcmp(extension, "KHR_lights_punctual") == 0;
        requiresEmissiveStrength =
            requiresEmissiveStrength || std::strcmp(extension, "KHR_materials_emissive_strength") == 0;
    }

    std::size_t emissiveMaterialCount = 0u;
    for (cgltf_size index = 0u; index < data->materials_count; ++index) {
        emissiveMaterialCount += data->materials[index].has_emissive_strength != 0 ? 1u : 0u;
    }

    std::vector<const cgltf_node*> nodes;
    for (cgltf_size index = 0u; index < data->scene->nodes_count; ++index) {
        nodes.push_back(data->scene->nodes[index]);
    }
    std::size_t pointLightCount = 0u;
    std::size_t spotLightCount = 0u;
    std::size_t cachedShadowCount = 0u;
    std::size_t dynamicShadowCount = 0u;
    for (std::size_t index = 0u; index < nodes.size(); ++index) {
        const cgltf_node* node = nodes[index];
        if (node->light != nullptr) {
            const renderer::assets::GltfPunctualLightDecodeResult decoded =
                renderer::assets::decodeGltfPunctualLight(*node);
            if (!requireTrue(decoded.succeeded(), "every M03 punctual light must decode")) {
                return false;
            }
            pointLightCount += decoded.source.type == renderer::contracts::GpuLightType::Point ? 1u : 0u;
            spotLightCount += decoded.source.type == renderer::contracts::GpuLightType::Spot ? 1u : 0u;
            cachedShadowCount +=
                decoded.source.shadowPolicy == renderer::contracts::GpuLightShadowPolicy::RasterCached ? 1u : 0u;
            dynamicShadowCount +=
                decoded.source.shadowPolicy == renderer::contracts::GpuLightShadowPolicy::RasterDynamic ? 1u : 0u;
        }
        for (cgltf_size child = 0u; child < node->children_count; ++child) {
            nodes.push_back(node->children[child]);
        }
    }

    return requireTrue(requiresPunctualLights && requiresEmissiveStrength,
                       "M03 must require punctual-light and emissive-strength extensions") &&
           requireTrue(data->lights_count == 4u && pointLightCount == 3u && spotLightCount == 1u,
                       "M03 must lock three point lights and one spot light") &&
           requireTrue(cachedShadowCount == 2u && dynamicShadowCount == 2u,
                       "M03 must lock cached and dynamic local-shadow coverage") &&
           requireTrue(emissiveMaterialCount == 2u, "M03 must lock two emissive fixture materials");
}

bool testStructuredDecodeFailure() {
    cgltf_light light{};
    light.type = cgltf_light_type_point;
    light.color[0] = 1.0f;
    light.color[1] = 1.0f;
    light.color[2] = 1.0f;
    light.intensity = 10.0f;
    light.range = 0.0f;
    cgltf_node node{};
    node.light = &light;
    node.has_matrix = 1;
    node.matrix[0] = 1.0f;
    node.matrix[5] = 1.0f;
    node.matrix[10] = 1.0f;
    node.matrix[15] = 1.0f;
    const renderer::assets::GltfPunctualLightDecodeResult missingPolicy =
        renderer::assets::decodeGltfPunctualLight(node);
    if (!requireTrue(missingPolicy.error == renderer::assets::GltfPunctualLightDecodeError::InvalidShadowPolicy,
                     "glTF lights must declare an explicit shadow policy")) {
        return false;
    }

    char invalidShadowPolicyExtras[] = "{\"mecraftShadowPolicy\":\"automatic\"}";
    light.extras.data = invalidShadowPolicyExtras;
    const renderer::assets::GltfPunctualLightDecodeResult invalidPolicy =
        renderer::assets::decodeGltfPunctualLight(node);
    if (!requireTrue(invalidPolicy.error == renderer::assets::GltfPunctualLightDecodeError::InvalidShadowPolicy,
                     "unknown glTF shadow policies must fail explicitly")) {
        return false;
    }

    char shadowPolicyExtras[] = "{\"mecraftShadowPolicy\":\"none\"}";
    light.extras.data = shadowPolicyExtras;
    const renderer::assets::GltfPunctualLightDecodeResult invalidRange =
        renderer::assets::decodeGltfPunctualLight(node);
    if (!requireTrue(invalidRange.error == renderer::assets::GltfPunctualLightDecodeError::InvalidPhysicalValue,
                     "unbounded local lights must fail the finite clustered-light contract") ||
        !requireTrue(invalidRange.normalizationField == renderer::contracts::GpuLightField::Range,
                     "glTF physical failures must preserve the failed field")) {
        return false;
    }

    light.range = 4.0f;
    node.matrix[4] = 0.5f;
    const renderer::assets::GltfPunctualLightDecodeResult invalidTransform =
        renderer::assets::decodeGltfPunctualLight(node);
    return requireTrue(invalidTransform.error == renderer::assets::GltfPunctualLightDecodeError::InvalidTransform,
                       "sheared glTF light nodes must fail explicitly") &&
           requireTrue(invalidTransform.instantiationError ==
                           renderer::contracts::AnalyticLightInstantiationError::ShearedTransform,
                       "glTF transform failures must preserve the instantiation error");
}

bool testStaticMeshIntegrationSource() {
    const std::string sourcePath =
        std::string(MECRAFT_TEST_SOURCE_DIR) + "/src/renderer/renderers/StaticMeshRenderer.cpp";
    std::ifstream stream(sourcePath, std::ios::binary);
    if (!requireTrue(stream.is_open(), "StaticMeshRenderer source must be readable")) {
        return false;
    }
    const std::string source((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return requireTrue(source.find("KHR_lights_punctual") != std::string::npos,
                       "the static asset extension contract must accept punctual lights") &&
           requireTrue(source.find("decodeGltfPunctualLight(*node)") != std::string::npos,
                       "the default-scene traversal must decode punctual light nodes");
}

} // namespace

int main() {
    if (!testParsedPunctualLights() || !testValidationAtriumAsset() || !testStructuredDecodeFailure() ||
        !testStaticMeshIntegrationSource()) {
        return EXIT_FAILURE;
    }
    std::cout << "[gltf_punctual_light_test] PASS\n";
    return EXIT_SUCCESS;
}
