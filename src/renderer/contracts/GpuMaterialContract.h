#ifndef MECRAFT_GPU_MATERIAL_CONTRACT_H
#define MECRAFT_GPU_MATERIAL_CONTRACT_H

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace renderer::contracts {

inline constexpr uint32_t kGpuMaterialContractVersion = 1u;
inline constexpr uint32_t kLabPbrMaterialVersion = 13u;
inline constexpr size_t kGpuMaterialTextureSemanticCount = 12u;

/// Identifies the twelve texture meanings shared by glTF materials and GPU
/// scene records.
enum class GpuMaterialTextureSemantic : uint32_t {
    BaseColor = 0u,
    MetallicRoughnessOrSpecularGlossiness = 1u,
    Normal = 2u,
    Occlusion = 3u,
    Emissive = 4u,
    SpecularWeight = 5u,
    SpecularColor = 6u,
    Clearcoat = 7u,
    ClearcoatRoughness = 8u,
    ClearcoatNormal = 9u,
    Transmission = 10u,
    Thickness = 11u,
    Count = 12u
};

/// Selects the material equations used to interpret factors and property
/// textures.
enum class GpuMaterialWorkflow : uint32_t { MetallicRoughness = 0u, SpecularGlossiness = 1u, LabPbr = 2u };

/// Selects the coverage or optical composition path used by one material.
enum class GpuMaterialAlphaMode : uint32_t { Opaque = 0u, Mask = 1u, Blend = 2u, Transmission = 3u, Additive = 4u };

/// Records optional material capabilities without changing the fixed GPU
/// layout.
enum class GpuMaterialFlag : uint32_t {
    DoubleSided = 1u << 0u,
    Specular = 1u << 1u,
    Ior = 1u << 2u,
    Clearcoat = 1u << 3u,
    Transmission = 1u << 4u,
    Volume = 1u << 5u,
    InfiniteAttenuationDistance = 1u << 6u
};

using GpuMaterialFlags = uint32_t;

/// Returns the bit corresponding to one optional material capability.
/// @param flag Capability to encode.
/// @return Unsigned mask containing exactly one capability bit.
[[nodiscard]] constexpr GpuMaterialFlags gpuMaterialFlagBit(const GpuMaterialFlag flag) {
    return static_cast<GpuMaterialFlags>(flag);
}

inline constexpr GpuMaterialFlags kGpuMaterialKnownFlags =
    gpuMaterialFlagBit(GpuMaterialFlag::DoubleSided) | gpuMaterialFlagBit(GpuMaterialFlag::Specular) |
    gpuMaterialFlagBit(GpuMaterialFlag::Ior) | gpuMaterialFlagBit(GpuMaterialFlag::Clearcoat) |
    gpuMaterialFlagBit(GpuMaterialFlag::Transmission) | gpuMaterialFlagBit(GpuMaterialFlag::Volume) |
    gpuMaterialFlagBit(GpuMaterialFlag::InfiniteAttenuationDistance);

/// Tests whether a material capability is present in a packed flag mask.
/// @param flags Complete packed capability mask.
/// @param flag Capability to test.
/// @return True when the requested capability bit is set.
[[nodiscard]] constexpr bool hasGpuMaterialFlag(const GpuMaterialFlags flags, const GpuMaterialFlag flag) {
    return (flags & gpuMaterialFlagBit(flag)) != 0u;
}

/// Stores one resolved texture and sampler table reference before GPU packing.
struct GpuMaterialTextureBinding {
    uint32_t textureIndex = 0u;
    uint32_t samplerIndex = 0u;
};

/// Defines the immutable 256-byte CPU/GPU material record used by raster and
/// ray paths.
struct alignas(16) GpuMaterial final {
    /// Linear RGBA base color multiplier.
    glm::vec4 baseColorFactor{1.0f};
    /// Linear emissive RGB multiplier and independent scalar strength.
    glm::vec4 emissiveFactorAndStrength{0.0f, 0.0f, 0.0f, 1.0f};
    /// Metallic, perceptual roughness, normal scale, and occlusion strength.
    glm::vec4 materialFactors{1.0f, 1.0f, 1.0f, 1.0f};
    /// Specular-glossiness RGB factor and glossiness factor.
    glm::vec4 specularGlossinessFactors{1.0f};
    /// Dielectric specular RGB color factor and scalar weight.
    glm::vec4 dielectricSpecularFactors{1.0f};
    /// Clearcoat amount, perceptual roughness, normal scale, and reserved value.
    glm::vec4 clearcoatFactors{0.0f, 0.0f, 1.0f, 0.0f};
    /// Transmission, thickness, alpha cutoff, and index of refraction.
    glm::vec4 transmissionVolumeFactors{0.0f, 0.0f, 0.5f, 1.5f};
    /// Linear attenuation RGB and finite distance, or zero distance with the
    /// InfiniteAttenuationDistance flag.
    glm::vec4 attenuationColorAndDistance{1.0f, 1.0f, 1.0f, 0.0f};
    /// Wetness, porosity, subsurface, and product-defined surface factor.
    glm::vec4 gameplaySurfaceFactors{0.0f};
    /// Texture table indices in GpuMaterialTextureSemantic order.
    std::array<glm::uvec4, 3> textureIndices{};
    /// Sampler table indices in GpuMaterialTextureSemantic order.
    std::array<glm::uvec4, 3> samplerIndices{};
    /// Alpha mode, workflow, capability flags, and contract version.
    glm::uvec4 modesAndFlags{static_cast<uint32_t>(GpuMaterialAlphaMode::Opaque),
                             static_cast<uint32_t>(GpuMaterialWorkflow::MetallicRoughness), 0u,
                             kGpuMaterialContractVersion};
};

/// Carries format-level glTF factors into the versioned GPU material
/// normalizer.
struct GltfMaterialNormalizationInput final {
    glm::vec4 baseColorFactor{1.0f};
    glm::vec3 emissiveFactor{0.0f};
    float emissiveStrength = 1.0f;
    float metallicFactor = 1.0f;
    float perceptualRoughnessFactor = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    glm::vec3 specularGlossinessFactor{1.0f};
    float glossinessFactor = 1.0f;
    glm::vec3 dielectricSpecularColorFactor{1.0f};
    float dielectricSpecularWeightFactor = 1.0f;
    float clearcoatFactor = 0.0f;
    float clearcoatPerceptualRoughnessFactor = 0.0f;
    float clearcoatNormalScale = 1.0f;
    float transmissionFactor = 0.0f;
    float thicknessFactor = 0.0f;
    float alphaCutoff = 0.5f;
    float ior = 1.5f;
    glm::vec3 attenuationColor{1.0f};
    float attenuationDistance = 0.0f;
    glm::vec4 gameplaySurfaceFactors{0.0f};
    std::array<GpuMaterialTextureBinding, kGpuMaterialTextureSemanticCount> textureBindings{};
    GpuMaterialAlphaMode alphaMode = GpuMaterialAlphaMode::Opaque;
    GpuMaterialWorkflow workflow = GpuMaterialWorkflow::MetallicRoughness;
    GpuMaterialFlags flags = 0u;
};

/// Identifies every deterministic GPU material normalization failure.
enum class GpuMaterialNormalizationError : uint8_t {
    None,
    InvalidAlphaMode,
    InvalidWorkflow,
    UnknownFlags,
    NonFiniteValue,
    ValueOutOfRange,
    IncompatibleWorkflowExtensions,
    VolumeRequiresTransmission,
    TransmissionAlphaConflict,
    AttenuationDistanceConflict,
    UnsupportedLabPbrMetalId,
    InvalidLabPbrBaseColor
};

/// Identifies the semantic field associated with a normalization failure.
enum class GpuMaterialField : uint8_t {
    None,
    AlphaMode,
    Workflow,
    BaseColorFactor,
    EmissiveFactor,
    EmissiveStrength,
    MetallicFactor,
    PerceptualRoughnessFactor,
    NormalScale,
    OcclusionStrength,
    SpecularGlossinessFactor,
    GlossinessFactor,
    DielectricSpecularColorFactor,
    DielectricSpecularWeightFactor,
    ClearcoatFactor,
    ClearcoatPerceptualRoughnessFactor,
    ClearcoatNormalScale,
    TransmissionFactor,
    ThicknessFactor,
    AlphaCutoff,
    Ior,
    AttenuationColor,
    AttenuationDistance,
    GameplaySurfaceFactors,
    Flags,
    LabPbrMetalId,
    LabPbrBaseColor
};

/// Returns one normalized GPU record or a stable semantic error and field.
struct GpuMaterialNormalizationResult final {
    GpuMaterial material;
    GpuMaterialNormalizationError error = GpuMaterialNormalizationError::None;
    GpuMaterialField field = GpuMaterialField::None;

    /// Reports whether every input factor satisfies the material contract.
    /// @return True only when no normalization error was recorded.
    [[nodiscard]] bool succeeded() const;
};

/// Stores the decoded LabPBR 1.3 normal texture semantics.
struct LabPbrNormalSample final {
    glm::vec3 tangentNormal{0.0f, 0.0f, 1.0f};
    float materialAo = 1.0f;
    float height = 1.0f;
};

/// Stores the decoded LabPBR 1.3 specular texture semantics.
struct LabPbrSpecularSample final {
    float perceptualRoughness = 1.0f;
    glm::vec3 f0{0.0f};
    float metalness = 0.0f;
    float porosity = 0.0f;
    float subsurface = 0.0f;
    float emission = 0.0f;
    bool emissionProvided = false;
    uint32_t metalId = 0u;
};

/// Returns one LabPBR sample or a stable channel validation error.
struct LabPbrSpecularDecodeResult final {
    LabPbrSpecularSample sample;
    GpuMaterialNormalizationError error = GpuMaterialNormalizationError::None;
    GpuMaterialField field = GpuMaterialField::None;

    /// Reports whether all encoded channels have defined LabPBR 1.3 semantics.
    /// @return True only when no decoding error was recorded.
    [[nodiscard]] bool succeeded() const;
};

/// Returns the stable identifier used by logs and tests for one normalization
/// error.
/// @param error Error to identify.
/// @return Process-lifetime string containing the stable identifier.
[[nodiscard]] const char* gpuMaterialNormalizationErrorStableId(GpuMaterialNormalizationError error);

/// Returns the stable identifier used by diagnostics for one material field.
/// @param field Semantic field to identify.
/// @return Process-lifetime string containing the stable identifier.
[[nodiscard]] const char* gpuMaterialFieldStableId(GpuMaterialField field);

/// Reports whether one texture semantic must use an sRGB sampling format.
/// @param semantic Texture meaning to classify.
/// @param workflow Workflow that determines the second texture's encoding.
/// @return True for color data and false for linearly encoded property data.
[[nodiscard]] bool gpuMaterialTextureUsesSrgb(GpuMaterialTextureSemantic semantic, GpuMaterialWorkflow workflow);

/// Returns the explicit one-pixel texture used when one glTF texture is absent.
/// @param semantic Texture meaning whose multiplicative identity is required.
/// @return RGBA8 texel with the exact identity value for the semantic.
[[nodiscard]] std::array<uint8_t, 4> gpuMaterialDefaultTexturePixel(GpuMaterialTextureSemantic semantic);

/// Validates and packs glTF material factors into the fixed GPU record.
/// @param input Fully resolved glTF factors, extension flags, and table
/// bindings.
/// @return Packed material or a stable field-specific validation error.
[[nodiscard]] GpuMaterialNormalizationResult normalizeGltfMaterial(const GltfMaterialNormalizationInput& input);

/// Decodes a DirectX-oriented LabPBR 1.3 normal/AO/height texel.
/// @param texel Exact RGBA8 source texel after image decoding.
/// @return Tangent-space normal, material AO, and encoded height.
[[nodiscard]] LabPbrNormalSample decodeLabPbrNormal(const std::array<uint8_t, 4>& texel);

/// Expands one block tile's authored LabPBR height channel to the full byte
/// range consumed by parallax occlusion mapping.
/// @param rgbaPixels Mutable tightly packed RGBA8 tile pixels.
/// @param pixelCount Number of RGBA8 pixels in the tile.
/// @param sourceHasAlpha True when the decoded source image authored alpha.
void normalizeLabPbrBlockHeightRange(uint8_t* rgbaPixels, size_t pixelCount, bool sourceHasAlpha);

/// Validates whether one LabPBR green-channel value has defined metal
/// semantics.
/// @param encodedMetalId Exact unsigned green-channel value.
/// @return None for dielectric, defined metal, or custom-metal values.
[[nodiscard]] GpuMaterialNormalizationError validateLabPbrMetalId(uint8_t encodedMetalId);

/// Decodes one LabPBR 1.3 specular texel into unified material semantics.
/// @param texel Exact RGBA8 source texel after image decoding.
/// @param baseColor Linear base color used by custom metal ID 255.
/// @return Decoded sample or a stable validation error.
[[nodiscard]] LabPbrSpecularDecodeResult decodeLabPbrSpecular(const std::array<uint8_t, 4>& texel,
                                                              const glm::vec3& baseColor);

static_assert(kGpuMaterialTextureSemanticCount == static_cast<size_t>(GpuMaterialTextureSemantic::Count));
static_assert(static_cast<uint32_t>(GpuMaterialTextureSemantic::BaseColor) == 0u);
static_assert(static_cast<uint32_t>(GpuMaterialTextureSemantic::Thickness) == 11u);
static_assert(static_cast<uint32_t>(GpuMaterialAlphaMode::Additive) == 4u);
static_assert(static_cast<uint32_t>(GpuMaterialWorkflow::LabPbr) == 2u);
static_assert(alignof(GpuMaterial) == 16u);
static_assert(sizeof(GpuMaterial) == 256u);
static_assert(offsetof(GpuMaterial, baseColorFactor) == 0u);
static_assert(offsetof(GpuMaterial, emissiveFactorAndStrength) == 16u);
static_assert(offsetof(GpuMaterial, materialFactors) == 32u);
static_assert(offsetof(GpuMaterial, specularGlossinessFactors) == 48u);
static_assert(offsetof(GpuMaterial, dielectricSpecularFactors) == 64u);
static_assert(offsetof(GpuMaterial, clearcoatFactors) == 80u);
static_assert(offsetof(GpuMaterial, transmissionVolumeFactors) == 96u);
static_assert(offsetof(GpuMaterial, attenuationColorAndDistance) == 112u);
static_assert(offsetof(GpuMaterial, gameplaySurfaceFactors) == 128u);
static_assert(offsetof(GpuMaterial, textureIndices) == 144u);
static_assert(offsetof(GpuMaterial, samplerIndices) == 192u);
static_assert(offsetof(GpuMaterial, modesAndFlags) == 240u);
static_assert(std::is_standard_layout_v<GpuMaterial>);
static_assert(std::is_trivially_copyable_v<GpuMaterial>);

} // namespace renderer::contracts

#endif // MECRAFT_GPU_MATERIAL_CONTRACT_H
