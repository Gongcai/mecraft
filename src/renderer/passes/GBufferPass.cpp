#include "GBufferPass.h"
#include "../core/RenderScene.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/RenderSettings.h"
#include "../debug/RenderDebugService.h"
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


#include <algorithm>

namespace {
void setLoadAttachment(RhiColorAttachment& attachment, const RhiTextureViewHandle view) {
    attachment.view = view;
    attachment.loadOp = RhiLoadOp::Load;
    attachment.storeOp = RhiStoreOp::Store;
}

bool beginObjectGBufferRendering(RhiDevice& rhiDevice,
                                 RhiCommandList& commandList,
                                 DeferredRenderTargets& targets,
                                 const char* debugName,
                                 const bool clearPerObjectVelocity) {
    if (!targets.ensureGBufferTextureViews(rhiDevice) ||
        !targets.ensurePerObjectVelocityTextureView(rhiDevice)) {
        return false;
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

    commandList.beginRendering(renderingInfo);
    return true;
}

void endObjectGBufferRendering(RhiCommandList& commandList) {
    commandList.endRendering();
}
} // namespace

void GBufferPass::init(ResourceMgr& resourceMgr) {
    (void)resourceMgr;
}

void GBufferPass::shutdown() {
}

bool GBufferPass::executeEntities(RhiCommandList& commandList,
                                  const IWorldView& worldView,
                                  const FrameContext& ctx,
                                  const RenderSettings& settings,
                                  DeferredRenderTargets& targets,
                                  HumanoidRenderer* humanoidRenderer,
                                  ecs::GameplayRegistry* gameplayRegistry,
                                  bool renderLocalPlayerModel) {
    if (humanoidRenderer == nullptr || gameplayRegistry == nullptr) {
        return true;
    }

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const HumanoidRenderer::RenderMode mode = renderLocalPlayerModel
        ? HumanoidRenderer::kRenderAll
        : HumanoidRenderer::kRenderMobsOnly;
    humanoidRenderer->prepareFrame(worldView, *gameplayRegistry, mode);
    if (!beginObjectGBufferRendering(
            rhiDevice, commandList, targets, "GBuffer.Entities", false)) {
        return false;
    }
    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::GBuffer)
        : GpuTimerSegmentToken{};

    // Rasterize with the same projection flavor as terrain. The previous
    // matrix carries the current frame's jitter so the per-object velocity
    // subtraction cancels the sub-pixel offset and stores true motion only.
    const glm::mat4& viewProj = usesTemporalProjectionJitter(
        settings.upscale.type, settings.taa.enabled)
        ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    const glm::mat4& previousViewProj = ctx.previousViewProjWithCurrentJitter;

    humanoidRenderer->renderPreparedToGBuffer(commandList, viewProj, previousViewProj);
    humanoidRenderer->finishFrame();

    endObjectGBufferRendering(commandList);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, gpuTimer);
    }
    return true;
}

bool GBufferPass::executeBlockEntities(RhiCommandList& commandList,
                                       const IWorldView& worldView,
                                       const FrameContext& ctx,
                                       const RenderSettings& settings,
                                       DeferredRenderTargets& targets,
                                       BlockEntityRenderer* blockEntityRenderer) {
    if (blockEntityRenderer == nullptr) {
        return true;
    }

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    blockEntityRenderer->prepareFrame(worldView);
    const GpuTimerSegmentToken preparationGpuTimer = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::GBuffer)
        : GpuTimerSegmentToken{};
    if (!blockEntityRenderer->prepareGBuffer(commandList)) {
        if (ctx.debugService != nullptr) {
            ctx.debugService->cancelGpuTimer(preparationGpuTimer);
        }
        return false;
    }
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, preparationGpuTimer);
    }
    if (!beginObjectGBufferRendering(
            rhiDevice, commandList, targets, "GBuffer.BlockEntities", false)) {
        return false;
    }
    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::GBuffer)
        : GpuTimerSegmentToken{};

    const glm::mat4& viewProj = usesTemporalProjectionJitter(
        settings.upscale.type, settings.taa.enabled)
        ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    blockEntityRenderer->renderToGBuffer(commandList, viewProj);

    endObjectGBufferRendering(commandList);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, gpuTimer);
    }
    return true;
}

bool GBufferPass::executeDrops(RhiCommandList& commandList,
                               const IWorldView& worldView,
                               const FrameContext& ctx,
                               const RenderSettings& settings,
                               DeferredRenderTargets& targets,
                               DropRenderer* dropRenderer,
                               DropSystem* dropSystem) {
    if (dropRenderer == nullptr || dropSystem == nullptr) {
        return true;
    }

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    dropRenderer->prepareFrame(worldView, *dropSystem);
    if (!beginObjectGBufferRendering(
            rhiDevice, commandList, targets, "GBuffer.Drops", false)) {
        return false;
    }
    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::GBuffer)
        : GpuTimerSegmentToken{};

    // Match entity velocity: both matrices carry the current frame's jitter
    // so the per-object velocity subtraction stays jitter-free.
    const glm::mat4& viewProj = usesTemporalProjectionJitter(
        settings.upscale.type, settings.taa.enabled)
        ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    const glm::mat4& previousViewProj = ctx.previousViewProjWithCurrentJitter;
    dropRenderer->renderItemsToGBuffer(commandList, viewProj, previousViewProj);
    dropRenderer->renderBlocksToGBuffer(commandList, viewProj, previousViewProj, ctx.animationTime);
    dropRenderer->finishGBufferFrame();

    endObjectGBufferRendering(commandList);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, gpuTimer);
    }
    return true;
}

bool GBufferPass::executeFallingBlocks(RhiCommandList& commandList,
                                       const IWorldView& worldView,
                                       const FrameContext& ctx,
                                       const RenderSettings& settings,
                                       DeferredRenderTargets& targets,
                                       FallingBlockRenderer* fallingBlockRenderer,
                                       ecs::GameplayRegistry* gameplayRegistry) {
    if (fallingBlockRenderer == nullptr || gameplayRegistry == nullptr) {
        return true;
    }

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return false;
    }

    fallingBlockRenderer->prepareFrame(worldView, *gameplayRegistry);
    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!beginObjectGBufferRendering(
            rhiDevice, commandList, targets, "GBuffer.FallingBlocks", false)) {
        return false;
    }
    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::GBuffer)
        : GpuTimerSegmentToken{};

    const glm::mat4& viewProj = usesTemporalProjectionJitter(
        settings.upscale.type, settings.taa.enabled)
        ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    const glm::mat4& previousViewProj = ctx.previousViewProjWithCurrentJitter;
    fallingBlockRenderer->renderToGBuffer(commandList, viewProj, previousViewProj, ctx.animationTime);

    endObjectGBufferRendering(commandList);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, gpuTimer);
    }
    return true;
}
