#include "renderer/contracts/RtgiSamplingContract.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <cmath>

namespace renderer::contracts {
namespace {
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr glm::vec2 kR2Increment{0.7548776662466927f, 0.5698402909980532f};

[[nodiscard]] bool finite(const glm::vec2& value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

uint32_t rtgiSampleHash(uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

uint32_t rtgiStableHitIdentityHash(const uint32_t stableMaterialId, const uint32_t stableGeometryId) {
    return rtgiSampleHash(stableMaterialId * 0x9e3779b9u ^ stableGeometryId * 0x85ebca6bu);
}

glm::vec2 rtgiCranleyPattersonRotation(const uint32_t frameIndex) {
    const float sequenceIndex = static_cast<float>(frameIndex & 0x00ffffffu) + 1.0f;
    return glm::fract(kR2Increment * sequenceIndex);
}

std::optional<glm::vec3> rtgiCosineHemisphereDirection(const glm::vec2& sample, const glm::vec3& normal) {
    if (!finite(sample) || sample.x < 0.0f || sample.x >= 1.0f || sample.y < 0.0f || sample.y >= 1.0f ||
        !finite(normal)) {
        return std::nullopt;
    }
    const float normalLengthSquared = glm::dot(normal, normal);
    if (!std::isfinite(normalLengthSquared) || normalLengthSquared <= 1.0e-12f) {
        return std::nullopt;
    }

    const glm::vec3 unitNormal = normal / std::sqrt(normalLengthSquared);
    const glm::vec3 helper =
        std::abs(unitNormal.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 tangent = glm::normalize(glm::cross(helper, unitNormal));
    const glm::vec3 bitangent = glm::cross(unitNormal, tangent);
    const float radius = std::sqrt(sample.y);
    const float angle = kTwoPi * sample.x;
    const glm::vec3 direction = tangent * (radius * std::cos(angle)) + bitangent * (radius * std::sin(angle)) +
                                unitNormal * std::sqrt(1.0f - sample.y);
    if (!finite(direction)) {
        return std::nullopt;
    }
    return glm::normalize(direction);
}

std::optional<uint32_t> encodeRtgiTraceValidation(const RtgiTraceClassification classification,
                                                  const uint32_t candidateCount, const uint32_t confirmedCount) {
    const uint32_t classificationValue = static_cast<uint32_t>(classification);
    if (classificationValue > kRtgiTraceValidationClassificationMask ||
        candidateCount > kRtgiTraceValidationCandidateMask || confirmedCount > kRtgiTraceValidationConfirmedMask) {
        return std::nullopt;
    }
    return classificationValue | (candidateCount << kRtgiTraceValidationCandidateShift) |
           (confirmedCount << kRtgiTraceValidationConfirmedShift);
}

} // namespace renderer::contracts
