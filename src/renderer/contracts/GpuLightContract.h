#ifndef MECRAFT_GPU_LIGHT_CONTRACT_H
#define MECRAFT_GPU_LIGHT_CONTRACT_H

#include "SceneIdentityContract.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <limits>
#include <type_traits>

namespace renderer::contracts {

inline constexpr uint32_t kGpuLightContractVersion = 3u;
inline constexpr uint32_t kGpuLightInvalidResourceIndex =
    std::numeric_limits<uint32_t>::max();

/// Selects the analytic light shape evaluated by raster and ray paths.
enum class GpuLightType : uint32_t {
    Directional = 0u,
    Point = 1u,
    Spot = 2u,
    Rect = 3u
};

/// Identifies the physical unit supplied by an authoring or asset source.
enum class GpuLightIntensityUnit : uint32_t {
    Lux = 0u,
    Lumen = 1u,
    Candela = 2u,
    Nit = 3u
};

/// Selects how one light obtains and updates its shadow allocation.
enum class GpuLightShadowPolicy : uint32_t {
    None = 0u,
    RasterDynamic = 1u,
    RasterCached = 2u,
    RayQuery = 3u
};

/// Controls which lighting products receive energy from one light.
enum class GpuLightContributionFlag : uint32_t {
    Diffuse = 1u << 0u,
    Specular = 1u << 1u,
    Volumetric = 1u << 2u
};

using GpuLightContributionFlags = uint32_t;

/// Returns the bit corresponding to one light contribution channel.
/// @param flag Contribution channel to encode.
/// @return Unsigned mask containing exactly one contribution bit.
[[nodiscard]] constexpr GpuLightContributionFlags gpuLightContributionFlagBit(
    const GpuLightContributionFlag flag) {
    return static_cast<GpuLightContributionFlags>(flag);
}

inline constexpr GpuLightContributionFlags kGpuLightKnownContributionFlags =
    gpuLightContributionFlagBit(GpuLightContributionFlag::Diffuse) |
    gpuLightContributionFlagBit(GpuLightContributionFlag::Specular) |
    gpuLightContributionFlagBit(GpuLightContributionFlag::Volumetric);

/// Tests whether one lighting product receives energy from a light.
/// @param flags Complete packed contribution mask.
/// @param flag Contribution channel to test.
/// @return True when the requested contribution bit is present.
[[nodiscard]] constexpr bool hasGpuLightContributionFlag(
    const GpuLightContributionFlags flags,
    const GpuLightContributionFlag flag) {
    return (flags & gpuLightContributionFlagBit(flag)) != 0u;
}

/// Defines the immutable 96-byte CPU/GPU light record shared by clustered
/// deferred, Forward+, volumetric, and ray-traced lighting paths.
struct alignas(16) GpuLight final {
    /// Camera-relative position in meters and finite influence range in meters.
    /// Directional lights store zero in all four components.
    glm::vec4 positionAndRange{0.0f};
    /// Unit direction in which the light emits. Point lights store zero in
    /// xyz because their direction is position-derived. Finite local lights
    /// store inverse squared range in w; directional lights store zero in w.
    glm::vec4 direction{0.0f};
    /// Linear RGB chromaticity and normalized shading intensity. The fourth
    /// component stores lux, candela, or nit according to the light type.
    glm::vec4 colorAndIntensity{0.0f};
    /// Spot cosine inner/outer angles and Rect width/height in meters.
    glm::vec4 spotCosinesAndRectSize{0.0f};
    /// Type, stable light ID, shadow policy, and shadow allocation index.
    glm::uvec4 classificationAndIdentity{
        static_cast<uint32_t>(GpuLightType::Directional), 0u,
        static_cast<uint32_t>(GpuLightShadowPolicy::None),
        kGpuLightInvalidResourceIndex};
    /// Cookie index, IES profile index, contribution flags, and contract version.
    glm::uvec4 resourcesAndFlags{kGpuLightInvalidResourceIndex,
                                 kGpuLightInvalidResourceIndex, 0u,
                                 kGpuLightContractVersion};
};

/// Preserves the author-requested shadow policy until the local-shadow pass
/// has allocated a stable resource slot. The embedded GPU record must always
/// carry the None policy and invalid shadow index while it is scene input.
struct SceneLight final {
    GpuLight light;
    GpuLightShadowPolicy requestedShadowPolicy =
        GpuLightShadowPolicy::None;
};

/// Carries source-level light values into strict physical-unit normalization.
struct GpuLightNormalizationInput final {
    StableLightId lightId;
    GpuLightType type = GpuLightType::Point;
    glm::vec3 positionMeters{0.0f};
    glm::vec3 emissionDirection{0.0f};
    float rangeMeters = 0.0f;
    glm::vec3 colorLinear{1.0f};
    float intensity = 0.0f;
    GpuLightIntensityUnit intensityUnit = GpuLightIntensityUnit::Candela;
    float innerConeAngleRadians = 0.0f;
    float outerConeAngleRadians = 0.0f;
    glm::vec2 rectSizeMeters{0.0f};
    GpuLightShadowPolicy shadowPolicy = GpuLightShadowPolicy::None;
    uint32_t shadowIndex = kGpuLightInvalidResourceIndex;
    uint32_t cookieIndex = kGpuLightInvalidResourceIndex;
    uint32_t iesProfileIndex = kGpuLightInvalidResourceIndex;
    GpuLightContributionFlags contributionFlags =
        gpuLightContributionFlagBit(GpuLightContributionFlag::Diffuse) |
        gpuLightContributionFlagBit(GpuLightContributionFlag::Specular);
};

/// Identifies every deterministic GPU light normalization failure.
enum class GpuLightNormalizationError : uint8_t {
    None,
    InvalidType,
    InvalidStableId,
    InvalidIntensityUnit,
    NonFiniteValue,
    ValueOutOfRange,
    InvalidDirection,
    InvalidSpotCone,
    InvalidRectSize,
    InvalidShadowPolicy,
    ShadowIndexConflict,
    UnknownContributionFlags
};

/// Identifies the semantic field associated with one normalization failure.
enum class GpuLightField : uint8_t {
    None,
    Type,
    StableLightId,
    Position,
    Direction,
    Range,
    Color,
    Intensity,
    IntensityUnit,
    InnerConeAngle,
    OuterConeAngle,
    RectSize,
    ShadowPolicy,
    ShadowIndex,
    ContributionFlags
};

/// Returns one normalized GPU record or a stable semantic error and field.
struct GpuLightNormalizationResult final {
    GpuLight light;
    GpuLightNormalizationError error = GpuLightNormalizationError::None;
    GpuLightField field = GpuLightField::None;

    /// Reports whether every source value satisfies the light contract.
    /// @return True only when no normalization error was recorded.
    [[nodiscard]] bool succeeded() const;
};

/// Stores one asset-local analytic light before a visible scene instance
/// supplies its stable identity and camera-relative transform.
struct AnalyticLightSourceDefinition final {
    GpuLightType type = GpuLightType::Point;
    glm::vec3 localPositionMeters{0.0f};
    glm::vec3 localEmissionDirection{0.0f};
    float rangeMeters = 0.0f;
    glm::vec3 colorLinear{1.0f};
    float intensity = 0.0f;
    GpuLightIntensityUnit intensityUnit =
        GpuLightIntensityUnit::Candela;
    float innerConeAngleRadians = 0.0f;
    float outerConeAngleRadians = 0.0f;
    glm::vec2 rectSizeMeters{0.0f};
    GpuLightShadowPolicy shadowPolicy = GpuLightShadowPolicy::None;
    GpuLightContributionFlags contributionFlags =
        gpuLightContributionFlagBit(GpuLightContributionFlag::Diffuse) |
        gpuLightContributionFlagBit(GpuLightContributionFlag::Specular);
};

/// Identifies failures while transforming an asset-local analytic light into
/// the camera-relative coordinate system consumed by clustered lighting.
enum class AnalyticLightInstantiationError : uint8_t {
    None,
    InvalidCameraPosition,
    NonFiniteTransform,
    NonAffineTransform,
    DegenerateTransformBasis,
    ShearedTransform,
    NormalizationFailed
};

/// Returns one instantiated GPU light or a stable transform/normalization
/// error without publishing a partial record.
struct AnalyticLightInstantiationResult final {
    SceneLight sceneLight;
    AnalyticLightInstantiationError error =
        AnalyticLightInstantiationError::None;
    GpuLightNormalizationError normalizationError =
        GpuLightNormalizationError::None;
    GpuLightField normalizationField = GpuLightField::None;

    /// Reports whether the source transform and physical light values are valid.
    /// @return True only when a complete GPU light record was produced.
    [[nodiscard]] bool succeeded() const;
};

/// Validates and converts source physical units into the fixed GPU record.
/// Directional intensity is stored as lux, Point and Spot intensity as
/// candela, and Rect intensity as nit.
/// @param input Fully resolved source light and resource indices.
/// @return Packed light or a stable field-specific validation error.
[[nodiscard]] GpuLightNormalizationResult
normalizeGpuLight(const GpuLightNormalizationInput& input);

/// Validates the packed inverse squared range against the finite range.
/// @param light Normalized GPU light record.
/// @return True when directional lights store zeros and every local light
/// stores a finite positive reciprocal matching positionAndRange.w.
[[nodiscard]] bool gpuLightPackedRangeValid(const GpuLight& light);

/// Applies one affine scene transform to an asset-local analytic light.
/// Translation affects its position, the orthonormalized basis affects its
/// direction, and scale is deliberately excluded from physical light range
/// and intensity. Sheared transforms are rejected because they do not define
/// one unambiguous light orientation.
/// @param source Asset-local physical light definition.
/// @param lightId Stable identity owned by the visible scene instance.
/// @param localToWorld Complete affine transform of the scene instance.
/// @param cameraPositionMeters World-space camera position subtracted before upload.
/// @return Camera-relative GPU light or a structured failure.
[[nodiscard]] AnalyticLightInstantiationResult instantiateAnalyticLight(
    const AnalyticLightSourceDefinition& source,
    StableLightId lightId,
    const glm::mat4& localToWorld,
    const glm::vec3& cameraPositionMeters);

/// Returns the stable identifier used by logs and tests for one error.
/// @param error Error to identify.
/// @return Process-lifetime string containing the stable identifier.
[[nodiscard]] const char*
gpuLightNormalizationErrorStableId(GpuLightNormalizationError error);

/// Returns the stable identifier used by diagnostics for one light field.
/// @param field Semantic field to identify.
/// @return Process-lifetime string containing the stable identifier.
[[nodiscard]] const char* gpuLightFieldStableId(GpuLightField field);

/// Returns the stable identifier for one analytic-light instantiation error.
/// @param error Error to identify.
/// @return Process-lifetime string containing the stable identifier.
[[nodiscard]] const char* analyticLightInstantiationErrorStableId(
    AnalyticLightInstantiationError error);

static_assert(sizeof(GpuLight) == 96u);
static_assert(alignof(GpuLight) == 16u);
static_assert(std::is_trivially_copyable_v<GpuLight>);
static_assert(std::is_standard_layout_v<GpuLight>);

} // namespace renderer::contracts

#endif // MECRAFT_GPU_LIGHT_CONTRACT_H
