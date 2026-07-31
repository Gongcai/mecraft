#ifndef MECRAFT_GAMEPLAY_HUD_PRESENTER_H
#define MECRAFT_GAMEPLAY_HUD_PRESENTER_H

#include "GameplayPresentationSnapshot.h"
#include "../../ui/core/UIRenderContext.h"
#include <functional>
#ifdef MECRAFT_DEBUG
#include "../../ui/Dashboard.h"
#endif

class Window;
class UIRenderer;
class GameStateMachine;
class InputManager;
struct InputSnapshot;
class RhiDevice;
class RhiCommandList;

namespace ecs {
class GameplayRegistry;
}
class World;
class RenderResourceHub;
class RenderScene;
class PostProcessPass;

/// Renders gameplay HUD, state machine overlays, and debug dashboard.
/// Extracted from Game::renderUI() to decouple UI presentation from Game.
class GameplayHudPresenter {
public:
    GameplayHudPresenter(Window& window, UIRenderer& uiRenderer, InputManager& input)
        : m_window(window), m_uiRenderer(uiRenderer), m_input(input) {}

    /// Render gameplay UI overlay.
    void render(const GameplayPresentationSnapshot& snap, RhiDevice& rhiDevice, GameStateMachine& stateMachine);
    UIRenderContext prepareRenderContext(const GameplayPresentationSnapshot& snap, RhiDevice& rhiDevice,
                                         int surfaceWidth, int surfaceHeight);
    bool prepareTextFrame(RhiCommandList& commandList);
    void renderPrepared(const UIRenderContext& context, GameStateMachine& stateMachine);

#ifdef MECRAFT_DEBUG
    /// Set the Dashboard instance (owned by Game, injected here for rendering).
    void setDashboard(Dashboard* dashboard) { m_dashboard = dashboard; }
    [[nodiscard]] bool hasDashboard() const { return m_dashboard != nullptr; }

    /// Build ImGui draw data and upload its GPU resources before rendering begins.
    [[nodiscard]] bool prepareDashboard(RhiCommandList& commandList, int framebufferWidth, int framebufferHeight,
                                        ecs::GameplayRegistry& reg, World& world, const Camera& camera,
                                        RenderResourceHub& renderer, RenderScene& renderScene,
                                        PostProcessPass& postProcess, Dashboard::FrameProfilerStats& profilerStats,
                                        const std::function<void(int)>& renderDistanceSetter);

    /// Record the prepared ImGui draws inside the active UI rendering scope.
    void recordDashboard(RhiCommandList& commandList) const;
#endif

private:
    Window& m_window;
    UIRenderer& m_uiRenderer;
    InputManager& m_input;
    PlayerStatsData m_playerStats;
#ifdef MECRAFT_DEBUG
    Dashboard* m_dashboard = nullptr; // Non-owning, injected by Game via setDashboard()
#endif
};

#endif // MECRAFT_GAMEPLAY_HUD_PRESENTER_H
