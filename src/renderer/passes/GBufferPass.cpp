#include "GBufferPass.h"
#include "../core/RenderScene.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/Shader.h"
#include "../core/RenderSettings.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../renderers/BlockEntityRenderer.h"
#include "../renderers/HumanoidRenderer.h"
#include "../renderers/DropRenderer.h"
#include "../renderers/FallingBlockRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../ecs/GameplayRegistry.h"
#include "../../world/IWorldView.h"

#include <glad/glad.h>

#include <algorithm>

namespace {
void setLoadAttachment(RhiColorAttachment& attachment, const RhiTextureViewHandle view) {
    attachment.view = view;
    attachment.loadOp = RhiLoadOp::Load;
    attachment.storeOp = RhiStoreOp::Store;
}

RhiCommandList* beginObjectGBufferRendering(RhiDevice& rhiDevice,
                                            DeferredRenderTargets& targets,
                                            const char* debugName,
                                            const bool clearPerObjectVelocity) {
    if (!targets.ensureGBufferTextureViews(rhiDevice) ||
        !targets.ensurePerObjectVelocityTextureView(rhiDevice)) {
        return nullptr;
    }

    RhiColorAttachment attachments[6];
    setLoadAttachment(attachments[0], targets.albedoTextureViewHandle());
    setLoadAttachment(attachments[1], targets.normalAoTextureViewHandle());
    setLoadAttachment(attachments[2], targets.voxelLightTextureViewHandle());
    setLoadAttachment(attachments[3], targets.materialTextureViewHandle());
    setLoadAttachment(attachments[4], targets.materialAuxTextureViewHandle());
    attachments[5].view = targets.perObjectVelocityTextureViewHandle();
    attachments[5].loadOp = clearPerObjectVelocity ? RhiLoadOp::Clear : RhiLoadOp::Load;
    attachments[5].storeOp = RhiStoreOp::Store;
    attachments[5].clearColor[0] = 0.0f;
    attachments[5].clearColor[1] = 0.0f;
    attachments[5].clearColor[2] = 0.0f;
    attachments[5].clearColor[3] = 0.0f;

    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = targets.depthTextureViewHandle();
    depthAttachment.depthLoadOp = RhiLoadOp::Load;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = debugName;
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = attachments;
    renderingInfo.colorAttachmentCount = 6u;
    renderingInfo.depthStencilAttachment = &depthAttachment;

    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);
    return &commandList;
}
} // namespace

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

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    RhiCommandList* commandList = beginObjectGBufferRendering(rhiDevice, targets, "GBuffer.Entities", true);
    if (commandList == nullptr) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // Rasterize with the same projection flavor as terrain, but reproject
    // moving objects into the resolved history grid (raw previous view-proj).
    const glm::mat4& viewProj = settings.taa.enabled ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    const glm::mat4& previousViewProj = ctx.previousViewProj;

    const HumanoidRenderer::RenderMode mode = renderLocalPlayerModel
        ? HumanoidRenderer::kRenderAll
        : HumanoidRenderer::kRenderMobsOnly;
    humanoidRenderer->renderToGBuffer(worldView, *gameplayRegistry, viewProj, previousViewProj, mode);

    commandList->endRendering();
    rhiDevice.submitFrame(*commandList);
}

void GBufferPass::executeBlockEntities(const IWorldView& worldView, const FrameContext& ctx,
                                       const RenderSettings& settings,
                                       DeferredRenderTargets& targets,
                                       BlockEntityRenderer* blockEntityRenderer) {
    if (blockEntityRenderer == nullptr) {
        return;
    }

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    RhiCommandList* commandList = beginObjectGBufferRendering(rhiDevice, targets, "GBuffer.BlockEntities", false);
    if (commandList == nullptr) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    const glm::mat4& viewProj = settings.taa.enabled ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    const glm::mat4& previousViewProj = ctx.previousViewProj;
    blockEntityRenderer->renderToGBuffer(worldView, viewProj, previousViewProj);

    commandList->endRendering();
    rhiDevice.submitFrame(*commandList);
}

void GBufferPass::executeDrops(const IWorldView& worldView, const FrameContext& ctx,
                                const RenderSettings& settings,
                                DeferredRenderTargets& targets,
                                DropRenderer* dropRenderer, DropSystem* dropSystem) {
    if (dropRenderer == nullptr || dropSystem == nullptr) {
        return;
    }

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    RhiCommandList* commandList = beginObjectGBufferRendering(rhiDevice, targets, "GBuffer.Drops", false);
    if (commandList == nullptr) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    // Cross-shaped block drops emit single-sided quads without back-faces
    glDisable(GL_CULL_FACE);

    // Match entity velocity: current rasterization may be jittered, while
    // previous positions target the resolved history grid.
    const glm::mat4& viewProj = settings.taa.enabled ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    const glm::mat4& previousViewProj = ctx.previousViewProj;
    dropRenderer->renderToGBuffer(worldView, *dropSystem, viewProj, previousViewProj, ctx.animationTime);

    commandList->endRendering();
    rhiDevice.submitFrame(*commandList);
}

void GBufferPass::executeFallingBlocks(const IWorldView& worldView, const FrameContext& ctx,
                                       const RenderSettings& settings,
                                       DeferredRenderTargets& targets,
                                       FallingBlockRenderer* fallingBlockRenderer,
                                       ecs::GameplayRegistry* gameplayRegistry) {
    if (fallingBlockRenderer == nullptr || gameplayRegistry == nullptr) {
        return;
    }

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    RhiCommandList* commandList = beginObjectGBufferRendering(rhiDevice, targets, "GBuffer.FallingBlocks", false);
    if (commandList == nullptr) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    const glm::mat4& viewProj = settings.taa.enabled ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    const glm::mat4& previousViewProj = ctx.previousViewProj;
    fallingBlockRenderer->renderToGBuffer(worldView, *gameplayRegistry, viewProj, previousViewProj, ctx.animationTime);

    glBindVertexArray(0);
    commandList->endRendering();
    rhiDevice.submitFrame(*commandList);
}
