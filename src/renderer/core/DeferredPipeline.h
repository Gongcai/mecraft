#ifndef MECRAFT_DEFERRED_PIPELINE_H
#define MECRAFT_DEFERRED_PIPELINE_H

#include "RenderPipeline.h"
#include "../passes/SsaoPass.h"
#include "../passes/VelocityPass.h"
#include "../passes/ReflectionPass.h"
#include "../passes/TemporalResolvePass.h"
#include "../passes/MotionBlurPass.h"
#include "../passes/DepthOfFieldPass.h"
#include "../passes/DeferredLightingPass.h"
#include "../passes/CloudPass.h"
#include "../passes/SceneCompositePass.h"
#include "../passes/VolumetricPass.h"
#include "../passes/SkyCapturePass.h"
#include "../passes/GBufferPass.h"
#include "../passes/ShadowPass.h"
#include "../passes/WaterCompositePass.h"
#include "../passes/DebugPass.h"

#include <memory>

class ResourceMgr;

namespace shadow { class ShadowRenderer; }

/// Container for all extracted deferred rendering passes.
/// Owns pass lifecycle (init/shutdown) and provides accessors.
/// Implements RenderPipeline interface for pipeline switching.
class DeferredPipeline : public RenderPipeline {
public:
    // Legacy init (used during transition)
    void init(ResourceMgr& resourceMgr, shadow::ShadowRenderer* shadowRenderer);

    // RenderPipeline interface
    void init(SharedRenderResources& shared) override;
    void shutdown() override;
    FrameOutput renderFrame(const FrameContext& ctx) override;
    const char* name() const override { return "Deferred (Shader Effects)"; }
    bool supportsDeferred() const override { return true; }
    bool supportsDebugView() const override { return true; }

    // Pass accessors
    SsaoPass* ssaoPass() const { return m_ssaoPass.get(); }
    VelocityPass* velocityPass() const { return m_velocityPass.get(); }
    ReflectionPass* reflectionPass() const { return m_reflectionPass.get(); }
    TemporalResolvePass* taaPass() const { return m_taaPass.get(); }
    MotionBlurPass* motionBlurPass() const { return m_motionBlurPass.get(); }
    DepthOfFieldPass* dofPass() const { return m_dofPass.get(); }
    DeferredLightingPass* lightingPass() const { return m_lightingPass.get(); }
    CloudPass* cloudPass() const { return m_cloudPass.get(); }
    SceneCompositePass* sceneCompositePass() const { return m_sceneCompositePass.get(); }
    VolumetricPass* volumetricPass() const { return m_volumetricPass.get(); }
    SkyCapturePass* skyCapturePass() const { return m_skyCapturePass.get(); }
    GBufferPass* gbufferPass() const { return m_gbufferPass.get(); }
    ShadowPass* shadowPass() const { return m_shadowPass.get(); }
    WaterCompositePass* waterCompositePass() const { return m_waterCompositePass.get(); }
    DebugPass* debugPass() const { return m_debugPass.get(); }

private:
    std::unique_ptr<SsaoPass> m_ssaoPass;
    std::unique_ptr<VelocityPass> m_velocityPass;
    std::unique_ptr<ReflectionPass> m_reflectionPass;
    std::unique_ptr<TemporalResolvePass> m_taaPass;
    std::unique_ptr<MotionBlurPass> m_motionBlurPass;
    std::unique_ptr<DepthOfFieldPass> m_dofPass;
    std::unique_ptr<DeferredLightingPass> m_lightingPass;
    std::unique_ptr<CloudPass> m_cloudPass;
    std::unique_ptr<SceneCompositePass> m_sceneCompositePass;
    std::unique_ptr<VolumetricPass> m_volumetricPass;
    std::unique_ptr<SkyCapturePass> m_skyCapturePass;
    std::unique_ptr<GBufferPass> m_gbufferPass;
    std::unique_ptr<ShadowPass> m_shadowPass;
    std::unique_ptr<WaterCompositePass> m_waterCompositePass;
    std::unique_ptr<DebugPass> m_debugPass;

    // Shared resources (non-owning, set during init)
    ResourceMgr* m_resourceMgr = nullptr;
    shadow::ShadowRenderer* m_shadowRenderer = nullptr;
};

#endif // MECRAFT_DEFERRED_PIPELINE_H
