#include "RtgiQualityValidationContract.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace renderer::contracts {
namespace {

constexpr double kLuminanceRed = 0.2126;
constexpr double kLuminanceGreen = 0.7152;
constexpr double kLuminanceBlue = 0.0722;
constexpr double kSsimLuminanceStability = 0.01 * 0.01;
constexpr double kSsimContrastStability = 0.03 * 0.03;
constexpr double kRelativeErrorDenominator = 1.0e-4;

struct LuminanceErrorSample final {
    double relative = 0.0;
    double absolute = 0.0;
    double compared = 0.0;
    double reference = 0.0;
    uint32_t x = 0u;
    uint32_t y = 0u;
};

[[nodiscard]] bool validExtent(const RtgiLinearImage& image) {
    const uint64_t pixelCount = static_cast<uint64_t>(image.width) * image.height;
    return image.width != 0u && image.height != 0u && pixelCount <= std::numeric_limits<size_t>::max() &&
           image.pixels.size() == pixelCount;
}

[[nodiscard]] bool validRoi(const RtgiLinearImage& image, const RtgiValidationRoi& roi) {
    return roi.width != 0u && roi.height != 0u && roi.x <= image.width && roi.y <= image.height &&
           roi.width <= image.width - roi.x && roi.height <= image.height - roi.y;
}

[[nodiscard]] double luminance(const glm::vec3& value) {
    return kLuminanceRed * value.x + kLuminanceGreen * value.y + kLuminanceBlue * value.z;
}

[[nodiscard]] size_t pixelIndex(const RtgiLinearImage& image, const uint32_t x, const uint32_t y) {
    return static_cast<size_t>(y) * image.width + x;
}

} // namespace

RtgiQualityMetricError validateRtgiLinearImage(const RtgiLinearImage& image, const RtgiValidationRoi& roi) {
    if (!validExtent(image)) {
        return RtgiQualityMetricError::InvalidImage;
    }
    if (!validRoi(image, roi)) {
        return RtgiQualityMetricError::InvalidRoi;
    }
    for (uint32_t y = roi.y; y < roi.y + roi.height; ++y) {
        for (uint32_t x = roi.x; x < roi.x + roi.width; ++x) {
            const glm::vec3 value = image.pixels[pixelIndex(image, x, y)];
            if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) {
                return RtgiQualityMetricError::NonFiniteRadiance;
            }
            if (value.x < 0.0f || value.y < 0.0f || value.z < 0.0f) {
                return RtgiQualityMetricError::NegativeRadiance;
            }
        }
    }
    return RtgiQualityMetricError::None;
}

RtgiQualityMetricError calculateRtgiTemporalVariance(const std::vector<const RtgiLinearImage*>& rawFrames,
                                                     const std::vector<const RtgiLinearImage*>& denoisedFrames,
                                                     const RtgiValidationRoi& roi,
                                                     RtgiTemporalVarianceMetrics& metrics) {
    metrics = {};
    if (rawFrames.size() < 2u || rawFrames.size() != denoisedFrames.size()) {
        return RtgiQualityMetricError::InsufficientFrames;
    }
    const RtgiLinearImage* const firstRaw = rawFrames.front();
    const RtgiLinearImage* const firstDenoised = denoisedFrames.front();
    if (firstRaw == nullptr || firstDenoised == nullptr) {
        return RtgiQualityMetricError::InvalidImage;
    }
    if (firstRaw->width != firstDenoised->width || firstRaw->height != firstDenoised->height) {
        return RtgiQualityMetricError::ImageExtentMismatch;
    }
    for (size_t frame = 0u; frame < rawFrames.size(); ++frame) {
        if (rawFrames[frame] == nullptr || denoisedFrames[frame] == nullptr ||
            rawFrames[frame]->width != firstRaw->width || rawFrames[frame]->height != firstRaw->height ||
            denoisedFrames[frame]->width != firstRaw->width || denoisedFrames[frame]->height != firstRaw->height) {
            return RtgiQualityMetricError::ImageExtentMismatch;
        }
        const RtgiQualityMetricError rawError = validateRtgiLinearImage(*rawFrames[frame], roi);
        const RtgiQualityMetricError denoisedError = validateRtgiLinearImage(*denoisedFrames[frame], roi);
        if (rawError != RtgiQualityMetricError::None) {
            return rawError;
        }
        if (denoisedError != RtgiQualityMetricError::None) {
            return denoisedError;
        }
    }

    const double frameCount = static_cast<double>(rawFrames.size());
    const double inverseFrameCount = 1.0 / frameCount;
    const double inverseSampleCount = 1.0 / (frameCount - 1.0);
    double rawVarianceSum = 0.0;
    double denoisedVarianceSum = 0.0;
    for (uint32_t y = roi.y; y < roi.y + roi.height; ++y) {
        for (uint32_t x = roi.x; x < roi.x + roi.width; ++x) {
            double rawSum = 0.0;
            double rawSquareSum = 0.0;
            double denoisedSum = 0.0;
            double denoisedSquareSum = 0.0;
            for (size_t frame = 0u; frame < rawFrames.size(); ++frame) {
                const double rawValue = luminance(rawFrames[frame]->pixels[pixelIndex(*firstRaw, x, y)]);
                const double denoisedValue = luminance(denoisedFrames[frame]->pixels[pixelIndex(*firstRaw, x, y)]);
                rawSum += rawValue;
                rawSquareSum += rawValue * rawValue;
                denoisedSum += denoisedValue;
                denoisedSquareSum += denoisedValue * denoisedValue;
            }
            rawVarianceSum += (rawSquareSum - rawSum * rawSum * inverseFrameCount) * inverseSampleCount;
            denoisedVarianceSum +=
                (denoisedSquareSum - denoisedSum * denoisedSum * inverseFrameCount) * inverseSampleCount;
        }
    }
    const double pixelCount = static_cast<double>(roi.width) * roi.height;
    metrics.rawVariance = std::max(0.0, rawVarianceSum / pixelCount);
    metrics.denoisedVariance = std::max(0.0, denoisedVarianceSum / pixelCount);
    metrics.reductionPercent = metrics.rawVariance > 0.0
                                   ? (metrics.rawVariance - metrics.denoisedVariance) * 100.0 / metrics.rawVariance
                                   : 0.0;
    return RtgiQualityMetricError::None;
}

RtgiQualityMetricError compareRtgiLinearReference(const RtgiLinearImage& denoised, const RtgiLinearImage& reference,
                                                  const RtgiValidationRoi& roi,
                                                  RtgiReferenceComparisonMetrics& metrics) {
    metrics = {};
    if (denoised.width != reference.width || denoised.height != reference.height) {
        return RtgiQualityMetricError::ImageExtentMismatch;
    }
    const RtgiQualityMetricError denoisedError = validateRtgiLinearImage(denoised, roi);
    const RtgiQualityMetricError referenceError = validateRtgiLinearImage(reference, roi);
    if (denoisedError != RtgiQualityMetricError::None) {
        return denoisedError;
    }
    if (referenceError != RtgiQualityMetricError::None) {
        return referenceError;
    }

    const double pixelCount = static_cast<double>(roi.width) * roi.height;
    double denoisedSum = 0.0;
    double referenceSum = 0.0;
    double denoisedSquareSum = 0.0;
    double referenceSquareSum = 0.0;
    double productSum = 0.0;
    std::vector<LuminanceErrorSample> errors;
    errors.reserve(static_cast<size_t>(roi.width) * roi.height);
    uint64_t denominatorFloorPixelCount = 0u;
    for (uint32_t y = roi.y; y < roi.y + roi.height; ++y) {
        for (uint32_t x = roi.x; x < roi.x + roi.width; ++x) {
            const double denoisedLuminance = luminance(denoised.pixels[pixelIndex(denoised, x, y)]);
            const double referenceLuminance = luminance(reference.pixels[pixelIndex(reference, x, y)]);
            denoisedSum += denoisedLuminance;
            referenceSum += referenceLuminance;
            denoisedSquareSum += denoisedLuminance * denoisedLuminance;
            referenceSquareSum += referenceLuminance * referenceLuminance;
            productSum += denoisedLuminance * referenceLuminance;
            const double absoluteError = std::abs(denoisedLuminance - referenceLuminance);
            errors.push_back({absoluteError / std::max(referenceLuminance, kRelativeErrorDenominator), absoluteError,
                              denoisedLuminance, referenceLuminance, x, y});
            denominatorFloorPixelCount += referenceLuminance < kRelativeErrorDenominator ? 1u : 0u;
        }
    }
    const double denoisedMean = denoisedSum / pixelCount;
    const double referenceMean = referenceSum / pixelCount;
    const double inverseSampleCount = pixelCount > 1.0 ? 1.0 / (pixelCount - 1.0) : 0.0;
    const double denoisedVariance =
        std::max(0.0, (denoisedSquareSum - pixelCount * denoisedMean * denoisedMean) * inverseSampleCount);
    const double referenceVariance =
        std::max(0.0, (referenceSquareSum - pixelCount * referenceMean * referenceMean) * inverseSampleCount);
    const double covariance = (productSum - pixelCount * denoisedMean * referenceMean) * inverseSampleCount;
    metrics.luminanceSsim =
        ((2.0 * denoisedMean * referenceMean + kSsimLuminanceStability) * (2.0 * covariance + kSsimContrastStability)) /
        ((denoisedMean * denoisedMean + referenceMean * referenceMean + kSsimLuminanceStability) *
         (denoisedVariance + referenceVariance + kSsimContrastStability));
    const size_t p50Index = static_cast<size_t>(std::ceil(0.50 * errors.size())) - 1u;
    const size_t p95Index = static_cast<size_t>(std::ceil(0.95 * errors.size())) - 1u;
    std::sort(errors.begin(), errors.end(), [](const LuminanceErrorSample& lhs, const LuminanceErrorSample& rhs) {
        if (lhs.relative != rhs.relative) {
            return lhs.relative < rhs.relative;
        }
        if (lhs.y != rhs.y) {
            return lhs.y < rhs.y;
        }
        return lhs.x < rhs.x;
    });
    metrics.relativeLuminanceErrorP50 = errors[p50Index].relative;
    metrics.relativeLuminanceErrorP95 = errors[p95Index].relative;
    metrics.comparedLuminanceAtRelativeP95 = errors[p95Index].compared;
    metrics.referenceLuminanceAtRelativeP95 = errors[p95Index].reference;
    metrics.relativeP95X = errors[p95Index].x;
    metrics.relativeP95Y = errors[p95Index].y;
    metrics.denominatorFloorPixelPercent =
        100.0 * static_cast<double>(denominatorFloorPixelCount) / pixelCount;
    std::nth_element(errors.begin(), errors.begin() + p95Index, errors.end(),
                     [](const LuminanceErrorSample& lhs, const LuminanceErrorSample& rhs) {
                         return lhs.absolute < rhs.absolute;
                     });
    metrics.absoluteLuminanceErrorP95 = errors[p95Index].absolute;
    return RtgiQualityMetricError::None;
}

const char* rtgiQualityMetricErrorStableId(const RtgiQualityMetricError error) {
    switch (error) {
    case RtgiQualityMetricError::None: return "None";
    case RtgiQualityMetricError::InvalidImage: return "InvalidImage";
    case RtgiQualityMetricError::InvalidRoi: return "InvalidRoi";
    case RtgiQualityMetricError::ImageExtentMismatch: return "ImageExtentMismatch";
    case RtgiQualityMetricError::InsufficientFrames: return "InsufficientFrames";
    case RtgiQualityMetricError::NonFiniteRadiance: return "NonFiniteRadiance";
    case RtgiQualityMetricError::NegativeRadiance: return "NegativeRadiance";
    }
    std::abort();
}

} // namespace renderer::contracts
