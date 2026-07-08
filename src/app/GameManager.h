#ifndef MECRAFT_GAMEMANAGER_H
#define MECRAFT_GAMEMANAGER_H

#include <memory>
#include <string>
#include <array>
#include <vector>

#include "AppLaunchOptions.h"
#include "../engine/platform/Window.h"
#include "../engine/input/InputManager.h"
#include "../game/session/GameSessionConfig.h"
#include "../player/ActionMap.h"
#include "../engine/input/InputContextManager.h"
#include "../resource/ResourceMgr.h"
#include "../audio/AudioEngine.h"
#include "../audio/BgmSystem.h"
#include "../ui/core/UIRenderer.h"
#include "states/AppStateMachine.h"
#include "states/AppStateDependencies.h"
#include "../locale/LocaleManager.h"
#include "../thread/ThreadPool.h"

class RhiDevice;

class GameManager {
public:
    GameManager();
    ~GameManager();

    [[nodiscard]] bool init(int width, int height, const char* title, AppLaunchOptions launchOptions = {});
    void run();
    void shutdown();

private:
    bool initWindow(int width, int height, const char* title);
    bool initRhiDevice();
    
    [[nodiscard]] AppStateDependencies makeAppStateDependencies();

    [[nodiscard]] static double clampFrameTime(double dt);
    [[nodiscard]] bool makeBenchmarkSessionConfig(GameSessionConfig& outConfig) const;
    [[nodiscard]] bool configureInputReplay();
    void activateInputReplayForScope(AppLaunchOptions::InputReplayScope scope);
    void recordBenchmarkFrame(double frameTime);
    void closeWindowIfBenchmarkComplete();
    void writeBenchmarkReport();

    struct BenchmarkFrameStats {
        bool active = false;
        size_t frameCount = 0;
        double replayActiveSeconds = 0.0;
        double totalFrameMs = 0.0;
        double minFrameMs = 0.0;
        double maxFrameMs = 0.0;
        std::vector<double> frameTimesMs;
    };

    Window m_window;
    InputManager m_input;
    ActionMap m_actionMap;
    InputContextManager m_contextManager;
    ResourceMgr m_resourceMgr;
    AudioEngine m_audioEngine;
    BgmSystem m_bgmSystem;
    UIRenderer m_uiRenderer;
    LocaleManager m_localeManager;
    ThreadPool m_threadPool;
    std::unique_ptr<RhiDevice> m_rhiDevice;

    AppStateMachine m_appStateMachine;
    AppLaunchOptions m_launchOptions{};
    BenchmarkFrameStats m_benchmarkStats{};
    bool m_benchmarkReplayWasActive = false;
    bool m_benchmarkReportWritten = false;
};

#endif //MECRAFT_GAMEMANAGER_H
