#include "GBufferPass.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/Shader.h"
#include "../core/RenderSettings.h"
#include "../renderers/HumanoidRenderer.h"
#include "../renderers/DropRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../ecs/GameplayRegistry.h"
#include "../../world/IWorldView.h"

void GBufferPass::init(ResourceMgr& resourceMgr) {
    m_entityGBufferShader = resourceMgr.getShader("entity_gbuffer");
}

void GBufferPass::shutdown() {
    m_entityGBufferShader = nullptr;
}

void GBufferPass::executeEntities(const IWorldView& worldView, const FrameContext& ctx,
                                   const RenderSettings& settings,
                                   DeferredRenderTargets& targets,
                                   HumanoidRenderer* humanoidRenderer,
                                   ecs::GameplayRegistry* gameplayRegistry,
                                   bool renderLocalPlayerModel) {
    if (humanoidRenderer == nullptr || gameplayRegistry == nullptr ||
        m_entityGBufferShader == nullptr) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // Per-object velocity: attach RG16F texture as GL_COLOR_ATTACHMENT5
    targets.clearPerObjectVelocity();
    targets.attachPerObjectVelocityToGBuffer();

    // Use the same projection flavor as terrain so depth and velocity agree.
    const glm::mat4& viewProj = settings.taa.enabled ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    const glm::mat4& previousViewProj = settings.taa.enabled ? ctx.previousJitteredViewProj : ctx.previousViewProj;

    const HumanoidRenderer::RenderMode mode = renderLocalPlayerModel
        ? HumanoidRenderer::kRenderAll
        : HumanoidRenderer::kRenderMobsOnly;
    humanoidRenderer->renderToGBuffer(worldView, *gameplayRegistry, viewProj, previousViewProj, mode);

    glBindVertexArray(0);
}

void GBufferPass::executeDrops(const IWorldView& worldView, const FrameContext& ctx,
                                const RenderSettings& settings,
                                DeferredRenderTargets& targets,
                                DropRenderer* dropRenderer, DropSystem* dropSystem) {
    if (dropRenderer == nullptr || dropSystem == nullptr) {
        targets.detachPerObjectVelocityFromGBuffer();
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    // Cross-shaped block drops emit single-sided quads without back-faces
    glDisable(GL_CULL_FACE);

    const glm::mat4& viewProj = settings.taa.enabled ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    const glm::mat4& previousViewProj = settings.taa.enabled ? ctx.previousJitteredViewProj : ctx.previousViewProj;
    dropRenderer->renderToGBuffer(worldView, *dropSystem, viewProj, previousViewProj, ctx.animationTime);

    // Detach per-object velocity from GBuffer FBO and restore 5-target MRT.
    targets.detachPerObjectVelocityFromGBuffer();

    glBindVertexArray(0);
}
