#ifndef MECRAFT_RTGI_SAMPLING_CONTRACT_H
#define MECRAFT_RTGI_SAMPLING_CONTRACT_H

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
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
inline constexpr uint32_t kRtgiTraceCounterContractVersion = 1u;

/// Stable word offsets written by the RTGI validation-image reduction shader.
enum class RtgiTraceCounterWord : size_t {
    CandidateLow = 0u,
    CandidateHigh,
    ConfirmedLow,
    ConfirmedHigh,
    PeakCandidatePerPixel,
    PeakConfirmedPerPixel,
    PixelLow,
    PixelHigh,
    InvariantError,
    ContractVersion,
    Count
};

inline constexpr size_t kRtgiTraceCounterWordCount = static_cast<size_t>(RtgiTraceCounterWord::Count);

/// Push constants for reducing one complete RTGI validation image.
struct alignas(16) RtgiTraceCounterPushConstants final {
    glm::uvec4 renderExtentAndContract{1u, 1u, kRtgiTraceCounterContractVersion, 0u};
};

static_assert(sizeof(RtgiTraceCounterPushConstants) == 16u);

/// One completed asynchronous GPU reduction of the RTGI validation image.
struct RtgiTraceCounterFrameStats final {
    bool supported = false;
    bool valid = false;
    uint64_t sequence = 0u;
    uint64_t frameIndex = 0u;
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint64_t pixelCount = 0u;
    uint64_t candidateCount = 0u;
    uint64_t confirmedCount = 0u;
    uint32_t peakCandidateCountPerPixel = 0u;
    uint32_t peakConfirmedCountPerPixel = 0u;
};

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
    // Reserved to preserve the 128-byte shader ABI; modern RTGI reads analytic GpuLight records.
    glm::vec4 terrainLightScales{0.0f};
    glm::uvec4 flags{0u};
};

static_assert(sizeof(RtgiSecondaryLightingParams) == 128u);

/// Hashes one sample-sequence value with the integer permutation shared by GLSL.
/// @param value Input sequence value.
/// @return Deterministic 32-bit permutation of value.
[[nodiscard]] uint32_t rtgiSampleHash(uint32_t value);

/// Builds the inverse view-projection that reconstructs positions in the active RT scene space.
/// @param projection Current projection matrix, including the same jitter used for the depth buffer.
/// @param view Absolute camera view matrix; only its orientation is used.
/// @param cameraPosition Absolute camera position in world space.
/// @param sceneOrigin Origin subtracted from TLAS and GPU Scene transforms.
/// @param inverseViewProjection Receives clip-to-scene-space reconstruction matrix.
/// @return False when an input is non-finite, singular, or produces a non-finite result.
[[nodiscard]] bool makeRtgiCameraRelativeInverseViewProjection(const glm::mat4& projection, const glm::mat4& view,
                                                               const glm::vec3& cameraPosition,
                                                               const glm::vec3& sceneOrigin,
                                                               glm::mat4& inverseViewProjection);

/// Hashes stable material and geometry identities for the RTGI validation image.
/// @param stableMaterialId Non-zero stable material identity from primitive metadata.
/// @param stableGeometryId Non-zero stable geometry identity from primitive metadata.
/// @return Deterministic 32-bit identity word shared with GLSL validation.
[[nodiscard]] uint32_t rtgiStableHitIdentityHash(uint32_t stableMaterialId, uint32_t stableGeometryId);

/// Hashes one resident terrain BLAS generation independently from its TLAS custom index.
/// @param blasRevision Resident terrain BLAS revision stored in the TLAS hit-data table.
/// @param vertexAddress Device address of the retained generation's immutable vertex buffer.
/// @return Deterministic resident-generation identity shared with GLSL validation.
[[nodiscard]] uint32_t rtgiTerrainHitIdentityHash(uint64_t blasRevision, uint64_t vertexAddress);

/// Produces the per-frame low-discrepancy Cranley-Patterson rotation used by RTGI.
/// @param frameIndex Low 32 bits of the deterministic render-frame index.
/// @return Two values in the half-open interval [0, 1).
[[nodiscard]] glm::vec2 rtgiCranleyPattersonRotation(uint32_t frameIndex);

/// Produces a deterministic per-pixel permutation of the temporal R2 rotation.
/// @param frameIndex Low 32 bits of the deterministic render-frame index.
/// @param pixel Integer render-resolution pixel coordinate.
/// @return Pixel-scrambled low-discrepancy rotation in the half-open interval [0, 1).
[[nodiscard]] glm::vec2 rtgiPixelScrambledCranleyPattersonRotation(uint32_t frameIndex, const glm::uvec2& pixel);

/// Produces one randomized low-discrepancy Reference sample from a complete
/// periodic 64-point Hammersley set. A six-bit parity partition gives both
/// 32-sample halves balanced dyadic coverage, and validation warmup cannot
/// change the set.
/// @param frameIndex Consecutive rendered frame index.
/// @param pixel Pixel coordinate used to derive an independent set rotation.
/// @return Two values in the half-open interval [0, 1).
[[nodiscard]] glm::vec2 rtgiReferenceHammersleySample(uint32_t frameIndex, const glm::uvec2& pixel);

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

/// Validates and decodes one fixed-layout GPU counter readback.
/// @param words Exact reduction payload copied from the GPU statistics buffer.
/// @param sequence Non-zero monotonic readback identity assigned by the owning pass.
/// @param frameIndex Source render-frame identity captured when the copy was recorded.
/// @param width Source validation-image width.
/// @param height Source validation-image height.
/// @return Validated 64-bit counter snapshot, or no value when any GPU invariant is broken.
[[nodiscard]] std::optional<RtgiTraceCounterFrameStats>
decodeRtgiTraceCounterReadback(const std::array<uint32_t, kRtgiTraceCounterWordCount>& words, uint64_t sequence,
                               uint64_t frameIndex, uint32_t width, uint32_t height);

} // namespace renderer::contracts

#endif // MECRAFT_RTGI_SAMPLING_CONTRACT_H
