#ifndef MECRAFT_RTGI_EMISSIVE_TEMPORAL_PASS_H
#define MECRAFT_RTGI_EMISSIVE_TEMPORAL_PASS_H

#include "RenderPass.h"
#include "renderer/core/FrameContext.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiRenderGraph.h"

#include <array>
#include <cstdint>

class RhiCommandList;
class RhiDevice;

/// Reprojects and temporally filters stochastic RTGI emissive-direct radiance.
class RtgiEmissiveTemporalPass final : public RenderPass {
public:
    static constexpr uint32_t kMaximumHistoryFrameCount = 32u;
    static constexpr float kNormalRejectionThreshold = 0.95f;

    /// Current emissive signal and the NRD guide textures used for reprojection validation.
    struct GraphResources final {
        RgTextureHandle currentEmissiveDirectRadiance;
        RgTextureHandle motion;
        RgTextureHandle reprojectionCoverage;
        RgTextureHandle normalRoughness;
        RgTextureHandle viewZ;
    };

    /// Explicit temporal validity and depth rejection contract for one frame.
    struct Settings final {
        bool historyValid = false;
        float relativeDepthThreshold = 0.02f;
        float maximumViewZ = 1000.0f;
    };

    /// Final pass and full-precision filtered emissive signal produced for same-frame consumers.
    struct GraphOutput final {
        RgPassHandle pass;
        RgTextureHandle filteredEmissiveDirectRadiance;

        /// Reports whether both graph handles were created successfully.
        /// @return True when the temporal pass and filtered signal are valid.
        [[nodiscard]] bool isValid() const {
            return pass.isValid() && filteredEmissiveDirectRadiance.isValid();
        }
    };

    /// Diagnostics for the latest successfully recorded temporal dispatch.
    struct Stats final {
        bool dispatched = false;
        bool historyInputEnabled = false;
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t readGeneration = 0u;
        uint32_t writeGeneration = 0u;
        float preExposureRatio = 1.0f;
        float relativeDepthThreshold = 0.02f;
    };

    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "RTGI Emissive Temporal"; }

    /// Invalidates accumulated lighting while retaining allocated history textures.
    void invalidateHistory();

    /// Adds history initialization when required and one emissive temporal compute pass.
    /// @param graph Render graph receiving the persistent-history imports and passes.
    /// @param ctx Current frame extent and pre-exposure values.
    /// @param settings Temporal reset and surface-rejection parameters.
    /// @param resources Current emissive radiance and NRD motion/surface guides.
    /// @param dependency Pass that completed all current-frame inputs.
    /// @return Final pass and filtered emissive graph handle, or invalid handles on contract failure.
    [[nodiscard]] GraphOutput addGraphPass(RenderGraph& graph, const FrameContext& ctx, const Settings& settings,
                                           const GraphResources& resources, RgPassHandle dependency);

    /// Commits the written history generation only after successful graph submission.
    /// @param succeeded True when every graph pass recorded and submitted successfully.
    void finishGraphExecution(bool succeeded);

    /// Returns diagnostics for the latest successfully recorded dispatch.
    /// @return Immutable temporal-pass statistics.
    [[nodiscard]] const Stats& stats() const { return m_stats; }

private:
    struct HistoryGeneration final {
        RhiTextureHandle emissive;
        RhiTextureViewHandle emissiveView;
        RhiTextureHandle normalViewZ;
        RhiTextureViewHandle normalViewZView;
    };

    struct TemporalViews final {
        RhiTextureViewHandle currentEmissiveDirectRadiance;
        RhiTextureViewHandle motion;
        RhiTextureViewHandle reprojectionCoverage;
        RhiTextureViewHandle normalRoughness;
        RhiTextureViewHandle viewZ;
        RhiTextureViewHandle previousEmissive;
        RhiTextureViewHandle previousNormalViewZ;
        RhiTextureViewHandle outputEmissive;
        RhiTextureViewHandle outputNormalViewZ;
    };

    [[nodiscard]] bool recordHistoryInitialize(RhiCommandList& commandList,
                                               RhiTextureViewHandle emissiveHistory,
                                               RhiTextureViewHandle normalViewZHistory,
                                               uint32_t width, uint32_t height) const;
    [[nodiscard]] bool recordTemporal(RhiCommandList& commandList, const FrameContext& ctx,
                                      const Settings& settings, bool historyInputEnabled,
                                      uint32_t readGeneration, uint32_t writeGeneration,
                                      const TemporalViews& views);
    [[nodiscard]] bool ensurePipeline(RhiDevice& rhiDevice);
    [[nodiscard]] bool ensureHistoryResources(RhiDevice& rhiDevice, uint32_t width, uint32_t height);
    [[nodiscard]] bool ensureBindGroup(RhiDevice& rhiDevice, const TemporalViews& views,
                                       uint32_t width, uint32_t height);
    void destroyHistoryResources();
    void destroyRhiResources();

    RhiDevice* m_rhiDevice = nullptr;
    RhiShaderHandle m_shader;
    RhiSamplerHandle m_sampler;
    RhiBindGroupLayoutHandle m_bindGroupLayout;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiPipelineHandle m_pipeline;
    RhiBindGroupHandle m_bindGroup;
    std::array<RhiTextureViewHandle, 9u> m_boundViews{};
    std::array<HistoryGeneration, 2u> m_historyGenerations{};
    std::array<bool, 2u> m_generationInitialized{};
    uint32_t m_historyWidth = 0u;
    uint32_t m_historyHeight = 0u;
    uint32_t m_readGeneration = 0u;
    bool m_historyValid = false;
    bool m_framePending = false;
    uint32_t m_pendingReadGeneration = 0u;
    uint32_t m_pendingWriteGeneration = 0u;
    bool m_pendingReadInitialization = false;
    Stats m_stats;
};

#endif // MECRAFT_RTGI_EMISSIVE_TEMPORAL_PASS_H
