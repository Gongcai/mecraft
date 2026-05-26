#include "DeferredPipeline.h"
#include "../../resource/ResourceMgr.h"
#include "../shadow/ShadowRenderer.h"

void DeferredPipeline::init(ResourceMgr& resourceMgr, shadow::ShadowRenderer* shadowRenderer) {
    m_skyCapturePass = std::make_unique<SkyCapturePass>();
    m_gbufferPass = std::make_unique<GBufferPass>();
    m_shadowPass = std::make_unique<ShadowPass>();
    m_waterCompositePass = std::make_unique<WaterCompositePass>();
    m_velocityPass = std::make_unique<VelocityPass>();
    m_ssaoPass = std::make_unique<SsaoPass>();
    m_lightingPass = std::make_unique<DeferredLightingPass>();
    m_reflectionPass = std::make_unique<ReflectionPass>();
    m_cloudPass = std::make_unique<CloudPass>();
    m_sceneCompositePass = std::make_unique<SceneCompositePass>();
    m_volumetricPass = std::make_unique<VolumetricPass>();
    m_taaPass = std::make_unique<TemporalResolvePass>();
    m_motionBlurPass = std::make_unique<MotionBlurPass>();
    m_dofPass = std::make_unique<DepthOfFieldPass>();

    m_skyCapturePass->init(resourceMgr);
    m_gbufferPass->init(resourceMgr);
    m_shadowPass->init(resourceMgr);
    m_waterCompositePass->init(resourceMgr);
    m_velocityPass->init(resourceMgr);
    m_ssaoPass->init(resourceMgr);
    m_lightingPass->init(resourceMgr);
    m_reflectionPass->init(resourceMgr);
    m_cloudPass->init(resourceMgr);
    m_sceneCompositePass->init(resourceMgr);
    m_volumetricPass->init(resourceMgr);
    m_taaPass->init(resourceMgr);
    m_motionBlurPass->init(resourceMgr);
    m_dofPass->init(resourceMgr);

    if (shadowRenderer) {
        m_shadowPass->setShadowRenderer(shadowRenderer);
        m_lightingPass->setShadowRenderer(shadowRenderer);
        m_volumetricPass->setShadowRenderer(shadowRenderer);
    }

    // WaterCompositePass needs TerrainRenderer and WorldRenderBuffer —
    // injected later by Renderer after those are created.
}

void DeferredPipeline::shutdown() {
    m_dofPass.reset();
    m_motionBlurPass.reset();
    m_taaPass.reset();
    m_volumetricPass.reset();
    m_sceneCompositePass.reset();
    m_cloudPass.reset();
    m_reflectionPass.reset();
    m_lightingPass.reset();
    m_ssaoPass.reset();
    m_velocityPass.reset();
    m_shadowPass.reset();
    m_waterCompositePass.reset();
    m_gbufferPass.reset();
    m_skyCapturePass.reset();
}
