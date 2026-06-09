#include "VelocityPass.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/Shader.h"
#include "../../resource/ResourceMgr.h"

#include <glm/glm.hpp>
#include <algorithm>

void VelocityPass::init(ResourceMgr& resourceMgr) {
    m_velocityShader = resourceMgr.getShader("velocity_resolve");
}

void VelocityPass::shutdown() {
    m_velocityShader = nullptr;
}

void VelocityPass::execute(const FrameContext& ctx, const RenderSettings& settings,
                            DeferredRenderTargets& targets) {
    if (m_velocityShader == nullptr) return;

    targets.bindVelocity();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_velocityShader->use();
    m_velocityShader->setInt("uDepthTex", 0);
    m_velocityShader->setInt("uPerObjectVelocityTex", 1);
    // Velocity must reproject into the resolved history grid, not the previous
    // frame's jittered sample grid. Keep the current depth "raw" relative to
    // the non-jittered inverse, matching DerivativeMain's Temporal.frag path.
    m_velocityShader->setMat4("uInvViewProj", ctx.camera.invViewProj);
    m_velocityShader->setMat4("uPreviousViewProj", ctx.previousViewProj);
    m_velocityShader->setVec2("uScreenSize",
        glm::vec2(static_cast<float>(std::max(1, targets.width())),
                   static_cast<float>(std::max(1, targets.height()))));
    m_velocityShader->setInt("uForceZeroVelocity", settings.taa.forceZeroVelocity ? 1 : 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, targets.depthTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, targets.perObjectVelocityTexture());
    RenderPass::renderFullscreen(targets.fullscreenVao(), *m_velocityShader);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
