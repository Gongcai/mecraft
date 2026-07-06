#include "MotionBlurPass.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/Shader.h"
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

    // Save current SceneResolved to history so we can read it while writing SceneResolved
    targets.copySceneResolvedToHistory();

    targets.bindSceneResolved();
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
    glBindTexture(GL_TEXTURE_2D, targets.historySceneTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, targets.velocityTexture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, targets.depthTexture());
    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_motionBlurShader);

    for (int i = 2; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
