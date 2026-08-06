#ifndef MECRAFT_NRD_GUIDE_PREP_PASS_H
#define MECRAFT_NRD_GUIDE_PREP_PASS_H

#include "RenderPass.h"
#include "renderer/core/FrameContext.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiRenderGraph.h"

#include <array>
#include <cstdint>

class RhiCommandList;
class RhiDevice;

/// Converts the deferred G-buffer into the fixed NRD diffuse guide encodings.
class NrdGuidePrepPass final : public RenderPass {
public:
    /// Deferred inputs and graph-owned NRD guide outputs.
    struct GraphResources final {
        RgTextureHandle depth;
        RgTextureHandle normalAo;
        RgTextureHandle material;
        RgTextureHandle velocity;
        RgTextureHandle validation;
        RgTextureHandle previousValidation;
        RgTextureHandle motion;
        RgTextureHandle normalRoughness;
        RgTextureHandle viewZ;
        RgTextureHandle confidence;
        RgTextureHandle currentValidationHistory;
    };

    /// Explicit projection and valid-depth range used to prepare one guide frame.
    struct Settings final {
        float denoisingRange = 500.0f;
        bool historyValid = false;
        // Selects the projection used by the G-buffer depth reconstruction.
        // The NRD common matrices remain non-jittered, while the guide must
        // match the projection that produced the current and previous depth.
        bool useJitteredProjection = false;
        // Compares the reprojected raw RT identity only when the active TLAS
        // generation changed. Stochastic rays may hit different geometry on
        // ordinary frames, which is not a temporal-history invalidation.
        bool validateHitIdentity = false;
    };

    /// Latest successfully recorded guide dispatch diagnostics.
    struct Stats final {
        bool dispatched = false;
        uint32_t width = 0u;
        uint32_t height = 0u;
        float denoisingRange = 0.0f;
    };

    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "NRD Guide Prep"; }

    /// Adds one compute pass that writes NRD motion, normal/roughness, and linear View-Z guides.
    /// @param graph Render graph receiving the guide declaration.
    /// @param ctx Current frame matrices and render extent.
    /// @param settings Projection selection and finite denoising range.
    /// @param resources Deferred inputs and fixed-format NRD outputs.
    /// @param dependency Pass that completed the G-buffer and dense velocity.
    /// @return Guide pass handle, or an invalid handle when the contract is invalid.
    [[nodiscard]] RgPassHandle addGraphPass(RenderGraph& graph, const FrameContext& ctx, const Settings& settings,
                                            const GraphResources& resources, RgPassHandle dependency);

    /// Returns diagnostics for the latest successfully recorded dispatch.
    [[nodiscard]] const Stats& stats() const { return m_stats; }

private:
    struct GuideViews final {
        RhiTextureViewHandle depth;
        RhiTextureViewHandle normalAo;
        RhiTextureViewHandle material;
        RhiTextureViewHandle velocity;
        RhiTextureViewHandle validation;
        RhiTextureViewHandle previousValidation;
        RhiTextureViewHandle motion;
        RhiTextureViewHandle normalRoughness;
        RhiTextureViewHandle viewZ;
        RhiTextureViewHandle confidence;
        RhiTextureViewHandle currentValidationHistory;
    };

    [[nodiscard]] bool recordGuide(RhiCommandList& commandList, const FrameContext& ctx, const Settings& settings,
                                   const GuideViews& views);
    [[nodiscard]] bool ensurePipeline(RhiDevice& rhiDevice);
    [[nodiscard]] bool ensureBindGroup(RhiDevice& rhiDevice, const GuideViews& views, uint32_t width, uint32_t height);
    void destroyRhiResources();

    RhiDevice* m_rhiDevice = nullptr;
    RhiShaderHandle m_shader;
    RhiSamplerHandle m_sampler;
    RhiBindGroupLayoutHandle m_bindGroupLayout;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiPipelineHandle m_pipeline;
    RhiBindGroupHandle m_bindGroup;
    std::array<RhiTextureViewHandle, 11u> m_boundViews{};
    Stats m_stats;
};

#endif // MECRAFT_NRD_GUIDE_PREP_PASS_H
