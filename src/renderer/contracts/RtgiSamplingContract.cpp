#include "renderer/contracts/RtgiSamplingContract.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <limits>
#include <utility>

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

[[nodiscard]] bool finite(const glm::mat4& value) {
    for (uint32_t column = 0u; column < 4u; ++column) {
        for (uint32_t row = 0u; row < 4u; ++row) {
            if (!std::isfinite(value[column][row])) {
                return false;
            }
        }
    }
    return true;
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

bool makeRtgiCameraRelativeInverseViewProjection(const glm::mat4& projection, const glm::mat4& view,
                                                 const glm::vec3& cameraPosition, const glm::vec3& sceneOrigin,
                                                 glm::mat4& inverseViewProjection) {
    if (!finite(projection) || !finite(view) || !finite(cameraPosition) || !finite(sceneOrigin)) {
        return false;
    }
    glm::mat4 viewRotation = view;
    viewRotation[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    const glm::mat4 inverseProjection = glm::inverse(projection);
    const glm::mat4 inverseViewRotation = glm::inverse(viewRotation);
    const glm::vec3 cameraRelativePosition = cameraPosition - sceneOrigin;
    inverseViewProjection =
        glm::translate(glm::mat4(1.0f), cameraRelativePosition) * inverseViewRotation * inverseProjection;
    return finite(inverseViewProjection);
}

uint32_t rtgiStableHitIdentityHash(const uint32_t stableMaterialId, const uint32_t stableGeometryId) {
    return rtgiSampleHash(stableMaterialId * 0x9e3779b9u ^ stableGeometryId * 0x85ebca6bu);
}

uint32_t rtgiTerrainHitIdentityHash(const uint64_t blasRevision, const uint64_t vertexAddress) {
    const uint32_t revisionLow = static_cast<uint32_t>(blasRevision);
    const uint32_t revisionHigh = static_cast<uint32_t>(blasRevision >> 32u);
    const uint32_t addressLow = static_cast<uint32_t>(vertexAddress);
    const uint32_t addressHigh = static_cast<uint32_t>(vertexAddress >> 32u);
    const uint32_t revisionMix = revisionLow ^ revisionHigh * 0x9e3779b9u;
    const uint32_t addressMix = addressLow * 0x27d4eb2du ^ addressHigh * 0x85ebca6bu;
    return rtgiSampleHash(revisionMix ^ addressMix);
}

glm::vec2 rtgiCranleyPattersonRotation(const uint32_t frameIndex) {
    const float sequenceIndex = static_cast<float>(frameIndex & 0x00ffffffu) + 1.0f;
    return glm::fract(kR2Increment * sequenceIndex);
}

glm::vec2 rtgiPixelScrambledCranleyPattersonRotation(const uint32_t frameIndex, const glm::uvec2& pixel) {
    const uint32_t scramble = rtgiSampleHash(pixel.x * 1973u + pixel.y * 9277u + 26699u);
    const uint32_t stride = 1u + 2u * ((scramble >> 3u) & 3u);
    glm::vec2 rotation = rtgiCranleyPattersonRotation(frameIndex * stride);
    if ((scramble & 1u) != 0u) {
        std::swap(rotation.x, rotation.y);
    }
    if ((scramble & 2u) != 0u) {
        rotation.x = glm::fract(-rotation.x);
    }
    if ((scramble & 4u) != 0u) {
        rotation.y = glm::fract(-rotation.y);
    }
    return rotation;
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

std::optional<glm::vec3> rtgiVoxelGeometricNormal(const glm::vec3& shadingNormal) {
    if (!finite(shadingNormal) || glm::dot(shadingNormal, shadingNormal) <= 1.0e-12f) {
        return std::nullopt;
    }
    const glm::vec3 magnitude = glm::abs(shadingNormal);
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z) {
        return glm::vec3(std::copysign(1.0f, shadingNormal.x), 0.0f, 0.0f);
    }
    if (magnitude.y >= magnitude.z) {
        return glm::vec3(0.0f, std::copysign(1.0f, shadingNormal.y), 0.0f);
    }
    return glm::vec3(0.0f, 0.0f, std::copysign(1.0f, shadingNormal.z));
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

std::optional<RtgiTraceCounterFrameStats>
decodeRtgiTraceCounterReadback(const std::array<uint32_t, kRtgiTraceCounterWordCount>& words, const uint64_t sequence,
                               const uint64_t frameIndex, const uint32_t width, const uint32_t height) {
    const auto word = [&](const RtgiTraceCounterWord index) {
        return words[static_cast<size_t>(index)];
    };
    if (sequence == 0u || width == 0u || height == 0u ||
        word(RtgiTraceCounterWord::ContractVersion) != kRtgiTraceCounterContractVersion ||
        word(RtgiTraceCounterWord::InvariantError) != 0u) {
        return std::nullopt;
    }

    const auto combineWords = [](const uint32_t low, const uint32_t high) {
        return static_cast<uint64_t>(low) | (static_cast<uint64_t>(high) << 32u);
    };
    const uint64_t pixelCount =
        combineWords(word(RtgiTraceCounterWord::PixelLow), word(RtgiTraceCounterWord::PixelHigh));
    const uint64_t expectedPixelCount = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    const uint64_t candidateCount =
        combineWords(word(RtgiTraceCounterWord::CandidateLow), word(RtgiTraceCounterWord::CandidateHigh));
    const uint64_t confirmedCount =
        combineWords(word(RtgiTraceCounterWord::ConfirmedLow), word(RtgiTraceCounterWord::ConfirmedHigh));
    const uint32_t peakCandidate = word(RtgiTraceCounterWord::PeakCandidatePerPixel);
    const uint32_t peakConfirmed = word(RtgiTraceCounterWord::PeakConfirmedPerPixel);
    const auto withinPerPixelBound = [](const uint64_t count, const uint64_t pixels, const uint32_t maximumPerPixel) {
        return pixels > std::numeric_limits<uint64_t>::max() / maximumPerPixel || count <= pixels * maximumPerPixel;
    };
    if (pixelCount != expectedPixelCount || confirmedCount > candidateCount ||
        peakCandidate > kRtgiTraceValidationCandidateMask || peakConfirmed > kRtgiTraceValidationConfirmedMask ||
        peakConfirmed > peakCandidate ||
        !withinPerPixelBound(candidateCount, pixelCount, kRtgiTraceValidationCandidateMask) ||
        !withinPerPixelBound(confirmedCount, pixelCount, kRtgiTraceValidationConfirmedMask) ||
        (candidateCount == 0u) != (peakCandidate == 0u) || (confirmedCount == 0u) != (peakConfirmed == 0u)) {
        return std::nullopt;
    }

    RtgiTraceCounterFrameStats stats;
    stats.supported = true;
    stats.valid = true;
    stats.sequence = sequence;
    stats.frameIndex = frameIndex;
    stats.width = width;
    stats.height = height;
    stats.pixelCount = pixelCount;
    stats.candidateCount = candidateCount;
    stats.confirmedCount = confirmedCount;
    stats.peakCandidateCountPerPixel = peakCandidate;
    stats.peakConfirmedCountPerPixel = peakConfirmed;
    return stats;
}

} // namespace renderer::contracts
