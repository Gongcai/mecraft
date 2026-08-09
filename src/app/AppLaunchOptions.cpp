#include "AppLaunchOptions.h"

#include "renderer/rhi/RhiDeviceFactory.h"

#include <cstdlib>
#include <limits>

AppLaunchOptions::AppLaunchOptions() : rhiBackend(renderer::rhi::defaultRhiBackend()) {}

bool AppLaunchOptions::validationEnabled() const {
    return !validationScenePath.empty();
}

RhiBackend resolveLaunchRhiBackend(const AppLaunchOptions& options, const std::optional<RhiBackend> savedBackend) {
    if (options.rhiBackendExplicit) {
        return options.rhiBackend;
    }
    return savedBackend.value_or(options.rhiBackend);
}

std::optional<ValidationScene> parseValidationScene(const std::string_view value) {
    if (value == "voxel") {
        return ValidationScene::Voxel;
    }
    if (value == "model") {
        return ValidationScene::Model;
    }
    return std::nullopt;
}

const char* validationSceneStableId(const ValidationScene scene) {
    switch (scene) {
    case ValidationScene::None: return "none";
    case ValidationScene::Voxel: return "voxel";
    case ValidationScene::Model: return "model";
    }
    std::abort();
}

std::optional<RtgiCutoutTraversalMode> parseRtgiCutoutTraversalMode(const std::string_view value) {
    if (value == "candidate_loop") {
        return RtgiCutoutTraversalMode::CandidateLoop;
    }
    if (value == "opacity_micromap") {
        return RtgiCutoutTraversalMode::OpacityMicromap;
    }
    return std::nullopt;
}

const char* rtgiCutoutTraversalModeStableId(const RtgiCutoutTraversalMode mode) {
    switch (mode) {
    case RtgiCutoutTraversalMode::CandidateLoop: return "candidate_loop";
    case RtgiCutoutTraversalMode::OpacityMicromap: return "opacity_micromap";
    }
    std::abort();
}

bool validateAppLaunchOptions(const AppLaunchOptions& options, std::string& error) {
    if (!options.validationEnabled()) {
        if (!options.validationCapturePath.empty() || !options.validationReportPath.empty() ||
            options.validationWarmupFramesSet || options.validationSampleFramesSet || options.validationWidthSet ||
            options.validationHeightSet || options.validationRtgiCutoutTraversal.has_value()) {
            error = "Validation options require --validation-scene-file";
            return false;
        }
        return true;
    }
    if (options.validationCapturePath.empty()) {
        error = "--validation-capture is required for validation runs";
        return false;
    }
    if (options.validationReportPath.empty()) {
        error = "--validation-report is required for validation runs";
        return false;
    }
    if (options.validationSampleFrames < 2u) {
        error = "--validation-sample-frames must be at least 2";
        return false;
    }
    if (options.validationWarmupFrames > std::numeric_limits<uint32_t>::max() - options.validationSampleFrames) {
        error = "Validation warmup and sample frame counts overflow the frame index";
        return false;
    }
    if (options.validationWidth == 0u || options.validationHeight == 0u) {
        error = "Validation dimensions must be greater than zero";
        return false;
    }
    if (options.validationWidth > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        options.validationHeight > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        error = "Validation dimensions exceed the window size contract";
        return false;
    }
    if (options.recordInput || options.replayInput) {
        error = "Validation runs cannot record or replay input";
        return false;
    }
    if (options.benchmarkDurationSeconds > 0.0 || !options.benchmarkReportPath.empty()) {
        error = "Validation runs use frame counts and --validation-report";
        return false;
    }
    if (options.autoStartGameplay) {
        error = "Validation scenes cannot start gameplay benchmark mode";
        return false;
    }
    if (!options.benchmarkWorldName.empty() || !options.benchmarkWorldDisplayName.empty()) {
        error = "Validation runs do not accept writable benchmark worlds";
        return false;
    }
    if (options.benchmarkSeedSet || options.benchmarkRenderDistanceSet) {
        error = "Validation scene files own voxel world options";
        return false;
    }
    return true;
}
