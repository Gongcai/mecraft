#include "app/validation/RtgiQualityReport.h"
#include "app/validation/RtgiQualityProfile.h"
#include "renderer/capture/TextureCapture.h"

#include <glm/gtc/packing.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[rtgi_quality_report_test] FAIL: " << message << '\n';
        return false;
    }
    return true;
}

std::string frameName(const char* prefix, const uint32_t index) {
    std::string name = prefix;
    name += index < 10u ? "000" : "00";
    name += std::to_string(index);
    name += ".exr";
    return name;
}

bool writeFrame(const std::filesystem::path& path, const uint32_t width, const uint32_t height, const float value) {
    std::vector<uint16_t> pixels(static_cast<size_t>(width) * height * 4u);
    for (size_t pixel = 0u; pixel < static_cast<size_t>(width) * height; ++pixel) {
        pixels[pixel * 4u] = glm::packHalf1x16(value);
        pixels[pixel * 4u + 1u] = glm::packHalf1x16(value);
        pixels[pixel * 4u + 2u] = glm::packHalf1x16(value);
        pixels[pixel * 4u + 3u] = glm::packHalf1x16(1.0f);
    }
    return renderer::capture::writeLinearExr(path, width, height, pixels).succeeded();
}

bool writeNanFrame(const std::filesystem::path& path) {
    std::vector<uint16_t> pixels(2u * 2u * 4u, glm::packHalf1x16(1.0f));
    pixels[0u] = 0x7e00u;
    return renderer::capture::writeLinearExr(path, 2u, 2u, pixels).succeeded();
}

bool writeNegativeFrame(const std::filesystem::path& path) {
    std::vector<uint16_t> pixels(2u * 2u * 4u, glm::packHalf1x16(1.0f));
    pixels[0u] = glm::packHalf1x16(-1.0f);
    return renderer::capture::writeLinearExr(path, 2u, 2u, pixels).succeeded();
}

} // namespace

int main() {
    namespace validation = app::validation;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("mecraft_rtgi_quality_report_test_" + std::to_string(unique));
    const std::filesystem::path quality = root / "quality";
    const std::filesystem::path reference = root / "reference";
    std::error_code fileError;
    std::filesystem::create_directories(quality, fileError);
    std::filesystem::create_directories(reference, fileError);
    if (!requireTrue(!fileError, "temporary sequence directories must be created")) {
        return 1;
    }

    const std::filesystem::path manifest = root / "profiles.json";
    std::ofstream manifestOutput(manifest);
    manifestOutput << R"({
  "kind": "mecraft.rtgi_quality_profiles",
  "version": 2,
  "profiles": [{
    "id": "synthetic_static",
    "version": 2,
    "scene_contract_id": "synthetic_scene",
    "render_settings_id": "synthetic_settings",
    "camera_time_seconds": 2.0,
    "capture_width": 2,
    "capture_height": 2,
    "roi": {"x": 0, "y": 0, "width": 2, "height": 2}
  }]
})";
    manifestOutput.close();

    bool sequenceWritten = true;
    for (uint32_t frame = 0u; frame < validation::kRtgiQualitySequenceFrameCount; ++frame) {
        const float rawValue = frame % 2u == 0u ? 0.0f : 2.0f;
        sequenceWritten = sequenceWritten && writeFrame(quality / frameName("rtgi_raw_", frame), 2u, 2u, rawValue) &&
                          writeFrame(quality / frameName("rtgi_denoised_", frame), 2u, 2u, 1.0f);
    }
    for (uint32_t frame = 0u; frame < validation::kRtgiQualityReferenceSpp; ++frame) {
        const float referenceValue = frame < validation::kRtgiQualityReferenceSpp / 2u ? 0.5f : 1.5f;
        sequenceWritten =
            sequenceWritten && writeFrame(reference / frameName("rtgi_raw_", frame), 2u, 2u, referenceValue);
    }
    if (!requireTrue(sequenceWritten, "synthetic EXR sequences must be written")) {
        return 1;
    }

    validation::RtgiQualityReportRequest request;
    request.profileManifestPath = manifest;
    request.profileId = "synthetic_static";
    request.qualitySequenceDirectory = quality;
    request.referenceSequenceDirectory = reference;
    request.referenceOutputPath = root / "reference_64spp.exr";
    request.reportOutputPath = root / "quality_report.json";
    validation::RtgiQualityReportSummary summary;
    std::string detail;
    validation::RtgiQualityReportError error = validation::generateRtgiQualityReport(request, summary, detail);

    renderer::capture::LinearExrImage averagedReference;
    const renderer::capture::TextureCaptureResult referenceRead =
        renderer::capture::readLinearExr(request.referenceOutputPath, averagedReference);
    std::ifstream reportInput(request.reportOutputPath);
    const nlohmann::json report = nlohmann::json::parse(reportInput, nullptr, false);
    const bool averagedPixelsCorrect =
        referenceRead.succeeded() && averagedReference.rgb16f.size() == 12u &&
        std::all_of(averagedReference.rgb16f.begin(), averagedReference.rgb16f.end(),
                    [](const uint16_t value) { return value == glm::packHalf1x16(1.0f); });
    if (!requireTrue(error == validation::RtgiQualityReportError::None,
                     "complete finite sequences must generate a report") ||
        !requireTrue(averagedPixelsCorrect, "64 reference samples must average into the written linear EXR") ||
        !requireTrue(std::abs(summary.variance.rawVariance - (32.0 / 31.0)) < 1.0e-12 &&
                         summary.variance.denoisedVariance == 0.0 && summary.variance.reductionPercent == 100.0,
                     "report metrics must use the complete raw and denoised sequences") ||
        !requireTrue(summary.rawMeanLuminance == 1.0 && summary.denoisedMeanLuminance == 1.0 &&
                         summary.referenceMeanLuminance == 1.0 &&
                         summary.referenceConvergence.relativeLuminanceErrorP95 > 0.1 &&
                         !summary.referenceConvergencePassed,
                     "report diagnostics must expose an unconverged reference without changing its full average") ||
        !requireTrue(summary.availableMetricsPassed && !summary.completeStaticGatePassed,
                     "reference convergence diagnostics must not become an undeclared static quality gate") ||
        !requireTrue(!report.is_discarded() && report.at("gates").at("leakage_band").at("passed").is_null() &&
                         report.at("gates").at("as_pending").at("passed").is_null() &&
                         !report.at("diagnostics").at("reference_half_luminance_ssim").at("passed").get<bool>() &&
                         report.at("diagnostics")
                             .at("reference_half_relative_luminance_error_p95")
                             .at("passed")
                             .get<bool>() == false &&
                         !report.at("complete_static_gate_passed").get<bool>(),
                     "JSON must expose Leakage Band and AS Pending as unavailable evidence")) {
        std::cerr << "[rtgi_quality_report_test] detail: " << detail << '\n';
        return 1;
    }

    const std::filesystem::path lastRaw =
        quality / frameName("rtgi_raw_", validation::kRtgiQualitySequenceFrameCount - 1u);
    std::filesystem::remove(lastRaw, fileError);
    error = validation::generateRtgiQualityReport(request, summary, detail);
    if (!requireTrue(error == validation::RtgiQualityReportError::MissingSequenceFrame,
                     "a missing numbered frame must be rejected")) {
        return 1;
    }
    if (!writeFrame(lastRaw, 2u, 2u, 2.0f) || !writeFrame(quality / "rtgi_raw_0032.exr", 2u, 2u, 1.0f)) {
        return 1;
    }
    error = validation::generateRtgiQualityReport(request, summary, detail);
    if (!requireTrue(error == validation::RtgiQualityReportError::UnexpectedSequenceFrame,
                     "an out-of-range sequence number must be rejected")) {
        return 1;
    }
    std::filesystem::remove(quality / "rtgi_raw_0032.exr", fileError);
    if (!writeFrame(lastRaw, 1u, 1u, 1.0f)) {
        return 1;
    }
    error = validation::generateRtgiQualityReport(request, summary, detail);
    if (!requireTrue(error == validation::RtgiQualityReportError::ImageExtentMismatch,
                     "a frame extent mismatch must be rejected")) {
        return 1;
    }
    if (!writeNanFrame(lastRaw)) {
        return 1;
    }
    error = validation::generateRtgiQualityReport(request, summary, detail);
    if (!requireTrue(error == validation::RtgiQualityReportError::NonFiniteRadiance,
                     "a non-finite half-float channel must be rejected")) {
        return 1;
    }
    if (!writeNegativeFrame(lastRaw)) {
        return 1;
    }
    error = validation::generateRtgiQualityReport(request, summary, detail);
    if (!requireTrue(error == validation::RtgiQualityReportError::NegativeRadiance,
                     "a negative half-float radiance channel must be rejected")) {
        return 1;
    }

    std::filesystem::remove_all(root, fileError);
    if (!requireTrue(!fileError, "temporary report fixtures must be removed")) {
        return 1;
    }
    std::cout << "[rtgi_quality_report_test] PASS\n";
    return 0;
}
