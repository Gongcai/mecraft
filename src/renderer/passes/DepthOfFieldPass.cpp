#include "DepthOfFieldPass.h"
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

void DepthOfFieldPass::init(ResourceMgr& resourceMgr) {
    m_dofShader = resourceMgr.getShader("dof");
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
}

void DepthOfFieldPass::shutdown() {
    m_dofShader = nullptr;
    m_noiseTexture = {};
}

void DepthOfFieldPass::execute(const FrameContext& ctx, const RenderSettings& settings,
                                DeferredRenderTargets& targets) {
    if (m_dofShader == nullptr) return;

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSceneResolvedTextureView(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.sceneResolvedTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "DepthOfField";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    targets.copySceneResolvedToHistory(rhiDevice);
    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    m_dofShader->use();
    m_dofShader->setInt("uSceneTex", 0);
    m_dofShader->setInt("uDepthTex", 1);
    m_dofShader->setInt("uNoiseTex", 2);
    m_dofShader->setMat4("uProjection", ctx.camera.projection);
    m_dofShader->setMat4("uInvProjection", glm::inverse(ctx.camera.projection));
    m_dofShader->setFloat("uFocusDistance", settings.postProcess.dofFocusDistance);
    m_dofShader->setFloat("uAperture", settings.postProcess.dofAperture);
    m_dofShader->setFloat("uDofIntensity", settings.postProcess.dofIntensity);
    m_dofShader->setFloat("uDofAnamorphic", 1.0f);
    m_dofShader->setVec2("uScreenSize",
        glm::vec2(static_cast<float>(std::max(1, targets.width())),
                   static_cast<float>(std::max(1, targets.height()))));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.historySceneTextureHandle()));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(targets.depthTextureHandle()));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(m_noiseTexture));
    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_dofShader);

    for (int i = 2; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);
}
