#include "SsgiPass.h"
#include "../core/RenderScene.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/Shader.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"
#include "../../resource/ResourceMgr.h"

#include <glad/glad.h>

#include <algorithm>
#include <glm/glm.hpp>

void SsgiPass::init(ResourceMgr& resourceMgr) {
    m_ssgiShader = resourceMgr.getShader("ssgi");
    m_ssgiUpsampleShader = resourceMgr.getShader("ssgi_upsample");
    m_ssgiDenoiseShader = resourceMgr.getShader("ssgi_denoise");
    m_ssgiTemporalShader = resourceMgr.getShader("ssgi_temporal");
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
}

void SsgiPass::shutdown() {
    m_ssgiShader = nullptr;
    m_ssgiUpsampleShader = nullptr;
    m_ssgiDenoiseShader = nullptr;
    m_ssgiTemporalShader = nullptr;
    m_noiseTexture = {};
}

void SsgiPass::execute(const FrameContext& ctx, const RenderSettings& settings,
                       DeferredRenderTargets& targets) {
    if (!settings.ssgi.enabled) {
        return;
    }

    renderSsgiBase(ctx, settings, targets);
    if (m_ssgiUpsampleShader != nullptr) {
        renderSsgiUpsample(ctx, targets);
    }
    const bool temporalActive = settings.ssgi.temporalEnabled && m_ssgiTemporalShader != nullptr;
    if (temporalActive) {
        renderSsgiTemporal(ctx, settings.ssgi, targets);
    }
    const bool denoiseActive = settings.ssgi.denoiseEnabled &&
        m_ssgiDenoiseShader != nullptr &&
        std::clamp(settings.ssgi.denoiseIterations, 0, 4) > 0;
    if (denoiseActive) {
        renderSsgiDenoise(ctx, settings.ssgi, targets,
                          temporalActive
                              ? renderer::rhi::gl::textureId(targets.ssgiTemporalTextureHandle())
                              : renderer::rhi::gl::textureId(targets.ssgiTextureHandle()),
                          temporalActive
                              ? renderer::rhi::gl::textureId(targets.ssgiTemporalMomentsTextureHandle())
                              : 0);
    } else if (temporalActive) {
        targets.copySsgiTemporalToSsgi();
    }
}

void SsgiPass::renderSsgiBase(const FrameContext& ctx, const RenderSettings& settings,
                              DeferredRenderTargets& targets) {
    if (m_ssgiShader == nullptr) {
        return;
    }
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsgiHalfResTextureView(*ctx.shared->rhiDevice)) {
        return;
    }
    const SsgiSettings& ssgi = settings.ssgi;

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.ssgiHalfResTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 0.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SsgiHalfRes";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.halfWidth())),
        static_cast<uint32_t>(std::max(1, targets.halfHeight()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    const int halfW = std::max(1, targets.width() / 2);
    const int halfH = std::max(1, targets.height() / 2);

    m_ssgiShader->use();
    m_ssgiShader->setInt("uSceneLightingTex", 0);
    m_ssgiShader->setInt("uAlbedoTex", 1);
    m_ssgiShader->setInt("uNormalAoTex", 2);
    m_ssgiShader->setInt("uMaterialAuxTex", 3);
    m_ssgiShader->setInt("uDepthTex", 4);
    m_ssgiShader->setInt("uNoiseTex", 5);
    m_ssgiShader->setMat4("uViewProj",
        settings.taa.enabled ? ctx.camera.jitteredViewProj : ctx.camera.viewProj);
    m_ssgiShader->setMat4("uInvViewProj",
        settings.taa.enabled ? ctx.camera.jitteredInvViewProj : ctx.camera.invViewProj);
    m_ssgiShader->setVec3("uCameraPos", ctx.camera.position);
    m_ssgiShader->setVec2("uHalfResolution",
        glm::vec2(static_cast<float>(halfW), static_cast<float>(halfH)));
    m_ssgiShader->setFloat("uRadius", ssgi.radius);
    m_ssgiShader->setFloat("uStrength", ssgi.strength);
    m_ssgiShader->setFloat("uMaxDistance", ssgi.maxDistance);
    m_ssgiShader->setFloat("uThickness", ssgi.thickness);
    m_ssgiShader->setFloat("uRadianceFilterStrength", ssgi.radianceFilterStrength);
    m_ssgiShader->setFloat("uColorBleedStrength", ssgi.colorBleedStrength);
    m_ssgiShader->setInt("uSamples", std::clamp(ssgi.samples, 1, 32));
    m_ssgiShader->setInt("uFrameIndex", static_cast<int>(ctx.frameIndex & 0x7fffffffULL));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.sceneLightingTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.albedoTextureHandle()));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.normalAoTextureHandle()));
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.materialAuxTextureHandle()));
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(m_noiseTexture));

    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_ssgiShader);

    for (int i = 5; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}

void SsgiPass::renderSsgiUpsample(const FrameContext& ctx, DeferredRenderTargets& targets) {
    if (m_ssgiUpsampleShader == nullptr) {
        return;
    }
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsgiTextureView(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.ssgiTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 0.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "Ssgi";
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

    const int halfW = std::max(1, targets.width() / 2);
    const int halfH = std::max(1, targets.height() / 2);

    m_ssgiUpsampleShader->use();
    m_ssgiUpsampleShader->setInt("uSsgiHalfResTex", 0);
    m_ssgiUpsampleShader->setInt("uDepthTex", 1);
    m_ssgiUpsampleShader->setVec2("uHalfResSize",
        glm::vec2(static_cast<float>(halfW), static_cast<float>(halfH)));
    m_ssgiUpsampleShader->setFloat("uNear", ctx.camera.nearPlane);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.ssgiHalfResTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_ssgiUpsampleShader);

    for (int i = 1; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}

void SsgiPass::renderSsgiDenoise(const FrameContext& ctx, const SsgiSettings& ssgi,
                                 DeferredRenderTargets& targets, const uint32_t initialInputTexture,
                                 const uint32_t momentsTexture) {
    if (m_ssgiDenoiseShader == nullptr) {
        return;
    }

    const int iterations = std::clamp(ssgi.denoiseIterations, 0, 4);
    if (iterations <= 0) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    const glm::vec2 screenSize(
        static_cast<float>(std::max(1, targets.width())),
        static_cast<float>(std::max(1, targets.height())));

    m_ssgiDenoiseShader->use();
    m_ssgiDenoiseShader->setInt("uInputTex", 0);
    m_ssgiDenoiseShader->setInt("uDepthTex", 1);
    m_ssgiDenoiseShader->setInt("uNormalAoTex", 2);
    m_ssgiDenoiseShader->setInt("uMomentsTex", 3);
    m_ssgiDenoiseShader->setInt("uMomentsAvailable", momentsTexture != 0 ? 1 : 0);
    m_ssgiDenoiseShader->setVec2("uScreenSize", screenSize);
    m_ssgiDenoiseShader->setFloat("uNear", ctx.camera.nearPlane);
    m_ssgiDenoiseShader->setFloat("uStrength", ssgi.denoiseStrength);

    int outputSlot = 0;
    for (int i = 0; i < iterations; ++i) {
        outputSlot = i & 1;
        targets.bindSsgiDenoise(outputSlot);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        const GLuint passInputTexture = (i == 0)
            ? initialInputTexture
            : renderer::rhi::gl::textureId(targets.ssgiDenoiseTextureHandle(1 - outputSlot));
        m_ssgiDenoiseShader->setFloat("uStepWidth", static_cast<float>(1 << i));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, passInputTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.normalAoTextureHandle()));
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, momentsTexture);

        RenderPass::renderFullscreen(targets.fullscreenVao(), *m_ssgiDenoiseShader);
    }

    targets.copySsgiDenoiseToSsgi(outputSlot);

    for (int i = 3; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
}

void SsgiPass::renderSsgiTemporal(const FrameContext& ctx, const SsgiSettings& ssgi,
                                  DeferredRenderTargets& targets) {
    if (m_ssgiTemporalShader == nullptr) {
        return;
    }

    targets.bindSsgiTemporal();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_ssgiTemporalShader->use();
    m_ssgiTemporalShader->setInt("uCurrentTex", 0);
    m_ssgiTemporalShader->setInt("uHistoryTex", 1);
    m_ssgiTemporalShader->setInt("uVelocityTex", 2);
    m_ssgiTemporalShader->setInt("uDepthTex", 3);
    m_ssgiTemporalShader->setInt("uNormalAoTex", 4);
    m_ssgiTemporalShader->setInt("uHistoryDepthTex", 5);
    m_ssgiTemporalShader->setInt("uHistoryMomentsTex", 6);
    m_ssgiTemporalShader->setVec2("uScreenSize",
        glm::vec2(static_cast<float>(std::max(1, targets.width())),
                  static_cast<float>(std::max(1, targets.height()))));
    m_ssgiTemporalShader->setFloat("uHistoryWeight", ssgi.historyWeight);
    m_ssgiTemporalShader->setFloat("uNear", ctx.camera.nearPlane);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.ssgiTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.ssgiHistoryTexturePrevHandle()));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.velocityTextureHandle()));
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.normalAoTextureHandle()));
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.historyDepthTexturePrevHandle()));
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.ssgiMomentsHistoryTexturePrevHandle()));

    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_ssgiTemporalShader);

    for (int i = 6; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);

    targets.copySsgiTemporalToHistory();
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
