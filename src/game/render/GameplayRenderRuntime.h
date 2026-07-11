#ifndef MECRAFT_GAMEPLAY_RENDER_RUNTIME_H
#define MECRAFT_GAMEPLAY_RENDER_RUNTIME_H

#include <memory>

class ResourceMgr;
class GameSession;
class UIRenderer;
class RenderResourceHub;
class RenderScene;
class FirstPersonHeldItemRenderer;
class RhiDevice;
class ThreadPool;
class Window;

#ifdef MECRAFT_DEBUG
#include "../../ui/Dashboard.h"
class DebugFrameProfiler;
#endif

/// Owns all render-time objects for a gameplay session.
/// Extracted from Game's internal RenderRuntime struct and initRenderers().
///
/// Initialization order: must be called after GameSession::init() and
/// GameSession::initWorld(), but before GameSession::initECS().
/// Lifetime: one gameplay session (from world load to return to menu).
class GameplayRenderRuntime {
public:
    GameplayRenderRuntime();
    ~GameplayRenderRuntime();

    // Non-copyable, non-movable (owns GPU resources)
    GameplayRenderRuntime(const GameplayRenderRuntime&) = delete;
    GameplayRenderRuntime& operator=(const GameplayRenderRuntime&) = delete;

    /// Initialize all renderers and connect to session systems.
    [[nodiscard]] bool init(ResourceMgr& resourceMgr,
                            GameSession& session,
                            UIRenderer& uiRenderer,
                            ThreadPool& threadPool,
                            RhiDevice& rhiDevice);

    /// Shutdown all renderers in reverse order of initialization.
    void shutdown();

    // Accessors (valid after init())
    [[nodiscard]] RenderResourceHub& resourceHub();
    [[nodiscard]] RenderScene& renderScene();
    [[nodiscard]] FirstPersonHeldItemRenderer& firstPersonHeldItemRenderer();

#ifdef MECRAFT_DEBUG
    void initDebug(Window& window);
    void publishDebugStats(double frameTime);
    [[nodiscard]] Dashboard* dashboard();
    [[nodiscard]] DebugFrameProfiler* profiler();
    [[nodiscard]] Dashboard::FrameProfilerStats* dashboardProfilerStats();
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif // MECRAFT_GAMEPLAY_RENDER_RUNTIME_H
