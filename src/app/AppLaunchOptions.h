#ifndef MECRAFT_APP_LAUNCH_OPTIONS_H
#define MECRAFT_APP_LAUNCH_OPTIONS_H

#include "renderer/rhi/RhiTypes.h"

#include <filesystem>
#include <optional>
#include <string>

struct AppLaunchOptions {
    AppLaunchOptions();

    enum class InputReplayScope {
        App,
        Gameplay
    };

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
};

[[nodiscard]] RhiBackend resolveLaunchRhiBackend(
    const AppLaunchOptions& options,
    std::optional<RhiBackend> savedBackend);

#endif // MECRAFT_APP_LAUNCH_OPTIONS_H
