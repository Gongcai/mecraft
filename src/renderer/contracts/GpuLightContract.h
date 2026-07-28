#ifndef MECRAFT_GPU_LIGHT_CONTRACT_H
#define MECRAFT_GPU_LIGHT_CONTRACT_H

#include "SceneIdentityContract.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <limits>
#include <type_traits>

namespace renderer::contracts {

inline constexpr uint32_t kGpuLightContractVersion = 1u;
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
    Dynamic = 1u,
    Cached = 2u
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
    /// Unit direction in which the light emits. Point lights store zero
    /// because their direction is position-derived.
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

/// Validates and converts source physical units into the fixed GPU record.
/// Directional intensity is stored as lux, Point and Spot intensity as
/// candela, and Rect intensity as nit.
/// @param input Fully resolved source light and resource indices.
/// @return Packed light or a stable field-specific validation error.
[[nodiscard]] GpuLightNormalizationResult
normalizeGpuLight(const GpuLightNormalizationInput& input);

/// Returns the stable identifier used by logs and tests for one error.
/// @param error Error to identify.
/// @return Process-lifetime string containing the stable identifier.
[[nodiscard]] const char*
gpuLightNormalizationErrorStableId(GpuLightNormalizationError error);

/// Returns the stable identifier used by diagnostics for one light field.
/// @param field Semantic field to identify.
/// @return Process-lifetime string containing the stable identifier.
[[nodiscard]] const char* gpuLightFieldStableId(GpuLightField field);

static_assert(sizeof(GpuLight) == 96u);
static_assert(alignof(GpuLight) == 16u);
static_assert(std::is_trivially_copyable_v<GpuLight>);
static_assert(std::is_standard_layout_v<GpuLight>);

} // namespace renderer::contracts

#endif // MECRAFT_GPU_LIGHT_CONTRACT_H
