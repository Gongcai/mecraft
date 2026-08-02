#ifndef MECRAFT_RTGI_TRACE_PASS_H
#define MECRAFT_RTGI_TRACE_PASS_H

#include "RenderPass.h"
#include "renderer/core/FrameContext.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiRenderGraph.h"

#include <array>
#include <cstdint>

class RhiCommandList;
class RhiDevice;

/// Records the Vulkan raw diffuse RTGI ray-query signal before material shading and denoising.
class RtgiTracePass final : public RenderPass {
public:
    /// Immutable inputs and outputs declared by the caller's render graph.
    struct GraphResources final {
        RgTextureHandle depth;
        RgTextureHandle normalAo;
        RgTextureHandle materialAux;
        RgTextureHandle blueNoise;
        RgTextureHandle diffuseRadianceHitDistance;
        RgTextureHandle validation;
    };

    /// Explicit trace controls for the first opaque-only RTGI production slice.
    struct Settings final {
        float maxRayDistance = 64.0f;
        float minimumRayOriginBias = 0.001f;
        uint8_t instanceMask = 1u;
        bool useJitteredProjection = false;
    };

    /// Latest successfully recorded raw-trace dispatch diagnostics.
    struct Stats final {
        bool dispatched = false;
        uint64_t frameIndex = 0u;
        uint64_t sceneTlasRevision = 0u;
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint8_t instanceMask = 0u;
    };

    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "RTGI Trace"; }

    /// Adds one Vulkan compute ray-query pass with exact G-buffer and raw-signal dependencies.
    /// @param graph Render graph receiving the trace declaration.
    /// @param ctx Current frame matrices, dimensions, and shared Global Bindless/TLAS owners.
    /// @param settings Explicit opaque trace distance, origin bias, mask, and projection contract.
    /// @param resources Imported G-buffer/noise inputs and storage-image outputs.
    /// @param dependency Pass that completes all trace inputs before dispatch.
    /// @return Trace pass handle, or an invalid handle when any production contract is invalid.
    [[nodiscard]] RgPassHandle addGraphPass(RenderGraph& graph, const FrameContext& ctx, const Settings& settings,
                                            const GraphResources& resources, RgPassHandle dependency);

    /// Returns diagnostics for the latest successfully recorded dispatch.
    /// @return Immutable dispatch snapshot; dispatched is false before the first successful record.
    [[nodiscard]] const Stats& stats() const { return m_stats; }

private:
    struct TraceViews final {
        RhiTextureViewHandle depth;
        RhiTextureViewHandle normalAo;
        RhiTextureViewHandle materialAux;
        RhiTextureViewHandle blueNoise;
        RhiTextureViewHandle diffuseRadianceHitDistance;
        RhiTextureViewHandle validation;
    };

    [[nodiscard]] bool recordTrace(RhiCommandList& commandList, const FrameContext& ctx, const Settings& settings,
                                   const TraceViews& views, uint64_t sceneTlasRevision);
    [[nodiscard]] bool ensurePipeline(RhiDevice& rhiDevice, RhiBindGroupLayoutHandle globalBindlessLayout);
    [[nodiscard]] bool ensureBindGroup(RhiDevice& rhiDevice, const TraceViews& views);
    void destroyRhiResources();

    RhiDevice* m_rhiDevice = nullptr;
    RhiShaderHandle m_shader;
    RhiSamplerHandle m_sampler;
    RhiBindGroupLayoutHandle m_globalBindlessLayout;
    RhiBindGroupLayoutHandle m_traceBindGroupLayout;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiPipelineHandle m_pipeline;
    RhiBindGroupHandle m_traceBindGroup;
    std::array<RhiTextureViewHandle, 6u> m_boundViews{};
    Stats m_stats;
};

#endif // MECRAFT_RTGI_TRACE_PASS_H
