#ifndef MECRAFT_GAME_FRAME_ORCHESTRATOR_H
#define MECRAFT_GAME_FRAME_ORCHESTRATOR_H

#include <cstdint>
#include <functional>

#ifdef MECRAFT_DEBUG
#include "../../ui/Dashboard.h"
#endif

class GameSession;
class InputManager;
class GameplayRenderRuntime;
class Window;
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
                        InputManager& input,
                        GameplayRenderRuntime* renderRuntime,
                        double fixedStep,
                        double& accumulator);

    /// Update audio listener position/orientation from player state.
    void syncAudioListener(AudioListenerSyncSystem& audioSync,
                           float deltaTime,
                           GameSession& session);

    /// Render a full frame (scene, precipitation, UI).
    void renderFrame(GameSession& session,
                     GameplayRenderRuntime& renderRuntime,
                     GameplayHudPresenter* hudPresenter,
                     Window& window,
                     float frameTime);

    /// Set a callback to be invoked after 3D scene render but before UI overlay.
    /// Used for screenshot capture.
    void setPreUiCallback(std::function<void()> callback) { m_preUiCallback = std::move(callback); }

private:
    uint32_t m_lastHeldItemSwingSequence = 0;
    std::function<void()> m_preUiCallback;
};

#endif // MECRAFT_GAME_FRAME_ORCHESTRATOR_H
