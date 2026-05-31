//
// Created by Caiwe on 2026/3/25.
//

#ifndef MECRAFT_DASHBOARD_H
#define MECRAFT_DASHBOARD_H

// Dashboard 调试 UI 仅在 Debug 模式下可用
#ifdef MECRAFT_DEBUG

#include <array>
#include <cstddef>
#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/imgui_impl_glfw.h"
#include "../third_party/imgui/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include "../engine/camera/Camera.h"
#include "../engine/platform/Time.h"
#include "../engine/platform/Window.h"
#include "../ecs/GameplayRegistry.h"
#include "../ecs/util/PlayerQuery.h"
#include "../world/World.h"
#include "../renderer/core/RenderResourceHub.h"
#include "../renderer/core/RenderScene.h"
#include "../renderer/renderers/PostProcessRenderer.h"
class FirstPersonHeldItemRenderer;
class PostProcessRenderer;
class UIRenderer;
class Dashboard {
public:
    struct FrameProfilerStats {
        static constexpr size_t kFixedHistorySamples = 120;

        double frameMs = 0.0;
        double fixedUpdateMs = 0.0;
        double fixedInputMs = 0.0;
        double fixedStateUpdateMs = 0.0;
        double fixedParticleUpdateMs = 0.0;
        double fixedDropUpdateMs = 0.0;
        double fixedWorldUpdateMs = 0.0;
        double audioMs = 0.0;
        double renderMs = 0.0;

        size_t fixedHistoryCount = 0;
        std::array<float, kFixedHistorySamples> fixedUpdateHistory{};
        std::array<float, kFixedHistorySamples> fixedInputHistory{};
        std::array<float, kFixedHistorySamples> fixedStateHistory{};
        std::array<float, kFixedHistorySamples> fixedParticleHistory{};
        std::array<float, kFixedHistorySamples> fixedDropHistory{};
        std::array<float, kFixedHistorySamples> fixedWorldHistory{};
    };

    Dashboard();
    ~Dashboard();
    void init(const Window& window);
    void shutdown();
    void setFirstPersonHeldItemRenderer(FirstPersonHeldItemRenderer* renderer);
    void render(ecs::GameplayRegistry& registry,
                World &world,
                Camera &camera,
                RenderResourceHub &render,
                RenderScene& renderScene,
                PostProcessRenderer& postProcess,
                UIRenderer& uiRenderer,
                const FrameProfilerStats& profilerStats);
private:
    void showPlayerStats(ecs::GameplayRegistry& registry);
    void showWorldStats(World& world, ecs::GameplayRegistry& registry);
    void showCameraStats( Camera& camera);
    void showPerformanceStats(World& world, RenderResourceHub &render, RenderScene& renderScene, PostProcessRenderer& postProcess, const FrameProfilerStats& profilerStats);
    void showCrosshairSettings(UIRenderer& uiRenderer);
    void showHotbarSettings(UIRenderer& uiRenderer);
    void showInventoryPanelSettings(UIRenderer& uiRenderer);
    void showCraftingGridSettings(UIRenderer& uiRenderer);
    void showHeldItemPreviewSettings(FirstPersonHeldItemRenderer& firstPersonHeldItemRenderer);
    void showTextSettings(UIRenderer& uiRenderer);

    FirstPersonHeldItemRenderer* m_firstPersonHeldItemRenderer = nullptr;
    float m_fontScale = 1.5f; // Global ImGui font scale
    bool m_initialized = false;
};

#endif // NDEBUG


#endif //MECRAFT_DASHBOARD_H
