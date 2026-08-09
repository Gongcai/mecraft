#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

using Json = nlohmann::json;

bool requireTrue(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "[rtgi_reference_capture_manifest_test] FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool hasString(const Json& object, const char* field, const char* expected) {
    const auto value = object.find(field);
    return value != object.end() && value->is_string() && value->get_ref<const std::string&>() == expected;
}

bool readFiniteP95(const Json& stages, const char* stage, double& value) {
    const auto stageValue = stages.find(stage);
    if (stageValue == stages.end() || !stageValue->is_object()) {
        return false;
    }
    const auto p95 = stageValue->find("p95");
    if (p95 == stageValue->end() || !p95->is_number()) {
        return false;
    }
    value = p95->get<double>();
    return std::isfinite(value) && value >= 0.0;
}

bool readFinitePercentile(const Json& object, const char* field, double& value) {
    const auto percentile = object.find(field);
    if (percentile == object.end() || !percentile->is_number()) {
        return false;
    }
    value = percentile->get<double>();
    return std::isfinite(value) && value >= 0.0;
}

} // namespace

int main() {
    const std::filesystem::path root = std::filesystem::path(MECRAFT_TEST_SOURCE_DIR) /
                                       "assets/validation/reference_captures";
    std::ifstream manifestInput(root / "rtgi_manifest.json");
    const Json manifest = Json::parse(manifestInput, nullptr, false);
    if (!requireTrue(static_cast<bool>(manifestInput) && !manifest.is_discarded(),
                     "RTGI manifest must contain valid JSON") ||
        !requireTrue(hasString(manifest, "kind", "mecraft.rtgi_reference_capture_manifest"),
                     "RTGI manifest kind must remain versioned") ||
        !requireTrue(manifest.value("version", 0u) == 1u, "only RTGI manifest version 1 is supported")) {
        return 1;
    }

    const Json& profile = manifest.at("capture_profile");
    if (!requireTrue(hasString(profile, "backend", "vulkan") && profile.value("width", 0u) == 1280u &&
                         profile.value("height", 0u) == 720u && profile.value("warmup_frame_count", 0u) == 300u &&
                         profile.value("sample_frame_count", 0u) == 3u &&
                         hasString(profile, "nrd_method", "RELAX_DIFFUSE"),
                     "RTGI capture profile must remain the fixed Vulkan RELAX profile")) {
        return 1;
    }

    const Json& acceptance = manifest.at("acceptance");
    if (!requireTrue(acceptance.value("report_window_must_be_valid", false),
                     "RTGI acceptance must require valid GPU timing windows") ||
        !requireTrue(acceptance.value("ssgi_p95_ms", -1.0) == 0.0,
                     "RTGI acceptance must exclude SSGI from indirect diffuse lighting")) {
        return 1;
    }

    const Json& captures = manifest.at("captures");
    if (!requireTrue(captures.is_array() && captures.size() == 2u,
                     "RTGI manifest must contain the voxel and model captures")) {
        return 1;
    }
    for (const Json& capture : captures) {
        const std::filesystem::path imagePath = root / capture.at("image").get<std::string>();
        const std::filesystem::path reportPath = root / capture.at("report").get<std::string>();
        std::error_code imageError;
        if (!requireTrue(std::filesystem::is_regular_file(imagePath, imageError) && !imageError,
                         "RTGI reference image must be present") ||
            !requireTrue(std::filesystem::file_size(imagePath, imageError) > 0u && !imageError,
                         "RTGI reference image must be non-empty")) {
            return 1;
        }

        std::ifstream reportInput(reportPath);
        const Json report = Json::parse(reportInput, nullptr, false);
        if (!requireTrue(static_cast<bool>(reportInput) && !report.is_discarded(),
                         "RTGI validation report must contain valid JSON") ||
            !requireTrue(hasString(report, "kind", "mecraft.validation_capture_report") &&
                             report.value("rhi_backend", "") == "vulkan" &&
                             report.at("scene_contract").at("id") == capture.at("scene_contract_id") &&
                             report.at("render_settings").at("id") == capture.at("render_settings_id"),
                         "RTGI report identities must match the capture manifest")) {
            return 1;
        }

        const Json& timing = report.at("render_graph_stage_ms");
        if (!requireTrue(timing.value("valid", false) && timing.value("window_sample_count", 0u) == 3u,
                         "RTGI report must contain the complete sampled GPU timing window")) {
            return 1;
        }
        const Json& frameTiming = report.at("render_graph_frame_ms");
        if (!requireTrue(frameTiming.value("scope", "") == "primary_render_graph" &&
                             !frameTiming.value("complete_frame", true) && frameTiming.value("cpu_valid", false) &&
                             frameTiming.value("gpu_valid", false) && frameTiming.value("cpu_sample_count", 0u) == 3u &&
                             frameTiming.value("gpu_sample_count", 0u) == 3u,
                         "RTGI report must expose the primary Render Graph CPU/GPU timing window")) {
            return 1;
        }
        const auto completeFrameTiming = report.find("complete_gpu_frame_ms");
        if (!requireTrue(completeFrameTiming != report.end() && completeFrameTiming->is_object() &&
                             completeFrameTiming->value("scope", "") == "scene_render_graphs" &&
                             completeFrameTiming->value("complete_frame", false) &&
                             completeFrameTiming->value("valid", false) &&
                             completeFrameTiming->value("window_capacity", 0u) == 1000u &&
                             completeFrameTiming->value("sample_count", 0u) == 3u &&
                             completeFrameTiming->value("observed_sample_count", 0u) == 3u,
                         "RTGI report must expose the complete scene-render GPU timing window")) {
            return 1;
        }
        const Json& stages = timing.at("stages");
        double rtgiP95 = 0.0;
        double nrdP95 = 0.0;
        double ssgiP95 = 0.0;
        double completeFrameP95 = 0.0;
        if (!requireTrue(readFiniteP95(stages, "RTGI", rtgiP95) && rtgiP95 > 0.0 &&
                             readFiniteP95(stages, "NRD", nrdP95) && nrdP95 > 0.0 &&
                             readFiniteP95(stages, "SSGI", ssgiP95) && ssgiP95 == 0.0 &&
                             readFiniteP95(*completeFrameTiming, "span_ms", completeFrameP95),
                         "RTGI, NRD, and complete scene GPU spans must have valid statistics")) {
            return 1;
        }

        const auto accelerationWorkValue = report.find("acceleration_structure_work");
        if (!requireTrue(accelerationWorkValue != report.end() && accelerationWorkValue->is_object(),
                         "RTGI report must contain acceleration-structure workload statistics")) {
            return 1;
        }
        const Json& accelerationWork = *accelerationWorkValue;
        if (!requireTrue(accelerationWork.value("scope", "") == "scene_acceleration_structures" &&
                             accelerationWork.value("valid", false) && accelerationWork.value("gpu_valid", false) &&
                             accelerationWork.value("window_capacity", 0u) == 1000u &&
                             accelerationWork.value("sample_count", 0u) == 3u &&
                             accelerationWork.value("observed_sample_count", 0u) == 3u,
                         "Acceleration-structure workload window must be complete and valid")) {
            return 1;
        }
        const Json& accelerationStages = accelerationWork.at("stages");
        constexpr std::array<const char*, 5u> requiredAccelerationStages{
            "SceneTLAS", "TerrainBLAS.Build", "TerrainBLAS.Compaction", "AS.DynamicResourcePreparation",
            "RTGI.SceneTLASBootstrap"};
        for (const char* stageName : requiredAccelerationStages) {
            const auto stageValue = accelerationStages.find(stageName);
            if (!requireTrue(stageValue != accelerationStages.end() && stageValue->is_object(),
                             "Acceleration-structure stage entry must be present")) {
                return 1;
            }
            double cpuP95 = 0.0;
            double gpuP95 = 0.0;
            const Json& stage = *stageValue;
            if (!requireTrue(readFinitePercentile(stage.at("cpu_ms"), "p95", cpuP95) &&
                                 readFinitePercentile(stage.at("gpu_ms"), "p95", gpuP95) &&
                                 stage.at("operation_count").is_number_unsigned() &&
                                 stage.at("peak_per_frame").is_object() &&
                                 stage.at("peak_per_frame").at("operations").is_number_unsigned() &&
                                 stage.at("peak_per_frame").at("instances").is_number_unsigned() &&
                                 stage.at("peak_per_frame").at("primitives").is_number_unsigned() &&
                                 stage.at("peak_per_frame").at("scratch_bytes").is_number_unsigned() &&
                                 stage.at("peak_per_frame").at("structure_bytes").is_number_unsigned(),
                             "Acceleration-structure stage must expose CPU/GPU p95 and per-frame peaks")) {
                return 1;
            }
        }
        const Json& residency = accelerationWork.at("residency");
        const Json& latestResidency = residency.at("latest");
        const bool commonResidencyValid = latestResidency.at("scene_tlas_instances").is_number_unsigned() &&
                                          latestResidency.value("scene_tlas_instances", 0u) > 0u &&
                                          latestResidency.value("scene_tlas_unique_blas", 0u) > 0u &&
                                          latestResidency.value("scene_tlas_bytes", uint64_t{0u}) > 0u &&
                                          latestResidency.value("scene_referenced_blas_bytes", uint64_t{0u}) > 0u;
        const bool sceneResidencyValid = report.value("scene", "") == "voxel"
                                             ? latestResidency.value("terrain_blas", 0u) > 0u &&
                                                   latestResidency.value("terrain_blas_bytes", uint64_t{0u}) > 0u &&
                                                   latestResidency.value("terrain_primitives", uint64_t{0u}) > 0u
                                             : report.value("scene", "") == "model" &&
                                                   latestResidency.value("terrain_blas", 0u) == 0u &&
                                                   latestResidency.value("terrain_blas_bytes", uint64_t{0u}) == 0u &&
                                                   latestResidency.value("terrain_primitives", uint64_t{0u}) == 0u;
        if (!requireTrue(residency.at("peak").is_object() && commonResidencyValid && sceneResidencyValid,
                         "Acceleration-structure residency must match the captured scene")) {
            return 1;
        }
        const auto staticBlasValue = accelerationWork.find("static_blas");
        if (!requireTrue(staticBlasValue != accelerationWork.end() && staticBlasValue->is_object(),
                         "Static BLAS asset-load statistics must be present")) {
            return 1;
        }
        const Json& staticBlas = *staticBlasValue;
        const Json& staticBuild = staticBlas.at("build");
        const Json& staticCompaction = staticBlas.at("compaction");
        double staticBuildCpuMs = 0.0;
        double staticBuildGpuMs = 0.0;
        double staticCompactionCpuMs = 0.0;
        double staticCompactionGpuMs = 0.0;
        if (!requireTrue(staticBlas.value("scope", "") == "asset_load" && staticBlas.value("supported", false) &&
                             staticBlas.value("asset_count", 0u) > 0u &&
                             staticBlas.value("resident_asset_count", 0u) == staticBlas.value("asset_count", 0u) &&
                             staticBlas.value("geometry_count", uint64_t{0u}) > 0u &&
                             staticBlas.value("primitive_count", uint64_t{0u}) > 0u &&
                             staticBuild.value("count", uint64_t{0u}) > 0u &&
                             staticBuild.value("scratch_peak_bytes", uint64_t{0u}) > 0u &&
                             staticBuild.value("uncompacted_blas_bytes", uint64_t{0u}) > 0u &&
                             readFinitePercentile(staticBuild, "cpu_ms", staticBuildCpuMs) && staticBuildCpuMs > 0.0 &&
                             readFinitePercentile(staticBuild, "gpu_ms", staticBuildGpuMs) && staticBuildGpuMs > 0.0 &&
                             staticCompaction.value("count", uint64_t{0u}) > 0u &&
                             staticCompaction.value("compacted_blas_bytes", uint64_t{0u}) > 0u &&
                             readFinitePercentile(staticCompaction, "cpu_ms", staticCompactionCpuMs) &&
                             staticCompactionCpuMs > 0.0 &&
                             readFinitePercentile(staticCompaction, "gpu_ms", staticCompactionGpuMs) &&
                             staticCompactionGpuMs > 0.0,
                         "Static BLAS reports must contain resident geometry and real build timings")) {
            return 1;
        }
    }

    std::cout << "[rtgi_reference_capture_manifest_test] PASS\n";
    return 0;
}
