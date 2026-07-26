#ifndef MECRAFT_HIZ_PASS_H
#define MECRAFT_HIZ_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../rhi/RhiHandles.h"
#include "../rhi/RhiRenderGraph.h"

#include <vector>

class DeferredRenderTargets;
class RhiCommandList;
class RhiDevice;

/// Builds the Hi-Z occlusion pyramid: an R32Float mip chain holding the
/// FARTHEST depth of each footprint, reduced from the previous frame's depth
/// buffer. GPU occlusion culling samples it to conservatively reject
/// indirect draws whose bounds sit behind everything in their screen rect.
class HiZPass : public RenderPass {
public:
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "HiZ"; }

    struct GraphResources {
        RgTextureHandle historyDepthPrevious;
        RgTextureHandle hiZ;
    };

    /// Adds one compute reduction pass per pyramid mip. Each pass declares
    /// its exact mip subresources so the graph plans the inter-mip barriers.
    [[nodiscard]] RgPassHandle addGraphPasses(RenderGraph& graph,
                                              const FrameContext& ctx,
                                              DeferredRenderTargets& targets,
                                              const GraphResources& resources,
                                              RgPassHandle dependency);

private:
    bool ensurePipeline(RhiDevice& rhiDevice);
    bool ensureMipBindGroup(RhiDevice& rhiDevice,
                            uint32_t mip,
                            RhiTextureViewHandle sourceView,
                            RhiTextureViewHandle destView);
    [[nodiscard]] bool recordMip(RhiCommandList& commandList,
                                 const FrameContext& ctx,
                                 DeferredRenderTargets& targets,
                                 uint32_t mip);
    void destroyRhiResources();

    RhiDevice* m_rhiDevice = nullptr;
    RhiShaderHandle m_shader;
    RhiSamplerHandle m_sampler;
    RhiBindGroupLayoutHandle m_bindGroupLayout;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiPipelineHandle m_pipeline;
    struct MipBinding {
        RhiBindGroupHandle bindGroup;
        RhiTextureViewHandle boundSource;
        RhiTextureViewHandle boundDest;
    };
    std::vector<MipBinding> m_mipBindings;
};

#endif // MECRAFT_HIZ_PASS_H
