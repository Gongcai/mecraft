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
    options.validationRtgiHdrCaptureDirectory = "hdr";
    options.validationRtgiHdrCaptureMode = ValidationRtgiHdrCaptureMode::RawAndDenoised;

    app::validation::ValidationRunController controller;
    if (!requireTrue(controller.configure(options), "a valid Camera Path must configure validation") ||
        !requireTrue(controller.renderSettingsProfile().id == "m0_voxel_render_settings" &&
                         controller.renderSettingsProfile().version ==
                             app::validation::kValidationRenderSettingsVersion &&
                         renderer::contracts::stableContentHashHex(controller.renderSettingsProfile().contentHash) ==
                             "dd51efecb1ce4c75" &&
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
    const app::validation::ValidationRenderClock firstRenderClock = controller.currentRenderClock();
    if (!requireTrue(frame != nullptr && !frame->collectPerformance && nearlyEqual(frame->cameraPose.position.x, 12.0),
                     "warmup must hold the first Camera Path pose") ||
        !requireTrue(firstRenderClock.frameIndex == 0u && nearlyEqual(firstRenderClock.renderTimeSeconds, 0.0),
                     "validation render attempts must start from a deterministic zero clock") ||
        !requireTrue(controller.completeRenderAttempt(), "the first render attempt must advance independently") ||
        !requireTrue(controller.currentFrame() == frame && controller.currentRenderClock().frameIndex == 1u,
                     "readiness retries must advance render time without consuming the logical frame") ||
        !requireTrue(controller.completeFrame(true), "the first warmup frame must complete") ||
        !requireTrue(controller.completeFrame(true), "the second warmup frame must complete")) {
        return 1;
    }

    frame = controller.currentFrame();
    if (!requireTrue(frame != nullptr && frame->collectPerformance && !frame->captureAfterRender &&
                         frame->captureRtgiHdrAfterRender && frame->rtgiHdrCaptureSampleIndex == 0u &&
                         frame->rtgiRawCapturePath == "hdr/rtgi_raw_0000.exr" &&
                         frame->nrdDiffuseCapturePath == "hdr/rtgi_denoised_0000.exr" &&
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
    if (!requireTrue(frame != nullptr && frame->captureAfterRender && frame->captureRtgiHdrAfterRender &&
                         frame->rtgiHdrCaptureSampleIndex == 2u &&
                         frame->rtgiRawCapturePath == "hdr/rtgi_raw_0002.exr" &&
                         frame->nrdDiffuseCapturePath == "hdr/rtgi_denoised_0002.exr" &&
                         nearlyEqual(frame->cameraPose.position.x, -16.0),
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
                             app::validation::kValidationRtgiVoxelRenderSettingsVersion &&
                         rtgiController.renderSettingsProfile().settings.rtgi.enabled &&
                         rtgiController.renderSettingsProfile().settings.nrd.enabled &&
                         !rtgiController.renderSettingsProfile().settings.ssgi.enabled &&
                         rtgiController.renderSettingsProfile().settings.nrd.disocclusionThreshold == 0.02f &&
                         !rtgiController.renderSettingsProfile().settings.occlusion.hiZEnabled &&
                         !rtgiController.renderSettingsProfile().settings.shadow.gpuCascadeCullEnabled,
                     "M3 validation must select the fixed RTGI/RELAX profile without SSGI")) {
        return 1;
    }
    rtgiOptions.validationRtgiCutoutTraversal = RtgiCutoutTraversalMode::OpacityMicromap;
    app::validation::ValidationRunController rtgiOmmController;
    if (!requireTrue(rtgiOmmController.configure(rtgiOptions), "the RTGI OMM validation axis must configure") ||
        !requireTrue(rtgiOmmController.renderSettingsProfile().contentHash ==
                             rtgiController.renderSettingsProfile().contentHash &&
                         rtgiOmmController.renderSettingsProfile().settings.rtgi.cutoutTraversal ==
                             RtgiCutoutTraversalMode::CandidateLoop &&
                         rtgiOmmController.runtimeRenderSettings().rtgi.cutoutTraversal ==
                             RtgiCutoutTraversalMode::OpacityMicromap,
                     "the traversal implementation must not alter the locked quality-profile identity")) {
        return 1;
    }

    AppLaunchOptions qualityOptions = options;
    qualityOptions.validationScenePath =
        std::filesystem::path(MECRAFT_TEST_SOURCE_DIR) / "assets/validation/scenes/v01_window_room.json";
    qualityOptions.validationWarmupFrames = 0u;
    qualityOptions.validationSampleFrames = app::validation::kRtgiQualitySequenceFrameCount;
    qualityOptions.validationRtgiQualityProfile = "v01_window_room_static";
    app::validation::ValidationRunController qualityController;
    if (!requireTrue(qualityController.configure(qualityOptions), "the V01 static RTGI profile must configure") ||
        !requireTrue(qualityController.rtgiQualityProfile().has_value() &&
                         qualityController.renderSettingsProfile().id ==
                             app::validation::kValidationRtgiVoxelRenderSettingsId &&
                         qualityController.runtimeRenderSettings().rtgi.enabled &&
                         qualityController.runtimeRenderSettings().nrd.enabled,
                     "a static profile must select the versioned RTGI quality renderer") ||
        !requireTrue(qualityController.beginScene(ValidationScene::Voxel), "the V01 static profile must start")) {
        return 1;
    }
    frame = qualityController.currentFrame();
    if (!requireTrue(frame != nullptr && frame->collectPerformance && nearlyEqual(frame->cameraPose.position.x, 0.0) &&
                         nearlyEqual(frame->cameraPose.position.z, -3.5),
                     "all static quality samples must use the locked two-second camera pose") ||
        !requireTrue(qualityController.completeFrame(true), "the first static quality sample must complete")) {
        return 1;
    }
    frame = qualityController.currentFrame();
    if (!requireTrue(frame != nullptr && nearlyEqual(frame->cameraPose.position.x, 0.0) &&
                         nearlyEqual(frame->cameraPose.position.z, -3.5) &&
                         frame->rtgiLeakageNormalCapturePath.empty() && frame->rtgiLeakageViewZCapturePath.empty(),
                     "the static quality camera must not advance along the path")) {
        return 1;
    }
    while (qualityController.completedSampleFrames() + 1u < app::validation::kRtgiQualitySequenceFrameCount) {
        if (!requireTrue(qualityController.completeFrame(true),
                         "intermediate static quality samples must complete")) {
            return 1;
        }
    }
    frame = qualityController.currentFrame();
    if (!requireTrue(frame != nullptr &&
                         frame->rtgiLeakageNormalCapturePath == "hdr/rtgi_leakage_normal.exr" &&
                         frame->rtgiLeakageViewZCapturePath == "hdr/rtgi_leakage_viewz.exr",
                     "the final static quality sample must capture fixed Leakage Band guides")) {
        return 1;
    }

    qualityOptions.validationSampleFrames = app::validation::kRtgiQualityReferenceSpp;
    qualityOptions.validationRtgiHdrCaptureMode = ValidationRtgiHdrCaptureMode::Raw;
    qualityOptions.validationRtgiReference = true;
    app::validation::ValidationRunController referenceController;
    if (!requireTrue(referenceController.configure(qualityOptions), "the V01 64-spp Reference axis must configure") ||
        !requireTrue(referenceController.runtimeRenderSettings().rtgi.enabled &&
                         referenceController.runtimeRenderSettings().rtgi.referenceSamplingEnabled &&
                         !referenceController.runtimeRenderSettings().nrd.enabled,
                     "Reference capture must advance raw RTGI samples with NRD disabled")) {
        return 1;
    }

    std::cout << "[validation_run_controller_test] PASS\n";
    return 0;
}
