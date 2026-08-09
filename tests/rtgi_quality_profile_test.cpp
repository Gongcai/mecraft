#include "app/validation/RtgiQualityProfile.h"

#include <filesystem>
#include <iostream>

namespace {

[[nodiscard]] bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[rtgi_quality_profile_test] FAIL: " << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    const std::filesystem::path source =
        std::filesystem::path(MECRAFT_TEST_SOURCE_DIR) / "assets/validation/rtgi_quality_profiles.json";
    const struct ExpectedProfile final {
        const char* id;
        const char* sceneContractId;
        const char* renderSettingsId;
        renderer::contracts::RtgiValidationRoi roi;
    } expected[] = {
        {"v01_window_room_static", "v01_window_room", "m3_voxel_rtgi_quality", {256u, 544u, 256u, 128u}},
        {"v02_cave_turn_static", "v02_cave_turn", "m3_voxel_rtgi_quality", {384u, 216u, 512u, 288u}},
        {"m03_sponza_atrium_static", "m03_sponza_atrium", "m3_model_rtgi_quality", {384u, 216u, 512u, 288u}}};
    for (const ExpectedProfile& item : expected) {
        app::validation::RtgiQualityProfile profile;
        std::string detail;
        if (!requireTrue(app::validation::loadRtgiQualityProfile(source, item.id, profile, detail) ==
                             app::validation::RtgiQualityProfileError::None,
                         "every M3 static profile must load") ||
            !requireTrue(profile.id == item.id && profile.sceneContractId == item.sceneContractId &&
                             profile.renderSettingsId == item.renderSettingsId && profile.version == 2u &&
                             profile.cameraTimeSeconds == 2.0 && profile.captureWidth == 1280u &&
                             profile.captureHeight == 720u && profile.roi.x == item.roi.x &&
                             profile.roi.y == item.roi.y && profile.roi.width == item.roi.width &&
                             profile.roi.height == item.roi.height,
                         "static profile identity, camera, extent, and ROI must remain exact")) {
            return 1;
        }
    }

    app::validation::RtgiQualityProfile missing;
    std::string detail;
    if (!requireTrue(app::validation::loadRtgiQualityProfile(source, "missing", missing, detail) ==
                         app::validation::RtgiQualityProfileError::MissingProfile,
                     "unknown static quality profiles must be rejected")) {
        return 1;
    }
    std::cout << "[rtgi_quality_profile_test] PASS\n";
    return 0;
}
