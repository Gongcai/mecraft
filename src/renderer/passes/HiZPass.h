#ifndef MECRAFT_HIZ_PASS_H
#define MECRAFT_HIZ_PASS_H

#include "RenderPass.h"
#include "../core/FrameContext.h"
#include "../rhi/RhiHandles.h"
#include "../debug/RenderDebugService.h"
#include "../rhi/RhiRenderGraph.h"

#include <vector>

class DeferredRenderTargets;
class WorldRenderBuffer;
struct RenderSettings;
class RhiCommandList;
class RhiDevice;

/// Builds the Hi-Z occlusion pyramid: an R32Float mip chain holding the
/// FARTHEST depth of each footprint. History predicts the first terrain batch;
/// current depth conservatively retests history rejects in the same frame.
class HiZPass : public RenderPass {
public:
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "HiZ"; }

    struct GraphResources {
        RgTextureHandle sourceDepth;
        RgTextureHandle hiZ;
        bool currentFrame = false;
    };

    /// Adds one compute reduction pass per pyramid mip. Each pass declares
    /// its exact mip subresources so the graph plans the inter-mip barriers.
    [[nodiscard]] RgPassHandle addGraphPasses(RenderGraph& graph, const FrameContext& ctx,
                                              DeferredRenderTargets& targets, const GraphResources& resources,
                                              RgPassHandle dependency);

    /// Adds the occlusion cull pass: one compute thread per indirect terrain
    /// draw, masking instances whose sub-chunk box is behind the pyramid.
    /// Latest culled/total command counts, delayed by the readback ring.
    [[nodiscard]] const HiZCullFrameStats& cullStats() const { return m_cullStats; }

    /// Invalidates delayed culling counters when the Hi-Z cull path is not active.
    /// This prevents a previous enabled frame from being presented as current data.
    void invalidateCullStats();

    [[nodiscard]] RgPassHandle addCullPass(RenderGraph& graph, const FrameContext& ctx, const RenderSettings& settings,
                                           DeferredRenderTargets& targets, WorldRenderBuffer& worldBuffer,
                                           RgTextureHandle hiZ, RgPassHandle dependency, bool retest = false);

    /// Commits the readback ring position only after graph submission.
    /// @param succeeded True when every graph command list was submitted.
    void finishGraphExecution(bool succeeded);

private:
    bool ensurePipeline(RhiDevice& rhiDevice);
    bool ensureMipBindGroup(RhiDevice& rhiDevice, uint32_t mip, RhiTextureViewHandle sourceView,
                            RhiTextureViewHandle destView);
    [[nodiscard]] bool recordMip(RhiCommandList& commandList, const FrameContext& ctx, DeferredRenderTargets& targets,
                                 uint32_t mip, bool currentFrame);
    void destroyRhiResources();
    bool ensureCullPipeline(RhiDevice& rhiDevice);
    bool ensureCullStatsBuffers(RhiDevice& rhiDevice);
    bool ensureCullBindGroup(RhiDevice& rhiDevice, int slot, RhiBufferHandle commandBuffer, uint64_t commandCapacity,
                             RhiBufferHandle metadataBuffer, uint64_t metadataCapacity, RhiTextureViewHandle hiZView);
    [[nodiscard]] bool recordCull(RhiCommandList& commandList, const FrameContext& ctx, const RenderSettings& settings,
                                  DeferredRenderTargets& targets, WorldRenderBuffer& worldBuffer, bool retest);

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

    RhiShaderHandle m_cullShader;
    RhiBindGroupLayoutHandle m_cullBindGroupLayout;
    RhiPipelineLayoutHandle m_cullPipelineLayout;
    RhiPipelineHandle m_cullPipeline;
    struct CullBinding {
        RhiBindGroupHandle bindGroup;
        RhiBufferHandle boundCommands;
        // The metadata pool reallocates as chunk streaming grows it; a stale
        // binding would feed garbage origins to the cull test.
        RhiBufferHandle boundMetadata;
        RhiTextureViewHandle boundHiZ;
    };
    CullBinding m_cullBindings[2];

    static constexpr uint32_t kCullStatsRingSize = 3u;
    RhiBufferHandle m_cullCounterBuffer;
    RhiBufferHandle m_cullReadbackBuffers[kCullStatsRingSize];
    uint32_t m_cullTotalsRing[kCullStatsRingSize][2] = {};
    bool m_cullRingWritten[kCullStatsRingSize] = {};
    uint32_t m_cullRingWriteIndex = 0u;
    uint32_t m_pendingCullRingIndex = 0u;
    uint32_t m_pendingCullTotals[2] = {};
    bool m_cullSubmissionPending = false;
    HiZCullFrameStats m_cullStats;
};

#endif // MECRAFT_HIZ_PASS_H
