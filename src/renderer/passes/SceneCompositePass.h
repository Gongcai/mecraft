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
class VoxelGiClipmap;

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
        RgTextureHandle voxelGi;
        RgTextureHandle output;
        RgTextureHandle reactiveMask;
        RgTextureHandle transparencyMask;
    };

    /// Adds the scene composite draw to the frame graph.
    /// @param graph Graph receiving the scene composite pass.
    /// @param ctx Frame state used to build scene composite parameters.
    /// @param settings Render settings controlling composite features.
    /// @param targets Persistent target set supplying attachment views.
    /// @param voxelGiClipmap Optional clipmap sampled by the voxel GI variant.
    /// @param resources Graph resources read and written by this pass.
    /// @param dependency Pass that must complete before scene composite.
    /// @return The scene composite pass handle, or an invalid handle on setup failure.
    [[nodiscard]] RgPassHandle addGraphPasses(
        RenderGraph& graph,
        const FrameContext& ctx,
        const RenderSettings& settings,
        DeferredRenderTargets& targets,
        const VoxelGiClipmap* voxelGiClipmap,
        const GraphResources& resources,
        RgPassHandle dependency);

private:
    bool ensureRhiPipelines(RhiDevice& rhiDevice);
    bool ensureBindGroup(RhiDevice& rhiDevice, bool voxelGiEnabled,
                         const std::array<RhiTextureViewHandle, 11>& views);
    bool ensureVoxelGiTextureView(RhiDevice& rhiDevice, const VoxelGiClipmap& voxelGiClipmap);
    [[nodiscard]] bool recordGraphPass(RhiCommandList& commandList,
                                       const FrameContext& ctx,
                                       const RenderSettings& settings,
                                       DeferredRenderTargets& targets,
                                       const VoxelGiClipmap* voxelGiClipmap);
    void destroyBindGroups();
    void destroyVoxelBindGroup();
    void destroyRhiResources();
    void destroyVoxelGiTextureView();

    RhiDevice* m_rhiDevice = nullptr;
    RhiBufferHandle m_uniformBuffer;
    RhiSamplerHandle m_nearestSampler;
    RhiSamplerHandle m_linearSampler;
    RhiSamplerHandle m_voxelSampler;
    RhiBindGroupLayoutHandle m_baseBindGroupLayout;
    RhiBindGroupLayoutHandle m_voxelBindGroupLayout;
    RhiPipelineLayoutHandle m_basePipelineLayout;
    RhiPipelineLayoutHandle m_voxelPipelineLayout;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_baseFragmentShader;
    RhiShaderHandle m_voxelFragmentShader;
    RhiPipelineHandle m_basePipeline;
    RhiPipelineHandle m_voxelPipeline;
    RhiBindGroupHandle m_baseBindGroup;
    RhiBindGroupHandle m_voxelBindGroup;
    std::array<RhiTextureViewHandle, 11> m_baseBoundViews = {};
    std::array<RhiTextureViewHandle, 11> m_voxelBoundViews = {};

    RhiTextureViewHandle m_voxelGiTextureView;
    RhiTextureHandle m_voxelGiViewTexture;
    RhiDevice* m_voxelGiViewDevice = nullptr;
};

#endif // MECRAFT_SCENE_COMPOSITE_PASS_H
