#ifndef MECRAFT_GAME_FRAME_ORCHESTRATOR_H
#define MECRAFT_GAME_FRAME_ORCHESTRATOR_H

class GameSession;
class GameStateMachine;
class RenderResourceHub;
class RenderScene;
class Window;
class InputManager;
class UIRenderer;
class PostProcessRenderer;
class AudioListenerSyncSystem;
class GameplayHudPresenter;

struct HeldItemPreviewMotion;
struct BlockTargetRenderData;
struct BlockBreakRenderData;

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

    /// Render a full frame (scene, precipitation, held item, UI).
    void renderFrame(GameSession& session,
                     RenderResourceHub& renderer,
                     RenderScene& renderScene,
                     GameStateMachine& stateMachine,
                     PostProcessRenderer& postProcess,
                     GameplayHudPresenter* hudPresenter,
                     InputManager& input,
                     UIRenderer& uiRenderer,
                     Window& window,
                     float frameTime);
};

#endif // MECRAFT_GAME_FRAME_ORCHESTRATOR_H
