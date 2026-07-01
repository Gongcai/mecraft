#include "SsgiPass.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/Shader.h"
#include "../../resource/ResourceMgr.h"

#include <algorithm>
#include <glm/glm.hpp>

void SsgiPass::init(ResourceMgr& resourceMgr) {
    m_ssgiShader = resourceMgr.getShader("ssgi");
    m_ssgiUpsampleShader = resourceMgr.getShader("ssgi_upsample");
    m_ssgiTemporalShader = resourceMgr.getShader("ssgi_temporal");
    m_noiseTexture = resourceMgr.getTexture2D("shader_noise2d");
}

void SsgiPass::shutdown() {
    m_ssgiShader = nullptr;
    m_ssgiUpsampleShader = nullptr;
    m_ssgiTemporalShader = nullptr;
    m_noiseTexture = 0;
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
    if (settings.ssgi.temporalEnabled && m_ssgiTemporalShader != nullptr) {
        renderSsgiTemporal(ctx, settings.ssgi, targets);
    }
}

void SsgiPass::renderSsgiBase(const FrameContext& ctx, const RenderSettings& settings,
                              DeferredRenderTargets& targets) {
    if (m_ssgiShader == nullptr) {
        return;
    }
    const SsgiSettings& ssgi = settings.ssgi;

    targets.bindSsgiHalfRes();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

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
    m_ssgiShader->setInt("uSamples", std::clamp(ssgi.samples, 1, 32));
    m_ssgiShader->setInt("uFrameIndex", static_cast<int>(ctx.frameIndex & 0x7fffffffULL));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, targets.sceneLightingTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, targets.albedoTexture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, targets.normalAoTexture());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, targets.materialAuxTexture());
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, targets.depthTexture());
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, m_noiseTexture);

    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_ssgiShader);

    for (int i = 5; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
}

void SsgiPass::renderSsgiUpsample(const FrameContext& ctx, DeferredRenderTargets& targets) {
    if (m_ssgiUpsampleShader == nullptr) {
        return;
    }

    targets.bindSsgi();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const int halfW = std::max(1, targets.width() / 2);
    const int halfH = std::max(1, targets.height() / 2);

    m_ssgiUpsampleShader->use();
    m_ssgiUpsampleShader->setInt("uSsgiHalfResTex", 0);
    m_ssgiUpsampleShader->setInt("uDepthTex", 1);
    m_ssgiUpsampleShader->setVec2("uHalfResSize",
        glm::vec2(static_cast<float>(halfW), static_cast<float>(halfH)));
    m_ssgiUpsampleShader->setFloat("uNear", ctx.camera.nearPlane);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, targets.ssgiHalfResTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, targets.depthTexture());
    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_ssgiUpsampleShader);

    for (int i = 1; i >= 0; --i) {
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
    m_ssgiTemporalShader->setVec2("uScreenSize",
        glm::vec2(static_cast<float>(std::max(1, targets.width())),
                  static_cast<float>(std::max(1, targets.height()))));
    m_ssgiTemporalShader->setFloat("uHistoryWeight", ssgi.historyWeight);
    m_ssgiTemporalShader->setFloat("uNear", ctx.camera.nearPlane);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, targets.ssgiTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, targets.ssgiHistoryTexturePrev());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, targets.velocityTexture());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, targets.depthTexture());

    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_ssgiTemporalShader);

    for (int i = 3; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);

    targets.copySsgiTemporalToHistory();
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
