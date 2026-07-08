#include "SceneCompositePass.h"
#include "../core/RenderScene.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/Shader.h"
#include "../gi/VoxelGiClipmap.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"
#include "../../resource/ResourceMgr.h"

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <algorithm>

void SceneCompositePass::init(ResourceMgr& resourceMgr) {
    m_sceneCompositeShader = resourceMgr.getShader("scene_composite");
}

void SceneCompositePass::shutdown() {
    m_sceneCompositeShader = nullptr;
}

void SceneCompositePass::execute(const FrameContext& ctx, const RenderSettings& settings,
                                  DeferredRenderTargets& targets, const VoxelGiClipmap* voxelGiClipmap) {
    if (m_sceneCompositeShader == nullptr) {
        return;
    }
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSceneCompositeTextureView(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.sceneCompositeTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SceneComposite";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    m_sceneCompositeShader->use();

    // Sampler assignments
    m_sceneCompositeShader->setInt("uSceneLightingTex", 0);
    m_sceneCompositeShader->setInt("uReflectionTex", 1);
    m_sceneCompositeShader->setInt("uCloudTex", 2);
    m_sceneCompositeShader->setInt("uDepthTex", 3);
    m_sceneCompositeShader->setInt("uNormalAoTex", 4);
    m_sceneCompositeShader->setInt("uMaterialTex", 5);
    m_sceneCompositeShader->setInt("uMaterialAuxTex", 6);
    m_sceneCompositeShader->setInt("uVelocityTex", 7);
    m_sceneCompositeShader->setInt("uHistorySceneTex", 8);
    m_sceneCompositeShader->setInt("uHistoryDepthTex", 9);
    m_sceneCompositeShader->setInt("uHistoryReflectionTex", 10);
    m_sceneCompositeShader->setInt("uHistoryCloudTex", 11);
    m_sceneCompositeShader->setInt("uSkyCaptureTex", 12);
    m_sceneCompositeShader->setInt("uAlbedoTex", 13);
    m_sceneCompositeShader->setInt("uAtmosphereLut", 14);
    m_sceneCompositeShader->setInt("uSsgiTex", 15);
    m_sceneCompositeShader->setInt("uVoxelGiTex", 16);

    // Camera / TAA
    m_sceneCompositeShader->setMat4("uViewProj",
        settings.taa.enabled ? ctx.camera.jitteredViewProj : ctx.camera.viewProj);
    m_sceneCompositeShader->setMat4("uInvViewProj",
        settings.taa.enabled ? ctx.camera.jitteredInvViewProj : ctx.camera.invViewProj);
    m_sceneCompositeShader->setMat4("uPreviousViewProj",
        settings.taa.enabled ? ctx.previousJitteredViewProj : ctx.previousViewProj);
    m_sceneCompositeShader->setMat4("uPreviousInvViewProj", ctx.previousInvViewProj);
    m_sceneCompositeShader->setVec2("uJitter", ctx.jitter);
    m_sceneCompositeShader->setVec2("uPreviousJitter", ctx.prevJitter);
    m_sceneCompositeShader->setInt("uFrameIndex", static_cast<int>(ctx.frameIndex & 0x7fffffffULL));
    m_sceneCompositeShader->setFloat("uTime", ctx.shaderTime);

    // Sky / atmosphere
    m_sceneCompositeShader->setFloat("uWeatherWetness", ctx.weather.wetness);
    m_sceneCompositeShader->setVec3("uCameraPos", ctx.camera.position);
    m_sceneCompositeShader->setVec3("uSunDirection", ctx.skyColors.sunDirection);
    m_sceneCompositeShader->setVec3("uMoonDirection", ctx.skyColors.moonDirection);
    m_sceneCompositeShader->setFloat("uSunVisibility", ctx.skyColors.sunVisibility);
    m_sceneCompositeShader->setFloat("uSkyIntensity", ctx.skyIntensity);
    m_sceneCompositeShader->setFloat("uMoonVisibility", ctx.skyColors.moonVisibility);
    m_sceneCompositeShader->setFloat("uMoonPhaseAngle", ctx.skyColors.moonPhaseAngle);
    m_sceneCompositeShader->setFloat("uSkyWetness", ctx.weather.skyWetness);

    // Composite strengths
    m_sceneCompositeShader->setFloat("uCloudCompositeStrength", settings.cloud.sceneCloudCompositeStrength);
    m_sceneCompositeShader->setFloat("uReflectionCompositeStrength", settings.reflection.sceneReflectionCompositeStrength);
    m_sceneCompositeShader->setInt("uSsgiEnabled", settings.ssgi.enabled ? 1 : 0);
    const bool voxelGiEnabled = settings.voxelGi.enabled &&
                                voxelGiClipmap != nullptr &&
                                voxelGiClipmap->valid();
    m_sceneCompositeShader->setInt("uVoxelGiEnabled", voxelGiEnabled ? 1 : 0);
    m_sceneCompositeShader->setInt("uVoxelGiDebugEnabled", settings.voxelGi.debugEnabled ? 1 : 0);
    m_sceneCompositeShader->setFloat("uVoxelGiStrength", settings.voxelGi.strength);
    m_sceneCompositeShader->setFloat("uVoxelGiVoxelSize", voxelGiEnabled ? voxelGiClipmap->voxelSize() : 1.0f);
    m_sceneCompositeShader->setFloat("uVoxelGiResolution", voxelGiEnabled ? static_cast<float>(voxelGiClipmap->resolution()) : 1.0f);
    m_sceneCompositeShader->setFloat("uVoxelGiMipCount", voxelGiEnabled ? static_cast<float>(voxelGiClipmap->mipLevels()) : 1.0f);
    m_sceneCompositeShader->setFloat("uVoxelGiNormalBias", settings.voxelGi.normalBias);
    m_sceneCompositeShader->setFloat("uVoxelGiSampleDistance", settings.voxelGi.sampleDistance);
    m_sceneCompositeShader->setFloat("uVoxelGiTraceDistance", settings.voxelGi.traceDistance);
    m_sceneCompositeShader->setFloat("uVoxelGiConeAperture", settings.voxelGi.coneAperture);
    m_sceneCompositeShader->setFloat("uVoxelGiOccupancyScale", settings.voxelGi.occupancyScale);
    m_sceneCompositeShader->setFloat("uVoxelGiOcclusionStrength", settings.voxelGi.occlusionStrength);
    m_sceneCompositeShader->setFloat("uVoxelGiReceiverShadowBoost", settings.voxelGi.receiverShadowBoost);
    m_sceneCompositeShader->setInt("uVoxelGiConeSteps", settings.voxelGi.coneSteps);
    m_sceneCompositeShader->setVec3("uVoxelGiOrigin", voxelGiEnabled ? voxelGiClipmap->origin() : glm::vec3(0.0f));
    m_sceneCompositeShader->setInt("uReflectionDebugMode", settings.debug.reflectionDebugMode);

    // State
    m_sceneCompositeShader->setInt("uIsEyeInWater", ctx.eyeInWater ? 1 : 0);
    m_sceneCompositeShader->setVec3("uWaterAbsorption", glm::vec3(0.4f, 0.14f, 0.08f));
    m_sceneCompositeShader->setFloat("uBlindness", 0.0f);
    m_sceneCompositeShader->setFloat("uDarknessFactor", 0.0f);

    // Texture bindings
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.sceneLightingTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.reflectionTextureHandle()));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.cloudTextureHandle()));
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.normalAoTextureHandle()));
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.materialTextureHandle()));
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.materialAuxTextureHandle()));
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.velocityTextureHandle()));
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.historySceneTexturePrevHandle()));
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.historyDepthTexturePrevHandle()));
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.historyReflectionTexturePrevHandle()));
    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.historyCloudTexturePrevHandle()));
    glActiveTexture(GL_TEXTURE12);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.skyCaptureTextureHandle()));
    glActiveTexture(GL_TEXTURE13);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.albedoTextureHandle()));
    glActiveTexture(GL_TEXTURE14);
    glBindTexture(GL_TEXTURE_3D, renderer::rhi::gl::textureId(targets.atmosphereLutTextureHandle()));
    glActiveTexture(GL_TEXTURE15);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.ssgiTextureHandle()));
    glActiveTexture(GL_TEXTURE16);
    glBindTexture(GL_TEXTURE_3D,
                  voxelGiEnabled
                      ? renderer::rhi::gl::textureId(voxelGiClipmap->textureHandle())
                      : 0);

    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_sceneCompositeShader);

    glActiveTexture(GL_TEXTURE16);
    glBindTexture(GL_TEXTURE_3D, 0);
    glActiveTexture(GL_TEXTURE15);
    glBindTexture(GL_TEXTURE_2D, 0);
    for (int i = 13; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE14);
    glBindTexture(GL_TEXTURE_3D, 0);
    glActiveTexture(GL_TEXTURE0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}
