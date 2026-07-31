#include "renderer/contracts/GpuMaterialContract.h"

#include <glm/geometric.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[gpu_material_contract_test] FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool near(const float actual, const float expected, const float tolerance) {
    return std::abs(actual - expected) <= tolerance;
}

bool near(const glm::vec3& actual, const glm::vec3& expected, const float tolerance) {
    return near(actual.x, expected.x, tolerance) && near(actual.y, expected.y, tolerance) &&
           near(actual.z, expected.z, tolerance);
}

bool testGpuLayoutAndPacking() {
    using namespace renderer::contracts;

    GltfMaterialNormalizationInput input;
    input.baseColorFactor = {0.8f, 0.7f, 0.6f, 0.5f};
    input.emissiveFactor = {0.1f, 0.2f, 0.3f};
    input.emissiveStrength = 4.0f;
    input.metallicFactor = 0.75f;
    input.perceptualRoughnessFactor = 0.35f;
    input.normalScale = 0.9f;
    input.occlusionStrength = 0.65f;
    input.dielectricSpecularColorFactor = {0.9f, 0.8f, 0.7f};
    input.dielectricSpecularWeightFactor = 0.6f;
    input.clearcoatFactor = 0.4f;
    input.clearcoatPerceptualRoughnessFactor = 0.2f;
    input.clearcoatNormalScale = 0.8f;
    input.alphaCutoff = 0.45f;
    input.ior = 1.6f;
    input.attenuationColor = {0.7f, 0.8f, 0.9f};
    input.alphaMode = GpuMaterialAlphaMode::Mask;
    input.flags = gpuMaterialFlagBit(GpuMaterialFlag::DoubleSided) | gpuMaterialFlagBit(GpuMaterialFlag::Specular) |
                  gpuMaterialFlagBit(GpuMaterialFlag::Ior) | gpuMaterialFlagBit(GpuMaterialFlag::Clearcoat);
    for (size_t index = 0u; index < input.textureBindings.size(); ++index) {
        input.textureBindings[index] = {static_cast<uint32_t>(100u + index), static_cast<uint32_t>(200u + index)};
    }

    const GpuMaterialNormalizationResult result = normalizeGltfMaterial(input);
    if (!requireTrue(result.succeeded(), "valid glTF factors must normalize") ||
        !requireTrue(result.material.baseColorFactor == input.baseColorFactor,
                     "base color factor must preserve all four components") ||
        !requireTrue(result.material.emissiveFactorAndStrength ==
                         glm::vec4(input.emissiveFactor, input.emissiveStrength),
                     "emissive factor and strength must remain independent") ||
        !requireTrue(result.material.materialFactors == glm::vec4(0.75f, 0.35f, 0.9f, 0.65f),
                     "metallic-roughness factors must use perceptual roughness") ||
        !requireTrue(result.material.transmissionVolumeFactors == glm::vec4(0.0f, 0.0f, 0.45f, 1.6f),
                     "alpha cutoff and IOR must occupy their fixed fields") ||
        !requireTrue(result.material.modesAndFlags.w == kGpuMaterialContractVersion,
                     "every packed material must carry the contract version")) {
        return false;
    }

    for (size_t index = 0u; index < input.textureBindings.size(); ++index) {
        const uint32_t packedTexture = result.material.textureIndices[index / 4u][index % 4u];
        const uint32_t packedSampler = result.material.samplerIndices[index / 4u][index % 4u];
        if (!requireTrue(packedTexture == 100u + index, "texture indices must preserve semantic order") ||
            !requireTrue(packedSampler == 200u + index, "sampler indices must preserve semantic order")) {
            return false;
        }
    }
    return true;
}

bool testStructuredErrors() {
    using namespace renderer::contracts;

    GltfMaterialNormalizationInput input;
    input.perceptualRoughnessFactor = std::numeric_limits<float>::quiet_NaN();
    GpuMaterialNormalizationResult result = normalizeGltfMaterial(input);
    if (!requireTrue(result.error == GpuMaterialNormalizationError::NonFiniteValue &&
                         result.field == GpuMaterialField::PerceptualRoughnessFactor,
                     "non-finite roughness must report its stable field") ||
        !requireTrue(std::string(gpuMaterialNormalizationErrorStableId(result.error)) == "NonFiniteValue",
                     "normalization errors must expose stable identifiers")) {
        return false;
    }

    input = {};
    input.flags = 1u << 31u;
    result = normalizeGltfMaterial(input);
    if (!requireTrue(result.error == GpuMaterialNormalizationError::UnknownFlags &&
                         result.field == GpuMaterialField::Flags,
                     "unknown material capability bits must be rejected")) {
        return false;
    }

    input = {};
    input.workflow = GpuMaterialWorkflow::SpecularGlossiness;
    input.flags = gpuMaterialFlagBit(GpuMaterialFlag::Clearcoat);
    result = normalizeGltfMaterial(input);
    if (!requireTrue(result.error == GpuMaterialNormalizationError::IncompatibleWorkflowExtensions,
                     "legacy workflow must reject metallic-roughness extensions")) {
        return false;
    }

    input = {};
    input.alphaMode = GpuMaterialAlphaMode::Transmission;
    result = normalizeGltfMaterial(input);
    if (!requireTrue(result.error == GpuMaterialNormalizationError::TransmissionAlphaConflict,
                     "transmission mode must require the transmission capability")) {
        return false;
    }

    input = {};
    input.alphaMode = GpuMaterialAlphaMode::Transmission;
    input.flags = gpuMaterialFlagBit(GpuMaterialFlag::Transmission) | gpuMaterialFlagBit(GpuMaterialFlag::Volume);
    input.transmissionFactor = 0.8f;
    input.thicknessFactor = 0.25f;
    input.attenuationDistance = 3.5f;
    result = normalizeGltfMaterial(input);
    if (!requireTrue(result.succeeded(), "finite transmission volume must normalize") ||
        !requireTrue(result.material.attenuationColorAndDistance.w == 3.5f,
                     "finite attenuation distance must remain explicit")) {
        return false;
    }

    input.attenuationDistance = 0.0f;
    input.flags |= gpuMaterialFlagBit(GpuMaterialFlag::InfiniteAttenuationDistance);
    result = normalizeGltfMaterial(input);
    if (!requireTrue(result.succeeded(), "infinite attenuation must use an explicit flag") ||
        !requireTrue(hasGpuMaterialFlag(result.material.modesAndFlags.z, GpuMaterialFlag::InfiniteAttenuationDistance),
                     "packed flags must preserve infinite attenuation")) {
        return false;
    }

    input.flags &= ~gpuMaterialFlagBit(GpuMaterialFlag::InfiniteAttenuationDistance);
    result = normalizeGltfMaterial(input);
    return requireTrue(result.error == GpuMaterialNormalizationError::ValueOutOfRange &&
                           result.field == GpuMaterialField::AttenuationDistance,
                       "finite volume attenuation distance must be strictly positive");
}

bool testGltfNumericDomains() {
    using namespace renderer::contracts;

    GltfMaterialNormalizationInput input;
    input.normalScale = -2.0f;
    input.clearcoatNormalScale = -0.5f;
    input.alphaCutoff = 1.25f;
    const GpuMaterialNormalizationResult result = normalizeGltfMaterial(input);
    return requireTrue(result.succeeded(), "glTF normal scales and alpha cutoff must preserve their schema domains") &&
           requireTrue(result.material.materialFactors.z == -2.0f, "negative normal scale must remain unchanged") &&
           requireTrue(result.material.clearcoatFactors.z == -0.5f,
                       "negative clearcoat normal scale must remain unchanged") &&
           requireTrue(result.material.transmissionVolumeFactors.z == 1.25f,
                       "alpha cutoff above one must remain unchanged");
}

bool testTextureSemantics() {
    using namespace renderer::contracts;

    if (!requireTrue(
            gpuMaterialTextureUsesSrgb(GpuMaterialTextureSemantic::BaseColor, GpuMaterialWorkflow::MetallicRoughness),
            "base color textures must use sRGB sampling") ||
        !requireTrue(!gpuMaterialTextureUsesSrgb(GpuMaterialTextureSemantic::MetallicRoughnessOrSpecularGlossiness,
                                                 GpuMaterialWorkflow::MetallicRoughness),
                     "metallic-roughness textures must use linear sampling") ||
        !requireTrue(gpuMaterialTextureUsesSrgb(GpuMaterialTextureSemantic::MetallicRoughnessOrSpecularGlossiness,
                                                GpuMaterialWorkflow::SpecularGlossiness),
                     "specular-glossiness textures must use sRGB sampling") ||
        !requireTrue(gpuMaterialDefaultTexturePixel(GpuMaterialTextureSemantic::Normal) ==
                         std::array<uint8_t, 4>{128u, 128u, 255u, 255u},
                     "normal texture identity must encode positive tangent Z") ||
        !requireTrue(gpuMaterialDefaultTexturePixel(GpuMaterialTextureSemantic::Transmission) ==
                         std::array<uint8_t, 4>{255u, 255u, 255u, 255u},
                     "multiplicative property texture identities must be white")) {
        return false;
    }
    return true;
}

bool testLabPbrReferenceVectors() {
    using namespace renderer::contracts;

    const LabPbrNormalSample neutralNormal = decodeLabPbrNormal({128u, 128u, 255u, 255u});
    if (!requireTrue(near(neutralNormal.tangentNormal, {0.00392157f, -0.00392157f, 0.9999846f}, 1.0e-5f),
                     "LabPBR normal decoding must reconstruct Z and invert Y") ||
        !requireTrue(near(neutralNormal.materialAo, 1.0f, 1.0e-6f), "normal blue must decode material AO") ||
        !requireTrue(near(neutralNormal.height, 1.0f, 1.0e-6f), "normal alpha must preserve encoded height")) {
        return false;
    }

    const LabPbrSpecularDecodeResult stone = decodeLabPbrSpecular({65u, 10u, 0u, 255u}, {0.4f, 0.4f, 0.4f});
    if (!requireTrue(stone.succeeded(), "dielectric LabPBR vector must decode") ||
        !requireTrue(near(stone.sample.perceptualRoughness, 190.0f / 255.0f, 1.0e-6f),
                     "smoothness must become perceptual roughness once") ||
        !requireTrue(near(stone.sample.f0, glm::vec3(10.0f / 255.0f), 1.0e-6f),
                     "LabPBR 1.3 dielectric F0 must remain linear") ||
        !requireTrue(stone.sample.porosity == 0.0f && stone.sample.subsurface == 0.0f &&
                         stone.sample.emission == 0.0f && !stone.sample.emissionProvided,
                     "alpha 255 must record that emission was not provided")) {
        return false;
    }

    const LabPbrSpecularDecodeResult glowstone = decodeLabPbrSpecular({176u, 5u, 230u, 204u}, {0.8f, 0.6f, 0.3f});
    if (!requireTrue(glowstone.succeeded(), "emissive LabPBR vector must decode") ||
        !requireTrue(near(glowstone.sample.subsurface, 165.0f / 190.0f, 1.0e-6f),
                     "blue values above 64 must decode subsurface") ||
        !requireTrue(near(glowstone.sample.emission, 204.0f / 254.0f, 1.0e-6f),
                     "alpha 204 must map linearly to emission") ||
        !requireTrue(glowstone.sample.emissionProvided, "alpha values through 254 must provide emission")) {
        return false;
    }

    const LabPbrSpecularDecodeResult iron = decodeLabPbrSpecular({225u, 230u, 0u, 255u}, {0.2f, 0.3f, 0.4f});
    if (!requireTrue(iron.succeeded() && iron.sample.metalId == 230u && iron.sample.metalness == 1.0f,
                     "metal ID 230 must identify iron") ||
        !requireTrue(near(iron.sample.f0, {0.531229f, 0.512357f, 0.495829f}, 2.0e-6f),
                     "iron must use its RGB conductor F0")) {
        return false;
    }

    const glm::vec3 customMetalColor{0.7f, 0.4f, 0.2f};
    const LabPbrSpecularDecodeResult customMetal = decodeLabPbrSpecular({220u, 255u, 12u, 255u}, customMetalColor);
    if (!requireTrue(customMetal.succeeded() && customMetal.sample.f0 == customMetalColor,
                     "metal ID 255 must use linear base color as F0") ||
        !requireTrue(near(customMetal.sample.porosity, 12.0f / 64.0f, 1.0e-6f),
                     "blue values through 64 must decode porosity") ||
        !requireTrue(!customMetal.sample.emissionProvided, "custom metal alpha 255 must not override emission")) {
        return false;
    }

    const LabPbrSpecularDecodeResult reserved = decodeLabPbrSpecular({0u, 254u, 0u, 255u}, {0.5f, 0.5f, 0.5f});
    return requireTrue(reserved.error == GpuMaterialNormalizationError::UnsupportedLabPbrMetalId &&
                           reserved.field == GpuMaterialField::LabPbrMetalId,
                       "undefined LabPBR metal IDs must produce a structured error");
}

bool testLabPbrBlockHeightNormalization() {
    using namespace renderer::contracts;

    std::array<uint8_t, 12> authored = {
        10u, 20u, 30u, 64u, 40u, 50u, 60u, 128u, 70u, 80u, 90u, 192u,
    };
    normalizeLabPbrBlockHeightRange(authored.data(), 3u, true);
    if (!requireTrue(authored[3] == 0u && authored[7] == 128u && authored[11] == 255u,
                     "block POM height must expand the authored alpha range") ||
        !requireTrue(authored[0] == 10u && authored[5] == 50u && authored[10] == 90u,
                     "height normalization must preserve normal and AO channels")) {
        return false;
    }

    std::array<uint8_t, 8> constant = {
        128u, 128u, 255u, 96u, 128u, 128u, 255u, 96u,
    };
    normalizeLabPbrBlockHeightRange(constant.data(), 2u, true);
    if (!requireTrue(constant[3] == 255u && constant[7] == 255u, "constant-height tiles must encode a flat surface")) {
        return false;
    }

    std::array<uint8_t, 4> rgbSource = {128u, 128u, 255u, 0u};
    normalizeLabPbrBlockHeightRange(rgbSource.data(), 1u, false);
    return requireTrue(rgbSource[3] == 255u, "alpha-less normal maps must encode a flat surface");
}

bool testShaderLayoutMirror() {
    const std::string shaderPath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/assets/shaders/gpu_material_contract.glsl";
    std::ifstream stream(shaderPath, std::ios::binary);
    if (!requireTrue(stream.is_open(), "GPU material GLSL contract must be readable")) {
        return false;
    }
    const std::string source((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (!requireTrue(source.find("GPU_MATERIAL_CONTRACT_VERSION = 1u") != std::string::npos,
                     "GLSL material contract must mirror the CPU version") ||
        !requireTrue(source.find("GPU_MATERIAL_TEXTURE_THICKNESS = 11u") != std::string::npos,
                     "GLSL texture semantics must mirror the CPU order") ||
        !requireTrue(source.find("GPU_MATERIAL_FLAG_INFINITE_ATTENUATION_DISTANCE") != std::string::npos,
                     "GLSL flags must mirror explicit infinite attenuation")) {
        return false;
    }
    constexpr std::array<const char*, 12> kOrderedFields{
        {"vec4 baseColorFactor;", "vec4 emissiveFactorAndStrength;", "vec4 materialFactors;",
         "vec4 specularGlossinessFactors;", "vec4 dielectricSpecularFactors;", "vec4 clearcoatFactors;",
         "vec4 transmissionVolumeFactors;", "vec4 attenuationColorAndDistance;", "vec4 gameplaySurfaceFactors;",
         "uvec4 textureIndices[3];", "uvec4 samplerIndices[3];", "uvec4 modesAndFlags;"}};
    size_t offset = 0u;
    for (const char* field : kOrderedFields) {
        const size_t found = source.find(field, offset);
        if (!requireTrue(found != std::string::npos, "GLSL material layout must mirror every CPU field")) {
            return false;
        }
        offset = found + std::string(field).size();
    }
    return true;
}

bool testSharedShaderIncludes() {
    const auto readSource = [](const char* relativePath, std::string& source) {
        const std::string path = std::string(MECRAFT_TEST_SOURCE_DIR) + "/" + relativePath;
        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open()) {
            return false;
        }
        source.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        return true;
    };

    std::string materialDecode;
    std::string pbrBrdf;
    if (!requireTrue(readSource("assets/shaders/material_decode.glsl", materialDecode),
                     "shared material decode include must be readable") ||
        !requireTrue(readSource("assets/shaders/pbr_brdf.glsl", pbrBrdf), "shared PBR include must be readable")) {
        return false;
    }

    if (!requireTrue(materialDecode.find("MaterialSample decodeGltfMaterial") != std::string::npos,
                     "material include must expose glTF material decoding") ||
        !requireTrue(materialDecode.find("decodeMaterialTangentNormal") != std::string::npos,
                     "material include must expose tangent normal decoding") ||
        !requireTrue(materialDecode.find("materialPassesAlphaTest") != std::string::npos,
                     "material include must expose the shared alpha test") ||
        !requireTrue(materialDecode.find("evaluateMaterialEmission") != std::string::npos,
                     "material include must expose shared emission evaluation") ||
        !requireTrue(pbrBrdf.find("pbrFresnelSchlick") != std::string::npos,
                     "PBR include must expose shared Schlick Fresnel") ||
        !requireTrue(pbrBrdf.find("pbrDistributionGgx") != std::string::npos,
                     "PBR include must expose shared GGX distribution") ||
        !requireTrue(pbrBrdf.find("pbrSmithGgxCorrelatedVisibility") != std::string::npos,
                     "PBR include must expose correlated Smith visibility") ||
        !requireTrue(pbrBrdf.find("pbrEvaluateDirectSpecular") != std::string::npos,
                     "PBR include must expose direct specular evaluation")) {
        return false;
    }

    constexpr std::array<const char*, 4> kForbiddenTokens{"sampler", "texture(", "uniform", "layout("};
    for (const char* token : kForbiddenTokens) {
        if (!requireTrue(materialDecode.find(token) == std::string::npos,
                         "material decode include must not depend on shader resources") ||
            !requireTrue(pbrBrdf.find(token) == std::string::npos, "PBR include must not depend on shader resources")) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    if (!testGpuLayoutAndPacking() || !testStructuredErrors() || !testGltfNumericDomains() || !testTextureSemantics() ||
        !testLabPbrReferenceVectors() || !testLabPbrBlockHeightNormalization() || !testShaderLayoutMirror() ||
        !testSharedShaderIncludes()) {
        return 1;
    }
    std::cout << "[gpu_material_contract_test] PASS\n";
    return 0;
}
