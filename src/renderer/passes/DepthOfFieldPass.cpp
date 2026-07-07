#include "DepthOfFieldPass.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/Shader.h"
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

    targets.copySceneResolvedToHistory();
    targets.bindSceneResolved();
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
    glBindTexture(GL_TEXTURE_2D, targets.historySceneTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, targets.depthTexture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer::rhi::gl::textureId(m_noiseTexture));
    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_dofShader);

    for (int i = 2; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
