#include "TemporalResolvePass.h"
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
#include <algorithm>

void TemporalResolvePass::init(ResourceMgr& resourceMgr) {
    m_temporalResolveShader = resourceMgr.getShader("temporal_resolve");
}

void TemporalResolvePass::shutdown() {
    m_temporalResolveShader = nullptr;
}

void TemporalResolvePass::execute(const FrameContext& ctx, const RenderSettings& settings,
                                   DeferredRenderTargets& targets) {
    if (m_temporalResolveShader == nullptr) return;

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSceneResolvedTextureView(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.sceneResolvedTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "TemporalResolve";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    // Copy current scene to scratch so TAA reads from TemporalCurrent + HistoryPrev.
    targets.copySceneResolvedToTemporalCurrent(rhiDevice);
    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    m_temporalResolveShader->use();
    m_temporalResolveShader->setInt("uCurrentTex", 0);
    m_temporalResolveShader->setInt("uHistoryTex", 1);
    m_temporalResolveShader->setInt("uVelocityTex", 2);
    m_temporalResolveShader->setInt("uDepthTex", 3);
    m_temporalResolveShader->setInt("uMaterialAuxTex", 4);
    m_temporalResolveShader->setVec2("uScreenSize",
        glm::vec2(static_cast<float>(std::max(1, targets.width())),
                   static_cast<float>(std::max(1, targets.height()))));
    m_temporalResolveShader->setVec2("uJitter", ctx.jitter);
    m_temporalResolveShader->setFloat("uSurfaceWetness", ctx.weather.surfaceWetness);
    m_temporalResolveShader->setInt("uRainWetSurfacesEnabled", settings.weather.rainLinesEnabled ? 1 : 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.temporalCurrentTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.historySceneTexturePrevHandle()));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.velocityTextureHandle()));
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.materialAuxTextureHandle()));

    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_temporalResolveShader);

    for (int i = 4; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}
