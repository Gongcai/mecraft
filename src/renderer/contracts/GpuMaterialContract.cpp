#include "renderer/contracts/GpuMaterialContract.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace renderer::contracts {
namespace {

constexpr float kByteToUnit = 1.0f / 255.0f;

[[nodiscard]] bool finite(const float value) {
    return std::isfinite(value);
}

[[nodiscard]] bool finite(const glm::vec3& value) {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

[[nodiscard]] bool finite(const glm::vec4& value) {
    return finite(value.x) && finite(value.y) && finite(value.z) && finite(value.w);
}

[[nodiscard]] bool inClosedUnitRange(const float value) {
    return value >= 0.0f && value <= 1.0f;
}

[[nodiscard]] bool inClosedUnitRange(const glm::vec3& value) {
    return inClosedUnitRange(value.x) && inClosedUnitRange(value.y) && inClosedUnitRange(value.z);
}

[[nodiscard]] bool inClosedUnitRange(const glm::vec4& value) {
    return inClosedUnitRange(value.x) && inClosedUnitRange(value.y) && inClosedUnitRange(value.z) &&
           inClosedUnitRange(value.w);
}

[[nodiscard]] bool validAlphaMode(const GpuMaterialAlphaMode mode) {
    switch (mode) {
    case GpuMaterialAlphaMode::Opaque:
    case GpuMaterialAlphaMode::Mask:
    case GpuMaterialAlphaMode::Blend:
    case GpuMaterialAlphaMode::Transmission:
    case GpuMaterialAlphaMode::Additive: return true;
    }
    return false;
}

[[nodiscard]] bool validWorkflow(const GpuMaterialWorkflow workflow) {
    switch (workflow) {
    case GpuMaterialWorkflow::MetallicRoughness:
    case GpuMaterialWorkflow::SpecularGlossiness:
    case GpuMaterialWorkflow::LabPbr: return true;
    }
    return false;
}

[[nodiscard]] GpuMaterialNormalizationResult failure(const GpuMaterialNormalizationError error,
                                                     const GpuMaterialField field) {
    GpuMaterialNormalizationResult result;
    result.error = error;
    result.field = field;
    return result;
}

struct MaterialFieldValidation final {
    GpuMaterialNormalizationError error = GpuMaterialNormalizationError::None;
    GpuMaterialField field = GpuMaterialField::None;

    [[nodiscard]] bool succeeded() const { return error == GpuMaterialNormalizationError::None; }
};

[[nodiscard]] MaterialFieldValidation validateUnitValue(const float value, const GpuMaterialField field) {
    if (!finite(value)) {
        return {GpuMaterialNormalizationError::NonFiniteValue, field};
    }
    if (!inClosedUnitRange(value)) {
        return {GpuMaterialNormalizationError::ValueOutOfRange, field};
    }
    return {};
}

[[nodiscard]] MaterialFieldValidation validateUnitValue(const glm::vec3& value, const GpuMaterialField field) {
    if (!finite(value)) {
        return {GpuMaterialNormalizationError::NonFiniteValue, field};
    }
    if (!inClosedUnitRange(value)) {
        return {GpuMaterialNormalizationError::ValueOutOfRange, field};
    }
    return {};
}

[[nodiscard]] MaterialFieldValidation validateUnitValue(const glm::vec4& value, const GpuMaterialField field) {
    if (!finite(value)) {
        return {GpuMaterialNormalizationError::NonFiniteValue, field};
    }
    if (!inClosedUnitRange(value)) {
        return {GpuMaterialNormalizationError::ValueOutOfRange, field};
    }
    return {};
}

[[nodiscard]] glm::vec3 conductorF0(const glm::vec3& n, const glm::vec3& k) {
    const glm::vec3 numerator = (n - glm::vec3(1.0f)) * (n - glm::vec3(1.0f)) + k * k;
    const glm::vec3 denominator = (n + glm::vec3(1.0f)) * (n + glm::vec3(1.0f)) + k * k;
    return numerator / denominator;
}

[[nodiscard]] glm::vec3 definedLabPbrMetalF0(const uint8_t metalId) {
    switch (metalId) {
    case 230u: return conductorF0({2.9114f, 2.9497f, 2.5845f}, {3.0893f, 2.9318f, 2.7670f});
    case 231u: return conductorF0({0.18299f, 0.42108f, 1.3734f}, {3.4242f, 2.3459f, 1.7704f});
    case 232u: return conductorF0({1.3456f, 0.96521f, 0.61722f}, {7.4746f, 6.3995f, 5.3031f});
    case 233u: return conductorF0({3.1071f, 3.1812f, 2.3230f}, {3.3314f, 3.3291f, 3.1350f});
    case 234u: return conductorF0({0.27105f, 0.67693f, 1.3164f}, {3.6092f, 2.6248f, 2.2921f});
    case 235u: return conductorF0({1.9100f, 1.8300f, 1.4400f}, {3.5100f, 3.4000f, 3.1800f});
    case 236u: return conductorF0({2.3757f, 2.0847f, 1.8453f}, {4.2655f, 3.7153f, 3.1365f});
    case 237u: return conductorF0({0.15943f, 0.14512f, 0.13547f}, {3.9291f, 3.1900f, 2.3808f});
    default: std::abort();
    }
}

void setPackedBinding(std::array<glm::uvec4, 3>& packed, const size_t semanticIndex, const uint32_t value) {
    packed[semanticIndex / 4u][semanticIndex % 4u] = value;
}

} // namespace

bool GpuMaterialNormalizationResult::succeeded() const {
    return error == GpuMaterialNormalizationError::None;
}

bool LabPbrSpecularDecodeResult::succeeded() const {
    return error == GpuMaterialNormalizationError::None;
}

const char* gpuMaterialNormalizationErrorStableId(const GpuMaterialNormalizationError error) {
    switch (error) {
    case GpuMaterialNormalizationError::None: return "None";
    case GpuMaterialNormalizationError::InvalidAlphaMode: return "InvalidAlphaMode";
    case GpuMaterialNormalizationError::InvalidWorkflow: return "InvalidWorkflow";
    case GpuMaterialNormalizationError::UnknownFlags: return "UnknownFlags";
    case GpuMaterialNormalizationError::NonFiniteValue: return "NonFiniteValue";
    case GpuMaterialNormalizationError::ValueOutOfRange: return "ValueOutOfRange";
    case GpuMaterialNormalizationError::IncompatibleWorkflowExtensions: return "IncompatibleWorkflowExtensions";
    case GpuMaterialNormalizationError::VolumeRequiresTransmission: return "VolumeRequiresTransmission";
    case GpuMaterialNormalizationError::TransmissionAlphaConflict: return "TransmissionAlphaConflict";
    case GpuMaterialNormalizationError::AttenuationDistanceConflict: return "AttenuationDistanceConflict";
    case GpuMaterialNormalizationError::UnsupportedLabPbrMetalId: return "UnsupportedLabPbrMetalId";
    case GpuMaterialNormalizationError::InvalidLabPbrBaseColor: return "InvalidLabPbrBaseColor";
    }
    std::abort();
}

const char* gpuMaterialFieldStableId(const GpuMaterialField field) {
    switch (field) {
    case GpuMaterialField::None: return "None";
    case GpuMaterialField::AlphaMode: return "AlphaMode";
    case GpuMaterialField::Workflow: return "Workflow";
    case GpuMaterialField::BaseColorFactor: return "BaseColorFactor";
    case GpuMaterialField::EmissiveFactor: return "EmissiveFactor";
    case GpuMaterialField::EmissiveStrength: return "EmissiveStrength";
    case GpuMaterialField::MetallicFactor: return "MetallicFactor";
    case GpuMaterialField::PerceptualRoughnessFactor: return "PerceptualRoughnessFactor";
    case GpuMaterialField::NormalScale: return "NormalScale";
    case GpuMaterialField::OcclusionStrength: return "OcclusionStrength";
    case GpuMaterialField::SpecularGlossinessFactor: return "SpecularGlossinessFactor";
    case GpuMaterialField::GlossinessFactor: return "GlossinessFactor";
    case GpuMaterialField::DielectricSpecularColorFactor: return "DielectricSpecularColorFactor";
    case GpuMaterialField::DielectricSpecularWeightFactor: return "DielectricSpecularWeightFactor";
    case GpuMaterialField::ClearcoatFactor: return "ClearcoatFactor";
    case GpuMaterialField::ClearcoatPerceptualRoughnessFactor: return "ClearcoatPerceptualRoughnessFactor";
    case GpuMaterialField::ClearcoatNormalScale: return "ClearcoatNormalScale";
    case GpuMaterialField::TransmissionFactor: return "TransmissionFactor";
    case GpuMaterialField::ThicknessFactor: return "ThicknessFactor";
    case GpuMaterialField::AlphaCutoff: return "AlphaCutoff";
    case GpuMaterialField::Ior: return "Ior";
    case GpuMaterialField::AttenuationColor: return "AttenuationColor";
    case GpuMaterialField::AttenuationDistance: return "AttenuationDistance";
    case GpuMaterialField::GameplaySurfaceFactors: return "GameplaySurfaceFactors";
    case GpuMaterialField::Flags: return "Flags";
    case GpuMaterialField::LabPbrMetalId: return "LabPbrMetalId";
    case GpuMaterialField::LabPbrBaseColor: return "LabPbrBaseColor";
    }
    std::abort();
}

bool gpuMaterialTextureUsesSrgb(const GpuMaterialTextureSemantic semantic, const GpuMaterialWorkflow workflow) {
    switch (semantic) {
    case GpuMaterialTextureSemantic::BaseColor:
    case GpuMaterialTextureSemantic::Emissive:
    case GpuMaterialTextureSemantic::SpecularColor: return true;
    case GpuMaterialTextureSemantic::MetallicRoughnessOrSpecularGlossiness:
        return workflow == GpuMaterialWorkflow::SpecularGlossiness;
    case GpuMaterialTextureSemantic::Normal:
    case GpuMaterialTextureSemantic::Occlusion:
    case GpuMaterialTextureSemantic::SpecularWeight:
    case GpuMaterialTextureSemantic::Clearcoat:
    case GpuMaterialTextureSemantic::ClearcoatRoughness:
    case GpuMaterialTextureSemantic::ClearcoatNormal:
    case GpuMaterialTextureSemantic::Transmission:
    case GpuMaterialTextureSemantic::Thickness: return false;
    case GpuMaterialTextureSemantic::Count: std::abort();
    }
    std::abort();
}

std::array<uint8_t, 4> gpuMaterialDefaultTexturePixel(const GpuMaterialTextureSemantic semantic) {
    switch (semantic) {
    case GpuMaterialTextureSemantic::Normal:
    case GpuMaterialTextureSemantic::ClearcoatNormal: return {128u, 128u, 255u, 255u};
    case GpuMaterialTextureSemantic::BaseColor:
    case GpuMaterialTextureSemantic::MetallicRoughnessOrSpecularGlossiness:
    case GpuMaterialTextureSemantic::Occlusion:
    case GpuMaterialTextureSemantic::Emissive:
    case GpuMaterialTextureSemantic::SpecularWeight:
    case GpuMaterialTextureSemantic::SpecularColor:
    case GpuMaterialTextureSemantic::Clearcoat:
    case GpuMaterialTextureSemantic::ClearcoatRoughness:
    case GpuMaterialTextureSemantic::Transmission:
    case GpuMaterialTextureSemantic::Thickness: return {255u, 255u, 255u, 255u};
    case GpuMaterialTextureSemantic::Count: std::abort();
    }
    std::abort();
}

GpuMaterialNormalizationResult normalizeGltfMaterial(const GltfMaterialNormalizationInput& input) {
    if (!validAlphaMode(input.alphaMode)) {
        return failure(GpuMaterialNormalizationError::InvalidAlphaMode, GpuMaterialField::AlphaMode);
    }
    if (!validWorkflow(input.workflow) || input.workflow == GpuMaterialWorkflow::LabPbr) {
        return failure(GpuMaterialNormalizationError::InvalidWorkflow, GpuMaterialField::Workflow);
    }

    if ((input.flags & ~kGpuMaterialKnownFlags) != 0u) {
        return failure(GpuMaterialNormalizationError::UnknownFlags, GpuMaterialField::Flags);
    }

    const std::array<MaterialFieldValidation, 14> validations{
        {validateUnitValue(input.baseColorFactor, GpuMaterialField::BaseColorFactor),
         validateUnitValue(input.emissiveFactor, GpuMaterialField::EmissiveFactor),
         validateUnitValue(input.metallicFactor, GpuMaterialField::MetallicFactor),
         validateUnitValue(input.perceptualRoughnessFactor, GpuMaterialField::PerceptualRoughnessFactor),
         validateUnitValue(input.occlusionStrength, GpuMaterialField::OcclusionStrength),
         validateUnitValue(input.specularGlossinessFactor, GpuMaterialField::SpecularGlossinessFactor),
         validateUnitValue(input.glossinessFactor, GpuMaterialField::GlossinessFactor),
         validateUnitValue(input.dielectricSpecularColorFactor, GpuMaterialField::DielectricSpecularColorFactor),
         validateUnitValue(input.dielectricSpecularWeightFactor, GpuMaterialField::DielectricSpecularWeightFactor),
         validateUnitValue(input.clearcoatFactor, GpuMaterialField::ClearcoatFactor),
         validateUnitValue(input.clearcoatPerceptualRoughnessFactor,
                           GpuMaterialField::ClearcoatPerceptualRoughnessFactor),
         validateUnitValue(input.transmissionFactor, GpuMaterialField::TransmissionFactor),
         validateUnitValue(input.attenuationColor, GpuMaterialField::AttenuationColor),
         validateUnitValue(input.gameplaySurfaceFactors, GpuMaterialField::GameplaySurfaceFactors)}};
    for (const MaterialFieldValidation& validation : validations) {
        if (!validation.succeeded()) {
            return failure(validation.error, validation.field);
        }
    }

    if (!finite(input.emissiveStrength) || input.emissiveStrength < 0.0f) {
        return failure(finite(input.emissiveStrength) ? GpuMaterialNormalizationError::ValueOutOfRange
                                                      : GpuMaterialNormalizationError::NonFiniteValue,
                       GpuMaterialField::EmissiveStrength);
    }
    if (!finite(input.normalScale)) {
        return failure(GpuMaterialNormalizationError::NonFiniteValue, GpuMaterialField::NormalScale);
    }
    if (!finite(input.clearcoatNormalScale)) {
        return failure(GpuMaterialNormalizationError::NonFiniteValue, GpuMaterialField::ClearcoatNormalScale);
    }
    if (!finite(input.thicknessFactor) || input.thicknessFactor < 0.0f) {
        return failure(finite(input.thicknessFactor) ? GpuMaterialNormalizationError::ValueOutOfRange
                                                     : GpuMaterialNormalizationError::NonFiniteValue,
                       GpuMaterialField::ThicknessFactor);
    }
    if (!finite(input.ior) || input.ior < 1.0f) {
        return failure(finite(input.ior) ? GpuMaterialNormalizationError::ValueOutOfRange
                                         : GpuMaterialNormalizationError::NonFiniteValue,
                       GpuMaterialField::Ior);
    }
    if (!finite(input.alphaCutoff) || input.alphaCutoff < 0.0f) {
        return failure(finite(input.alphaCutoff) ? GpuMaterialNormalizationError::ValueOutOfRange
                                                 : GpuMaterialNormalizationError::NonFiniteValue,
                       GpuMaterialField::AlphaCutoff);
    }
    if (!finite(input.attenuationDistance) || input.attenuationDistance < 0.0f) {
        return failure(finite(input.attenuationDistance) ? GpuMaterialNormalizationError::ValueOutOfRange
                                                         : GpuMaterialNormalizationError::NonFiniteValue,
                       GpuMaterialField::AttenuationDistance);
    }

    const bool specularGlossiness = input.workflow == GpuMaterialWorkflow::SpecularGlossiness;
    const GpuMaterialFlags modernExtensionMask =
        gpuMaterialFlagBit(GpuMaterialFlag::Specular) | gpuMaterialFlagBit(GpuMaterialFlag::Ior) |
        gpuMaterialFlagBit(GpuMaterialFlag::Clearcoat) | gpuMaterialFlagBit(GpuMaterialFlag::Transmission) |
        gpuMaterialFlagBit(GpuMaterialFlag::Volume) | gpuMaterialFlagBit(GpuMaterialFlag::InfiniteAttenuationDistance);
    if (specularGlossiness && (input.flags & modernExtensionMask) != 0u) {
        return failure(GpuMaterialNormalizationError::IncompatibleWorkflowExtensions, GpuMaterialField::Flags);
    }
    const bool hasTransmission = hasGpuMaterialFlag(input.flags, GpuMaterialFlag::Transmission);
    const bool hasVolume = hasGpuMaterialFlag(input.flags, GpuMaterialFlag::Volume);
    const bool hasInfiniteAttenuationDistance =
        hasGpuMaterialFlag(input.flags, GpuMaterialFlag::InfiniteAttenuationDistance);
    if (hasVolume && !hasTransmission) {
        return failure(GpuMaterialNormalizationError::VolumeRequiresTransmission, GpuMaterialField::Flags);
    }
    if (hasTransmission && input.alphaMode != GpuMaterialAlphaMode::Transmission) {
        return failure(GpuMaterialNormalizationError::TransmissionAlphaConflict, GpuMaterialField::AlphaMode);
    }
    if (!hasTransmission && input.alphaMode == GpuMaterialAlphaMode::Transmission) {
        return failure(GpuMaterialNormalizationError::TransmissionAlphaConflict, GpuMaterialField::Flags);
    }
    if ((!hasVolume && (hasInfiniteAttenuationDistance || input.attenuationDistance != 0.0f)) ||
        (hasInfiniteAttenuationDistance && input.attenuationDistance != 0.0f)) {
        return failure(GpuMaterialNormalizationError::AttenuationDistanceConflict,
                       GpuMaterialField::AttenuationDistance);
    }
    if (hasVolume && !hasInfiniteAttenuationDistance && input.attenuationDistance <= 0.0f) {
        return failure(GpuMaterialNormalizationError::ValueOutOfRange, GpuMaterialField::AttenuationDistance);
    }

    GpuMaterialNormalizationResult result;
    GpuMaterial& material = result.material;
    material.baseColorFactor = input.baseColorFactor;
    material.emissiveFactorAndStrength = glm::vec4(input.emissiveFactor, input.emissiveStrength);
    material.materialFactors = {specularGlossiness ? 0.0f : input.metallicFactor,
                                specularGlossiness ? 1.0f : input.perceptualRoughnessFactor, input.normalScale,
                                input.occlusionStrength};
    material.specularGlossinessFactors = glm::vec4(input.specularGlossinessFactor, input.glossinessFactor);
    material.dielectricSpecularFactors =
        glm::vec4(input.dielectricSpecularColorFactor, input.dielectricSpecularWeightFactor);
    material.clearcoatFactors = {input.clearcoatFactor, input.clearcoatPerceptualRoughnessFactor,
                                 input.clearcoatNormalScale, 0.0f};
    material.transmissionVolumeFactors = {input.transmissionFactor, input.thicknessFactor, input.alphaCutoff,
                                          input.ior};
    material.attenuationColorAndDistance = glm::vec4(input.attenuationColor, input.attenuationDistance);
    material.gameplaySurfaceFactors = input.gameplaySurfaceFactors;
    for (size_t semanticIndex = 0u; semanticIndex < input.textureBindings.size(); ++semanticIndex) {
        setPackedBinding(material.textureIndices, semanticIndex, input.textureBindings[semanticIndex].textureIndex);
        setPackedBinding(material.samplerIndices, semanticIndex, input.textureBindings[semanticIndex].samplerIndex);
    }
    material.modesAndFlags = {static_cast<uint32_t>(input.alphaMode), static_cast<uint32_t>(input.workflow),
                              input.flags, kGpuMaterialContractVersion};
    return result;
}

LabPbrNormalSample decodeLabPbrNormal(const std::array<uint8_t, 4>& texel) {
    glm::vec2 encoded{static_cast<float>(texel[0]) * kByteToUnit * 2.0f - 1.0f,
                      static_cast<float>(texel[1]) * kByteToUnit * 2.0f - 1.0f};
    encoded.y = -encoded.y;
    const float xyLengthSquared = glm::dot(encoded, encoded);
    glm::vec3 tangentNormal{encoded, std::sqrt(std::max(1.0f - xyLengthSquared, 0.0f))};
    const float lengthSquared = glm::dot(tangentNormal, tangentNormal);
    if (lengthSquared > 0.0f) {
        tangentNormal /= std::sqrt(lengthSquared);
    }

    LabPbrNormalSample sample;
    sample.tangentNormal = tangentNormal;
    sample.materialAo = static_cast<float>(texel[2]) * kByteToUnit;
    sample.height = static_cast<float>(texel[3]) * kByteToUnit;
    return sample;
}

void normalizeLabPbrBlockHeightRange(uint8_t* rgbaPixels, const size_t pixelCount, const bool sourceHasAlpha) {
    if (!sourceHasAlpha) {
        for (size_t pixel = 0u; pixel < pixelCount; ++pixel) {
            rgbaPixels[pixel * 4u + 3u] = 255u;
        }
        return;
    }

    uint8_t minHeight = 255u;
    uint8_t maxHeight = 0u;
    for (size_t pixel = 0u; pixel < pixelCount; ++pixel) {
        const uint8_t height = rgbaPixels[pixel * 4u + 3u];
        minHeight = std::min(minHeight, height);
        maxHeight = std::max(maxHeight, height);
    }

    if (maxHeight == minHeight) {
        for (size_t pixel = 0u; pixel < pixelCount; ++pixel) {
            rgbaPixels[pixel * 4u + 3u] = 255u;
        }
        return;
    }

    const float scale = 255.0f / static_cast<float>(maxHeight - minHeight);
    for (size_t pixel = 0u; pixel < pixelCount; ++pixel) {
        const size_t alphaIndex = pixel * 4u + 3u;
        const float normalized = static_cast<float>(rgbaPixels[alphaIndex] - minHeight) * scale;
        rgbaPixels[alphaIndex] = static_cast<uint8_t>(std::clamp(normalized + 0.5f, 0.0f, 255.0f));
    }
}

GpuMaterialNormalizationError validateLabPbrMetalId(const uint8_t encodedMetalId) {
    if (encodedMetalId <= 237u || encodedMetalId == 255u) {
        return GpuMaterialNormalizationError::None;
    }
    return GpuMaterialNormalizationError::UnsupportedLabPbrMetalId;
}

LabPbrSpecularDecodeResult decodeLabPbrSpecular(const std::array<uint8_t, 4>& texel, const glm::vec3& baseColor) {
    LabPbrSpecularDecodeResult result;
    if (!finite(baseColor) || !inClosedUnitRange(baseColor)) {
        result.error = GpuMaterialNormalizationError::InvalidLabPbrBaseColor;
        result.field = GpuMaterialField::LabPbrBaseColor;
        return result;
    }
    result.error = validateLabPbrMetalId(texel[1]);
    if (result.error != GpuMaterialNormalizationError::None) {
        result.field = GpuMaterialField::LabPbrMetalId;
        return result;
    }

    LabPbrSpecularSample& sample = result.sample;
    sample.perceptualRoughness = 1.0f - static_cast<float>(texel[0]) * kByteToUnit;
    if (texel[1] <= 229u) {
        sample.f0 = glm::vec3(static_cast<float>(texel[1]) * kByteToUnit);
    } else {
        sample.metalness = 1.0f;
        sample.metalId = texel[1];
        sample.f0 = texel[1] == 255u ? baseColor : definedLabPbrMetalF0(texel[1]);
    }

    if (texel[2] <= 64u) {
        sample.porosity = static_cast<float>(texel[2]) * (1.0f / 64.0f);
    } else {
        sample.subsurface = static_cast<float>(texel[2] - 65u) * (1.0f / 190.0f);
    }
    sample.emissionProvided = texel[3] != 255u;
    sample.emission = sample.emissionProvided ? static_cast<float>(texel[3]) * (1.0f / 254.0f) : 0.0f;
    return result;
}

} // namespace renderer::contracts
