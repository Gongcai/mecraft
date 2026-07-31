#ifndef MECRAFT_IMGUI_RHI_RENDERER_H
#define MECRAFT_IMGUI_RHI_RENDERER_H

#include <cstdint>
#include <string>
#include <vector>

#include "imgui.h"

#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiResources.h"
#include "renderer/rhi/RhiTypes.h"

class RhiCommandList;
class RhiDevice;
class Window;

/// Owns one Dear ImGui context and renders its draw data through the project RHI.
class ImGuiRhiRenderer {
public:
    ImGuiRhiRenderer() = default;
    ~ImGuiRhiRenderer();

    ImGuiRhiRenderer(const ImGuiRhiRenderer&) = delete;
    ImGuiRhiRenderer& operator=(const ImGuiRhiRenderer&) = delete;

    /// Creates an independent ImGui context and all renderer resources.
    /// @param window Native window used by the GLFW platform backend.
    /// @param rhiDevice Device used to allocate the renderer resources.
    /// @param dockingEnabled Enables docking for this context only.
    /// @param iniFile Persistent layout file owned by this renderer instance.
    /// @return True when the context and every GPU resource are ready.
    [[nodiscard]] bool init(const Window& window, RhiDevice& rhiDevice, bool dockingEnabled, std::string iniFile);

    /// Releases the ImGui context and all renderer-owned GPU resources.
    void shutdown();

    /// Starts one ImGui frame using the current framebuffer dimensions.
    [[nodiscard]] bool beginFrame(int framebufferWidth, int framebufferHeight);

    /// Finalizes ImGui draw data and uploads its transient geometry.
    /// @param commandList Recording command list used for buffer uploads.
    /// @return True when draw data is valid and ready for recordDraws().
    [[nodiscard]] bool prepareDrawData(RhiCommandList& commandList);

    /// Records prepared ImGui draws into an active swapchain rendering scope.
    void recordDraws(RhiCommandList& commandList) const;

    /// Registers a sampled texture for use as an ImGui image.
    /// @return Stable ImGui texture ID, or ImTextureID_Invalid on failure.
    [[nodiscard]] ImTextureID registerTexture(RhiTextureViewHandle textureView, RhiSamplerHandle sampler);

    /// Removes a previously registered sampled texture and its bind group.
    void unregisterTexture(ImTextureID textureId);

private:
    struct TextureBinding {
        ImTextureID id = ImTextureID_Invalid;
        RhiBindGroupHandle bindGroup;
        bool font = false;
    };

    struct PreparedDraw {
        RhiRect2D scissor;
        RhiBindGroupHandle bindGroup;
        uint32_t indexCount = 0u;
        uint32_t firstIndex = 0u;
        int32_t vertexOffset = 0;
        bool resetState = false;
    };

    [[nodiscard]] bool createRhiResources();
    void destroyRhiResources();
    [[nodiscard]] bool buildPreparedDraws(const ImDrawData& drawData);
    [[nodiscard]] bool uploadDrawBuffers(RhiCommandList& commandList);
    [[nodiscard]] RhiBindGroupHandle findBindGroup(ImTextureID textureId) const;
    void bindRenderState(RhiCommandList& commandList) const;

    RhiDevice* m_rhiDevice = nullptr;
    ImGuiContext* m_context = nullptr;
    std::string m_iniFile;
    RhiTextureHandle m_fontTexture;
    RhiTextureViewHandle m_fontTextureView;
    RhiSamplerHandle m_fontSampler;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_fragmentShader;
    RhiBindGroupLayoutHandle m_bindGroupLayout;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiPipelineHandle m_pipeline;
    RhiBufferHandle m_vertexBuffer;
    RhiBufferHandle m_indexBuffer;
    RhiResourceState m_vertexBufferState = RhiResourceState::Undefined;
    RhiResourceState m_indexBufferState = RhiResourceState::Undefined;
    uint64_t m_vertexBufferCapacity = 0u;
    uint64_t m_indexBufferCapacity = 0u;
    uint64_t m_nextTextureId = 1u;
    std::vector<TextureBinding> m_textureBindings;
    std::vector<ImDrawVert> m_vertices;
    std::vector<uint8_t> m_indexBytes;
    std::vector<PreparedDraw> m_preparedDraws;
    ImVec2 m_displayPos{};
    ImVec2 m_displaySize{};
    int32_t m_framebufferWidth = 0;
    int32_t m_framebufferHeight = 0;
    bool m_platformInitialized = false;
    bool m_frameStarted = false;
    bool m_framePrepared = false;
};

#endif // MECRAFT_IMGUI_RHI_RENDERER_H
