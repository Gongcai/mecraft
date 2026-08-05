#include "app/validation/ValidationRunController.h"

#include <cmath>
#include <filesystem>
#include <iostream>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[validation_run_controller_test] FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool nearlyEqual(const double lhs, const double rhs, const double tolerance = 1.0e-9) {
    return std::abs(lhs - rhs) <= tolerance;
}

} // namespace

int main() {
    AppLaunchOptions options;
    options.validationScenePath =
        std::filesystem::path(MECRAFT_TEST_SOURCE_DIR) / "assets/validation/scenes/m0_voxel_baseline.json";
    options.validationCapturePath = "capture.png";
    options.validationReportPath = "report.json";
    options.validationWarmupFrames = 2u;
    options.validationSampleFrames = 3u;

    app::validation::ValidationRunController controller;
    if (!requireTrue(controller.configure(options), "a valid Camera Path must configure validation") ||
        !requireTrue(controller.renderSettingsProfile().id == "m0_voxel_render_settings" &&
                         controller.renderSettingsProfile().version ==
                             app::validation::kValidationRenderSettingsVersion &&
                         renderer::contracts::stableContentHashHex(controller.renderSettingsProfile().contentHash) ==
                             "a64f59f7e206230c" &&
                         !controller.renderSettingsProfile().settings.upscale.dynamicResolutionEnabled &&
                         !controller.renderSettingsProfile().settings.upscale.fsr1Enabled,
                     "validation must publish the fixed versioned renderer profile") ||
        !requireTrue(controller.sceneContract().id == "m0_voxel_baseline" &&
                         controller.sceneContract().voxelWorld.has_value() &&
                         controller.sceneContract().voxelWorld->seed == 1234 &&
                         controller.sceneContract().voxelWorld->renderDistance == 4,
                     "validation must expose the verified scene and world identity") ||
        !requireTrue(controller.beginScene(ValidationScene::Voxel),
                     "the configured scene class must start validation")) {
        return 1;
    }

    const app::validation::ValidationFrame* frame = controller.currentFrame();
    if (!requireTrue(frame != nullptr && !frame->collectPerformance && nearlyEqual(frame->cameraPose.position.x, 12.0),
                     "warmup must hold the first Camera Path pose") ||
        !requireTrue(controller.completeFrame(true), "the first warmup frame must complete") ||
        !requireTrue(controller.completeFrame(true), "the second warmup frame must complete")) {
        return 1;
    }

    frame = controller.currentFrame();
    if (!requireTrue(frame != nullptr && frame->collectPerformance && !frame->captureAfterRender &&
                         nearlyEqual(frame->cameraPose.position.x, 12.0),
                     "sampling must start at the inclusive path origin") ||
        !requireTrue(controller.completeFrame(true), "the first sample must complete") ||
        !requireTrue(controller.consumeCompletedSampleFrame(), "each sample must publish one performance event") ||
        !requireTrue(!controller.consumeCompletedSampleFrame(), "sample events must be consumed exactly once")) {
        return 1;
    }

    frame = controller.currentFrame();
    if (!requireTrue(frame != nullptr && nearlyEqual(frame->cameraPose.position.x, -6.0),
                     "the middle sample must use the exact half-duration pose") ||
        !requireTrue(controller.completeFrame(true), "the middle sample must complete")) {
        return 1;
    }

    frame = controller.currentFrame();
    if (!requireTrue(frame != nullptr && frame->captureAfterRender && nearlyEqual(frame->cameraPose.position.x, -16.0),
                     "the final sample must request capture at the path endpoint") ||
        !requireTrue(!controller.completeFrame(false, "write failed") && controller.failed() &&
                         controller.error() == app::validation::ValidationRunError::CaptureFailed,
                     "capture failure must terminate validation explicitly")) {
        return 1;
    }

    AppLaunchOptions invalid = options;
    invalid.validationSampleFrames = 1u;
    std::string error;
    if (!requireTrue(!validateAppLaunchOptions(invalid, error), "validation must reject a single sample frame")) {
        return 1;
    }

    AppLaunchOptions rtgiOptions = options;
    rtgiOptions.validationScenePath =
        std::filesystem::path(MECRAFT_TEST_SOURCE_DIR) / "assets/validation/scenes/m3_voxel_rtgi_cave.json";
    app::validation::ValidationRunController rtgiController;
    if (!requireTrue(rtgiController.configure(rtgiOptions), "the M3 RTGI Camera Path must configure validation") ||
        !requireTrue(rtgiController.renderSettingsProfile().id ==
                             app::validation::kValidationRtgiVoxelRenderSettingsId &&
                         rtgiController.renderSettingsProfile().version ==
                             app::validation::kValidationRtgiRenderSettingsVersion &&
                         rtgiController.renderSettingsProfile().settings.rtgi.enabled &&
                         rtgiController.renderSettingsProfile().settings.nrd.enabled &&
                         !rtgiController.renderSettingsProfile().settings.ssgi.enabled &&
                         rtgiController.renderSettingsProfile().settings.nrd.disocclusionThreshold == 0.02f,
                     "M3 validation must select the fixed RTGI/RELAX profile without SSGI")) {
        return 1;
    }

    std::cout << "[validation_run_controller_test] PASS\n";
    return 0;
}
