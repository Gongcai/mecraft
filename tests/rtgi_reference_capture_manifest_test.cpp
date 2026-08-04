#include <nlohmann/json.hpp>

#include <cmath>
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
        const Json& stages = timing.at("stages");
        double rtgiP95 = 0.0;
        double nrdP95 = 0.0;
        double ssgiP95 = 0.0;
        if (!requireTrue(readFiniteP95(stages, "RTGI", rtgiP95) && rtgiP95 > 0.0 &&
                             readFiniteP95(stages, "NRD", nrdP95) && nrdP95 > 0.0 &&
                             readFiniteP95(stages, "SSGI", ssgiP95) && ssgiP95 == 0.0,
                         "RTGI and NRD must be active while SSGI remains disabled")) {
            return 1;
        }
    }

    std::cout << "[rtgi_reference_capture_manifest_test] PASS\n";
    return 0;
}
