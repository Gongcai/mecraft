#include "renderer/contracts/RtgiQualityValidationContract.h"

#include <array>
#include <cmath>
#include <iostream>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[rtgi_quality_validation_contract_test] FAIL: " << message << '\n';
        return false;
    }
    return true;
}

renderer::contracts::RtgiLinearImage makeImage(const std::array<float, 4u>& luminance) {
    renderer::contracts::RtgiLinearImage image;
    image.width = 2u;
    image.height = 2u;
    for (const float value : luminance) {
        image.pixels.emplace_back(value, value, value);
    }
    return image;
}

} // namespace

int main() {
    using namespace renderer::contracts;
    const RtgiValidationRoi roi{0u, 0u, 2u, 2u};
    const RtgiLinearImage rawA = makeImage({1.0f, 2.0f, 3.0f, 4.0f});
    const RtgiLinearImage rawB = makeImage({3.0f, 4.0f, 5.0f, 6.0f});
    const RtgiLinearImage denoisedA = makeImage({1.5f, 2.5f, 3.5f, 4.5f});
    const RtgiLinearImage denoisedB = makeImage({2.0f, 3.0f, 4.0f, 5.0f});
    RtgiTemporalVarianceMetrics variance;
    if (!requireTrue(calculateRtgiTemporalVariance({&rawA, &rawB}, {&denoisedA, &denoisedB}, roi, variance) ==
                         RtgiQualityMetricError::None,
                     "matched finite frame sequences must calculate temporal variance") ||
        !requireTrue(std::abs(variance.rawVariance - 2.0) < 1.0e-12 &&
                         std::abs(variance.denoisedVariance - 0.125) < 1.0e-12 &&
                         std::abs(variance.reductionPercent - 93.75) < 1.0e-12,
                     "temporal variance must use per-pixel unbiased sequence variance")) {
        return 1;
    }

    const RtgiLinearImage reference = makeImage({2.0f, 3.0f, 4.0f, 5.0f});
    RtgiReferenceComparisonMetrics comparison;
    const bool exactComparison =
        compareRtgiLinearReference(reference, reference, roi, comparison) == RtgiQualityMetricError::None &&
        std::abs(comparison.luminanceSsim - 1.0) < 1.0e-12 && comparison.relativeLuminanceErrorP50 == 0.0 &&
        comparison.relativeLuminanceErrorP95 == 0.0 && comparison.absoluteLuminanceErrorP95 == 0.0 &&
        std::abs(comparison.comparedLuminanceAtRelativeP95 - 5.0) < 1.0e-6 &&
        std::abs(comparison.referenceLuminanceAtRelativeP95 - 5.0) < 1.0e-6 &&
        comparison.denominatorFloorPixelPercent == 0.0 &&
        comparison.relativeP95X == 1u && comparison.relativeP95Y == 1u;
    if (!requireTrue(exactComparison, "an image must exactly match itself in linear reference metrics")) {
        std::cerr << "[rtgi_quality_validation_contract_test] exact metrics: ssim=" << comparison.luminanceSsim
                  << " p50=" << comparison.relativeLuminanceErrorP50
                  << " p95=" << comparison.relativeLuminanceErrorP95
                  << " absolute_p95=" << comparison.absoluteLuminanceErrorP95
                  << " compared=" << comparison.comparedLuminanceAtRelativeP95
                  << " reference=" << comparison.referenceLuminanceAtRelativeP95
                  << " floor_percent=" << comparison.denominatorFloorPixelPercent
                  << " pixel=(" << comparison.relativeP95X << ',' << comparison.relativeP95Y << ")\n";
        return 1;
    }

    const RtgiLinearImage denominatorFloor = makeImage({0.0f, 0.001f, 1.0f, 2.0f});
    if (!requireTrue(compareRtgiLinearReference(denominatorFloor, denominatorFloor, roi, comparison) ==
                             RtgiQualityMetricError::None &&
                         comparison.denominatorFloorPixelPercent == 25.0,
                     "reference diagnostics must count pixels that use the relative-error denominator floor")) {
        return 1;
    }

    RtgiLinearImage nonFinite = reference;
    nonFinite.pixels[1].x = std::numeric_limits<float>::infinity();
    if (!requireTrue(validateRtgiLinearImage(nonFinite, roi) == RtgiQualityMetricError::NonFiniteRadiance &&
                         calculateRtgiTemporalVariance({&rawA}, {&denoisedA}, roi, variance) ==
                             RtgiQualityMetricError::InsufficientFrames,
                     "invalid radiance and incomplete sequences must reject metric evaluation")) {
        return 1;
    }

    std::cout << "[rtgi_quality_validation_contract_test] PASS\n";
    return 0;
}
