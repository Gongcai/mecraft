#ifndef MECRAFT_GAME_FRAME_ORCHESTRATOR_H
#define MECRAFT_GAME_FRAME_ORCHESTRATOR_H

#include <cstdint>

#ifdef MECRAFT_DEBUG
#include "../../ui/Dashboard.h"
#endif

class GameSession;
class GameStateMachine;
class RenderResourceHub;
class RenderScene;
class Window;
class PostProcessRenderer;
class FirstPersonHeldItemRenderer;
class AudioListenerSyncSystem;
class GameplayHudPresenter;

namespace ecs { class GameplayRegistry; }

/// Orchestrates the per-frame update/render sequence for a gameplay session.
/// Extracted from Game to reduce its responsibilities.
/// Owns no state; delegates to session, renderers, and presenters.
class GameFrameOrchestrator {
public:
    /// Run a fixed timestep update (physics, ECS, world, state machine).
    /// @return true if quit-to-menu was requested
    bool runFixedUpdate(GameSession& session,
                        GameStateMachine& stateMachine,
                        double fixedStep,
                        double& accumulator);

    /// Update audio listener position/orientation from player state.
    void syncAudioListener(AudioListenerSyncSystem& audioSync,
                           float deltaTime,
                           ecs::GameplayRegistry& reg);

    /// Render a full frame (scene, precipitation, UI).
    void renderFrame(GameSession& session,
                     RenderResourceHub& renderer,
                     RenderScene& renderScene,
                     FirstPersonHeldItemRenderer& firstPersonHeldItemRenderer,
                     GameStateMachine& stateMachine,
                     PostProcessRenderer& postProcess,
                     GameplayHudPresenter* hudPresenter,
                     Window& window,
                     float frameTime);

#ifdef MECRAFT_DEBUG
    /// Set debug profiler stats for dashboard rendering.
    void setDebugProfilerStats(const Dashboard::FrameProfilerStats* stats) { m_debugProfilerStats = stats; }
#endif

private:
    uint32_t m_lastHeldItemSwingSequence = 0;
#ifdef MECRAFT_DEBUG
    const Dashboard::FrameProfilerStats* m_debugProfilerStats = nullptr;
#endif
};

#endif // MECRAFT_GAME_FRAME_ORCHESTRATOR_H
