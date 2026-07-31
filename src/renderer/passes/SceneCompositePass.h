#ifndef MECRAFT_SCENE_COMPOSITE_PASS_H
#define MECRAFT_SCENE_COMPOSITE_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiHandles.h"
#include "../rhi/RhiRenderGraph.h"

#include <array>

class DeferredRenderTargets;
class ResourceMgr;
class RhiCommandList;
class RhiDevice;

/// Scene composite pass: combines lighting, reflections, clouds, and atmosphere into final HDR scene.
class SceneCompositePass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "SceneComposite"; }

    struct GraphResources {
        RgTextureHandle sceneLighting;
        RgTextureHandle reflection;
        RgTextureHandle cloud;
        RgTextureHandle depth;
        RgTextureHandle normalAo;
        RgTextureHandle material;
        RgTextureHandle materialAux;
        RgTextureHandle skyCapture;
        RgTextureHandle albedo;
        RgTextureHandle ssgi;
        RgTextureHandle output;
        RgTextureHandle reactiveMask;
        RgTextureHandle transparencyMask;
    };

    /// Adds the scene composite draw to the frame graph.
    /// @param graph Graph receiving the scene composite pass.
    /// @param ctx Frame state used to build scene composite parameters.
    /// @param settings Render settings controlling composite features.
    /// @param targets Persistent target set supplying attachment views.
    /// @param resources Graph resources read and written by this pass.
    /// @param dependency Pass that must complete before scene composite.
    /// @return The scene composite pass handle, or an invalid handle on setup failure.
    [[nodiscard]] RgPassHandle addGraphPasses(RenderGraph& graph, const FrameContext& ctx,
                                              const RenderSettings& settings, DeferredRenderTargets& targets,
                                              const GraphResources& resources, RgPassHandle dependency);

private:
    bool ensureRhiPipelines(RhiDevice& rhiDevice);
    bool ensureBindGroup(RhiDevice& rhiDevice, const std::array<RhiTextureViewHandle, 10>& views);
    [[nodiscard]] bool recordGraphPass(RhiCommandList& commandList, const FrameContext& ctx,
                                       const RenderSettings& settings, DeferredRenderTargets& targets);
    void destroyBindGroups();
    void destroyRhiResources();

    RhiDevice* m_rhiDevice = nullptr;
    RhiBufferHandle m_uniformBuffer;
    RhiSamplerHandle m_nearestSampler;
    RhiSamplerHandle m_linearSampler;
    RhiBindGroupLayoutHandle m_bindGroupLayout;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_fragmentShader;
    RhiPipelineHandle m_pipeline;
    RhiBindGroupHandle m_bindGroup;
    std::array<RhiTextureViewHandle, 10> m_boundViews = {};
};

#endif // MECRAFT_SCENE_COMPOSITE_PASS_H
