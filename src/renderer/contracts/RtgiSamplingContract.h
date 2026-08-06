#ifndef MECRAFT_RTGI_SAMPLING_CONTRACT_H
#define MECRAFT_RTGI_SAMPLING_CONTRACT_H

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>

namespace renderer::contracts {

/// Stable per-pixel classifications written by the raw RTGI validation image.
enum class RtgiTraceClassification : uint32_t { Sky = 0u, Translucent = 1u, Miss = 2u, Hit = 3u, NonFinite = 4u };

inline constexpr uint32_t kRtgiTraceValidationClassificationMask = 0xffu;
inline constexpr uint32_t kRtgiTraceValidationCandidateShift = 8u;
inline constexpr uint32_t kRtgiTraceValidationCandidateMask = 0xfffu;
inline constexpr uint32_t kRtgiTraceValidationConfirmedShift = 20u;
inline constexpr uint32_t kRtgiTraceValidationConfirmedMask = 0xfffu;
inline constexpr float kRtgiVoxelSurfaceExpansion = 1.0f / 2048.0f;
inline constexpr float kRtgiMinimumRayOriginBias = kRtgiVoxelSurfaceExpansion * 2.0f;
inline constexpr uint32_t kRtgiSecondaryLightingTerrainNormalMapBit = 1u << 0u;
inline constexpr uint32_t kRtgiSecondaryLightingTerrainSpecularMapBit = 1u << 1u;
inline constexpr float kRtgiMetallicDiffuseTransportFloor = 0.35f;

/// Push constants shared by the production RTGI trace pass and Vulkan smoke validation.
struct alignas(16) RtgiTracePushConstants final {
    glm::mat4 inverseViewProjection{1.0f};
    glm::vec4 cameraPositionAndMaxDistance{0.0f, 0.0f, 0.0f, 64.0f};
    glm::vec4 renderExtentAndBias{1.0f, 1.0f, 0.001f, 0.0f};
    glm::uvec4 frameMaskAndFlags{0u, 1u, 0u, 0u};
    glm::uvec4 materialGeometryCounts{0u};
};

static_assert(sizeof(RtgiTracePushConstants) == 128u);

/// Uniform parameters used to shade one RTGI secondary hit without extending
/// the Vulkan-minimum 128-byte push-constant contract.
struct alignas(16) RtgiSecondaryLightingParams final {
    glm::vec4 sunDirectionAndVisibility{0.0f, 1.0f, 0.0f, 1.0f};
    glm::vec4 moonDirectionAndVisibility{0.0f, -1.0f, 0.0f, 0.0f};
    glm::vec4 sunRadiance{1.0f, 1.0f, 1.0f, 0.0f};
    glm::vec4 moonRadiance{0.0f};
    glm::vec4 skyAmbientRadiance{0.0f};
    // The fourth component scales scene-referred secondary radiance into the current pre-exposed domain.
    glm::vec4 traceAndEmissionScales{128.0f, 1.5f, 1.0f, 1.0f};
    // x: Minecraft block-light strength; y: terrain block-light bounce boost.
    glm::vec4 terrainLightScales{1.0f, 1.35f, 0.0f, 0.0f};
    glm::uvec4 flags{0u};
};

static_assert(sizeof(RtgiSecondaryLightingParams) == 128u);

/// Hashes one sample-sequence value with the integer permutation shared by GLSL.
/// @param value Input sequence value.
/// @return Deterministic 32-bit permutation of value.
[[nodiscard]] uint32_t rtgiSampleHash(uint32_t value);

/// Hashes stable material and geometry identities for the RTGI validation image.
/// @param stableMaterialId Non-zero stable material identity from primitive metadata.
/// @param stableGeometryId Non-zero stable geometry identity from primitive metadata.
/// @return Deterministic 32-bit identity word shared with GLSL validation.
[[nodiscard]] uint32_t rtgiStableHitIdentityHash(uint32_t stableMaterialId, uint32_t stableGeometryId);

/// Hashes one terrain hit location together with the resident BLAS revision referenced by its TLAS instance.
/// @param blasRevision Resident terrain BLAS revision stored in the TLAS hit-data table.
/// @param customIndex TLAS instance custom index used to address the hit-data table.
/// @param geometryIndex BLAS geometry range containing the hit triangle.
/// @param primitiveIndex Triangle index inside the geometry range.
/// @return Deterministic diagnostic identity shared with GLSL validation.
[[nodiscard]] uint32_t rtgiTerrainHitIdentityHash(uint64_t blasRevision, uint32_t customIndex,
                                                  uint32_t geometryIndex, uint32_t primitiveIndex);

/// Produces the per-frame low-discrepancy Cranley-Patterson rotation used by RTGI.
/// @param frameIndex Low 32 bits of the deterministic render-frame index.
/// @return Two values in the half-open interval [0, 1).
[[nodiscard]] glm::vec2 rtgiCranleyPattersonRotation(uint32_t frameIndex);

/// Maps one two-dimensional sample to a cosine-weighted world-space hemisphere direction.
/// @param sample Two values in the half-open interval [0, 1).
/// @param normal Finite non-zero world-space surface normal.
/// @return Unit direction in the normal hemisphere, or no value when the contract is invalid.
[[nodiscard]] std::optional<glm::vec3> rtgiCosineHemisphereDirection(const glm::vec2& sample, const glm::vec3& normal);

/// Recovers the axis-aligned geometric face normal of a voxel surface from its shading normal.
/// @param shadingNormal Finite non-zero world-space normal after terrain normal mapping.
/// @return Signed dominant axis, or no value when the input is invalid.
[[nodiscard]] std::optional<glm::vec3> rtgiVoxelGeometricNormal(const glm::vec3& shadingNormal);

/// Packs one per-pixel trace classification and Cutout Candidate/Confirmed counters into 32 bits.
/// @param classification Stable raw-trace result classification.
/// @param candidateCount Number of non-opaque triangle candidates evaluated by the ray query.
/// @param confirmedCount Number of candidates explicitly confirmed after material alpha testing.
/// @return Packed validation word, or no value when either counter exceeds its 12-bit field.
[[nodiscard]] std::optional<uint32_t> encodeRtgiTraceValidation(RtgiTraceClassification classification,
                                                                uint32_t candidateCount, uint32_t confirmedCount);

/// Decodes the stable classification stored in bits 0 through 7.
[[nodiscard]] constexpr RtgiTraceClassification rtgiTraceValidationClassification(const uint32_t packed) {
    return static_cast<RtgiTraceClassification>(packed & kRtgiTraceValidationClassificationMask);
}

/// Decodes the Cutout Candidate count stored in bits 8 through 19.
[[nodiscard]] constexpr uint32_t rtgiTraceValidationCandidateCount(const uint32_t packed) {
    return (packed >> kRtgiTraceValidationCandidateShift) & kRtgiTraceValidationCandidateMask;
}

/// Decodes the explicitly confirmed Cutout count stored in bits 20 through 31.
[[nodiscard]] constexpr uint32_t rtgiTraceValidationConfirmedCount(const uint32_t packed) {
    return (packed >> kRtgiTraceValidationConfirmedShift) & kRtgiTraceValidationConfirmedMask;
}

} // namespace renderer::contracts

#endif // MECRAFT_RTGI_SAMPLING_CONTRACT_H
