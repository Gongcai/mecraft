#include "app/AppLaunchOptions.h"
#include "app/validation/RtgiQualityProfile.h"

#include <iostream>
#include <limits>
#include <optional>
#include <string>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    AppLaunchOptions options;
    options.rhiBackend = RhiBackend::OpenGL;

    if (!requireTrue(resolveLaunchRhiBackend(options, RhiBackend::Vulkan) == RhiBackend::Vulkan,
                     "saved backend must select the next launch backend")) {
        return 1;
    }
    if (!requireTrue(resolveLaunchRhiBackend(options, std::nullopt) == RhiBackend::OpenGL,
                     "configured default must select the backend when no saved value exists")) {
        return 1;
    }

    options.rhiBackendExplicit = true;
    options.rhiBackend = RhiBackend::OpenGL;
    if (!requireTrue(resolveLaunchRhiBackend(options, RhiBackend::Vulkan) == RhiBackend::OpenGL,
                     "explicit command-line backend must override the saved backend")) {
        return 1;
    }

    if (!requireTrue(parseValidationScene("voxel") == ValidationScene::Voxel &&
                         parseValidationScene("model") == ValidationScene::Model &&
                         !parseValidationScene("Voxel").has_value(),
                     "validation scene identifiers must use the stable lowercase contract") ||
        !requireTrue(std::string(validationSceneStableId(ValidationScene::Voxel)) == "voxel" &&
                         std::string(validationSceneStableId(ValidationScene::Model)) == "model",
                     "validation scene reports must use stable identifiers")) {
        return 1;
    }
    if (!requireTrue(parseRtgiCutoutTraversalMode("candidate_loop") == RtgiCutoutTraversalMode::CandidateLoop &&
                         parseRtgiCutoutTraversalMode("opacity_micromap") == RtgiCutoutTraversalMode::OpacityMicromap &&
                         !parseRtgiCutoutTraversalMode("candidate").has_value() &&
                         std::string(rtgiCutoutTraversalModeStableId(RtgiCutoutTraversalMode::OpacityMicromap)) ==
                             "opacity_micromap",
                     "validation RTGI traversal identifiers must be strict and stable")) {
        return 1;
    }
    if (!requireTrue(
            parseValidationRtgiHdrCaptureMode("raw") == ValidationRtgiHdrCaptureMode::Raw &&
                parseValidationRtgiHdrCaptureMode("denoised") == ValidationRtgiHdrCaptureMode::Denoised &&
                parseValidationRtgiHdrCaptureMode("raw_and_denoised") == ValidationRtgiHdrCaptureMode::RawAndDenoised &&
                !parseValidationRtgiHdrCaptureMode("both").has_value() &&
                std::string(validationRtgiHdrCaptureModeStableId(ValidationRtgiHdrCaptureMode::RawAndDenoised)) ==
                    "raw_and_denoised",
            "validation RTGI HDR capture identifiers must be strict and stable")) {
        return 1;
    }

    AppLaunchOptions validation;
    validation.validationScenePath = "scene.json";
    validation.validationCapturePath = "capture.png";
    validation.validationReportPath = "report.json";
    std::string error;
    if (!requireTrue(validateAppLaunchOptions(validation, error),
                     "a complete voxel validation contract must be accepted")) {
        return 1;
    }

    AppLaunchOptions invalid = validation;
    invalid.validationScenePath.clear();
    if (!requireTrue(!validateAppLaunchOptions(invalid, error), "validation outputs must require a scene descriptor")) {
        return 1;
    }
    invalid = validation;
    invalid.validationSampleFrames = 1u;
    if (!requireTrue(!validateAppLaunchOptions(invalid, error), "validation must include both Camera Path endpoints")) {
        return 1;
    }
    invalid = validation;
    invalid.validationWarmupFrames = std::numeric_limits<uint32_t>::max();
    if (!requireTrue(!validateAppLaunchOptions(invalid, error), "validation frame counts must not overflow")) {
        return 1;
    }
    invalid = validation;
    invalid.validationWidth = 0u;
    if (!requireTrue(!validateAppLaunchOptions(invalid, error), "validation dimensions must be nonzero")) {
        return 1;
    }
    invalid = validation;
    invalid.validationRtgiHdrCaptureDirectory = "hdr";
    if (!requireTrue(!validateAppLaunchOptions(invalid, error),
                     "an RTGI HDR capture directory must require an explicit capture mode")) {
        return 1;
    }
    invalid = validation;
    invalid.validationRtgiHdrCaptureMode = ValidationRtgiHdrCaptureMode::Raw;
    if (!requireTrue(!validateAppLaunchOptions(invalid, error),
                     "an RTGI HDR capture mode must require an output directory")) {
        return 1;
    }
    AppLaunchOptions quality = validation;
    quality.validationSampleFrames = app::validation::kRtgiQualitySequenceFrameCount;
    quality.validationRtgiHdrCaptureDirectory = "hdr";
    quality.validationRtgiHdrCaptureMode = ValidationRtgiHdrCaptureMode::RawAndDenoised;
    quality.validationRtgiQualityProfile = "v01_window_room_static";
    if (!requireTrue(validateAppLaunchOptions(quality, error),
                     "a static RTGI quality profile must require its exact sequence contract")) {
        return 1;
    }
    quality.validationSampleFrames = 31u;
    if (!requireTrue(!validateAppLaunchOptions(quality, error),
                     "a static RTGI quality profile must reject non-32-frame sequences")) {
        return 1;
    }
    invalid = validation;
    invalid.recordInput = true;
    if (!requireTrue(!validateAppLaunchOptions(invalid, error), "validation must reject input recording")) {
        return 1;
    }
    invalid = validation;
    invalid.benchmarkWorldName = "baseline";
    if (!requireTrue(!validateAppLaunchOptions(invalid, error), "validation must reject writable benchmark worlds")) {
        return 1;
    }
    invalid = validation;
    invalid.benchmarkSeedSet = true;
    if (!requireTrue(!validateAppLaunchOptions(invalid, error), "scene descriptors must own voxel world options")) {
        return 1;
    }
    invalid = validation;
    invalid.autoStartGameplay = true;
    if (!requireTrue(!validateAppLaunchOptions(invalid, error), "validation must reject benchmark gameplay mode")) {
        return 1;
    }

    AppLaunchOptions pathsWithoutScene;
    pathsWithoutScene.validationReportPath = "report.json";
    if (!requireTrue(!validateAppLaunchOptions(pathsWithoutScene, error), "validation paths must require a scene")) {
        return 1;
    }
    AppLaunchOptions framesWithoutScene;
    framesWithoutScene.validationWarmupFramesSet = true;
    framesWithoutScene.validationWarmupFrames = 2u;
    if (!requireTrue(!validateAppLaunchOptions(framesWithoutScene, error),
                     "validation frame options must require a scene")) {
        return 1;
    }
    AppLaunchOptions traversalWithoutScene;
    traversalWithoutScene.validationRtgiCutoutTraversal = RtgiCutoutTraversalMode::OpacityMicromap;
    if (!requireTrue(!validateAppLaunchOptions(traversalWithoutScene, error),
                     "validation RTGI traversal overrides must require a scene")) {
        return 1;
    }
    AppLaunchOptions hdrCaptureWithoutScene;
    hdrCaptureWithoutScene.validationRtgiHdrCaptureDirectory = "hdr";
    hdrCaptureWithoutScene.validationRtgiHdrCaptureMode = ValidationRtgiHdrCaptureMode::Raw;
    if (!requireTrue(!validateAppLaunchOptions(hdrCaptureWithoutScene, error),
                     "validation RTGI HDR capture options must require a scene")) {
        return 1;
    }

    return 0;
}
