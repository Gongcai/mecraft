#ifndef MECRAFT_DEFERRED_PIPELINE_H
#define MECRAFT_DEFERRED_PIPELINE_H

#include "RenderPipeline.h"
#include "RenderSettings.h"
#include "../debug/RenderDebugService.h"
#include "../rhi/RhiRenderGraph.h"
#include "../mesh/WorldDrawBatch.h"
#include "../mesh/TerrainRenderer.h"
#include "../mesh/TerrainRenderCache.h"
#include "../passes/SsaoPass.h"
#include "../passes/SsgiPass.h"
#include "../passes/NrdGuidePrepPass.h"
#include "../passes/RtgiEmissiveTemporalPass.h"
#include "../passes/RtgiSignalPackPass.h"
#include "../passes/RtgiTracePass.h"
#include "../passes/RtgiValidationComposePass.h"
#include "../passes/HiZPass.h"
#include "../passes/VelocityPass.h"
#include "../passes/ReflectionPass.h"
#include "../passes/ReflectionProbeCapturePass.h"
#include "../passes/ReflectionProbeGridPass.h"
#include "../passes/TemporalResolvePass.h"
#include "../passes/MotionBlurPass.h"
#include "../passes/DepthOfFieldPass.h"
#include "../passes/LocalShadowPass.h"
#include "../passes/ClusteredLightingPass.h"
#include "../passes/DeferredLightingPass.h"
#include "../passes/CloudPass.h"
#include "../passes/SceneCompositePass.h"
#include "../passes/VolumetricPass.h"
#include "../passes/SkyCapturePass.h"
#include "../passes/SkyIblPass.h"
#include "../passes/GBufferPass.h"
#include "../passes/ShadowPass.h"
#include "../passes/WaterCompositePass.h"
#include "../passes/DebugPass.h"

#if defined(MECRAFT_ENABLE_NRD)
#include "../nrd/NrdRenderGraphBridge.h"
#endif

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

struct GameResources;
class World;
class Camera;
class Window;

namespace shadow {
class ShadowRenderer;
}

/// Container for all extracted deferred rendering passes.
/// Owns pass lifecycle (init/shutdown) and provides accessors.
/// Implements RenderPipeline interface for pipeline switching.
class DeferredPipeline : public RenderPipeline, public IReflectionProbeCaptureRenderer {
public:
    // RenderPipeline interface
    void init(SharedRenderResources& shared) override;
    void shutdown() override;
    FrameOutput renderFrame(const FrameContext& ctx, const RenderSettings& settings) override;
    const char* name() const override { return "Deferred (Shader Effects)"; }
    bool supportsDeferred() const override { return true; }
    bool supportsDebugView() const override { return true; }

    // Held block light value (set from Game)
    void setHeldBlockLightValue(int value) { m_heldBlockLightValue = value; }
    [[nodiscard]] bool setSceneLights(std::vector<renderer::contracts::SceneLight> lights);
    [[nodiscard]] bool recordReflectionProbeRadianceOpaque(RhiCommandList& commandList, const FrameContext& context,
                                                           const ReflectionProbeCaptureWork& work) override;
    [[nodiscard]] bool recordReflectionProbeRadianceTransparent(RhiCommandList& commandList,
                                                                const FrameContext& context,
                                                                const ReflectionProbeCaptureWork& work) override;
    void invalidateHistory();

    // Pass accessors
    SsaoPass* ssaoPass() const { return m_ssaoPass.get(); }
    SsgiPass* ssgiPass() const { return m_ssgiPass.get(); }
    VelocityPass* velocityPass() const { return m_velocityPass.get(); }
    ReflectionPass* reflectionPass() const { return m_reflectionPass.get(); }
    ReflectionProbeCapturePass* reflectionProbeCapturePass() const { return m_reflectionProbeCapturePass.get(); }
    ReflectionProbeGridPass* reflectionProbeGridPass() const { return m_reflectionProbeGridPass.get(); }
    TemporalResolvePass* taaPass() const { return m_taaPass.get(); }
    MotionBlurPass* motionBlurPass() const { return m_motionBlurPass.get(); }
    DepthOfFieldPass* dofPass() const { return m_dofPass.get(); }
    LocalShadowPass* localShadowPass() const { return m_localShadowPass.get(); }
    ClusteredLightingPass* clusteredLightingPass() const { return m_clusteredLightingPass.get(); }
    DeferredLightingPass* lightingPass() const { return m_lightingPass.get(); }
    CloudPass* cloudPass() const { return m_cloudPass.get(); }
    SceneCompositePass* sceneCompositePass() const { return m_sceneCompositePass.get(); }
    VolumetricPass* volumetricPass() const { return m_volumetricPass.get(); }
    SkyCapturePass* skyCapturePass() const { return m_skyCapturePass.get(); }
    SkyIblPass* skyIblPass() const { return m_skyIblPass.get(); }
    GBufferPass* gbufferPass() const { return m_gbufferPass.get(); }
    ShadowPass* shadowPass() const { return m_shadowPass.get(); }
    WaterCompositePass* waterCompositePass() const { return m_waterCompositePass.get(); }
    DebugPass* debugPass() const { return m_debugPass.get(); }
    HiZPass* hiZPass() const { return m_hiZPass.get(); }

    /// Builds dashboard statistics from the most recent Render Graph frame:
    /// CPU stage costs measured around the last submission plus per-pass GPU
    /// timings and scheduling gaps from the latest completed snapshot.
    /// @return Frame statistics; `valid` is false before the first submission.
    [[nodiscard]] RenderGraphFrameStats renderGraphFrameStats() const;

private:
    void initializePasses(GameResources& resources, shadow::ShadowRenderer* shadowRenderer);
    /// Ensures that FrameOutput RTGI signals have non-aliased storage until the next frame.
    /// @param rhiDevice Device that owns the persistent validation textures.
    /// @param width Render width in texels.
    /// @param height Render height in texels.
    /// @return True when both RGBA16F textures and views match the requested extent.
    [[nodiscard]] bool ensureRtgiValidationOutputTextures(RhiDevice& rhiDevice, uint32_t width, uint32_t height);
    /// Releases the persistent RTGI signal textures and their views.
    /// @param rhiDevice Device that owns the textures.
    void destroyRtgiValidationOutputTextures(RhiDevice& rhiDevice);

    // Pass instances
    std::unique_ptr<SsaoPass> m_ssaoPass;
    std::unique_ptr<SsgiPass> m_ssgiPass;
    std::unique_ptr<RtgiTracePass> m_rtgiTracePass;
    std::unique_ptr<NrdGuidePrepPass> m_nrdGuidePrepPass;
    std::unique_ptr<RtgiEmissiveTemporalPass> m_rtgiEmissiveTemporalPass;
    std::unique_ptr<RtgiSignalPackPass> m_rtgiSignalPackPass;
    std::unique_ptr<RtgiValidationComposePass> m_rtgiValidationComposePass;
#if defined(MECRAFT_ENABLE_NRD)
    std::unique_ptr<renderer::nrd::NrdRenderGraphBridge> m_nrdBridge;
    bool m_nrdClearHistory = true;
#endif
    std::unique_ptr<VelocityPass> m_velocityPass;
    std::unique_ptr<HiZPass> m_hiZPass;
    std::unique_ptr<ReflectionPass> m_reflectionPass;
    std::unique_ptr<ReflectionProbeCapturePass> m_reflectionProbeCapturePass;
    std::unique_ptr<ReflectionProbeGridPass> m_reflectionProbeGridPass;
    bool m_reflectionProbeGridOwnedByCapture = false;
    std::unique_ptr<TemporalResolvePass> m_taaPass;
    std::unique_ptr<MotionBlurPass> m_motionBlurPass;
    std::unique_ptr<DepthOfFieldPass> m_dofPass;
    std::unique_ptr<LocalShadowPass> m_localShadowPass;
    std::unique_ptr<ClusteredLightingPass> m_clusteredLightingPass;
    std::unique_ptr<DeferredLightingPass> m_lightingPass;
    std::unique_ptr<CloudPass> m_cloudPass;
    std::unique_ptr<SceneCompositePass> m_sceneCompositePass;
    std::unique_ptr<VolumetricPass> m_volumetricPass;
    std::unique_ptr<SkyCapturePass> m_skyCapturePass;
    std::unique_ptr<SkyIblPass> m_skyIblPass;
    std::unique_ptr<GBufferPass> m_gbufferPass;
    std::unique_ptr<ShadowPass> m_shadowPass;
    std::unique_ptr<WaterCompositePass> m_waterCompositePass;
    std::unique_ptr<DebugPass> m_debugPass;

    // Shared resources (non-owning, set during init)
    GameResources* m_resources = nullptr;
    shadow::ShadowRenderer* m_shadowRenderer = nullptr;
    SharedRenderResources* m_shared = nullptr;

    // Orchestration state (migrated from Renderer)
    bool m_deferredFrameActive = false;
    bool m_hasPreviousFrameData = false;
    bool m_waterRenderedBeforeTemporal = false;
    bool m_waterRenderedAfterTemporal = false;
    // Advances the production low-discrepancy sequence; trace inspection
    // freezes it so raw validation images remain comparable across frames.
    uint32_t m_rtgiTemporalSampleIndex = 0u;
    TemporalResetReasons m_lastTemporalResetReasons = temporalResetReasonBit(TemporalResetReason::FirstFrame);
    bool m_rtgiTraceInspectionActive = false;
    RhiTextureHandle m_rtgiRawDiffuseTexture;
    RhiTextureHandle m_nrdDiffuseTexture;
    RhiTextureHandle m_rtgiRawDiffuseValidationTexture;
    RhiTextureViewHandle m_rtgiRawDiffuseValidationView;
    RhiTextureHandle m_nrdDiffuseValidationTexture;
    RhiTextureViewHandle m_nrdDiffuseValidationView;
    RhiTextureHandle m_rtgiLeakageNormalValidationTexture;
    RhiTextureViewHandle m_rtgiLeakageNormalValidationView;
    RhiTextureHandle m_rtgiLeakageViewZValidationTexture;
    RhiTextureViewHandle m_rtgiLeakageViewZValidationView;
    uint32_t m_rtgiValidationOutputWidth = 0u;
    uint32_t m_rtgiValidationOutputHeight = 0u;
    bool m_rtgiValidationOutputInitialized = false;
    int m_heldBlockLightValue = 0;
    std::vector<renderer::contracts::SceneLight> m_sceneLights;

    // Settings (cached from RenderSettings for current frame)
    RenderSettings m_currentSettings;

    // Transparent batch state (populated by renderGBufferTerrain, consumed by water/transparent)
    std::vector<DrawBatchEntry> m_transparentBatch;
    TransparentPassPlan m_transparentPassPlan;
    RenderGraph m_renderGraph;

    // CPU costs of the last frame graph submission (declare/compile/record).
    double m_graphCpuBuildMs = 0.0;
    double m_graphCpuCompileMs = 0.0;
    double m_graphCpuExecuteMs = 0.0;
    double m_graphCpuRecordMs = 0.0;
    double m_graphCpuSubmitMs = 0.0;
    double m_graphCpuShadowPrepMs = 0.0;
    double m_graphCpuTerrainPrepMs = 0.0;
    double m_sceneTlasPrepareCpuMs = 0.0;
    double m_rtgiSceneTlasBootstrapCpuMs = 0.0;
    uint64_t m_accelerationStructureFrameSequence = 0u;
    uint32_t m_graphSubmitCount = 0u;
    uint32_t m_graphWorkerRecordedBatchCount = 0u;
    bool m_graphCpuStatsValid = false;
    bool m_asyncComputeGateLogged = false;
    bool m_terrainDrawsPrepared = false;
    renderer::contracts::StableReflectionProbeId m_voxelReflectionProbeId;
    glm::ivec3 m_voxelReflectionProbeCell{0};
    const IWorldView* m_voxelReflectionProbeWorldView = nullptr;
    uint64_t m_voxelReflectionProbeActiveChunkRevision = 0u;
    uint64_t m_voxelReflectionProbeBlockContentRevision = 0u;
    uint32_t m_voxelReflectionProbeCaptureRevision = 0u;
    bool m_voxelReflectionProbeCellInitialized = false;

    // Private orchestration methods
    [[nodiscard]] bool recordDeferredAuxiliaryClear(RhiCommandList& commandList, DeferredRenderTargets& targets,
                                                    bool clearReflection, bool clearSceneComposite, bool clearCloud,
                                                    bool clearSsaoFiltered);
    void commitDeferredHistoryState();
    [[nodiscard]] bool prepareSceneTlas(const glm::vec3& cameraPosition);
    [[nodiscard]] bool bootstrapSceneTlasForRtgi(RenderDebugService* debugService);
    [[nodiscard]] bool recordTerrainDrawPreparation(RhiCommandList& commandList, const FrameContext& ctx);
    [[nodiscard]] bool configureVoxelReflectionProbe(const FrameContext& ctx);
    [[nodiscard]] bool executeFrameGraph(const FrameContext& ctx, const RenderSettings& settings);
    [[nodiscard]] bool renderGBufferTerrain(RhiCommandList& commandList, const FrameContext& ctx,
                                            const RenderSettings& settings);
    [[nodiscard]] bool recordGenericTransparentPass(RhiCommandList& commandList, const FrameContext& ctx,
                                                    RhiTextureViewHandle transmissionSourceView);
    [[nodiscard]] bool hasGenericTransparentGeometry() const;
    [[nodiscard]] bool recordParticlesPass(RhiCommandList& commandList, const FrameContext& ctx);
    FrameOutput buildFrameOutput(const FrameContext& ctx);
};

#endif // MECRAFT_DEFERRED_PIPELINE_H
