#include "DebugPass.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/Shader.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"
#include "../shadow/ShadowRenderer.h"
#include "../../resource/ResourceMgr.h"

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <algorithm>

static constexpr int SHADOW_CASCADE_COUNT = shadow::ShadowRenderer::CASCADE_COUNT;

void DebugPass::init(ResourceMgr& resourceMgr) {
    m_deferredDebugShader = resourceMgr.getShader("deferred_debug");
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
}

void DebugPass::shutdown() {
    m_deferredDebugShader = nullptr;
    m_noiseTexture = {};
}

void DebugPass::execute(const FrameContext& ctx, const RenderSettings& settings,
                         DeferredRenderTargets& targets,
                         const int32_t framebuffer, const int width, const int height) {
    if (m_deferredDebugShader == nullptr) {
        return;
    }

    targets.bindDefaultLike(framebuffer, width, height);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    m_deferredDebugShader->use();
    m_deferredDebugShader->setInt("uAlbedoTex", 0);
    m_deferredDebugShader->setInt("uNormalAoTex", 1);
    m_deferredDebugShader->setInt("uVoxelLightTex", 2);
    m_deferredDebugShader->setInt("uMaterialTex", 3);
    m_deferredDebugShader->setInt("uDepthTex", 4);
    m_deferredDebugShader->setInt("uShadowMapRaw", 5);
    m_deferredDebugShader->setInt("uSsaoTex", 6);
    m_deferredDebugShader->setInt("uSceneLightingTex", 7);
    m_deferredDebugShader->setInt("uTransparentCompositeTex", 8);
    m_deferredDebugShader->setInt("uTransparentCompositeDepthTex", 9);
    m_deferredDebugShader->setInt("uVolumetricTex", 10);
    m_deferredDebugShader->setInt("uSkyCaptureTex", 11);
    m_deferredDebugShader->setInt("uVelocityTex", 12);
    m_deferredDebugShader->setInt("uHistorySceneTex", 13);
    m_deferredDebugShader->setInt("uHistoryDepthTex", 13);
    m_deferredDebugShader->setInt("uNoiseTex", 13);
    m_deferredDebugShader->setInt("uReflectionTex", 14);
    m_deferredDebugShader->setInt("uCloudTex", 15);
    m_deferredDebugShader->setInt("uSceneCompositeTex", 15);
    m_deferredDebugShader->setInt("uSceneResolvedTex", 15);
    m_deferredDebugShader->setInt("uTemporalCurrentTex", 17);
    m_deferredDebugShader->setInt("uMaterialAuxTex", 13);
    m_deferredDebugShader->setInt("uShadowColorTex", 14);
    m_deferredDebugShader->setInt("uShadowNormalTex", 15);
    m_deferredDebugShader->setInt("uHistoryReflectionTex", 14);
    m_deferredDebugShader->setInt("uHistoryCloudTex", 15);
    m_deferredDebugShader->setInt("uCsmShadowDepthTex", 16);
    m_deferredDebugShader->setInt("uSsgiTex", 18);

    // Shadow uniforms from ShadowRenderer
    if (m_shadowRenderer) {
        m_deferredDebugShader->setMat4("uShadowModelView", m_shadowRenderer->modelView());
        m_deferredDebugShader->setMat4("uShadowProjection", m_shadowRenderer->projection());
        m_deferredDebugShader->setMat4("uShadowProjectionInverse", m_shadowRenderer->projectionInverse());
        m_deferredDebugShader->setFloat("uShadowExtent", m_shadowRenderer->shadowExtent());
        m_deferredDebugShader->setFloat("uShadowTexelWorldSize", m_shadowRenderer->texelWorldSize());

        const shadow::ShadowRenderer::BiasSettings bias{
            settings.shadow.constantBias,
            settings.shadow.slopeBias,
            settings.shadow.normalOffset
        };
        m_shadowRenderer->bindShadowUniforms(*m_deferredDebugShader, ctx.moonShadowActive, bias);
    } else {
        m_deferredDebugShader->setMat4("uShadowModelView", glm::mat4(1.0f));
        m_deferredDebugShader->setMat4("uShadowProjection", glm::mat4(1.0f));
        m_deferredDebugShader->setMat4("uShadowProjectionInverse", glm::mat4(1.0f));
        m_deferredDebugShader->setFloat("uShadowExtent", 0.0f);
        m_deferredDebugShader->setFloat("uShadowTexelWorldSize", 0.0f);
        m_deferredDebugShader->setInt("uCsmCascadeCount", SHADOW_CASCADE_COUNT);
    }

    // Shadow settings uniforms
    m_deferredDebugShader->setFloat("uShadowMapSize", static_cast<float>(settings.shadow.resolution));
    m_deferredDebugShader->setFloat("uShadowDistance", std::max(64.0f, settings.shadow.distance));
    m_deferredDebugShader->setFloat("uShadowConstantBias", settings.shadow.constantBias);
    m_deferredDebugShader->setFloat("uShadowSlopeBias", settings.shadow.slopeBias);
    m_deferredDebugShader->setFloat("uShadowNormalOffset", settings.shadow.normalOffset);

    // Debug view and frame uniforms
    m_deferredDebugShader->setInt("uDebugViewMode", settings.debug.viewMode);
    m_deferredDebugShader->setInt("uFrameIndex", static_cast<int>(ctx.frameIndex & 0x7fffffffULL));
    m_deferredDebugShader->setInt("uFreezeBias", settings.volumetric.freezeBias ? 1 : 0);

    // Camera/frame data from FrameContext
    m_deferredDebugShader->setMat4("uInvViewProj", ctx.camera.invViewProj);
    m_deferredDebugShader->setFloat("uNearPlane", ctx.camera.nearPlane);
    m_deferredDebugShader->setFloat("uFarPlane", ctx.camera.farPlane);
    m_deferredDebugShader->setVec3("uCameraPos", ctx.camera.position);
    m_deferredDebugShader->setVec3("uSunDirection", ctx.skyColors.sunDirection);
    m_deferredDebugShader->setVec3("uMoonDirection", ctx.skyColors.moonDirection);

    if (m_shadowRenderer) {
        m_deferredDebugShader->setVec3("uShadowLightDirection", m_shadowRenderer->lightDirection());
    } else {
        m_deferredDebugShader->setVec3("uShadowLightDirection", ctx.skyColors.sunDirection);
    }
    m_deferredDebugShader->setInt("uShadowLightMode", ctx.moonShadowActive ? 1 : 0);

    // Lighting diagnostic uniforms (for debug view 45)
    m_deferredDebugShader->setVec3("uSunLightColor", ctx.skyColors.sunLightColor);
    m_deferredDebugShader->setVec3("uSkyAmbientColor", ctx.skyColors.skyAmbientColor);
    m_deferredDebugShader->setVec3("uHorizonScatterColor", ctx.skyColors.horizonScatterColor);
    m_deferredDebugShader->setVec3("uFogColor", ctx.fog.color);

    // Bind all intermediate textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.albedoTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.normalAoTextureHandle()));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.voxelLightTextureHandle()));
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.materialTextureHandle()));
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.shadowDepthTextureHandle()));
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, targets.ssaoFilteredTexture());
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.sceneLightingTextureHandle()));
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.transparentCompositeTextureHandle()));
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.transparentCompositeDepthTextureHandle()));
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.halfResTextureHandle()));
    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.skyCaptureTextureHandle()));
    glActiveTexture(GL_TEXTURE12);
    glBindTexture(GL_TEXTURE_2D, targets.velocityTexture());

    // Texture unit 13: multiplexed based on debug view mode
    glActiveTexture(GL_TEXTURE13);
    const bool materialAuxDebug =
        settings.debug.viewMode == 26 ||
        settings.debug.viewMode == 27 ||
        settings.debug.viewMode == 80;
    const bool historyDepthDebug = settings.debug.viewMode == 19;
    const bool shadowCompareDebug =
        settings.debug.viewMode == 21 ||
        settings.debug.viewMode == 22 ||
        settings.debug.viewMode == 34 ||
        settings.debug.viewMode == 35;
    const uint32_t noiseTexture = renderer::rhi::gl::textureId(m_noiseTexture);
    const uint32_t materialAuxTexture = renderer::rhi::gl::textureId(targets.materialAuxTextureHandle());
    glBindTexture(GL_TEXTURE_2D,
                  shadowCompareDebug ? noiseTexture
                                     : (materialAuxDebug ? materialAuxTexture
                                                         : (historyDepthDebug ? targets.historyDepthTexturePrev()
                                                                              : targets.historySceneTexturePrev())));

    // Texture unit 14: multiplexed based on debug view mode
    glActiveTexture(GL_TEXTURE14);
    const bool shadowCasterDebug = settings.debug.viewMode == 35;
    const bool reflectionHistoryDebug = settings.debug.viewMode == 28;
    const uint32_t shadowColorTexture = renderer::rhi::gl::textureId(targets.shadowColorTextureHandle());
    const uint32_t reflectionTexture = renderer::rhi::gl::textureId(targets.reflectionTextureHandle());
    glBindTexture(GL_TEXTURE_2D,
                  shadowCasterDebug ? shadowColorTexture
                                    : (reflectionHistoryDebug ? targets.historyReflectionTexturePrev()
                                                              : reflectionTexture));

    // Texture unit 15: multiplexed based on debug view mode
    glActiveTexture(GL_TEXTURE15);
    const bool cloudHistoryDebug = settings.debug.viewMode == 29;
    const bool sceneCompositeDebug =
        settings.debug.viewMode == 11 ||
        settings.debug.viewMode == 78;
    const bool sceneResolvedDebug =
        settings.debug.viewMode == 31 ||
        settings.debug.viewMode == 79;
    const uint32_t shadowNormalTexture = renderer::rhi::gl::textureId(targets.shadowNormalTextureHandle());
    const uint32_t sceneCompositeTexture = renderer::rhi::gl::textureId(targets.sceneCompositeTextureHandle());
    const uint32_t cloudTexture = renderer::rhi::gl::textureId(targets.cloudTextureHandle());
    glBindTexture(GL_TEXTURE_2D,
                  shadowCasterDebug ? shadowNormalTexture
                                    : (sceneResolvedDebug
                                           ? renderer::rhi::gl::textureId(targets.sceneResolvedTextureHandle())
                                          : (sceneCompositeDebug ? sceneCompositeTexture
                                                                 : (cloudHistoryDebug ? targets.historyCloudTexturePrev()
                                                                                      : cloudTexture))));

    glActiveTexture(GL_TEXTURE16);
    glBindTexture(GL_TEXTURE_2D_ARRAY,
                  renderer::rhi::gl::textureId(targets.csmShadowDepthTextureHandle()));
    glActiveTexture(GL_TEXTURE17);
    glBindTexture(GL_TEXTURE_2D, targets.temporalCurrentTexture());
    glActiveTexture(GL_TEXTURE18);
    glBindTexture(GL_TEXTURE_2D, targets.ssgiTexture());

    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_deferredDebugShader);

    // Cleanup texture bindings
    glActiveTexture(GL_TEXTURE18);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE17);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE16);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    for (int unit = 15; unit >= 0; --unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
