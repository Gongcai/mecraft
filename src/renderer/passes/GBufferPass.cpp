#include "GBufferPass.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/Shader.h"
#include "../renderers/HumanoidRenderer.h"
#include "../renderers/DropRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../ecs/GameplayRegistry.h"
#include "../../world/World.h"

void GBufferPass::init(ResourceMgr& resourceMgr) {
    m_entityGBufferShader = resourceMgr.getShader("entity_gbuffer");
}

void GBufferPass::shutdown() {
    m_entityGBufferShader = nullptr;
}

void GBufferPass::executeEntities(const World& world, const FrameContext& ctx,
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

    // Use jittered view-projection for TAA consistency with terrain.
    const glm::mat4& viewProj = ctx.camera.jitteredViewProj;

    const HumanoidRenderer::RenderMode mode = renderLocalPlayerModel
        ? HumanoidRenderer::kRenderAll
        : HumanoidRenderer::kRenderMobsOnly;
    humanoidRenderer->renderToGBuffer(world, *gameplayRegistry, viewProj, mode);

    glBindVertexArray(0);
}

void GBufferPass::executeDrops(const World& world, const FrameContext& ctx,
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

    const glm::mat4& viewProj = ctx.camera.jitteredViewProj;
    dropRenderer->renderToGBuffer(world, *dropSystem, viewProj, ctx.animationTime);

    // Detach per-object velocity from GBuffer FBO and restore 5-target MRT.
    targets.detachPerObjectVelocityFromGBuffer();

    glBindVertexArray(0);
}
