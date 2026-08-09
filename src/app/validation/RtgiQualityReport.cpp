#include "app/validation/RtgiQualityReport.h"

#include "app/validation/RtgiQualityProfile.h"
#include "renderer/capture/TextureCapture.h"

#include <glm/gtc/packing.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <vector>

namespace app::validation {
namespace {

using renderer::contracts::RtgiLinearImage;
using renderer::contracts::RtgiQualityMetricError;
using renderer::contracts::RtgiValidationRoi;
using Json = nlohmann::json;

struct SequenceData final {
    std::vector<RtgiLinearImage> frames;
    std::vector<glm::dvec3> sum;
};

[[nodiscard]] std::string frameName(const std::string_view prefix, const uint32_t index) {
    std::ostringstream output;
    output << prefix << std::setw(4) << std::setfill('0') << index << ".exr";
    return output.str();
}

[[nodiscard]] bool sequenceFileName(const std::string& name, const std::string_view prefix) {
    return name.size() >= prefix.size() + 4u && name.compare(0u, prefix.size(), prefix) == 0 &&
           name.compare(name.size() - 4u, 4u, ".exr") == 0;
}

[[nodiscard]] RtgiQualityReportError validateSequenceNames(const std::filesystem::path& directory,
                                                           const std::string_view prefix, const uint32_t frameCount,
                                                           std::string& detail) {
    std::error_code directoryError;
    std::filesystem::directory_iterator iterator(directory, directoryError);
    if (directoryError) {
        detail = directory.generic_u8string() + ": " + directoryError.message();
        return RtgiQualityReportError::SequenceDirectoryReadFailed;
    }
    std::vector<std::string> expected;
    expected.reserve(frameCount);
    for (uint32_t index = 0u; index < frameCount; ++index) {
        expected.push_back(frameName(prefix, index));
    }
    const std::filesystem::directory_iterator end;
    for (; iterator != end; iterator.increment(directoryError)) {
        if (directoryError) {
            detail = directory.generic_u8string() + ": " + directoryError.message();
            return RtgiQualityReportError::SequenceDirectoryReadFailed;
        }
        const std::filesystem::directory_entry& entry = *iterator;
        const std::string name = entry.path().filename().generic_u8string();
        if (sequenceFileName(name, prefix) && std::find(expected.begin(), expected.end(), name) == expected.end()) {
            detail = entry.path().generic_u8string();
            return RtgiQualityReportError::UnexpectedSequenceFrame;
        }
    }
    if (directoryError) {
        detail = directory.generic_u8string() + ": " + directoryError.message();
        return RtgiQualityReportError::SequenceDirectoryReadFailed;
    }
    for (const std::string& name : expected) {
        std::error_code statusError;
        const bool regular = std::filesystem::is_regular_file(directory / name, statusError);
        if (statusError || !regular) {
            detail = (directory / name).generic_u8string();
            return RtgiQualityReportError::MissingSequenceFrame;
        }
    }
    return RtgiQualityReportError::None;
}

[[nodiscard]] RtgiQualityReportError loadFrame(const std::filesystem::path& path, const uint32_t expectedWidth,
                                               const uint32_t expectedHeight, std::vector<glm::vec3>& pixels,
                                               std::string& detail) {
    renderer::capture::LinearExrImage exr;
    const renderer::capture::TextureCaptureResult readResult = renderer::capture::readLinearExr(path, exr);
    if (!readResult.succeeded()) {
        detail = path.generic_u8string() + ": " + readResult.detail;
        return RtgiQualityReportError::ExrReadFailed;
    }
    if (exr.width != expectedWidth || exr.height != expectedHeight) {
        detail = path.generic_u8string();
        return RtgiQualityReportError::ImageExtentMismatch;
    }
    pixels.resize(static_cast<size_t>(exr.width) * exr.height);
    for (size_t pixel = 0u; pixel < pixels.size(); ++pixel) {
        const glm::vec3 value{glm::unpackHalf1x16(exr.rgb16f[pixel * 3u]),
                              glm::unpackHalf1x16(exr.rgb16f[pixel * 3u + 1u]),
                              glm::unpackHalf1x16(exr.rgb16f[pixel * 3u + 2u])};
        if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) {
            detail = path.generic_u8string() + ": pixel " + std::to_string(pixel);
            return RtgiQualityReportError::NonFiniteRadiance;
        }
        if (value.x < 0.0f || value.y < 0.0f || value.z < 0.0f) {
            detail = path.generic_u8string() + ": pixel " + std::to_string(pixel);
            return RtgiQualityReportError::NegativeRadiance;
        }
        pixels[pixel] = value;
    }
    return RtgiQualityReportError::None;
}

[[nodiscard]] RtgiLinearImage cropImage(const std::vector<glm::vec3>& pixels, const uint32_t sourceWidth,
                                        const RtgiValidationRoi& roi) {
    RtgiLinearImage image;
    image.width = roi.width;
    image.height = roi.height;
    image.pixels.resize(static_cast<size_t>(roi.width) * roi.height);
    for (uint32_t y = 0u; y < roi.height; ++y) {
        const glm::vec3* source = pixels.data() + static_cast<size_t>(roi.y + y) * sourceWidth + roi.x;
        glm::vec3* destination = image.pixels.data() + static_cast<size_t>(y) * roi.width;
        std::copy_n(source, roi.width, destination);
    }
    return image;
}

[[nodiscard]] RtgiQualityReportError loadSequence(const std::filesystem::path& directory, const std::string_view prefix,
                                                  const uint32_t frameCount, const RtgiQualityProfile& profile,
                                                  const bool retainRoiFrames, const bool accumulateFullImage,
                                                  SequenceData& data, std::string& detail) {
    data = {};
    const RtgiQualityReportError nameError = validateSequenceNames(directory, prefix, frameCount, detail);
    if (nameError != RtgiQualityReportError::None) {
        return nameError;
    }
    const size_t accumulatedPixelCount = accumulateFullImage
                                             ? static_cast<size_t>(profile.captureWidth) * profile.captureHeight
                                             : static_cast<size_t>(profile.roi.width) * profile.roi.height;
    data.sum.assign(accumulatedPixelCount, glm::dvec3{0.0});
    if (retainRoiFrames) {
        data.frames.reserve(frameCount);
    }
    std::vector<glm::vec3> pixels;
    for (uint32_t frame = 0u; frame < frameCount; ++frame) {
        const std::filesystem::path path = directory / frameName(prefix, frame);
        const RtgiQualityReportError loadError =
            loadFrame(path, profile.captureWidth, profile.captureHeight, pixels, detail);
        if (loadError != RtgiQualityReportError::None) {
            return loadError;
        }
        RtgiLinearImage cropped = cropImage(pixels, profile.captureWidth, profile.roi);
        const std::vector<glm::vec3>& accumulatedPixels = accumulateFullImage ? pixels : cropped.pixels;
        for (size_t pixel = 0u; pixel < data.sum.size(); ++pixel) {
            data.sum[pixel] += glm::dvec3{accumulatedPixels[pixel]};
        }
        if (retainRoiFrames) {
            data.frames.push_back(std::move(cropped));
        }
    }
    return RtgiQualityReportError::None;
}

[[nodiscard]] RtgiLinearImage averageRoi(const SequenceData& sequence, const uint32_t frameCount,
                                         const RtgiValidationRoi& roi) {
    RtgiLinearImage image;
    image.width = roi.width;
    image.height = roi.height;
    image.pixels.resize(sequence.sum.size());
    const double inverseFrameCount = 1.0 / static_cast<double>(frameCount);
    for (size_t pixel = 0u; pixel < image.pixels.size(); ++pixel) {
        image.pixels[pixel] = glm::vec3{sequence.sum[pixel] * inverseFrameCount};
    }
    return image;
}

[[nodiscard]] RtgiLinearImage averageFrameRange(const std::vector<RtgiLinearImage>& frames, const size_t begin,
                                                const size_t end) {
    RtgiLinearImage image;
    image.width = frames.front().width;
    image.height = frames.front().height;
    image.pixels.assign(frames.front().pixels.size(), glm::vec3{0.0f});
    for (size_t frame = begin; frame < end; ++frame) {
        for (size_t pixel = 0u; pixel < image.pixels.size(); ++pixel) {
            image.pixels[pixel] += frames[frame].pixels[pixel];
        }
    }
    const float inverseFrameCount = 1.0f / static_cast<float>(end - begin);
    for (glm::vec3& pixel : image.pixels) {
        pixel *= inverseFrameCount;
    }
    return image;
}

[[nodiscard]] double meanLuminance(const RtgiLinearImage& image) {
    constexpr glm::dvec3 kLuminance{0.2126, 0.7152, 0.0722};
    double sum = 0.0;
    for (const glm::vec3& pixel : image.pixels) {
        sum += glm::dot(glm::dvec3{pixel}, kLuminance);
    }
    return sum / static_cast<double>(image.pixels.size());
}

[[nodiscard]] RtgiQualityReportError writeAveragedReference(const SequenceData& sequence, const uint32_t frameCount,
                                                            const RtgiQualityProfile& profile,
                                                            const std::filesystem::path& outputPath,
                                                            RtgiLinearImage& referenceRoi, std::string& detail) {
    const double inverseFrameCount = 1.0 / static_cast<double>(frameCount);
    std::vector<uint16_t> rgba16f(sequence.sum.size() * 4u);
    referenceRoi.width = profile.roi.width;
    referenceRoi.height = profile.roi.height;
    referenceRoi.pixels.resize(static_cast<size_t>(profile.roi.width) * profile.roi.height);
    for (size_t pixel = 0u; pixel < sequence.sum.size(); ++pixel) {
        const glm::dvec3 average = sequence.sum[pixel] * inverseFrameCount;
        for (uint32_t channel = 0u; channel < 3u; ++channel) {
            if (!std::isfinite(average[channel]) || average[channel] < 0.0 || average[channel] > 65504.0) {
                detail = "reference average pixel " + std::to_string(pixel);
                return RtgiQualityReportError::AveragedRadianceOutOfRange;
            }
            rgba16f[pixel * 4u + channel] = glm::packHalf1x16(static_cast<float>(average[channel]));
        }
        rgba16f[pixel * 4u + 3u] = glm::packHalf1x16(1.0f);
        const uint32_t x = static_cast<uint32_t>(pixel % profile.captureWidth);
        const uint32_t y = static_cast<uint32_t>(pixel / profile.captureWidth);
        if (x >= profile.roi.x && x < profile.roi.x + profile.roi.width && y >= profile.roi.y &&
            y < profile.roi.y + profile.roi.height) {
            const size_t roiPixel = static_cast<size_t>(y - profile.roi.y) * profile.roi.width + x - profile.roi.x;
            referenceRoi.pixels[roiPixel] =
                glm::vec3{glm::unpackHalf1x16(rgba16f[pixel * 4u]), glm::unpackHalf1x16(rgba16f[pixel * 4u + 1u]),
                          glm::unpackHalf1x16(rgba16f[pixel * 4u + 2u])};
        }
    }
    const renderer::capture::TextureCaptureResult writeResult =
        renderer::capture::writeLinearExr(outputPath, profile.captureWidth, profile.captureHeight, rgba16f);
    if (!writeResult.succeeded()) {
        detail = writeResult.detail;
        return RtgiQualityReportError::ReferenceWriteFailed;
    }
    return RtgiQualityReportError::None;
}

[[nodiscard]] Json gateJson(const bool evidenceAvailable, const double measured, const char* comparison,
                            const double threshold, const bool passed) {
    Json gate{{"evidence_available", evidenceAvailable}, {"comparison", comparison}, {"threshold", threshold}};
    gate["measured"] = measured;
    gate["passed"] = passed;
    return gate;
}

[[nodiscard]] Json missingGateJson(const char* requiredEvidence) {
    return Json{{"evidence_available", false}, {"required_evidence", requiredEvidence}, {"passed", nullptr}};
}

[[nodiscard]] RtgiQualityReportError loadAsPendingEvidence(const std::filesystem::path& path,
                                                           const RtgiQualityProfile& profile,
                                                           RtgiQualityReportSummary& summary, std::string& detail) {
    std::ifstream input(path);
    if (!input) {
        detail = path.generic_u8string();
        return RtgiQualityReportError::ValidationCaptureReportReadFailed;
    }
    const Json report = Json::parse(input, nullptr, false);
    if (report.is_discarded() || !report.is_object()) {
        detail = path.generic_u8string();
        return RtgiQualityReportError::ValidationCaptureReportReadFailed;
    }
    const Json* capture = report.contains("capture") ? &report["capture"] : nullptr;
    const Json* qualityProfile = report.contains("rtgi_quality_profile") ? &report["rtgi_quality_profile"] : nullptr;
    const Json* accelerationWork =
        report.contains("acceleration_structure_work") ? &report["acceleration_structure_work"] : nullptr;
    const Json* evidence = accelerationWork != nullptr && accelerationWork->contains("as_pending_evidence")
                               ? &(*accelerationWork)["as_pending_evidence"]
                               : nullptr;
    const bool identityValid = report.value("kind", std::string{}) == "mecraft.validation_capture_report" &&
                               capture != nullptr && capture->is_object() &&
                               capture->value("width", 0u) == profile.captureWidth &&
                               capture->value("height", 0u) == profile.captureHeight && qualityProfile != nullptr &&
                               qualityProfile->is_object() && qualityProfile->value("id", std::string{}) == profile.id &&
                               qualityProfile->value("version", 0u) == profile.version &&
                               report.value("requested_sample_frame_count", 0u) == kRtgiQualitySequenceFrameCount;
    const bool evidenceShapeValid = accelerationWork != nullptr && accelerationWork->is_object() &&
                                    accelerationWork->value("valid", false) && evidence != nullptr &&
                                    evidence->is_object() &&
                                    evidence->value("mode", std::string{}) == "conservative_whole_frame_mask" &&
                                    evidence->contains("sample_count") &&
                                    (*evidence)["sample_count"].is_number_unsigned() &&
                                    evidence->contains("pending_frame_count") &&
                                    (*evidence)["pending_frame_count"].is_number_unsigned() &&
                                    evidence->contains("invalid_pixel_count") &&
                                    (*evidence)["invalid_pixel_count"].is_number_unsigned();
    if (!identityValid || !evidenceShapeValid) {
        detail = path.generic_u8string() + ": AS Pending evidence identity or schema mismatch";
        return RtgiQualityReportError::ValidationCaptureReportMismatch;
    }
    const uint64_t sampleCount = (*evidence)["sample_count"].get<uint64_t>();
    const uint64_t pendingFrameCount = (*evidence)["pending_frame_count"].get<uint64_t>();
    const uint64_t invalidPixelCount = (*evidence)["invalid_pixel_count"].get<uint64_t>();
    const uint64_t pixelsPerFrame = static_cast<uint64_t>(profile.captureWidth) * profile.captureHeight;
    if (sampleCount != kRtgiQualitySequenceFrameCount || pendingFrameCount > sampleCount ||
        invalidPixelCount != pendingFrameCount * pixelsPerFrame) {
        detail = path.generic_u8string() + ": AS Pending evidence counts mismatch";
        return RtgiQualityReportError::ValidationCaptureReportMismatch;
    }
    summary.asPendingFrameCount = pendingFrameCount;
    summary.asPendingInvalidPixelCount = invalidPixelCount;
    summary.asPendingPassed = invalidPixelCount == 0u;
    return RtgiQualityReportError::None;
}

[[nodiscard]] RtgiQualityReportError writeReport(const RtgiQualityReportRequest& request,
                                                 const RtgiQualityProfile& profile,
                                                 const RtgiQualityReportSummary& summary, std::string& detail) {
    Json gates;
    gates["variance_reduction"] =
        gateJson(true, summary.variance.reductionPercent, ">=", kRtgiVarianceReductionThresholdPercent,
                 summary.varianceReductionPassed);
    gates["luminance_ssim"] = gateJson(true, summary.comparison.luminanceSsim, ">=", kRtgiLuminanceSsimThreshold,
                                       summary.luminanceSsimPassed);
    gates["relative_luminance_error_p95"] =
        gateJson(true, summary.comparison.relativeLuminanceErrorP95, "<=", kRtgiRelativeLuminanceErrorP95Threshold,
                 summary.relativeLuminanceErrorPassed);
    gates["radiance_validation"] = Json{{"evidence_available", true},
                                        {"non_finite_pixel_count", 0u},
                                        {"negative_radiance_pixel_count", 0u},
                                        {"passed", summary.radianceValidationPassed}};
    gates["leakage_band"] = missingGateJson("fixed depth/normal boundary band mask and leakage metric");
    gates["as_pending"] = Json{{"evidence_available", true},
                               {"mode", "conservative_whole_frame_mask"},
                               {"sample_count", kRtgiQualitySequenceFrameCount},
                               {"pending_frame_count", summary.asPendingFrameCount},
                               {"invalid_pixel_count", summary.asPendingInvalidPixelCount},
                               {"comparison", "=="},
                               {"threshold", 0u},
                               {"passed", summary.asPendingPassed}};

    Json root{{"kind", kRtgiQualityReportKind},
              {"version", kRtgiQualityReportVersion},
              {"profile",
               {{"id", profile.id},
                {"version", profile.version},
                {"scene_contract_id", profile.sceneContractId},
                {"render_settings_id", profile.renderSettingsId},
                {"capture_width", profile.captureWidth},
                {"capture_height", profile.captureHeight},
                {"roi",
                 {{"x", profile.roi.x},
                  {"y", profile.roi.y},
                  {"width", profile.roi.width},
                  {"height", profile.roi.height}}}}},
              {"samples",
               {{"quality_raw", kRtgiQualitySequenceFrameCount},
                {"quality_denoised", kRtgiQualitySequenceFrameCount},
                {"reference_raw", kRtgiQualityReferenceSpp}}},
              {"outputs",
               {{"reference_exr", request.referenceOutputPath.generic_u8string()},
                {"validation_capture_report", request.validationCaptureReportPath.generic_u8string()}}},
              {"metrics",
               {{"raw_temporal_variance", summary.variance.rawVariance},
                {"denoised_temporal_variance", summary.variance.denoisedVariance},
                {"variance_reduction_percent", summary.variance.reductionPercent},
                {"raw_mean_luminance", summary.rawMeanLuminance},
                {"denoised_mean_luminance", summary.denoisedMeanLuminance},
                {"reference_mean_luminance", summary.referenceMeanLuminance},
                {"reference_half_luminance_ssim", summary.referenceConvergence.luminanceSsim},
                {"reference_half_relative_luminance_error_p95",
                 summary.referenceConvergence.relativeLuminanceErrorP95},
                {"luminance_ssim", summary.comparison.luminanceSsim},
                {"relative_luminance_error_p50", summary.comparison.relativeLuminanceErrorP50},
                {"relative_luminance_error_p95", summary.comparison.relativeLuminanceErrorP95}}},
              {"diagnostics",
               {{"absolute_luminance_error_p95", summary.comparison.absoluteLuminanceErrorP95},
                {"relative_p95_pixel",
                 {{"coordinate_space", "capture"},
                  {"x", profile.roi.x + summary.comparison.relativeP95X},
                  {"y", profile.roi.y + summary.comparison.relativeP95Y},
                  {"denoised_luminance", summary.comparison.comparedLuminanceAtRelativeP95},
                  {"reference_luminance", summary.comparison.referenceLuminanceAtRelativeP95}}},
                {"relative_error_denominator_floor_pixel_percent",
                 summary.comparison.denominatorFloorPixelPercent},
                {"reference_half_relative_luminance_error_p50",
                 summary.referenceConvergence.relativeLuminanceErrorP50},
                {"reference_half_absolute_luminance_error_p95",
                 summary.referenceConvergence.absoluteLuminanceErrorP95},
                {"reference_half_relative_error_denominator_floor_pixel_percent",
                 summary.referenceConvergence.denominatorFloorPixelPercent},
                {"reference_half_relative_p95_pixel",
                 {{"coordinate_space", "capture"},
                  {"x", profile.roi.x + summary.referenceConvergence.relativeP95X},
                  {"y", profile.roi.y + summary.referenceConvergence.relativeP95Y},
                  {"first_half_luminance", summary.referenceConvergence.comparedLuminanceAtRelativeP95},
                  {"second_half_luminance", summary.referenceConvergence.referenceLuminanceAtRelativeP95}}},
                {"reference_half_luminance_ssim",
                 gateJson(true, summary.referenceConvergence.luminanceSsim, ">=", kRtgiLuminanceSsimThreshold,
                          summary.referenceConvergence.luminanceSsim >= kRtgiLuminanceSsimThreshold)},
                {"reference_half_relative_luminance_error_p95",
                 gateJson(true, summary.referenceConvergence.relativeLuminanceErrorP95, "<=",
                          kRtgiRelativeLuminanceErrorP95Threshold,
                          summary.referenceConvergence.relativeLuminanceErrorP95 <=
                              kRtgiRelativeLuminanceErrorP95Threshold)}}},
              {"gates", std::move(gates)},
              {"available_metrics_passed", summary.availableMetricsPassed},
              {"complete_static_gate_passed", summary.completeStaticGatePassed},
              {"missing_evidence", Json::array({"leakage_band"})}};

    const std::filesystem::path parent = request.reportOutputPath.parent_path();
    if (!parent.empty()) {
        std::error_code directoryError;
        std::filesystem::create_directories(parent, directoryError);
        if (directoryError) {
            detail = directoryError.message();
            return RtgiQualityReportError::ReportWriteFailed;
        }
    }
    std::ofstream output(request.reportOutputPath, std::ios::trunc);
    if (!output) {
        detail = request.reportOutputPath.generic_u8string();
        return RtgiQualityReportError::ReportWriteFailed;
    }
    output << root.dump(2) << '\n';
    output.flush();
    if (!output) {
        detail = request.reportOutputPath.generic_u8string();
        return RtgiQualityReportError::ReportWriteFailed;
    }
    return RtgiQualityReportError::None;
}

} // namespace

RtgiQualityReportError generateRtgiQualityReport(const RtgiQualityReportRequest& request,
                                                 RtgiQualityReportSummary& summary, std::string& detail) {
    summary = {};
    detail.clear();
    if (request.profileManifestPath.empty() || request.profileId.empty() || request.qualitySequenceDirectory.empty() ||
        request.referenceSequenceDirectory.empty() || request.validationCaptureReportPath.empty() ||
        request.validationCaptureReportPath.extension() != ".json" || request.referenceOutputPath.empty() ||
        request.referenceOutputPath.extension() != ".exr" || request.reportOutputPath.empty() ||
        request.reportOutputPath.extension() != ".json") {
        detail = "profile, sequence directories, validation report, .exr reference output, and .json report output are required";
        return RtgiQualityReportError::InvalidRequest;
    }

    RtgiQualityProfile profile;
    const RtgiQualityProfileError profileError =
        loadRtgiQualityProfile(request.profileManifestPath, request.profileId, profile, detail);
    if (profileError != RtgiQualityProfileError::None) {
        detail = std::string{rtgiQualityProfileErrorStableId(profileError)} + ": " + detail;
        return RtgiQualityReportError::ProfileLoadFailed;
    }
    RtgiQualityReportError error =
        loadAsPendingEvidence(request.validationCaptureReportPath, profile, summary, detail);
    if (error != RtgiQualityReportError::None) {
        return error;
    }

    SequenceData raw;
    SequenceData denoised;
    SequenceData reference;
    error = loadSequence(request.qualitySequenceDirectory, "rtgi_raw_", kRtgiQualitySequenceFrameCount, profile,
                         true, false, raw, detail);
    if (error != RtgiQualityReportError::None) {
        return error;
    }
    error = loadSequence(request.qualitySequenceDirectory, "rtgi_denoised_", kRtgiQualitySequenceFrameCount, profile,
                         true, false, denoised, detail);
    if (error != RtgiQualityReportError::None) {
        return error;
    }
    error = loadSequence(request.referenceSequenceDirectory, "rtgi_raw_", kRtgiQualityReferenceSpp, profile, true,
                         true, reference, detail);
    if (error != RtgiQualityReportError::None) {
        return error;
    }

    std::vector<const RtgiLinearImage*> rawFrames;
    std::vector<const RtgiLinearImage*> denoisedFrames;
    rawFrames.reserve(raw.frames.size());
    denoisedFrames.reserve(denoised.frames.size());
    for (size_t frame = 0u; frame < raw.frames.size(); ++frame) {
        rawFrames.push_back(&raw.frames[frame]);
        denoisedFrames.push_back(&denoised.frames[frame]);
    }
    const RtgiValidationRoi croppedRoi{0u, 0u, profile.roi.width, profile.roi.height};
    const RtgiQualityMetricError varianceError =
        renderer::contracts::calculateRtgiTemporalVariance(rawFrames, denoisedFrames, croppedRoi, summary.variance);
    if (varianceError != RtgiQualityMetricError::None) {
        detail = renderer::contracts::rtgiQualityMetricErrorStableId(varianceError);
        return RtgiQualityReportError::MetricEvaluationFailed;
    }

    const RtgiLinearImage rawAverage = averageRoi(raw, kRtgiQualitySequenceFrameCount, profile.roi);
    const RtgiLinearImage denoisedAverage = averageRoi(denoised, kRtgiQualitySequenceFrameCount, profile.roi);
    RtgiLinearImage referenceAverage;
    error = writeAveragedReference(reference, kRtgiQualityReferenceSpp, profile, request.referenceOutputPath,
                                   referenceAverage, detail);
    if (error != RtgiQualityReportError::None) {
        return error;
    }
    const RtgiQualityMetricError comparisonError = renderer::contracts::compareRtgiLinearReference(
        denoisedAverage, referenceAverage, croppedRoi, summary.comparison);
    if (comparisonError != RtgiQualityMetricError::None) {
        detail = renderer::contracts::rtgiQualityMetricErrorStableId(comparisonError);
        return RtgiQualityReportError::MetricEvaluationFailed;
    }
    const size_t referenceHalfFrameCount = reference.frames.size() / 2u;
    const RtgiLinearImage referenceFirstHalf = averageFrameRange(reference.frames, 0u, referenceHalfFrameCount);
    const RtgiLinearImage referenceSecondHalf =
        averageFrameRange(reference.frames, referenceHalfFrameCount, reference.frames.size());
    const RtgiQualityMetricError convergenceError = renderer::contracts::compareRtgiLinearReference(
        referenceFirstHalf, referenceSecondHalf, croppedRoi, summary.referenceConvergence);
    if (convergenceError != RtgiQualityMetricError::None) {
        detail = renderer::contracts::rtgiQualityMetricErrorStableId(convergenceError);
        return RtgiQualityReportError::MetricEvaluationFailed;
    }

    summary.rawMeanLuminance = meanLuminance(rawAverage);
    summary.denoisedMeanLuminance = meanLuminance(denoisedAverage);
    summary.referenceMeanLuminance = meanLuminance(referenceAverage);

    summary.varianceReductionPassed = summary.variance.reductionPercent >= kRtgiVarianceReductionThresholdPercent;
    summary.luminanceSsimPassed = summary.comparison.luminanceSsim >= kRtgiLuminanceSsimThreshold;
    summary.relativeLuminanceErrorPassed =
        summary.comparison.relativeLuminanceErrorP95 <= kRtgiRelativeLuminanceErrorP95Threshold;
    summary.referenceConvergencePassed =
        summary.referenceConvergence.luminanceSsim >= kRtgiLuminanceSsimThreshold &&
        summary.referenceConvergence.relativeLuminanceErrorP95 <= kRtgiRelativeLuminanceErrorP95Threshold;
    summary.radianceValidationPassed = true;
    summary.availableMetricsPassed = summary.varianceReductionPassed && summary.luminanceSsimPassed &&
                                     summary.relativeLuminanceErrorPassed && summary.radianceValidationPassed &&
                                     summary.asPendingPassed;
    summary.completeStaticGatePassed = false;
    return writeReport(request, profile, summary, detail);
}

const char* rtgiQualityReportErrorStableId(const RtgiQualityReportError error) {
    switch (error) {
    case RtgiQualityReportError::None: return "None";
    case RtgiQualityReportError::InvalidRequest: return "InvalidRequest";
    case RtgiQualityReportError::ProfileLoadFailed: return "ProfileLoadFailed";
    case RtgiQualityReportError::SequenceDirectoryReadFailed: return "SequenceDirectoryReadFailed";
    case RtgiQualityReportError::MissingSequenceFrame: return "MissingSequenceFrame";
    case RtgiQualityReportError::UnexpectedSequenceFrame: return "UnexpectedSequenceFrame";
    case RtgiQualityReportError::ExrReadFailed: return "ExrReadFailed";
    case RtgiQualityReportError::ImageExtentMismatch: return "ImageExtentMismatch";
    case RtgiQualityReportError::NonFiniteRadiance: return "NonFiniteRadiance";
    case RtgiQualityReportError::NegativeRadiance: return "NegativeRadiance";
    case RtgiQualityReportError::AveragedRadianceOutOfRange: return "AveragedRadianceOutOfRange";
    case RtgiQualityReportError::ReferenceWriteFailed: return "ReferenceWriteFailed";
    case RtgiQualityReportError::MetricEvaluationFailed: return "MetricEvaluationFailed";
    case RtgiQualityReportError::ValidationCaptureReportReadFailed: return "ValidationCaptureReportReadFailed";
    case RtgiQualityReportError::ValidationCaptureReportMismatch: return "ValidationCaptureReportMismatch";
    case RtgiQualityReportError::ReportWriteFailed: return "ReportWriteFailed";
    }
    std::abort();
}

} // namespace app::validation
