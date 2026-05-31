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
    /// Constructor using structured config and dependencies.
    Game(GameSessionConfig config, GameSessionDependencies deps);
    ~Game();

    void init();
    void shutdown();

    void fixedUpdate(double fixedStep, double& accumulator);
    void updateFrame(float deltaTime);
    void renderFrame(float frameTime);

    [[nodiscard]] bool isQuitToMenuRequested() const;
    void clearQuitToMenuRequest();

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
};

#endif //MECRAFT_GAME_H
