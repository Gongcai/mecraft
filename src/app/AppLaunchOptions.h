#ifndef MECRAFT_APP_LAUNCH_OPTIONS_H
#define MECRAFT_APP_LAUNCH_OPTIONS_H

#include "renderer/rhi/RhiTypes.h"
#include "renderer/core/RenderSettings.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

enum class ValidationScene : uint8_t { None, Voxel, Model };

/// Selects the linear RTGI signal captured for every sampled validation frame.
enum class ValidationRtgiHdrCaptureMode : uint8_t { Raw = 0, Denoised = 1, RawAndDenoised = 2 };

struct AppLaunchOptions {
    AppLaunchOptions();

    enum class InputReplayScope { App, Gameplay };

    bool recordInput = false;
    bool replayInput = false;
    std::filesystem::path inputRecordPath;
    std::filesystem::path inputReplayPath;
    InputReplayScope inputReplayScope = InputReplayScope::App;

    bool autoStartGameplay = false;
    bool benchmarkSeedSet = false;
    bool benchmarkRenderDistanceSet = false;
    int benchmarkSeed = 1234;
    int benchmarkRenderDistance = 16;
    std::string benchmarkWorldName;
    std::string benchmarkWorldDisplayName;
    std::filesystem::path benchmarkSaveRoot = "saves";
    bool benchmarkEnableSaving = true;
    double benchmarkDurationSeconds = 0.0;
    std::filesystem::path benchmarkReportPath;
    bool exitWhenPlaybackEnds = true;
    bool enableDebugDashboard = true;
    bool enableRhiDebugOutput = false;
    bool rhiBackendExplicit = false;
    RhiBackend rhiBackend;

    /// Opens a persisted model-scene document directly in the editor.
    std::filesystem::path modelScenePath;

    std::filesystem::path validationScenePath;
    std::filesystem::path validationCapturePath;
    std::filesystem::path validationReportPath;
    uint32_t validationWarmupFrames = 300u;
    uint32_t validationSampleFrames = 1000u;
    uint32_t validationWidth = 1280u;
    uint32_t validationHeight = 720u;
    bool validationWarmupFramesSet = false;
    bool validationSampleFramesSet = false;
    bool validationWidthSet = false;
    bool validationHeightSet = false;
    std::optional<RtgiCutoutTraversalMode> validationRtgiCutoutTraversal;
    std::filesystem::path validationRtgiHdrCaptureDirectory;
    std::optional<ValidationRtgiHdrCaptureMode> validationRtgiHdrCaptureMode;
    std::string validationRtgiQualityProfile;
    bool validationRtgiReference = false;

    /// Reports whether a deterministic validation run was requested.
    /// @return True when a versioned scene descriptor was supplied.
    [[nodiscard]] bool validationEnabled() const;
};

[[nodiscard]] RhiBackend resolveLaunchRhiBackend(const AppLaunchOptions& options,
                                                 std::optional<RhiBackend> savedBackend);

/// Parses the stable command-line identifier for a validation scene class.
/// @param value Lowercase scene identifier supplied by the user.
/// @return Parsed scene class, or no value when the identifier is invalid.
[[nodiscard]] std::optional<ValidationScene> parseValidationScene(std::string_view value);

/// Returns the stable identifier used by reports and diagnostics.
/// @param scene Validation scene class to identify.
/// @return Process-lifetime lowercase scene identifier.
[[nodiscard]] const char* validationSceneStableId(ValidationScene scene);

/// Parses the stable validation identifier for the measured RTGI cutout traversal implementation.
[[nodiscard]] std::optional<RtgiCutoutTraversalMode> parseRtgiCutoutTraversalMode(std::string_view value);

/// Returns the stable identifier emitted by validation reports and benchmark telemetry.
[[nodiscard]] const char* rtgiCutoutTraversalModeStableId(RtgiCutoutTraversalMode mode);

/// Parses the stable capture mode for per-sample linear RTGI HDR output.
[[nodiscard]] std::optional<ValidationRtgiHdrCaptureMode> parseValidationRtgiHdrCaptureMode(std::string_view value);

/// Returns the stable identifier emitted by validation reports and capture manifests.
[[nodiscard]] const char* validationRtgiHdrCaptureModeStableId(ValidationRtgiHdrCaptureMode mode);

/// Validates cross-option requirements for benchmark and validation modes.
/// @param options Complete launch configuration after command-line parsing.
/// @param error Receives a user-facing validation error on failure.
/// @return True when the launch configuration is internally consistent.
[[nodiscard]] bool validateAppLaunchOptions(const AppLaunchOptions& options, std::string& error);

#endif // MECRAFT_APP_LAUNCH_OPTIONS_H
