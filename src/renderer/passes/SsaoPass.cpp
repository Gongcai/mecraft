#include "SsaoPass.h"
#include "../core/RenderScene.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/Shader.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"
#include "../../resource/ResourceMgr.h"

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

void SsaoPass::init(ResourceMgr& resourceMgr) {
    m_ssaoShader = resourceMgr.getShader("ssao");
    m_ssaoFilterShader = resourceMgr.getShader("ssao_filter");
    m_ssaoUpsampleShader = resourceMgr.getShader("ssao_upsample");
    m_ssaoTemporalShader = resourceMgr.getShader("ssao_temporal");
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
}

void SsaoPass::shutdown() {
    m_ssaoShader = nullptr;
    m_ssaoFilterShader = nullptr;
    m_ssaoUpsampleShader = nullptr;
    m_ssaoTemporalShader = nullptr;
    m_noiseTexture = {};
}

void SsaoPass::execute(const FrameContext& ctx, const RenderSettings& settings,
                       DeferredRenderTargets& targets) {
    if (!settings.ssao.enabled) return;

    renderSsaoBase(ctx, settings.ssao, targets);
    if (settings.ssao.filterEnabled && m_ssaoFilterShader != nullptr) {
        renderSsaoFilter(ctx, targets);
    }
    if (m_ssaoUpsampleShader != nullptr) {
        renderSsaoUpsample(ctx, settings.ssao, targets);
    }
    if (settings.ssao.temporalEnabled && m_ssaoTemporalShader != nullptr) {
        renderSsaoTemporal(ctx, settings.ssao, targets);
    }
}

void SsaoPass::renderSsaoBase(const FrameContext& ctx, const SsaoSettings& ssao,
                               DeferredRenderTargets& targets) {
    if (m_ssaoShader == nullptr) return;
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsaoHalfResTextureView(*ctx.shared->rhiDevice)) {
        return;
    }

    // Render SSAO at half resolution for performance
    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.ssaoHalfResTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SsaoHalfRes";
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

    m_ssaoShader->use();
    m_ssaoShader->setInt("uDepthTex", 0);
    m_ssaoShader->setInt("uNormalAoTex", 1);
    m_ssaoShader->setInt("uNoiseTex", 2);
    const glm::mat4& proj = ctx.camera.projection;
    m_ssaoShader->setMat4("uProjection", proj);
    m_ssaoShader->setMat4("uInvProjection", glm::inverse(proj));
    m_ssaoShader->setFloat("uRadius", ssao.radius);
    m_ssaoShader->setFloat("uStrength", ssao.strength);
    // Half-res: invResolution refers to the half-res viewport for UV computation
    const int halfW = std::max(1, targets.width() / 2);
    const int halfH = std::max(1, targets.height() / 2);
    m_ssaoShader->setVec2("uInvResolution", glm::vec2(1.0f / halfW, 1.0f / halfH));
    m_ssaoShader->setInt("uFrameIndex", static_cast<int>(ctx.frameIndex % 64));
    m_ssaoShader->setInt("uSamples", std::clamp(ssao.samples, 1, 64));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.normalAoTextureHandle()));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(m_noiseTexture));
    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_ssaoShader);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}

void SsaoPass::renderSsaoFilter(const FrameContext& ctx, DeferredRenderTargets& targets) {
    if (m_ssaoFilterShader == nullptr) return;
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsaoHalfResFilteredTextureView(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.ssaoHalfResFilteredTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SsaoHalfResFilter";
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

    m_ssaoFilterShader->use();
    m_ssaoFilterShader->setInt("uSsaoTex", 0);
    m_ssaoFilterShader->setInt("uDepthTex", 1);
    m_ssaoFilterShader->setInt("uNormalAoTex", 2);
    m_ssaoFilterShader->setVec2("uScreenSize",
        glm::vec2(static_cast<float>(halfW), static_cast<float>(halfH)));
    m_ssaoFilterShader->setFloat("uNear", ctx.camera.nearPlane);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.ssaoHalfResTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.normalAoTextureHandle()));
    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_ssaoFilterShader);

    for (int i = 2; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}

void SsaoPass::renderSsaoUpsample(const FrameContext& ctx, const SsaoSettings& ssao, DeferredRenderTargets& targets) {
    if (m_ssaoUpsampleShader == nullptr) return;
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsaoFilteredTextureView(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.ssaoFilteredTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SsaoUpsample";
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

    m_ssaoUpsampleShader->use();
    m_ssaoUpsampleShader->setInt("uSsaoHalfResTex", 0);
    m_ssaoUpsampleShader->setInt("uDepthTex", 1);
    m_ssaoUpsampleShader->setVec2("uHalfResSize",
        glm::vec2(static_cast<float>(halfW), static_cast<float>(halfH)));
    m_ssaoUpsampleShader->setFloat("uNear", ctx.camera.nearPlane);

    // Read from filtered half-res if filter is enabled, otherwise raw half-res
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ssao.filterEnabled && m_ssaoFilterShader != nullptr
        ? renderer::rhi::gl::textureId(targets.ssaoHalfResFilteredTextureHandle())
        : renderer::rhi::gl::textureId(targets.ssaoHalfResTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_ssaoUpsampleShader);

    for (int i = 1; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}

void SsaoPass::renderSsaoTemporal(const FrameContext& ctx, const SsaoSettings& ssao, DeferredRenderTargets& targets) {
    if (m_ssaoTemporalShader == nullptr) return;
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsaoTemporalTextureView(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.ssaoTemporalTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SsaoTemporal";
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

    m_ssaoTemporalShader->use();
    m_ssaoTemporalShader->setInt("uCurrentTex", 0);
    m_ssaoTemporalShader->setInt("uHistoryTex", 1);
    m_ssaoTemporalShader->setInt("uVelocityTex", 2);
    m_ssaoTemporalShader->setInt("uDepthTex", 3);
    m_ssaoTemporalShader->setVec2("uScreenSize",
        glm::vec2(static_cast<float>(std::max(1, targets.width())),
                   static_cast<float>(std::max(1, targets.height()))));
    m_ssaoTemporalShader->setFloat("uHistoryWeight", ssao.historyWeight);
    m_ssaoTemporalShader->setFloat("uNear", ctx.camera.nearPlane);

    // Current SSAO: upsampled full-res result
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.ssaoFilteredTextureHandle()));
    // History SSAO (previous frame)
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.ssaoHistoryTexturePrevHandle()));
    // Velocity buffer
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.velocityTextureHandle()));
    // GBuffer depth
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));

    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_ssaoTemporalShader);

    for (int i = 3; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    commandList.endRendering();
    rhiDevice.submitFrame(commandList);

    // Copy temporal result to history[current] for next frame's reprojection
    targets.copySsaoTemporalToHistory();

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
