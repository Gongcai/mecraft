#include "renderer/contracts/ContentHashContract.h"

#include <nlohmann/json.hpp>
#include <stb/stb_image.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Json = nlohmann::json;

constexpr uint32_t kWidth = 1280u;
constexpr uint32_t kHeight = 720u;
constexpr uint32_t kWarmupFrames = 300u;
constexpr uint32_t kSampleFrames = 1000u;
constexpr uint32_t kMicromapSubdivisionLevel = 4u;
constexpr uint64_t kCutoutPrimitiveCount = 6926u;
constexpr uint64_t kMicrotrianglesPerPrimitive = 256u;

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

struct ImageMetrics {
    double rgbMae = 0.0;
    double rgbRmse = 0.0;
    double luminanceSsim = 0.0;
};

bool requireTrue(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "[rtgi_cutout_traversal_ab_test] FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool hasString(const Json& object, const char* field, const std::string_view expected) {
    const auto value = object.find(field);
    return value != object.end() && value->is_string() && value->get_ref<const std::string&>() == expected;
}

bool readJson(const std::filesystem::path& path, Json& output) {
    std::ifstream input(path);
    output = Json::parse(input, nullptr, false);
    return static_cast<bool>(input) && !output.is_discarded();
}

bool isLowercaseHex(const std::string_view value, const size_t length) {
    if (value.size() != length) {
        return false;
    }
    for (const char character : value) {
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool validateArtifact(const std::filesystem::path& root, const Json& entry, const char* pathField,
                      const char* sizeField, const char* fnvField, const char* shaField) {
    const std::filesystem::path path = root / entry.at(pathField).get<std::string>();
    std::error_code fileError;
    const uintmax_t byteSize = std::filesystem::file_size(path, fileError);
    const renderer::contracts::FileContentHashResult hash = renderer::contracts::stableFileContentHash(path);
    const std::string expectedFnv = entry.at(fnvField).get<std::string>();
    const std::string expectedSha = entry.at(shaField).get<std::string>();
    return requireTrue(!fileError && byteSize == entry.at(sizeField).get<uintmax_t>(),
                       "A/B artifact byte size must match its locked identity") &&
           requireTrue(hash.succeeded() && isLowercaseHex(expectedFnv, 16u) &&
                           renderer::contracts::stableContentHashHex(hash.hash) == expectedFnv,
                       "A/B artifact bytes must match their locked FNV-1a identity") &&
           requireTrue(isLowercaseHex(expectedSha, 64u), "A/B artifact SHA-256 identity must be canonical");
}

bool loadImage(const std::filesystem::path& path, Image& output) {
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.string().c_str(), &output.width, &output.height, &channels, 4);
    if (pixels == nullptr) {
        return false;
    }
    const size_t byteCount = static_cast<size_t>(output.width) * static_cast<size_t>(output.height) * 4u;
    output.rgba.assign(pixels, pixels + byteCount);
    stbi_image_free(pixels);
    return true;
}

ImageMetrics compareImages(const Image& candidate, const Image& micromap) {
    const size_t pixelCount = static_cast<size_t>(candidate.width) * static_cast<size_t>(candidate.height);
    double absoluteError = 0.0;
    double squaredError = 0.0;
    double candidateLuminanceMean = 0.0;
    double micromapLuminanceMean = 0.0;
    double candidateLuminanceSquared = 0.0;
    double micromapLuminanceSquared = 0.0;
    double luminanceProduct = 0.0;

    for (size_t pixel = 0u; pixel < pixelCount; ++pixel) {
        for (size_t channel = 0u; channel < 3u; ++channel) {
            const double lhs = static_cast<double>(candidate.rgba[pixel * 4u + channel]) / 255.0;
            const double rhs = static_cast<double>(micromap.rgba[pixel * 4u + channel]) / 255.0;
            const double difference = lhs - rhs;
            absoluteError += std::abs(difference);
            squaredError += difference * difference;
        }
        const auto luminance = [](const uint8_t* rgba) {
            return (0.2126 * static_cast<double>(rgba[0]) + 0.7152 * static_cast<double>(rgba[1]) +
                    0.0722 * static_cast<double>(rgba[2])) /
                   255.0;
        };
        const double candidateLuminance = luminance(candidate.rgba.data() + pixel * 4u);
        const double micromapLuminance = luminance(micromap.rgba.data() + pixel * 4u);
        candidateLuminanceMean += candidateLuminance;
        micromapLuminanceMean += micromapLuminance;
        candidateLuminanceSquared += candidateLuminance * candidateLuminance;
        micromapLuminanceSquared += micromapLuminance * micromapLuminance;
        luminanceProduct += candidateLuminance * micromapLuminance;
    }

    candidateLuminanceMean /= static_cast<double>(pixelCount);
    micromapLuminanceMean /= static_cast<double>(pixelCount);
    const double inverseSampleCount = 1.0 / static_cast<double>(pixelCount - 1u);
    const double candidateVariance = (candidateLuminanceSquared - static_cast<double>(pixelCount) *
                                                                      candidateLuminanceMean * candidateLuminanceMean) *
                                     inverseSampleCount;
    const double micromapVariance =
        (micromapLuminanceSquared - static_cast<double>(pixelCount) * micromapLuminanceMean * micromapLuminanceMean) *
        inverseSampleCount;
    const double covariance =
        (luminanceProduct - static_cast<double>(pixelCount) * candidateLuminanceMean * micromapLuminanceMean) *
        inverseSampleCount;
    constexpr double kLuminanceStability = 0.01 * 0.01;
    constexpr double kContrastStability = 0.03 * 0.03;

    ImageMetrics metrics;
    const double rgbSampleCount = static_cast<double>(pixelCount * 3u);
    metrics.rgbMae = absoluteError / rgbSampleCount;
    metrics.rgbRmse = std::sqrt(squaredError / rgbSampleCount);
    metrics.luminanceSsim = ((2.0 * candidateLuminanceMean * micromapLuminanceMean + kLuminanceStability) *
                             (2.0 * covariance + kContrastStability)) /
                            ((candidateLuminanceMean * candidateLuminanceMean +
                              micromapLuminanceMean * micromapLuminanceMean + kLuminanceStability) *
                             (candidateVariance + micromapVariance + kContrastStability));
    return metrics;
}

double readP95(const Json& report, const char* stage) {
    return report.at("render_graph_stage_ms").at("stages").at(stage).at("p95").get<double>();
}

double reductionPercent(const double baseline, const double optimized) {
    return (baseline - optimized) * 100.0 / baseline;
}

bool validateCommonReport(const Json& report, const Json& profile, const std::string_view mode) {
    const Json& scene = report.at("scene_contract");
    const Json& camera = report.at("camera_path");
    const Json& settings = report.at("render_settings");
    const Json& timing = report.at("render_graph_stage_ms");
    const Json& completeGpu = report.at("complete_gpu_frame_ms");
    const Json& counters = report.at("rtgi_trace_counters");
    const Json& accelerationWork = report.at("acceleration_structure_work");
    const Json& omm = accelerationWork.at("terrain_opacity_micromaps");
    return requireTrue(hasString(report, "kind", "mecraft.validation_capture_report") &&
                           hasString(report, "rhi_backend", "vulkan") && hasString(report, "scene", "voxel"),
                       "both A/B reports must be Vulkan voxel validation captures") &&
           requireTrue(
               hasString(scene, "id", profile.at("scene_contract_id").get_ref<const std::string&>()) &&
                   hasString(scene, "content_hash", profile.at("scene_contract_hash").get_ref<const std::string&>()) &&
                   hasString(camera, "id", profile.at("camera_path_id").get_ref<const std::string&>()) &&
                   hasString(camera, "content_hash", profile.at("camera_path_hash").get_ref<const std::string&>()),
               "both A/B reports must share the locked scene and Camera Path") &&
           requireTrue(
               hasString(settings, "id", profile.at("render_settings_id").get_ref<const std::string&>()) &&
                   hasString(settings, "content_hash",
                             profile.at("render_settings_hash").get_ref<const std::string&>()) &&
                   hasString(settings, "content_hash_scope", "quality_profile_excluding_traversal_implementation") &&
                   hasString(settings, "rtgi_cutout_traversal", mode),
               "both A/B reports must share quality settings and identify only their traversal implementation") &&
           requireTrue(report.value("warmup_frame_count", 0u) == kWarmupFrames &&
                           report.value("requested_sample_frame_count", 0u) == kSampleFrames &&
                           report.value("frame_count", 0u) == kSampleFrames && timing.value("valid", false) &&
                           timing.value("window_sample_count", 0u) == kSampleFrames &&
                           timing.value("observed_sample_count", 0u) == kSampleFrames,
                       "both A/B reports must contain the complete 300/1000 timing run") &&
           requireTrue(completeGpu.value("scope", "") == "scene_render_graphs" &&
                           completeGpu.value("complete_frame", false) && completeGpu.value("valid", false) &&
                           completeGpu.value("sample_count", 0u) == kSampleFrames &&
                           completeGpu.value("observed_sample_count", 0u) == kSampleFrames,
                       "both A/B reports must contain 1000 complete scene GPU spans") &&
           requireTrue(counters.value("valid", false) && counters.value("sample_count", 0u) == kSampleFrames &&
                           counters.value("observed_sample_count", 0u) == kSampleFrames &&
                           counters.at("totals").value("pixel_count", uint64_t{0u}) ==
                               static_cast<uint64_t>(kWidth) * kHeight * kSampleFrames,
                       "both A/B reports must contain 1000 complete RTGI counter reductions") &&
           requireTrue(hasString(omm, "mode", mode) &&
                           hasString(omm, "alpha_texture_hash",
                                     profile.at("alpha_texture_hash").get_ref<const std::string&>()) &&
                           hasString(omm, "profile_hash",
                                     profile.at("opacity_micromap_profile_hash").get_ref<const std::string&>()) &&
                           hasString(omm, "format", "four_state") &&
                           omm.value("subdivision_level", 0u) == kMicromapSubdivisionLevel,
                       "both A/B reports must share the locked alpha and Four-state OMM profile");
}

} // namespace

int main() {
    const std::filesystem::path root =
        std::filesystem::path(MECRAFT_TEST_SOURCE_DIR) / "assets/validation/reference_captures";
    Json manifest;
    if (!requireTrue(readJson(root / "rtgi_cutout_traversal_ab.json", manifest),
                     "A/B manifest must contain valid JSON") ||
        !requireTrue(hasString(manifest, "kind", "mecraft.rtgi_cutout_traversal_ab") &&
                         manifest.value("version", 0u) == 1u &&
                         hasString(manifest, "decision", "adopt_opacity_micromap"),
                     "A/B manifest must record the versioned OMM adoption decision")) {
        return 1;
    }

    const Json& profile = manifest.at("capture_profile");
    if (!requireTrue(hasString(profile, "backend", "vulkan") && profile.value("width", 0u) == kWidth &&
                         profile.value("height", 0u) == kHeight &&
                         profile.value("warmup_frame_count", 0u) == kWarmupFrames &&
                         profile.value("sample_frame_count", 0u) == kSampleFrames,
                     "A/B capture profile must remain the fixed Vulkan 300/1000 run")) {
        return 1;
    }

    const Json& candidateEntry = manifest.at("candidate_loop");
    const Json& micromapEntry = manifest.at("opacity_micromap");
    if (!requireTrue(hasString(candidateEntry, "image", "v03_forest_cutout_candidate_loop_vulkan_1280x720_1000.png") &&
                         candidateEntry.value("image_byte_size", uintmax_t{0u}) == 1884344u &&
                         hasString(candidateEntry, "image_fnv1a64", "bc9c742bbdf61fc3") &&
                         hasString(candidateEntry, "image_sha256",
                                   "aa480b974e327ed618b02a6b1802a5d0f142bacb69b67d7c365fd30c1b727260") &&
                         hasString(candidateEntry, "report",
                                   "v03_forest_cutout_candidate_loop_vulkan_1280x720_1000.report.json") &&
                         candidateEntry.value("report_byte_size", uintmax_t{0u}) == 17123u &&
                         hasString(candidateEntry, "report_fnv1a64", "da7d4c74da3ec042") &&
                         hasString(candidateEntry, "report_sha256",
                                   "295c1a317d2d892df2643bc882ec08df631428fcb7004639334c0b56a187ebe1"),
                     "Candidate Loop artifacts must retain their reviewed byte identities") ||
        !requireTrue(hasString(micromapEntry, "image", "v03_forest_cutout_opacity_micromap_vulkan_1280x720_1000.png") &&
                         micromapEntry.value("image_byte_size", uintmax_t{0u}) == 1887436u &&
                         hasString(micromapEntry, "image_fnv1a64", "2805c3303f59f5ec") &&
                         hasString(micromapEntry, "image_sha256",
                                   "283d9b923a436f53ce0dc86327225c9b4e2ac56656e6d67e53e13ae9d93c90ae") &&
                         hasString(micromapEntry, "report",
                                   "v03_forest_cutout_opacity_micromap_vulkan_1280x720_1000.report.json") &&
                         micromapEntry.value("report_byte_size", uintmax_t{0u}) == 17187u &&
                         hasString(micromapEntry, "report_fnv1a64", "2dc87342306ec887") &&
                         hasString(micromapEntry, "report_sha256",
                                   "a5c9f1cc7ccdba4989f763f8650310140065695670f3908612c59bbc536a81b3"),
                     "Opacity Micromap artifacts must retain their reviewed byte identities")) {
        return 1;
    }
    Json candidateReport;
    Json micromapReport;
    if (!validateArtifact(root, candidateEntry, "image", "image_byte_size", "image_fnv1a64", "image_sha256") ||
        !validateArtifact(root, candidateEntry, "report", "report_byte_size", "report_fnv1a64", "report_sha256") ||
        !validateArtifact(root, micromapEntry, "image", "image_byte_size", "image_fnv1a64", "image_sha256") ||
        !validateArtifact(root, micromapEntry, "report", "report_byte_size", "report_fnv1a64", "report_sha256") ||
        !requireTrue(readJson(root / candidateEntry.at("report").get<std::string>(), candidateReport) &&
                         readJson(root / micromapEntry.at("report").get<std::string>(), micromapReport),
                     "both A/B reports must exist and contain valid JSON") ||
        !validateCommonReport(candidateReport, profile, "candidate_loop") ||
        !validateCommonReport(micromapReport, profile, "opacity_micromap")) {
        return 1;
    }

    const Json& candidateOmm = candidateReport.at("acceleration_structure_work").at("terrain_opacity_micromaps");
    const Json& micromapOmm = micromapReport.at("acceleration_structure_work").at("terrain_opacity_micromaps");
    const Json& candidateLatest = candidateOmm.at("latest");
    const Json& micromapLatest = micromapOmm.at("latest");
    const Json& microtriangles = micromapLatest.at("active_microtriangles");
    const uint64_t opaqueMicrotriangles = microtriangles.value("opaque", uint64_t{0u});
    const uint64_t transparentMicrotriangles = microtriangles.value("transparent", uint64_t{0u});
    const uint64_t unknownMicrotriangles = microtriangles.value("unknown", uint64_t{0u});
    if (!requireTrue(candidateLatest.value("active_micromaps", 1u) == 0u &&
                         candidateLatest.value("active_bytes", uint64_t{1u}) == 0u &&
                         candidateOmm.at("window").value("peak_active_micromaps", 1u) == 0u,
                     "Candidate Loop must not create or retain Micromap resources") ||
        !requireTrue(micromapLatest.value("active_micromaps", 0u) == 52u &&
                         micromapLatest.value("active_bytes", uint64_t{0u}) == 598144u &&
                         opaqueMicrotriangles == 1145448u && transparentMicrotriangles == 510754u &&
                         unknownMicrotriangles == 116854u &&
                         opaqueMicrotriangles + transparentMicrotriangles + unknownMicrotriangles ==
                             kCutoutPrimitiveCount * kMicrotrianglesPerPrimitive,
                     "OMM residency must exactly partition all V03 Cutout microtriangles")) {
        return 1;
    }

    const Json& acceptance = manifest.at("acceptance");
    const double candidateTraceP95 = readP95(candidateReport, "RTGI.Trace");
    const double micromapTraceP95 = readP95(micromapReport, "RTGI.Trace");
    const double candidateCompleteP95 = candidateReport.at("complete_gpu_frame_ms").at("span_ms").at("p95");
    const double micromapCompleteP95 = micromapReport.at("complete_gpu_frame_ms").at("span_ms").at("p95");
    const uint64_t candidateCount =
        candidateReport.at("rtgi_trace_counters").at("totals").value("candidate_count", uint64_t{0u});
    const uint64_t micromapCandidateCount =
        micromapReport.at("rtgi_trace_counters").at("totals").value("candidate_count", uint64_t{0u});
    const uint64_t candidateMemory = candidateReport.at("rhi_memory").value("total_bytes", uint64_t{0u});
    const uint64_t micromapMemory = micromapReport.at("rhi_memory").value("total_bytes", uint64_t{0u});
    const double traceReduction = reductionPercent(candidateTraceP95, micromapTraceP95);
    const double completeGpuReduction = reductionPercent(candidateCompleteP95, micromapCompleteP95);
    const double candidateReduction =
        reductionPercent(static_cast<double>(candidateCount), static_cast<double>(micromapCandidateCount));
    if (!requireTrue(candidateTraceP95 > 0.0 &&
                         traceReduction >= acceptance.at("minimum_rtgi_trace_p95_reduction_percent").get<double>(),
                     "OMM must reduce RTGI Trace p95 by the accepted margin") ||
        !requireTrue(candidateCompleteP95 > 0.0 &&
                         completeGpuReduction >=
                             acceptance.at("minimum_complete_gpu_p95_reduction_percent").get<double>(),
                     "OMM must reduce complete GPU p95 by the accepted margin") ||
        !requireTrue(candidateCount > 0u &&
                         candidateReduction >= acceptance.at("minimum_candidate_reduction_percent").get<double>(),
                     "OMM must reduce shader-visible Candidate intersections by the accepted margin") ||
        !requireTrue(micromapMemory >= candidateMemory &&
                         micromapMemory - candidateMemory <=
                             acceptance.at("maximum_memory_increase_bytes").get<uint64_t>(),
                     "OMM memory increase must remain inside the accepted budget")) {
        return 1;
    }

    Image candidateImage;
    Image micromapImage;
    if (!requireTrue(loadImage(root / candidateEntry.at("image").get<std::string>(), candidateImage) &&
                         loadImage(root / micromapEntry.at("image").get<std::string>(), micromapImage),
                     "both A/B PNG files must decode as RGBA8") ||
        !requireTrue(candidateImage.width == static_cast<int>(kWidth) &&
                         candidateImage.height == static_cast<int>(kHeight) &&
                         micromapImage.width == candidateImage.width && micromapImage.height == candidateImage.height,
                     "both A/B PNG files must match the capture extent")) {
        return 1;
    }
    const ImageMetrics imageMetrics = compareImages(candidateImage, micromapImage);
    if (!requireTrue(imageMetrics.rgbMae <= acceptance.at("maximum_rgb_mae").get<double>() &&
                         imageMetrics.rgbRmse <= acceptance.at("maximum_rgb_rmse").get<double>() &&
                         imageMetrics.luminanceSsim >= acceptance.at("minimum_luminance_ssim").get<double>(),
                     "OMM final-frame differences must remain inside the temporal-noise image gate")) {
        std::cerr << "  RGB MAE=" << imageMetrics.rgbMae << " RMSE=" << imageMetrics.rgbRmse
                  << " luminance SSIM=" << imageMetrics.luminanceSsim << '\n';
        return 1;
    }

    const Json& measured = manifest.at("measured");
    constexpr double kMetricTolerance = 1.0e-12;
    if (!requireTrue(std::abs(measured.at("rgb_mae").get<double>() - imageMetrics.rgbMae) <= kMetricTolerance &&
                         std::abs(measured.at("rgb_rmse").get<double>() - imageMetrics.rgbRmse) <= kMetricTolerance &&
                         std::abs(measured.at("luminance_ssim").get<double>() - imageMetrics.luminanceSsim) <=
                             kMetricTolerance &&
                         std::abs(measured.at("rtgi_trace_p95_reduction_percent").get<double>() - traceReduction) <=
                             kMetricTolerance &&
                         std::abs(measured.at("complete_gpu_p95_reduction_percent").get<double>() -
                                  completeGpuReduction) <= kMetricTolerance &&
                         std::abs(measured.at("candidate_reduction_percent").get<double>() - candidateReduction) <=
                             kMetricTolerance &&
                         measured.at("memory_increase_bytes").get<uint64_t>() == micromapMemory - candidateMemory,
                     "saved A/B measurements must exactly describe the locked reports and images")) {
        return 1;
    }

    std::cout << std::setprecision(17) << "[rtgi_cutout_traversal_ab_test] PASS: RGB MAE=" << imageMetrics.rgbMae
              << " RMSE=" << imageMetrics.rgbRmse << " luminance SSIM=" << imageMetrics.luminanceSsim << '\n';
    return 0;
}
