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

/// Legacy init params (kept for backward compatibility with GameplayAppState).
struct GameInitParams {
    Window* window = nullptr;
    InputManager* input = nullptr;
    ActionMap* actionMap = nullptr;
    InputContextManager* contextManager = nullptr;
    ResourceMgr* resourceMgr = nullptr;
    AudioEngine* audioEngine = nullptr;
    BgmSystem* bgmSystem = nullptr;
    UIRenderer* uiRenderer = nullptr;
    LocaleManager* localeManager = nullptr;
    int seed = 1234;
};

class Game {

public:
    /// Legacy constructor (kept for backward compatibility with GameplayAppState).
    explicit Game(const GameInitParams& params);

    /// New constructor using structured config and dependencies.
    Game(GameSessionConfig config, GameSessionDependencies deps);
    ~Game();

    void init();
    void shutdown();

    void runFixedUpdate(double fixedStep, double& accumulator);
    void syncAudioListener(float deltaTime);
    void renderFrame(float frameTime);

    [[nodiscard]] bool isQuitToMenuRequested() const;
    void clearQuitToMenuRequest();

private:
    static constexpr double TICK_RATE = 1.0 / 60.0;

    // G1: Structured config and dependencies (replaces individual member references)
    GameSessionConfig m_config;
    GameSessionDependencies m_deps;

    // External service references (from GameSessionDependencies)
    GameInitParams m_params;
    Window& m_window;
    InputManager& m_input;
    ActionMap& m_actionMap;
    InputContextManager& m_contextManager;
    ResourceMgr& m_resourceMgr;
    AudioEngine& m_audioEngine;
    BgmSystem& m_bgmSystem;
    UIRenderer& m_uiRenderer;
    LocaleManager& m_localeManager;

    // G4: Game session (owns World, ECS, physics, crafting, particles, camera, presentation)
    GameSession m_session;

    // Render runtime (owns RenderResourceHub, RenderScene, entity renderers, post-process)
    std::unique_ptr<GameplayRenderRuntime> m_renderRuntime;

    // G3: Audio and HUD systems
    std::unique_ptr<GameplayHudPresenter> m_hudPresenter;
    std::unique_ptr<AudioListenerSyncSystem> m_audioSyncSystem;

    // G5: Frame orchestrator
    std::unique_ptr<GameFrameOrchestrator> m_frameOrchestrator;

    void initWorld();

    bool m_initialized = false;
};


#endif //MECRAFT_GAME_H
