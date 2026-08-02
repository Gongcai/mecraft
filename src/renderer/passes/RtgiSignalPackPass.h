#ifndef MECRAFT_RTGI_SIGNAL_PACK_PASS_H
#define MECRAFT_RTGI_SIGNAL_PACK_PASS_H

#include "RenderPass.h"
#include "renderer/contracts/RtgiNrdSignalContract.h"
#include "renderer/core/FrameContext.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiRenderGraph.h"

#include <array>
#include <cstdint>

class RhiCommandList;
class RhiDevice;

/// Packs one raw RTGI diffuse signal into distinct RELAX and REBLUR inputs.
class RtgiSignalPackPass final : public RenderPass {
public:
    /// Raw inputs, method-specific outputs, and the read-write validation image.
    struct GraphResources final {
        RgTextureHandle rawDiffuseRadianceHitDistance;
        RgTextureHandle depth;
        RgTextureHandle relaxDiffuseRadianceHitDistance;
        RgTextureHandle reblurDiffuseRadianceHitDistance;
        RgTextureHandle validation;
    };

    /// Explicit projection and REBLUR normalization controls.
    struct Settings final {
        renderer::contracts::RtgiReblurHitDistanceParameters reblurHitDistance;
        float diffuseRoughness = 1.0f;
        bool useJitteredProjection = false;
    };

    /// Latest successfully recorded signal-pack dispatch diagnostics.
    struct Stats final {
        bool dispatched = false;
        uint32_t width = 0u;
        uint32_t height = 0u;
        renderer::contracts::RtgiReblurHitDistanceParameters reblurHitDistance;
        float diffuseRoughness = 0.0f;
    };

    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "RTGI Signal Pack"; }

    /// Adds the Vulkan compute pass that produces both method-specific NRD inputs.
    /// @param graph Render graph receiving the signal-pack declaration.
    /// @param ctx Current frame matrices and render extent.
    /// @param settings Projection and REBLUR normalization contract.
    /// @param resources Raw signal, depth, packed outputs, and validation image.
    /// @param dependency Pass that completed the raw RTGI signal.
    /// @return Signal-pack pass handle, or an invalid handle for an invalid contract.
    [[nodiscard]] RgPassHandle addGraphPass(RenderGraph& graph, const FrameContext& ctx, const Settings& settings,
                                            const GraphResources& resources, RgPassHandle dependency);

    /// Returns diagnostics for the latest successfully recorded dispatch.
    [[nodiscard]] const Stats& stats() const { return m_stats; }

private:
    struct PackViews final {
        RhiTextureViewHandle rawDiffuseRadianceHitDistance;
        RhiTextureViewHandle depth;
        RhiTextureViewHandle relaxDiffuseRadianceHitDistance;
        RhiTextureViewHandle reblurDiffuseRadianceHitDistance;
        RhiTextureViewHandle validation;
    };

    [[nodiscard]] bool recordPack(RhiCommandList& commandList, const FrameContext& ctx, const Settings& settings,
                                  const PackViews& views);
    [[nodiscard]] bool ensurePipeline(RhiDevice& rhiDevice);
    [[nodiscard]] bool ensureBindGroup(RhiDevice& rhiDevice, const PackViews& views, uint32_t width, uint32_t height);
    void destroyRhiResources();

    RhiDevice* m_rhiDevice = nullptr;
    RhiShaderHandle m_shader;
    RhiSamplerHandle m_sampler;
    RhiBindGroupLayoutHandle m_bindGroupLayout;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiPipelineHandle m_pipeline;
    RhiBindGroupHandle m_bindGroup;
    std::array<RhiTextureViewHandle, 5u> m_boundViews{};
    Stats m_stats;
};

#endif // MECRAFT_RTGI_SIGNAL_PACK_PASS_H
