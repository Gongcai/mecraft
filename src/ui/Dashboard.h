//
// Created by Caiwe on 2026/3/25.
//

#ifndef MECRAFT_DASHBOARD_H
#define MECRAFT_DASHBOARD_H

// Dashboard 调试 UI 仅在 Debug 模式下可用
#ifndef NDEBUG

#include <array>
#include <cstddef>
#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/imgui_impl_glfw.h"
#include "../third_party/imgui/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include "../core/Camera.h"
#include "../core/Time.h"
#include "../core/Window.h"
#include "../player/Player.h"
#include "../world/World.h"
#include "../renderer/Renderer.h"
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
    void render(Player &player, World &world, Camera &camera, Renderer &render,
                UIRenderer& uiRenderer, const FrameProfilerStats& profilerStats);
private:
    void showPlayerStats( Player& player);
    void showWorldStats(World& world, const Player& player);
    void showCameraStats( Camera& camera);
    void showPerformanceStats(Renderer &render, const FrameProfilerStats& profilerStats);
    void showCrosshairSettings(UIRenderer& uiRenderer);
    void showHotbarSettings(UIRenderer& uiRenderer);
};

#endif // NDEBUG


#endif //MECRAFT_DASHBOARD_H