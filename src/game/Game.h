//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_GAME_H
#define MECRAFT_GAME_H
#include <memory>
#include <string>

#include "session/GameSessionConfig.h"
#include "session/GameSession.h"

class GameplayHudPresenter;
class AudioListenerSyncSystem;
class GameFrameOrchestrator;
class GameplayRenderRuntime;

class Game {
public:
    enum class LoadPhase {
        NotStarted,
        Session,
        RenderRuntime,
        Ecs,
        InitialChunks,
        Complete,
        Failed
    };

    struct LoadProgress {
        LoadPhase phase = LoadPhase::NotStarted;
        float progress = 0.0f;
        std::string label;
        int loadedChunks = 0;
        int targetChunks = 0;
        int inFlightChunks = 0;
        bool complete = false;
    };

    /// Constructor using structured config and dependencies.
    Game(GameSessionConfig config, GameSessionDependencies deps);
    ~Game();

    void init();
    void beginLoading();
    void updateLoading(float deltaTime);
    void shutdown();

    void fixedUpdate(double fixedStep, double& accumulator);
    void updateFrame(float deltaTime);
    void setFixedInterpolationAlpha(float alpha);
    void renderFrame(float frameTime);
#ifdef MECRAFT_DEBUG
    void publishDebugStats(float frameTime);
    void recordPollEvents(double ms,
                          unsigned keyEvents,
                          unsigned mouseButtonEvents,
                          unsigned cursorPosEvents,
                          unsigned scrollEvents,
                          unsigned charEvents,
                          double inputCallbackMs,
                          double cursorPosCallbackMs,
                          double imguiCallbackMs,
                          double imguiCursorPosCallbackMs,
                          double imguiCursorPosBackendMs,
                          double imguiWndProcMs,
                          double imguiWndProcSlowestMs,
                          unsigned imguiWndProcSlowestMsg,
                          unsigned imguiWndProcCount);
    void recordAppUpdateDispatch(double ms);
    void recordAppRenderDispatch(double ms);
#endif

    [[nodiscard]] bool isQuitToMenuRequested() const;
    [[nodiscard]] bool isInitialized() const { return m_initialized; }
    [[nodiscard]] bool isLoadingComplete() const { return m_loadPhase == LoadPhase::Complete; }
    [[nodiscard]] LoadProgress getLoadProgress() const;
    void clearQuitToMenuRequest();

    /// Capture a screenshot of the current framebuffer for save thumbnail.
    void captureExitScreenshot();

private:
    // G1: Structured config and dependencies
    GameSessionConfig m_config;
    GameSessionDependencies m_deps;

    // G4: Game session (owns World, ECS, physics, crafting, particles, camera, presentation)
    GameSession m_session;

    // Render runtime (owns RenderResourceHub, RenderScene, entity renderers, post-process)
    std::unique_ptr<GameplayRenderRuntime> m_renderRuntime;

    // G3: Audio and HUD systems
    std::unique_ptr<GameplayHudPresenter> m_hudPresenter;
    std::unique_ptr<AudioListenerSyncSystem> m_audioSyncSystem;

    // G5: Frame orchestrator
    std::unique_ptr<GameFrameOrchestrator> m_frameOrchestrator;

    bool m_initialized = false;
    bool m_captureScreenshotOnNextFrame = false;
    float m_fixedInterpolationAlpha = 0.0f;
    LoadPhase m_loadPhase = LoadPhase::NotStarted;
};

#endif //MECRAFT_GAME_H
