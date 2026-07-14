#include "GBufferPass.h"
#include "../core/RenderScene.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/RenderSettings.h"
#include "../debug/RenderDebugService.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiCommandListPool.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../renderers/BlockEntityRenderer.h"
#include "../renderers/HumanoidRenderer.h"
#include "../renderers/DropRenderer.h"
#include "../renderers/FallingBlockRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../ecs/GameplayRegistry.h"
#include "../../world/IWorldView.h"


#include <algorithm>
#include <cstdlib>

namespace {
void setLoadAttachment(RhiColorAttachment& attachment, const RhiTextureViewHandle view) {
    attachment.view = view;
    attachment.loadOp = RhiLoadOp::Load;
    attachment.storeOp = RhiStoreOp::Store;
}

RhiCommandList* beginObjectGBufferRendering(RhiDevice& rhiDevice,
                                            RhiCommandListPool& commandListPool,
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

    RhiCommandList* const commandListStorage =
        commandListPool.acquire(RhiCommandListType::Graphics);
    if (commandListStorage == nullptr ||
        !commandListStorage->begin({debugName, RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& commandList = *commandListStorage;
    targets.transitionTexture(commandList, targets.albedoTextureHandle(),
                              RhiResourceState::RenderTarget);
    targets.transitionTexture(commandList, targets.normalAoTextureHandle(),
                              RhiResourceState::RenderTarget);
    targets.transitionTexture(commandList, targets.voxelLightTextureHandle(),
                              RhiResourceState::RenderTarget);
    targets.transitionTexture(commandList, targets.materialTextureHandle(),
                              RhiResourceState::RenderTarget);
    targets.transitionTexture(commandList, targets.materialAuxTextureHandle(),
                              RhiResourceState::RenderTarget);
    targets.transitionTexture(commandList, targets.perObjectVelocityTextureHandle(),
                              RhiResourceState::RenderTarget);
    targets.transitionTexture(commandList, targets.depthTextureHandle(),
                              RhiResourceState::DepthWrite);
    commandList.beginRendering(renderingInfo);
    return &commandList;
}

void submitCommandList(RhiDevice& rhiDevice,
                       RhiCommandList& commandList,
                       const char* const debugName) {
    if (!commandList.end()) {
        std::abort();
    }
    RhiCommandList* commandLists[] = {&commandList};
    const RhiSubmitInfo submitInfo{debugName, commandLists, 1u};
    if (!rhiDevice.submit(submitInfo)) {
        std::abort();
    }
}

void endObjectGBufferRendering(RhiCommandList& commandList,
                               DeferredRenderTargets& targets) {
    commandList.endRendering();
    targets.transitionTexture(commandList, targets.albedoTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.normalAoTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.voxelLightTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.materialTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.materialAuxTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.perObjectVelocityTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.depthTextureHandle(),
                              RhiResourceState::DepthRead);
}
} // namespace

void GBufferPass::init(ResourceMgr& resourceMgr) {
    (void)resourceMgr;
}

void GBufferPass::shutdown() {
}

void GBufferPass::executeEntities(const IWorldView& worldView, const FrameContext& ctx,
                                   const RenderSettings& settings,
                                   DeferredRenderTargets& targets,
                                   HumanoidRenderer* humanoidRenderer,
                                   ecs::GameplayRegistry* gameplayRegistry,
                                   bool renderLocalPlayerModel) {
    if (humanoidRenderer == nullptr || gameplayRegistry == nullptr) {
        return;
    }

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        ctx.shared->commandListPool == nullptr) {
        return;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const HumanoidRenderer::RenderMode mode = renderLocalPlayerModel
        ? HumanoidRenderer::kRenderAll
        : HumanoidRenderer::kRenderMobsOnly;
    humanoidRenderer->prepareFrame(worldView, *gameplayRegistry, mode);
    RhiCommandList* commandList = beginObjectGBufferRendering(
        rhiDevice, *ctx.shared->commandListPool, targets, "GBuffer.Entities", true);
    if (commandList == nullptr) {
        return;
    }
    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(*commandList, GpuTimerPass::GBuffer)
        : GpuTimerSegmentToken{};

    // Rasterize with the same projection flavor as terrain, but reproject
    // moving objects into the resolved history grid (raw previous view-proj).
    const glm::mat4& viewProj = usesTemporalProjectionJitter(
        settings.upscale.type, settings.taa.enabled)
        ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    const glm::mat4& previousViewProj = ctx.previousViewProj;

    humanoidRenderer->renderPreparedToGBuffer(*commandList, viewProj, previousViewProj);
    humanoidRenderer->finishFrame();

    endObjectGBufferRendering(*commandList, targets);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(*commandList, gpuTimer);
    }
    submitCommandList(rhiDevice, *commandList, "GBuffer.Entities.Submit");
}

void GBufferPass::executeBlockEntities(const IWorldView& worldView, const FrameContext& ctx,
                                       const RenderSettings& settings,
                                       DeferredRenderTargets& targets,
                                       BlockEntityRenderer* blockEntityRenderer) {
    if (blockEntityRenderer == nullptr) {
        return;
    }

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        ctx.shared->commandListPool == nullptr) {
        return;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    blockEntityRenderer->prepareFrame(worldView);
    RhiCommandList* const preparationCommandListStorage =
        ctx.shared->commandListPool->acquire(RhiCommandListType::Graphics);
    if (preparationCommandListStorage == nullptr ||
        !preparationCommandListStorage->begin(
            {"GBuffer.BlockEntities.Preparation.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& preparationCommandList = *preparationCommandListStorage;
    const GpuTimerSegmentToken preparationGpuTimer = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(preparationCommandList, GpuTimerPass::GBuffer)
        : GpuTimerSegmentToken{};
    if (!blockEntityRenderer->prepareGBuffer(preparationCommandList)) {
        if (ctx.debugService != nullptr) {
            ctx.debugService->cancelGpuTimer(preparationGpuTimer);
        }
        submitCommandList(rhiDevice, preparationCommandList,
                          "GBuffer.BlockEntities.Preparation.Submit");
        return;
    }
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(preparationCommandList, preparationGpuTimer);
    }
    submitCommandList(rhiDevice, preparationCommandList,
                      "GBuffer.BlockEntities.Preparation.Submit");
    RhiCommandList* commandList = beginObjectGBufferRendering(
        rhiDevice, *ctx.shared->commandListPool, targets, "GBuffer.BlockEntities", false);
    if (commandList == nullptr) {
        return;
    }
    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(*commandList, GpuTimerPass::GBuffer)
        : GpuTimerSegmentToken{};

    const glm::mat4& viewProj = usesTemporalProjectionJitter(
        settings.upscale.type, settings.taa.enabled)
        ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    blockEntityRenderer->renderToGBuffer(*commandList, viewProj);

    endObjectGBufferRendering(*commandList, targets);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(*commandList, gpuTimer);
    }
    submitCommandList(rhiDevice, *commandList, "GBuffer.BlockEntities.Submit");
}

void GBufferPass::executeDrops(const IWorldView& worldView, const FrameContext& ctx,
                                const RenderSettings& settings,
                                DeferredRenderTargets& targets,
                                DropRenderer* dropRenderer, DropSystem* dropSystem) {
    if (dropRenderer == nullptr || dropSystem == nullptr) {
        return;
    }

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        ctx.shared->commandListPool == nullptr) {
        return;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    dropRenderer->prepareFrame(worldView, *dropSystem);
    RhiCommandList* commandList = beginObjectGBufferRendering(
        rhiDevice, *ctx.shared->commandListPool, targets, "GBuffer.Drops", false);
    if (commandList == nullptr) {
        return;
    }
    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(*commandList, GpuTimerPass::GBuffer)
        : GpuTimerSegmentToken{};

    // Match entity velocity: current rasterization may be jittered, while
    // previous positions target the resolved history grid.
    const glm::mat4& viewProj = usesTemporalProjectionJitter(
        settings.upscale.type, settings.taa.enabled)
        ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    const glm::mat4& previousViewProj = ctx.previousViewProj;
    dropRenderer->renderItemsToGBuffer(*commandList, viewProj, previousViewProj);
    dropRenderer->renderBlocksToGBuffer(*commandList, viewProj, previousViewProj, ctx.animationTime);
    dropRenderer->finishGBufferFrame();

    endObjectGBufferRendering(*commandList, targets);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(*commandList, gpuTimer);
    }
    submitCommandList(rhiDevice, *commandList, "GBuffer.Drops.Submit");
}

void GBufferPass::executeFallingBlocks(const IWorldView& worldView, const FrameContext& ctx,
                                       const RenderSettings& settings,
                                       DeferredRenderTargets& targets,
                                       FallingBlockRenderer* fallingBlockRenderer,
                                       ecs::GameplayRegistry* gameplayRegistry) {
    if (fallingBlockRenderer == nullptr || gameplayRegistry == nullptr) {
        return;
    }

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        ctx.shared->commandListPool == nullptr) {
        return;
    }

    fallingBlockRenderer->prepareFrame(worldView, *gameplayRegistry);
    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    RhiCommandList* commandList = beginObjectGBufferRendering(
        rhiDevice, *ctx.shared->commandListPool, targets, "GBuffer.FallingBlocks", false);
    if (commandList == nullptr) {
        return;
    }
    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(*commandList, GpuTimerPass::GBuffer)
        : GpuTimerSegmentToken{};

    const glm::mat4& viewProj = usesTemporalProjectionJitter(
        settings.upscale.type, settings.taa.enabled)
        ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    const glm::mat4& previousViewProj = ctx.previousViewProj;
    fallingBlockRenderer->renderToGBuffer(*commandList, viewProj, previousViewProj, ctx.animationTime);

    endObjectGBufferRendering(*commandList, targets);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(*commandList, gpuTimer);
    }
    submitCommandList(rhiDevice, *commandList, "GBuffer.FallingBlocks.Submit");
}
