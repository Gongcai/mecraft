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

/// Push constants shared by the production RTGI trace pass and Vulkan smoke validation.
struct alignas(16) RtgiTracePushConstants final {
    glm::mat4 inverseViewProjection{1.0f};
    glm::vec4 cameraPositionAndMaxDistance{0.0f, 0.0f, 0.0f, 64.0f};
    glm::vec4 renderExtentAndBias{1.0f, 1.0f, 0.001f, 0.0f};
    glm::uvec4 frameMaskAndFlags{0u, 1u, 0u, 0u};
};

static_assert(sizeof(RtgiTracePushConstants) == 112u);

/// Hashes one sample-sequence value with the integer permutation shared by GLSL.
/// @param value Input sequence value.
/// @return Deterministic 32-bit permutation of value.
[[nodiscard]] uint32_t rtgiSampleHash(uint32_t value);

/// Produces the per-frame two-dimensional Cranley-Patterson rotation used by RTGI.
/// @param frameIndex Low 32 bits of the deterministic render-frame index.
/// @return Two values in the half-open interval [0, 1).
[[nodiscard]] glm::vec2 rtgiCranleyPattersonRotation(uint32_t frameIndex);

/// Maps one two-dimensional sample to a cosine-weighted world-space hemisphere direction.
/// @param sample Two values in the half-open interval [0, 1).
/// @param normal Finite non-zero world-space surface normal.
/// @return Unit direction in the normal hemisphere, or no value when the contract is invalid.
[[nodiscard]] std::optional<glm::vec3> rtgiCosineHemisphereDirection(const glm::vec2& sample, const glm::vec3& normal);

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
