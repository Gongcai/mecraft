#include "MotionBlurPass.h"
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

void MotionBlurPass::init(ResourceMgr& resourceMgr) {
    m_motionBlurShader = resourceMgr.getShader("motion_blur");
}

void MotionBlurPass::shutdown() {
    m_motionBlurShader = nullptr;
}

void MotionBlurPass::execute(const FrameContext& ctx, const RenderSettings& settings,
                              DeferredRenderTargets& targets) {
    if (m_motionBlurShader == nullptr) return;

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSceneResolvedTextureView(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.sceneResolvedTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "MotionBlur";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    // Save current SceneResolved to history so we can read it while writing SceneResolved.
    targets.copySceneResolvedToHistory(rhiDevice);
    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    m_motionBlurShader->use();
    m_motionBlurShader->setInt("uSceneTex", 0);
    m_motionBlurShader->setInt("uVelocityTex", 1);
    m_motionBlurShader->setInt("uDepthTex", 2);
    m_motionBlurShader->setFloat("uStrength", settings.postProcess.motionBlurStrength);
    m_motionBlurShader->setInt("uSamples", settings.postProcess.motionBlurSamples);
    m_motionBlurShader->setVec2("uScreenSize",
        glm::vec2(static_cast<float>(std::max(1, targets.width())),
                   static_cast<float>(std::max(1, targets.height()))));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.historySceneTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.velocityTextureHandle()));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_motionBlurShader);

    for (int i = 2; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}
