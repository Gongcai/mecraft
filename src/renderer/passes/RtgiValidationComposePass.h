#ifndef MECRAFT_RTGI_VALIDATION_COMPOSE_PASS_H
#define MECRAFT_RTGI_VALIDATION_COMPOSE_PASS_H

#include "RenderPass.h"
#include "renderer/core/FrameContext.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiRenderGraph.h"

#include <array>
#include <cstdint>

class RhiCommandList;
class RhiDevice;

/// Composes scene-referred NRD indirect radiance with current-frame emissive direct radiance for validation capture.
class RtgiValidationComposePass final : public RenderPass {
public:
    /// Exact RGB encoding returned by the selected NRD diffuse method.
    enum class Encoding : uint8_t { RelaxLinearRgb = 0u, ReblurYCoCg = 1u };

    /// Denoised indirect input, full-precision pre-exposed emissive input, and linear scene-referred output.
    struct GraphResources final {
        RgTextureHandle denoisedIndirectRadianceHitDistance;
        RgTextureHandle emissiveDirectRadiance;
        RgTextureHandle combinedValidationRadiance;
    };

    /// Explicit NRD output encoding used by the compose shader.
    struct Settings final {
        Encoding encoding = Encoding::RelaxLinearRgb;
    };

    /// Latest successfully recorded validation-compose dispatch diagnostics.
    struct Stats final {
        bool dispatched = false;
        uint32_t width = 0u;
        uint32_t height = 0u;
        float inversePreExposure = 1.0f;
        Encoding encoding = Encoding::RelaxLinearRgb;
    };

    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "RTGI Validation Compose"; }

    /// Adds the Vulkan compute pass that restores a linear combined validation signal after NRD.
    /// @param graph Render graph receiving the compose declaration.
    /// @param ctx Current frame extent and pre-exposure contract.
    /// @param settings Exact encoding produced by the active NRD method.
    /// @param resources Denoised indirect input, emissive input, and combined output.
    /// @param dependency Pass that completed the NRD output.
    /// @return Compose pass handle, or an invalid handle for an invalid resource contract.
    [[nodiscard]] RgPassHandle addGraphPass(RenderGraph& graph, const FrameContext& ctx, const Settings& settings,
                                            const GraphResources& resources, RgPassHandle dependency);

    /// Returns diagnostics for the latest successfully recorded dispatch.
    /// @return Immutable compose-pass statistics.
    [[nodiscard]] const Stats& stats() const { return m_stats; }

private:
    struct ComposeViews final {
        RhiTextureViewHandle denoisedIndirectRadianceHitDistance;
        RhiTextureViewHandle emissiveDirectRadiance;
        RhiTextureViewHandle combinedValidationRadiance;
    };

    [[nodiscard]] bool recordCompose(RhiCommandList& commandList, const FrameContext& ctx, const Settings& settings,
                                     const ComposeViews& views);
    [[nodiscard]] bool ensurePipeline(RhiDevice& rhiDevice, Encoding encoding);
    [[nodiscard]] bool ensureBindGroup(RhiDevice& rhiDevice, const ComposeViews& views, uint32_t width,
                                       uint32_t height);
    void destroyRhiResources();

    RhiDevice* m_rhiDevice = nullptr;
    RhiShaderHandle m_shader;
    RhiSamplerHandle m_sampler;
    RhiBindGroupLayoutHandle m_bindGroupLayout;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiPipelineHandle m_pipeline;
    RhiBindGroupHandle m_bindGroup;
    std::array<RhiTextureViewHandle, 3u> m_boundViews{};
    Encoding m_pipelineEncoding = Encoding::RelaxLinearRgb;
    bool m_pipelineEncodingValid = false;
    Stats m_stats;
};

#endif // MECRAFT_RTGI_VALIDATION_COMPOSE_PASS_H
