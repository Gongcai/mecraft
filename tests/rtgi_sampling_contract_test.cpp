#include "renderer/contracts/RtgiSamplingContract.h"

#include <glm/geometric.hpp>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>

namespace {
[[nodiscard]] bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}
} // namespace

int main() {
    using namespace renderer::contracts;

    bool valid = true;
    valid = requireTrue(rtgiSampleHash(0u) == 0u && rtgiSampleHash(1u) == 1753845952u &&
                            rtgiSampleHash(0xffffffffu) == 1734902346u,
                        "RTGI sample hash must remain bit-exact") &&
            valid;

    const glm::vec2 firstRotation = rtgiCranleyPattersonRotation(0u);
    const glm::vec2 repeatedRotation = rtgiCranleyPattersonRotation(0u);
    const glm::vec2 nextRotation = rtgiCranleyPattersonRotation(1u);
    valid = requireTrue(firstRotation == repeatedRotation && firstRotation.x >= 0.0f && firstRotation.x < 1.0f &&
                            firstRotation.y >= 0.0f && firstRotation.y < 1.0f && firstRotation != nextRotation,
                        "RTGI Cranley-Patterson rotation must be deterministic and frame-varying") &&
            valid;

    const std::optional<glm::vec3> pole = rtgiCosineHemisphereDirection(glm::vec2(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    valid = requireTrue(pole.has_value() && glm::length(*pole - glm::vec3(0.0f, 1.0f, 0.0f)) <= 1.0e-6f,
                        "Zero radial RTGI sample must map to the surface normal") &&
            valid;

    const std::optional<glm::vec3> known =
        rtgiCosineHemisphereDirection(glm::vec2(0.25f, 0.75f), glm::vec3(0.0f, 1.0f, 0.0f));
    valid = requireTrue(known.has_value() && std::abs(glm::length(*known) - 1.0f) <= 1.0e-5f &&
                            std::abs(glm::dot(*known, glm::vec3(0.0f, 1.0f, 0.0f)) - 0.5f) <= 1.0e-5f,
                        "RTGI cosine sample must preserve unit length and the analytic cosine") &&
            valid;

    valid =
        requireTrue(!rtgiCosineHemisphereDirection(glm::vec2(-0.1f, 0.5f), glm::vec3(0.0f, 1.0f, 0.0f)).has_value() &&
                        !rtgiCosineHemisphereDirection(glm::vec2(0.5f), glm::vec3(0.0f)).has_value(),
                    "RTGI cosine sampling must reject invalid input contracts") &&
        valid;

    constexpr uint32_t kSampleCount = 4096u;
    double cosineSum = 0.0;
    for (uint32_t index = 0u; index < kSampleCount; ++index) {
        const glm::vec2 sample{static_cast<float>(rtgiSampleHash(index * 2u) & 0x00ffffffu) / 16777216.0f,
                               static_cast<float>(rtgiSampleHash(index * 2u + 1u) & 0x00ffffffu) / 16777216.0f};
        const std::optional<glm::vec3> direction = rtgiCosineHemisphereDirection(sample, glm::vec3(0.0f, 0.0f, 1.0f));
        if (!direction.has_value() || std::abs(glm::length(*direction) - 1.0f) > 1.0e-5f || direction->z < 0.0f) {
            valid = false;
            break;
        }
        cosineSum += direction->z;
    }
    const double meanCosine = cosineSum / static_cast<double>(kSampleCount);
    valid = requireTrue(valid && std::abs(meanCosine - (2.0 / 3.0)) <= 0.02,
                        "RTGI sample distribution must remain cosine weighted") &&
            valid;

    valid = requireTrue(static_cast<uint32_t>(RtgiTraceClassification::Sky) == 0u &&
                            static_cast<uint32_t>(RtgiTraceClassification::Translucent) == 1u &&
                            static_cast<uint32_t>(RtgiTraceClassification::Miss) == 2u &&
                            static_cast<uint32_t>(RtgiTraceClassification::Hit) == 3u &&
                            static_cast<uint32_t>(RtgiTraceClassification::NonFinite) == 4u,
                        "RTGI validation classifications must remain stable") &&
            valid;

    const std::optional<uint32_t> packedValidation = encodeRtgiTraceValidation(RtgiTraceClassification::Hit, 17u, 1u);
    valid =
        requireTrue(
            packedValidation.has_value() &&
                rtgiTraceValidationClassification(*packedValidation) == RtgiTraceClassification::Hit &&
                rtgiTraceValidationCandidateCount(*packedValidation) == 17u &&
                rtgiTraceValidationConfirmedCount(*packedValidation) == 1u &&
                !encodeRtgiTraceValidation(RtgiTraceClassification::Miss, kRtgiTraceValidationCandidateMask + 1u, 0u)
                     .has_value() &&
                !encodeRtgiTraceValidation(RtgiTraceClassification::Miss, 0u, kRtgiTraceValidationConfirmedMask + 1u)
                     .has_value(),
            "RTGI validation packing must preserve classification and Cutout counters") &&
        valid;
    return valid ? 0 : 1;
}
