#include "src/app/GameManager.h"
#include "src/renderer/rhi/RhiDeviceFactory.h"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace {

void printUsage() {
    std::cout << "Mecraft options:\n"
              << "  --record-input <file>              Record InputSnapshot frames to a replay file.\n"
              << "  --replay-input <file>              Play InputSnapshot frames from a replay file.\n"
              << "  --input-scope <app|gameplay>       Start replay at app launch or gameplay entry.\n"
              << "  --benchmark                        Start gameplay directly with benchmark defaults.\n"
              << "  --benchmark-world <folder>         Start gameplay directly with a save folder.\n"
              << "  --benchmark-seed <integer>         Seed used when creating/loading benchmark world.\n"
              << "  --benchmark-render-distance <n>    Render distance for benchmark gameplay.\n"
              << "  --benchmark-duration <seconds>     Close the app after replay has been active this long.\n"
              << "  --benchmark-report <file>          Write gameplay replay frame timing summary as JSON.\n"
              << "  --benchmark-save-root <path>       Save root for benchmark worlds.\n"
              << "  --benchmark-no-save                Disable saving for benchmark gameplay.\n"
              << "  --validation-scene-file <file>    Versioned scene identity used by validation.\n"
              << "  --validation-capture <file>       Write the final scene frame as PNG.\n"
              << "  --validation-report <file>        Write timing and capture metadata as JSON.\n"
              << "  --validation-warmup-frames <n>    Fixed warmup frame count; default 300.\n"
              << "  --validation-sample-frames <n>    Fixed measured frame count; default 1000.\n"
              << "  --validation-width <pixels>       Capture width; default 1280.\n"
              << "  --validation-height <pixels>      Capture height; default 720.\n"
              << "  --validation-rtgi-cutout-traversal <candidate_loop|opacity_micromap>\n"
              << "                                      Select the measured RTGI cutout implementation.\n"
              << "  --validation-rtgi-hdr-capture-dir <directory>\n"
              << "                                      Directory for linear RTGI HDR EXR sample frames.\n"
              << "  --validation-rtgi-hdr-capture <raw|denoised|raw_and_denoised>\n"
              << "                                      Select linear RTGI HDR signals written per sample frame.\n"
              << "  --validation-rtgi-quality-profile <id>\n"
              << "                                      Lock a static RTGI camera time, resolution, and ROI.\n"
              << "  --rhi-backend <opengl|vulkan>      Select the graphics backend.\n"
              << "  --rhi-debug-output                 Enable graphics backend debug output.\n"
              << "  --no-rhi-debug-output              Disable graphics backend debug output.\n"
              << "  --no-exit-on-replay-end            Keep app open when replay frames are exhausted.\n";
}

bool parseIntArg(const char* text, const char* name, int& out, std::string& error) {
    const char* end = text + std::char_traits<char>::length(text);
    int value = 0;
    const auto result = std::from_chars(text, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        error = std::string("Invalid integer for ") + name + ": " + text;
        return false;
    }
    out = value;
    return true;
}

bool parseDoubleArg(const char* text, const char* name, double& out, std::string& error) {
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (errno == ERANGE || end == text || *end != '\0' || value < 0.0 || !std::isfinite(value)) {
        error = std::string("Invalid number for ") + name + ": " + text;
        return false;
    }
    out = value;
    return true;
}

bool parseUint32Arg(const char* text, const char* name, uint32_t& out, std::string& error) {
    const char* end = text + std::char_traits<char>::length(text);
    uint32_t value = 0u;
    const auto result = std::from_chars(text, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        error = std::string("Invalid unsigned integer for ") + name + ": " + text;
        return false;
    }
    out = value;
    return true;
}

bool requireValue(int argc, char** argv, int& index, const char* name, const char*& out, std::string& error) {
    if (index + 1 >= argc) {
        error = std::string("Missing value for ") + name;
        return false;
    }
    out = argv[++index];
    return true;
}

bool parseLaunchOptions(int argc, char** argv, AppLaunchOptions& options, std::string& error) {
    options = AppLaunchOptions{};
    bool inputScopeSet = false;
    bool rhiDebugOutputSet = false;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            std::exit(0);
        }
        if (arg == "--record-input") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--record-input", value, error)) {
                return false;
            }
            options.recordInput = true;
            options.inputRecordPath = value;
        } else if (arg == "--replay-input") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--replay-input", value, error)) {
                return false;
            }
            options.replayInput = true;
            options.inputReplayPath = value;
        } else if (arg == "--input-scope") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--input-scope", value, error)) {
                return false;
            }
            const std::string scope = value;
            if (scope == "app") {
                options.inputReplayScope = AppLaunchOptions::InputReplayScope::App;
            } else if (scope == "gameplay") {
                options.inputReplayScope = AppLaunchOptions::InputReplayScope::Gameplay;
            } else {
                error = "Input scope must be app or gameplay";
                return false;
            }
            inputScopeSet = true;
        } else if (arg == "--benchmark") {
            options.autoStartGameplay = true;
        } else if (arg == "--benchmark-world") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--benchmark-world", value, error)) {
                return false;
            }
            options.autoStartGameplay = true;
            options.benchmarkWorldName = value;
        } else if (arg == "--benchmark-world-display-name") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--benchmark-world-display-name", value, error)) {
                return false;
            }
            options.benchmarkWorldDisplayName = value;
        } else if (arg == "--benchmark-seed") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--benchmark-seed", value, error) ||
                !parseIntArg(value, "--benchmark-seed", options.benchmarkSeed, error)) {
                return false;
            }
            options.benchmarkSeedSet = true;
        } else if (arg == "--benchmark-render-distance") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--benchmark-render-distance", value, error) ||
                !parseIntArg(value, "--benchmark-render-distance", options.benchmarkRenderDistance, error)) {
                return false;
            }
            options.benchmarkRenderDistanceSet = true;
        } else if (arg == "--benchmark-duration") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--benchmark-duration", value, error) ||
                !parseDoubleArg(value, "--benchmark-duration", options.benchmarkDurationSeconds, error)) {
                return false;
            }
        } else if (arg == "--benchmark-report") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--benchmark-report", value, error)) {
                return false;
            }
            options.benchmarkReportPath = value;
        } else if (arg == "--benchmark-save-root") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--benchmark-save-root", value, error)) {
                return false;
            }
            options.benchmarkSaveRoot = value;
        } else if (arg == "--benchmark-no-save") {
            options.benchmarkEnableSaving = false;
        } else if (arg == "--validation-scene-file") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--validation-scene-file", value, error)) {
                return false;
            }
            options.validationScenePath = value;
        } else if (arg == "--validation-capture") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--validation-capture", value, error)) {
                return false;
            }
            options.validationCapturePath = value;
        } else if (arg == "--validation-report") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--validation-report", value, error)) {
                return false;
            }
            options.validationReportPath = value;
        } else if (arg == "--validation-warmup-frames") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--validation-warmup-frames", value, error) ||
                !parseUint32Arg(value, "--validation-warmup-frames", options.validationWarmupFrames, error)) {
                return false;
            }
            options.validationWarmupFramesSet = true;
        } else if (arg == "--validation-sample-frames") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--validation-sample-frames", value, error) ||
                !parseUint32Arg(value, "--validation-sample-frames", options.validationSampleFrames, error)) {
                return false;
            }
            options.validationSampleFramesSet = true;
        } else if (arg == "--validation-width") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--validation-width", value, error) ||
                !parseUint32Arg(value, "--validation-width", options.validationWidth, error)) {
                return false;
            }
            options.validationWidthSet = true;
        } else if (arg == "--validation-height") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--validation-height", value, error) ||
                !parseUint32Arg(value, "--validation-height", options.validationHeight, error)) {
                return false;
            }
            options.validationHeightSet = true;
        } else if (arg == "--validation-rtgi-cutout-traversal") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--validation-rtgi-cutout-traversal", value, error)) {
                return false;
            }
            options.validationRtgiCutoutTraversal = parseRtgiCutoutTraversalMode(value);
            if (!options.validationRtgiCutoutTraversal.has_value()) {
                error = "Validation RTGI cutout traversal must be candidate_loop or opacity_micromap";
                return false;
            }
        } else if (arg == "--validation-rtgi-hdr-capture-dir") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--validation-rtgi-hdr-capture-dir", value, error)) {
                return false;
            }
            options.validationRtgiHdrCaptureDirectory = value;
        } else if (arg == "--validation-rtgi-hdr-capture") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--validation-rtgi-hdr-capture", value, error)) {
                return false;
            }
            options.validationRtgiHdrCaptureMode = parseValidationRtgiHdrCaptureMode(value);
            if (!options.validationRtgiHdrCaptureMode.has_value()) {
                error = "Validation RTGI HDR capture must be raw, denoised, or raw_and_denoised";
                return false;
            }
        } else if (arg == "--validation-rtgi-quality-profile") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--validation-rtgi-quality-profile", value, error)) {
                return false;
            }
            options.validationRtgiQualityProfile = value;
        } else if (arg == "--rhi-backend") {
            const char* value = nullptr;
            if (!requireValue(argc, argv, index, "--rhi-backend", value, error)) {
                return false;
            }
            const std::optional<RhiBackend> backend = renderer::rhi::parseRhiBackend(value);
            if (!backend) {
                error = "RHI backend must be opengl or vulkan";
                return false;
            }
            options.rhiBackend = *backend;
            options.rhiBackendExplicit = true;
        } else if (arg == "--rhi-debug-output") {
            options.enableRhiDebugOutput = true;
            rhiDebugOutputSet = true;
        } else if (arg == "--no-rhi-debug-output") {
            options.enableRhiDebugOutput = false;
            rhiDebugOutputSet = true;
        } else if (arg == "--no-exit-on-replay-end") {
            options.exitWhenPlaybackEnds = false;
        } else {
            error = "Unknown command line option: " + arg;
            return false;
        }
    }

    if (options.recordInput && options.replayInput) {
        error = "--record-input and --replay-input cannot be used together";
        return false;
    }
    if (!options.benchmarkReportPath.empty() && !options.autoStartGameplay) {
        error = "--benchmark-report requires --benchmark or --benchmark-world";
        return false;
    }
    if (!validateAppLaunchOptions(options, error)) {
        return false;
    }
    if (options.autoStartGameplay && !inputScopeSet) {
        options.inputReplayScope = AppLaunchOptions::InputReplayScope::Gameplay;
    }
    if (options.autoStartGameplay || options.validationEnabled()) {
        options.enableDebugDashboard = false;
        if (!rhiDebugOutputSet) {
            options.enableRhiDebugOutput = false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    AppLaunchOptions options;
    std::string error;
    if (!parseLaunchOptions(argc, argv, options, error)) {
        std::cerr << error << '\n';
        printUsage();
        return 1;
    }

    const int windowWidth = options.validationEnabled() ? static_cast<int>(options.validationWidth) : 1280;
    const int windowHeight = options.validationEnabled() ? static_cast<int>(options.validationHeight) : 720;
    GameManager app;
    if (!app.init(windowWidth, windowHeight, "Mecraft", std::move(options))) {
        app.shutdown();
        return 1;
    }
    const bool runSucceeded = app.run();
    app.shutdown();
    return runSucceeded ? 0 : 1;
}
