#ifndef MECRAFT_RTGI_QUALITY_VALIDATION_CONTRACT_H
#define MECRAFT_RTGI_QUALITY_VALIDATION_CONTRACT_H

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

namespace renderer::contracts {

/// Describes one scene-referred linear RGB image used by deterministic RTGI validation.
struct RtgiLinearImage final {
    uint32_t width = 0u;
    uint32_t height = 0u;
    std::vector<glm::vec3> pixels;
};

/// Identifies a rectangular pixel region inside a linear validation image.
struct RtgiValidationRoi final {
    uint32_t x = 0u;
    uint32_t y = 0u;
    uint32_t width = 0u;
    uint32_t height = 0u;
};

/// Reports the deterministic reason a linear HDR quality metric cannot be evaluated.
enum class RtgiQualityMetricError : uint8_t {
    None,
    InvalidImage,
    InvalidRoi,
    ImageExtentMismatch,
    InsufficientFrames,
    NonFiniteRadiance,
    NegativeRadiance
};

/// Contains temporal luminance variance before and after RTGI denoising.
struct RtgiTemporalVarianceMetrics final {
    double rawVariance = 0.0;
    double denoisedVariance = 0.0;
    double reductionPercent = 0.0;
};

/// Contains the display-independent comparison between a denoised image and a linear reference.
struct RtgiReferenceComparisonMetrics final {
    double luminanceSsim = 0.0;
    double relativeLuminanceErrorP50 = 0.0;
    double relativeLuminanceErrorP95 = 0.0;
    double absoluteLuminanceErrorP95 = 0.0;
    double comparedLuminanceAtRelativeP95 = 0.0;
    double referenceLuminanceAtRelativeP95 = 0.0;
    double denominatorFloorPixelPercent = 0.0;
    uint32_t relativeP95X = 0u;
    uint32_t relativeP95Y = 0u;
};

inline constexpr double kRtgiLeakageRelativeErrorThreshold = 0.10;
inline constexpr double kRtgiLeakageDepthDiscontinuityThreshold = 0.02;
inline constexpr double kRtgiLeakageNormalDotThreshold = 0.95;
inline constexpr uint32_t kRtgiLeakageMaximumBandWidthPixels = 2u;

/// Contains the fixed-boundary leakage extent measured from linear depth and world normals.
struct RtgiLeakageBandMetrics final {
    uint64_t boundaryPixelCount = 0u;
    uint64_t leakagePixelCount = 0u;
    uint32_t maximumBandWidthPixels = 0u;
    uint32_t maximumBandX = 0u;
    uint32_t maximumBandY = 0u;
    uint32_t maximumBandSeedX = 0u;
    uint32_t maximumBandSeedY = 0u;
    uint32_t maximumBandOppositeSeedX = 0u;
    uint32_t maximumBandOppositeSeedY = 0u;
    double denoisedLuminanceAtMaximum = 0.0;
    double referenceLuminanceAtMaximum = 0.0;
    double seedReferenceLuminanceAtMaximum = 0.0;
    double oppositeSeedReferenceLuminanceAtMaximum = 0.0;
    double seedRelativeDepthDifferenceAtMaximum = 0.0;
    double seedNormalDotAtMaximum = 1.0;
    bool positiveErrorAtMaximum = false;
};

/// Validates image dimensions, storage, and finite non-negative radiance in an ROI.
/// @param image Linear RGB image to validate.
/// @param roi Region to inspect.
/// @return None when the complete ROI is valid, otherwise the first deterministic validation error.
[[nodiscard]] RtgiQualityMetricError validateRtgiLinearImage(const RtgiLinearImage& image,
                                                             const RtgiValidationRoi& roi);

/// Calculates mean per-pixel temporal luminance variance for matched raw and denoised frame sequences.
/// @param rawFrames One-spp raw RTGI frames in deterministic sample order.
/// @param denoisedFrames Denoised frames with exactly matching extents and sequence count.
/// @param roi Fixed validation region that excludes sky and intentional animation.
/// @param metrics Receives raw variance, denoised variance, and percentage reduction.
/// @return None on success, otherwise the reason metric evaluation is invalid.
[[nodiscard]] RtgiQualityMetricError
calculateRtgiTemporalVariance(const std::vector<const RtgiLinearImage*>& rawFrames,
                              const std::vector<const RtgiLinearImage*>& denoisedFrames, const RtgiValidationRoi& roi,
                              RtgiTemporalVarianceMetrics& metrics);

/// Compares a denoised 32-frame result to its fixed-sample linear HDR reference.
/// @param denoised Accumulated denoised image.
/// @param reference 64-spp linear diffuse reference image.
/// @param roi Fixed validation region that excludes sky and exposure saturation.
/// @param metrics Receives global luminance SSIM, error percentiles, and the pixel represented by relative p95.
/// @return None on success, otherwise the reason metric evaluation is invalid.
[[nodiscard]] RtgiQualityMetricError compareRtgiLinearReference(const RtgiLinearImage& denoised,
                                                                const RtgiLinearImage& reference,
                                                                const RtgiValidationRoi& roi,
                                                                RtgiReferenceComparisonMetrics& metrics);

/// Measures connected luminance error extending away from depth or normal discontinuities.
/// @param denoised Accumulated denoised linear radiance.
/// @param reference Fixed-sample linear radiance reference.
/// @param worldNormal Unit world-space normals in signed RGB components.
/// @param viewZ Positive linear view depth replicated or stored in the red component.
/// @param roi Fixed static-quality region shared by all inputs.
/// @param metrics Receives boundary count, connected leakage pixels, and maximum band width.
/// @return None when all inputs and guides satisfy the leakage measurement contract.
[[nodiscard]] RtgiQualityMetricError calculateRtgiLeakageBand(const RtgiLinearImage& denoised,
                                                              const RtgiLinearImage& reference,
                                                              const RtgiLinearImage& worldNormal,
                                                              const RtgiLinearImage& viewZ,
                                                              const RtgiValidationRoi& roi,
                                                              RtgiLeakageBandMetrics& metrics);

/// Returns the stable diagnostic identifier for a quality metric error.
/// @param error Error value to identify.
/// @return Process-lifetime identifier string.
[[nodiscard]] const char* rtgiQualityMetricErrorStableId(RtgiQualityMetricError error);

} // namespace renderer::contracts

#endif // MECRAFT_RTGI_QUALITY_VALIDATION_CONTRACT_H
