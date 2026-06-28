#include "src/app/GameManager.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void printUsage() {
    std::cout
        << "Mecraft options:\n"
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
        << "  --gl-debug-output                  Enable OpenGL debug callback output.\n"
        << "  --no-gl-debug-output               Disable OpenGL debug callback output.\n"
        << "  --no-exit-on-replay-end            Keep app open when replay frames are exhausted.\n";
}

int parseIntArg(const char* text, const char* name) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (end == text || *end != '\0') {
        throw std::runtime_error(std::string("Invalid integer for ") + name + ": " + text);
    }
    return static_cast<int>(value);
}

double parseDoubleArg(const char* text, const char* name) {
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || *end != '\0' || value < 0.0) {
        throw std::runtime_error(std::string("Invalid number for ") + name + ": " + text);
    }
    return value;
}

const char* requireValue(int argc, char** argv, int& index, const char* name) {
    if (index + 1 >= argc) {
        throw std::runtime_error(std::string("Missing value for ") + name);
    }
    return argv[++index];
}

AppLaunchOptions parseLaunchOptions(int argc, char** argv) {
    AppLaunchOptions options;
    bool inputScopeSet = false;
    bool glDebugOutputSet = false;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            std::exit(0);
        }
        if (arg == "--record-input") {
            options.recordInput = true;
            options.inputRecordPath = requireValue(argc, argv, index, "--record-input");
        } else if (arg == "--replay-input") {
            options.replayInput = true;
            options.inputReplayPath = requireValue(argc, argv, index, "--replay-input");
        } else if (arg == "--input-scope") {
            const std::string scope = requireValue(argc, argv, index, "--input-scope");
            if (scope == "app") {
                options.inputReplayScope = AppLaunchOptions::InputReplayScope::App;
            } else if (scope == "gameplay") {
                options.inputReplayScope = AppLaunchOptions::InputReplayScope::Gameplay;
            } else {
                throw std::runtime_error("Input scope must be app or gameplay");
            }
            inputScopeSet = true;
        } else if (arg == "--benchmark") {
            options.autoStartGameplay = true;
        } else if (arg == "--benchmark-world") {
            options.autoStartGameplay = true;
            options.benchmarkWorldName = requireValue(argc, argv, index, "--benchmark-world");
        } else if (arg == "--benchmark-world-display-name") {
            options.benchmarkWorldDisplayName = requireValue(argc, argv, index, "--benchmark-world-display-name");
        } else if (arg == "--benchmark-seed") {
            options.benchmarkSeed = parseIntArg(requireValue(argc, argv, index, "--benchmark-seed"), "--benchmark-seed");
            options.benchmarkSeedSet = true;
        } else if (arg == "--benchmark-render-distance") {
            options.benchmarkRenderDistance = parseIntArg(requireValue(argc, argv, index, "--benchmark-render-distance"),
                                                          "--benchmark-render-distance");
            options.benchmarkRenderDistanceSet = true;
        } else if (arg == "--benchmark-duration") {
            options.benchmarkDurationSeconds = parseDoubleArg(requireValue(argc, argv, index, "--benchmark-duration"),
                                                              "--benchmark-duration");
        } else if (arg == "--benchmark-report") {
            options.benchmarkReportPath = requireValue(argc, argv, index, "--benchmark-report");
        } else if (arg == "--benchmark-save-root") {
            options.benchmarkSaveRoot = requireValue(argc, argv, index, "--benchmark-save-root");
        } else if (arg == "--benchmark-no-save") {
            options.benchmarkEnableSaving = false;
        } else if (arg == "--gl-debug-output") {
            options.enableGlDebugOutput = true;
            glDebugOutputSet = true;
        } else if (arg == "--no-gl-debug-output") {
            options.enableGlDebugOutput = false;
            glDebugOutputSet = true;
        } else if (arg == "--no-exit-on-replay-end") {
            options.exitWhenPlaybackEnds = false;
        } else {
            throw std::runtime_error("Unknown command line option: " + arg);
        }
    }

    if (options.recordInput && options.replayInput) {
        throw std::runtime_error("--record-input and --replay-input cannot be used together");
    }
    if (!options.benchmarkReportPath.empty() && !options.autoStartGameplay) {
        throw std::runtime_error("--benchmark-report requires --benchmark or --benchmark-world");
    }
    if (options.autoStartGameplay && !inputScopeSet) {
        options.inputReplayScope = AppLaunchOptions::InputReplayScope::Gameplay;
    }
    if (options.autoStartGameplay) {
        options.enableDebugDashboard = false;
        if (!glDebugOutputSet) {
            options.enableGlDebugOutput = false;
        }
    }
    return options;
}

} // namespace

int main(int argc, char** argv) {
    AppLaunchOptions options;
    try {
        options = parseLaunchOptions(argc, argv);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        printUsage();
        return 1;
    }

    GameManager app;
    app.init(1280, 720, "Mecraft", std::move(options));
    app.run();
    app.shutdown();
    return 0;
}
