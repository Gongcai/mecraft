//
// Created by Caiwe on 2026/3/25.
//

#ifndef MECRAFT_DASHBOARD_H
#define MECRAFT_DASHBOARD_H

#ifdef MECRAFT_DEBUG

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>
#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/imgui_impl_glfw.h"
#include <GLFW/glfw3.h>
#include "../engine/camera/Camera.h"
#include "../engine/platform/Time.h"
#include "../engine/platform/Window.h"
#include "../ecs/GameplayRegistry.h"
#include "../ecs/util/PlayerQuery.h"
#include "../world/World.h"
#include "../renderer/core/RenderResourceHub.h"
#include "../renderer/core/RenderScene.h"
#include "../renderer/passes/PostProcessPass.h"
#include "../renderer/rhi/RhiHandles.h"
#include "../renderer/rhi/RhiResources.h"
#include "../renderer/rhi/RhiTypes.h"
#include "../renderer/presentation/PresentationController.h"
class FirstPersonHeldItemRenderer;
class PostProcessPass;
class RhiCommandList;
class RhiDevice;
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
        double pollEventsMs = 0.0;
        double appUpdateDispatchMs = 0.0;
        double appRenderDispatchMs = 0.0;
        double renderSnapshotMs = 0.0;
        double renderSceneMs = 0.0;
        double renderUiMs = 0.0;
        double renderDashboardMs = 0.0;
        double swapBuffersMs = 0.0;
        double renderOtherMs = 0.0;
        double untrackedMs = 0.0;
        double pollInputCallbackMs = 0.0;
        double pollCursorPosCallbackMs = 0.0;
        double pollImguiCallbackMs = 0.0;
        double pollImguiCursorPosCallbackMs = 0.0;
        double pollImguiCursorPosBackendMs = 0.0;
        double pollImguiWndProcMs = 0.0;
        double pollImguiWndProcSlowestMs = 0.0;
        unsigned pollImguiWndProcSlowestMsg = 0;
        unsigned pollImguiWndProcCount = 0;
        unsigned pollEventCount = 0;
        unsigned pollKeyEventCount = 0;
        unsigned pollMouseButtonEventCount = 0;
        unsigned pollCursorPosEventCount = 0;
        unsigned pollScrollEventCount = 0;
        unsigned pollCharEventCount = 0;
        PresentationMode presentationMode = PresentationMode::Native;
        uint64_t realFramesAcquired = 0u;
        uint64_t realFramesPresented = 0u;
        uint64_t generatedFramesPresented = 0u;
        uint64_t displayedFrames = 0u;
        uint64_t presentationSkippedFrames = 0u;
        uint64_t presentationFailedOperations = 0u;

        // Max-frame-time snapshot: records all timings from the worst frame
        double maxFrameMs = 0.0;
        double maxFixedUpdateMs = 0.0;
        double maxFixedInputMs = 0.0;
        double maxFixedStateUpdateMs = 0.0;
        double maxFixedParticleUpdateMs = 0.0;
        double maxFixedDropUpdateMs = 0.0;
        double maxFixedWorldUpdateMs = 0.0;
        double maxAudioMs = 0.0;
        double maxRenderMs = 0.0;
        double maxPollEventsMs = 0.0;
        double maxAppUpdateDispatchMs = 0.0;
        double maxAppRenderDispatchMs = 0.0;
        double maxRenderSnapshotMs = 0.0;
        double maxRenderSceneMs = 0.0;
        double maxRenderUiMs = 0.0;
        double maxRenderDashboardMs = 0.0;
        double maxSwapBuffersMs = 0.0;
        double maxRenderOtherMs = 0.0;
        double maxUntrackedMs = 0.0;
        double maxPollInputCallbackMs = 0.0;
        double maxPollCursorPosCallbackMs = 0.0;
        double maxPollImguiCallbackMs = 0.0;
        double maxPollImguiCursorPosCallbackMs = 0.0;
        double maxPollImguiCursorPosBackendMs = 0.0;
        double maxPollImguiWndProcMs = 0.0;
        double maxPollImguiWndProcSlowestMs = 0.0;
        unsigned maxPollImguiWndProcSlowestMsg = 0;
        unsigned maxPollImguiWndProcCount = 0;
        unsigned maxPollEventCount = 0;
        unsigned maxPollKeyEventCount = 0;
        unsigned maxPollMouseButtonEventCount = 0;
        unsigned maxPollCursorPosEventCount = 0;
        unsigned maxPollScrollEventCount = 0;
        unsigned maxPollCharEventCount = 0;

        size_t frameHistoryCount = 0;
        size_t fixedHistoryCount = 0;
        std::array<float, kFixedHistorySamples> fpsHistory{};
        std::array<float, kFixedHistorySamples> renderHistory{};
        std::array<float, kFixedHistorySamples> fixedUpdateHistory{};
        std::array<float, kFixedHistorySamples> fixedInputHistory{};
        std::array<float, kFixedHistorySamples> fixedStateHistory{};
        std::array<float, kFixedHistorySamples> fixedParticleHistory{};
        std::array<float, kFixedHistorySamples> fixedDropHistory{};
        std::array<float, kFixedHistorySamples> fixedWorldHistory{};
    };

    Dashboard();
    ~Dashboard();
    [[nodiscard]] bool init(const Window& window, RhiDevice& rhiDevice);
    void shutdown();
    void setFirstPersonHeldItemRenderer(FirstPersonHeldItemRenderer* renderer);
    [[nodiscard]] bool prepareFrame(
        RhiCommandList& commandList,
        int framebufferWidth,
        int framebufferHeight,
        ecs::GameplayRegistry& registry,
        World& world,
        Camera& camera,
        RenderResourceHub& render,
        RenderScene& renderScene,
        PostProcessPass& postProcess,
        UIRenderer& uiRenderer,
        FrameProfilerStats& profilerStats,
        const std::function<void(int)>& renderDistanceSetter = {});
    void recordDraws(RhiCommandList& commandList) const;
private:
    struct PreparedDraw {
        RhiRect2D scissor;
        uint32_t indexCount = 0u;
        uint32_t firstIndex = 0u;
        int32_t vertexOffset = 0;
        bool resetState = false;
    };

    struct CachedWorldMetrics {
        size_t activeChunks = 0;
        size_t activeSubChunks = 0;
        size_t totalVertices = 0;
        size_t chunkStorageBytes = 0;
        double nextRefreshTime = 0.0;
    };

    void showPlayerStats(ecs::GameplayRegistry& registry);
    void showWorldStats(World& world,
                        ecs::GameplayRegistry& registry,
                        const std::function<void(int)>& renderDistanceSetter);
    void showCameraStats( Camera& camera);
    void showPerformanceStats(World& world, RenderResourceHub &render, RenderScene& renderScene, PostProcessPass& postProcess, FrameProfilerStats& profilerStats);
    void showGUIScaleSettings(UIRenderer& uiRenderer);
    void showCrosshairSettings(UIRenderer& uiRenderer);
    void showHotbarSettings(UIRenderer& uiRenderer);
    void showInventoryPanelSettings(UIRenderer& uiRenderer);
    void showCraftingGridSettings(UIRenderer& uiRenderer);
    void showHeldItemPreviewSettings(FirstPersonHeldItemRenderer& firstPersonHeldItemRenderer);
    void showTextSettings(UIRenderer& uiRenderer);
    void refreshWorldMetricsIfNeeded(World& world, double now, bool forceRefresh);
    [[nodiscard]] bool createRhiResources(RhiDevice& rhiDevice);
    void destroyRhiResources();
    [[nodiscard]] bool buildPreparedDraws(const ImDrawData& drawData,
                                          int framebufferWidth,
                                          int framebufferHeight);
    [[nodiscard]] bool uploadDrawBuffers(RhiCommandList& commandList);
    void bindRenderState(RhiCommandList& commandList) const;

    FirstPersonHeldItemRenderer* m_firstPersonHeldItemRenderer = nullptr;
    RhiDevice* m_rhiDevice = nullptr;
    RhiTextureHandle m_fontTexture;
    RhiTextureViewHandle m_fontTextureView;
    RhiSamplerHandle m_fontSampler;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_fragmentShader;
    RhiBindGroupLayoutHandle m_bindGroupLayout;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiPipelineHandle m_pipeline;
    RhiBindGroupHandle m_fontBindGroup;
    RhiBufferHandle m_vertexBuffer;
    RhiBufferHandle m_indexBuffer;
    RhiResourceState m_fontTextureState = RhiResourceState::Undefined;
    RhiResourceState m_vertexBufferState = RhiResourceState::VertexBuffer;
    RhiResourceState m_indexBufferState = RhiResourceState::IndexBuffer;
    uint64_t m_vertexBufferCapacity = 0u;
    uint64_t m_indexBufferCapacity = 0u;
    std::vector<ImDrawVert> m_vertices;
    std::vector<uint8_t> m_indexBytes;
    std::vector<PreparedDraw> m_preparedDraws;
    ImVec2 m_displayPos{};
    ImVec2 m_displaySize{};
    ImVec2 m_framebufferScale{1.0f, 1.0f};
    int32_t m_framebufferWidth = 0;
    int32_t m_framebufferHeight = 0;
    FrameProfilerStats m_displayProfilerStats{};
    GpuFrameStats m_displayGpuStats{};
    ShadowFrameStats m_displayShadowStats{};
    RenderWorkStats m_displayRenderWorkStats{};
    LightFrameStats m_displayLightStats{};
    CachedWorldMetrics m_cachedWorldMetrics{};
    double m_displayFps = 0.0;
    double m_nextProfilerStatsRefreshTime = 0.0;
    float m_profilerStatsRefreshIntervalSec = 0.5f;
    float m_fontScale = 1.5f;
    bool m_contextCreated = false;
    bool m_platformInitialized = false;
    bool m_framePrepared = false;
    bool m_initialized = false;
};

#endif // NDEBUG


#endif //MECRAFT_DASHBOARD_H
