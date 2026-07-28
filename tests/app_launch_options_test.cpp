#include "app/AppLaunchOptions.h"

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

    if (!requireTrue(parseValidationScene("voxel") ==
                         ValidationScene::Voxel &&
                         parseValidationScene("model") ==
                         ValidationScene::Model &&
                         !parseValidationScene("Voxel").has_value(),
                     "validation scene identifiers must use the stable lowercase contract") ||
        !requireTrue(std::string(validationSceneStableId(
                             ValidationScene::Voxel)) == "voxel" &&
                         std::string(validationSceneStableId(
                             ValidationScene::Model)) == "model",
                     "validation scene reports must use stable identifiers")) {
        return 1;
    }

    AppLaunchOptions validation;
    validation.validationScene = ValidationScene::Voxel;
    validation.validationCameraPath = "camera.json";
    validation.validationCapturePath = "capture.png";
    validation.validationReportPath = "report.json";
    std::string error;
    if (!requireTrue(validateAppLaunchOptions(validation, error),
                     "a complete voxel validation contract must be accepted")) {
        return 1;
    }

    AppLaunchOptions invalid = validation;
    invalid.validationCameraPath.clear();
    if (!requireTrue(!validateAppLaunchOptions(invalid, error),
                     "validation must require a Camera Path")) {
        return 1;
    }
    invalid = validation;
    invalid.validationSampleFrames = 1u;
    if (!requireTrue(!validateAppLaunchOptions(invalid, error),
                     "validation must include both Camera Path endpoints")) {
        return 1;
    }
    invalid = validation;
    invalid.validationWarmupFrames =
        std::numeric_limits<uint32_t>::max();
    if (!requireTrue(!validateAppLaunchOptions(invalid, error),
                     "validation frame counts must not overflow")) {
        return 1;
    }
    invalid = validation;
    invalid.validationWidth = 0u;
    if (!requireTrue(!validateAppLaunchOptions(invalid, error),
                     "validation dimensions must be nonzero")) {
        return 1;
    }
    invalid = validation;
    invalid.recordInput = true;
    if (!requireTrue(!validateAppLaunchOptions(invalid, error),
                     "validation must reject input recording")) {
        return 1;
    }
    invalid = validation;
    invalid.benchmarkWorldName = "baseline";
    if (!requireTrue(!validateAppLaunchOptions(invalid, error),
                     "validation must reject writable benchmark worlds")) {
        return 1;
    }
    invalid = validation;
    invalid.validationScene = ValidationScene::Model;
    invalid.benchmarkSeedSet = true;
    if (!requireTrue(!validateAppLaunchOptions(invalid, error),
                     "model validation must reject voxel world options")) {
        return 1;
    }

    AppLaunchOptions pathsWithoutScene;
    pathsWithoutScene.validationReportPath = "report.json";
    if (!requireTrue(!validateAppLaunchOptions(pathsWithoutScene, error),
                     "validation paths must require a scene")) {
        return 1;
    }
    AppLaunchOptions framesWithoutScene;
    framesWithoutScene.validationWarmupFramesSet = true;
    framesWithoutScene.validationWarmupFrames = 2u;
    if (!requireTrue(!validateAppLaunchOptions(framesWithoutScene, error),
                     "validation frame options must require a scene")) {
        return 1;
    }

    return 0;
}
