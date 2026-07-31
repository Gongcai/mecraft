#include "GBufferPass.h"
#include "../core/RenderScene.h"
#include "../targets/DeferredRenderTargets.h"
#include "../core/RenderSettings.h"
#include "../core/IDeferredGeometryProvider.h"
#include "../debug/RenderDebugService.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../renderers/BlockEntityRenderer.h"
#include "../renderers/StaticMeshRenderer.h"
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

void setClearAttachment(RhiColorAttachment& attachment, const RhiTextureViewHandle view, const float red,
                        const float green, const float blue, const float alpha) {
    attachment.view = view;
    attachment.loadOp = RhiLoadOp::Clear;
    attachment.storeOp = RhiStoreOp::Store;
    attachment.clearColor[0] = red;
    attachment.clearColor[1] = green;
    attachment.clearColor[2] = blue;
    attachment.clearColor[3] = alpha;
}

void setClearAttachmentUint(RhiColorAttachment& attachment, const RhiTextureViewHandle view, const uint32_t red,
                            const uint32_t green) {
    attachment.view = view;
    attachment.loadOp = RhiLoadOp::Clear;
    attachment.storeOp = RhiStoreOp::Store;
    attachment.clearValueType = RhiColorClearValueType::Uint;
    attachment.clearColorUint[0] = red;
    attachment.clearColorUint[1] = green;
    attachment.clearColorUint[2] = 0u;
    attachment.clearColorUint[3] = 0u;
}

bool beginObjectGBufferRendering(RhiDevice& rhiDevice, RhiCommandList& commandList, DeferredRenderTargets& targets,
                                 const char* debugName, const bool clearPerObjectVelocity) {
    if (!targets.ensureGBufferTextureViews(rhiDevice) || !targets.ensurePerObjectVelocityTextureView(rhiDevice)) {
        return false;
    }

    RhiColorAttachment attachments[8];
    setLoadAttachment(attachments[0], targets.albedoTextureViewHandle());
    setLoadAttachment(attachments[1], targets.normalAoTextureViewHandle());
    setLoadAttachment(attachments[2], targets.voxelLightTextureViewHandle());
    setLoadAttachment(attachments[3], targets.materialTextureViewHandle());
    setLoadAttachment(attachments[4], targets.materialAuxTextureViewHandle());
    setLoadAttachment(attachments[5], targets.f0MetallicTextureViewHandle());
    setLoadAttachment(attachments[6], targets.objectMaterialIdTextureViewHandle());
    attachments[7].view = targets.perObjectVelocityTextureViewHandle();
    attachments[7].loadOp = clearPerObjectVelocity ? RhiLoadOp::Clear : RhiLoadOp::Load;
    attachments[7].storeOp = RhiStoreOp::Store;
    attachments[7].clearColor[0] = 0.0f;
    attachments[7].clearColor[1] = 0.0f;
    attachments[7].clearColor[2] = 0.0f;
    attachments[7].clearColor[3] = 0.0f;

    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = targets.depthTextureViewHandle();
    depthAttachment.depthLoadOp = RhiLoadOp::Load;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = debugName;
    renderingInfo.renderArea = {0, 0, static_cast<uint32_t>(std::max(1, targets.width())),
                                static_cast<uint32_t>(std::max(1, targets.height()))};
    renderingInfo.colorAttachments = attachments;
    renderingInfo.colorAttachmentCount = 8u;
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

void GBufferPass::shutdown() {}

bool GBufferPass::executeEntities(RhiCommandList& commandList, const IWorldView& worldView, const FrameContext& ctx,
                                  const RenderSettings& settings, DeferredRenderTargets& targets,
                                  HumanoidRenderer* humanoidRenderer, ecs::GameplayRegistry* gameplayRegistry,
                                  bool renderLocalPlayerModel) {
    if (humanoidRenderer == nullptr || gameplayRegistry == nullptr) {
        return true;
    }

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const HumanoidRenderer::RenderMode mode =
        renderLocalPlayerModel ? HumanoidRenderer::kRenderAll : HumanoidRenderer::kRenderMobsOnly;
    if (!humanoidRenderer->prepareFrame(worldView, *gameplayRegistry, mode)) {
        return false;
    }
    if (!beginObjectGBufferRendering(rhiDevice, commandList, targets, "GBuffer.Entities", false)) {
        return false;
    }
    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
                                              ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::GBuffer)
                                              : GpuTimerSegmentToken{};

    // Rasterize with the same projection flavor as terrain. The previous
    // matrix carries the current frame's jitter so the per-object velocity
    // subtraction cancels the sub-pixel offset and stores true motion only.
    const glm::mat4& viewProj = usesTemporalProjectionJitter(settings.upscale.type, settings.taa.enabled)
                                    ? ctx.camera.jitteredViewProj
                                    : ctx.camera.viewProj;
    const glm::mat4& previousViewProj = ctx.previousViewProjWithCurrentJitter;

    humanoidRenderer->renderPreparedToGBuffer(commandList, viewProj, previousViewProj);
    humanoidRenderer->finishFrame();

    endObjectGBufferRendering(commandList);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, gpuTimer);
    }
    return true;
}

bool GBufferPass::executeBlockEntities(RhiCommandList& commandList, const IWorldView& worldView,
                                       const FrameContext& ctx, const RenderSettings& settings,
                                       DeferredRenderTargets& targets, BlockEntityRenderer* blockEntityRenderer) {
    if (blockEntityRenderer == nullptr) {
        return true;
    }

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!blockEntityRenderer->prepareFrame(worldView)) {
        return false;
    }
    const GpuTimerSegmentToken preparationGpuTimer =
        ctx.debugService != nullptr ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::GBuffer)
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
    if (!beginObjectGBufferRendering(rhiDevice, commandList, targets, "GBuffer.BlockEntities", false)) {
        return false;
    }
    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
                                              ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::GBuffer)
                                              : GpuTimerSegmentToken{};

    const glm::mat4& viewProj = usesTemporalProjectionJitter(settings.upscale.type, settings.taa.enabled)
                                    ? ctx.camera.jitteredViewProj
                                    : ctx.camera.viewProj;
    blockEntityRenderer->renderToGBuffer(commandList, viewProj);

    endObjectGBufferRendering(commandList);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, gpuTimer);
    }
    return true;
}

bool GBufferPass::executeStaticMeshes(RhiCommandList& commandList, const FrameContext& ctx,
                                      const RenderSettings& settings, DeferredRenderTargets& targets,
                                      StaticMeshRenderer* staticMeshRenderer) {
    if (staticMeshRenderer == nullptr) {
        return true;
    }
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return false;
    }
    const glm::mat4& viewProj = usesTemporalProjectionJitter(settings.upscale.type, settings.taa.enabled)
                                    ? ctx.camera.jitteredViewProj
                                    : ctx.camera.viewProj;
    if (!staticMeshRenderer->prepareGBuffer(commandList, viewProj, ctx.previousViewProjWithCurrentJitter, ctx)) {
        return false;
    }
    if (!beginObjectGBufferRendering(*ctx.shared->rhiDevice, commandList, targets, "GBuffer.StaticMeshes", false)) {
        return false;
    }
    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
                                              ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::GBuffer)
                                              : GpuTimerSegmentToken{};
    staticMeshRenderer->renderToGBuffer(commandList);
    endObjectGBufferRendering(commandList);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, gpuTimer);
    }
    return true;
}

bool GBufferPass::executeExternalGeometry(RhiCommandList& commandList, const FrameContext& ctx,
                                          const RenderSettings& settings, DeferredRenderTargets& targets,
                                          IDeferredGeometryProvider& geometryProvider) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice) ||
        !targets.ensurePerObjectVelocityTextureView(*ctx.shared->rhiDevice) ||
        !geometryProvider.prepareGBuffer(commandList, ctx)) {
        return false;
    }

    RhiColorAttachment attachments[8];
    setClearAttachment(attachments[0], targets.albedoTextureViewHandle(), 0.0f, 0.0f, 0.0f, 0.0f);
    setClearAttachment(attachments[1], targets.normalAoTextureViewHandle(), 0.5f, 0.5f, 1.0f, 1.0f);
    setClearAttachment(attachments[2], targets.voxelLightTextureViewHandle(), 0.0f, 0.0f, 0.0f, 1.0f);
    setClearAttachment(attachments[3], targets.materialTextureViewHandle(), 0.86f, 1.0f, 0.0f, 0.0f);
    setClearAttachment(attachments[4], targets.materialAuxTextureViewHandle(), 0.0f, 0.0f, 0.65f, 0.0f);
    setClearAttachment(attachments[5], targets.f0MetallicTextureViewHandle(), 0.0f, 0.0f, 0.0f, 0.0f);
    setClearAttachmentUint(attachments[6], targets.objectMaterialIdTextureViewHandle(), 0u, 0u);
    setClearAttachment(attachments[7], targets.perObjectVelocityTextureViewHandle(), 0.0f, 0.0f, 0.0f, 0.0f);

    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = targets.depthTextureViewHandle();
    depthAttachment.depthLoadOp = RhiLoadOp::Clear;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    depthAttachment.clearDepth = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "GBuffer.ExternalGeometry";
    renderingInfo.renderArea = {0, 0, static_cast<uint32_t>(std::max(1, targets.width())),
                                static_cast<uint32_t>(std::max(1, targets.height()))};
    renderingInfo.colorAttachments = attachments;
    renderingInfo.colorAttachmentCount = 8u;
    renderingInfo.depthStencilAttachment = &depthAttachment;

    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
                                              ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::GBuffer)
                                              : GpuTimerSegmentToken{};
    commandList.beginRendering(renderingInfo);
    commandList.setViewport({0.0f, 0.0f, static_cast<float>(std::max(1, targets.width())),
                             static_cast<float>(std::max(1, targets.height())), 0.0f, 1.0f});
    commandList.setScissor(renderingInfo.renderArea);
    const glm::mat4& viewProjection = usesTemporalProjectionJitter(settings.upscale.type, settings.taa.enabled)
                                          ? ctx.camera.jitteredViewProj
                                          : ctx.camera.viewProj;
    geometryProvider.renderToGBuffer(commandList, viewProjection, ctx.previousViewProjWithCurrentJitter);
    commandList.endRendering();
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, gpuTimer);
    }
    return true;
}

bool GBufferPass::executeDrops(RhiCommandList& commandList, const IWorldView& worldView, const FrameContext& ctx,
                               const RenderSettings& settings, DeferredRenderTargets& targets,
                               DropRenderer* dropRenderer, DropSystem* dropSystem) {
    if (dropRenderer == nullptr || dropSystem == nullptr) {
        return true;
    }

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!dropRenderer->prepareFrame(worldView, *dropSystem)) {
        return false;
    }
    if (!dropRenderer->prepareBlockGBuffer(commandList, ctx.animationTime)) {
        return false;
    }
    if (!beginObjectGBufferRendering(rhiDevice, commandList, targets, "GBuffer.Drops", false)) {
        return false;
    }
    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
                                              ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::GBuffer)
                                              : GpuTimerSegmentToken{};

    // Match entity velocity: both matrices carry the current frame's jitter
    // so the per-object velocity subtraction stays jitter-free.
    const glm::mat4& viewProj = usesTemporalProjectionJitter(settings.upscale.type, settings.taa.enabled)
                                    ? ctx.camera.jitteredViewProj
                                    : ctx.camera.viewProj;
    const glm::mat4& previousViewProj = ctx.previousViewProjWithCurrentJitter;
    dropRenderer->renderItemsToGBuffer(commandList, viewProj, previousViewProj);
    dropRenderer->renderBlocksToGBuffer(commandList, viewProj, previousViewProj);
    dropRenderer->finishGBufferFrame();

    endObjectGBufferRendering(commandList);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, gpuTimer);
    }
    return true;
}

bool GBufferPass::executeFallingBlocks(RhiCommandList& commandList, const IWorldView& worldView,
                                       const FrameContext& ctx, const RenderSettings& settings,
                                       DeferredRenderTargets& targets, FallingBlockRenderer* fallingBlockRenderer,
                                       ecs::GameplayRegistry* gameplayRegistry) {
    if (fallingBlockRenderer == nullptr || gameplayRegistry == nullptr) {
        return true;
    }

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return false;
    }

    if (!fallingBlockRenderer->prepareFrame(worldView, *gameplayRegistry)) {
        return false;
    }
    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!beginObjectGBufferRendering(rhiDevice, commandList, targets, "GBuffer.FallingBlocks", false)) {
        return false;
    }
    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
                                              ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::GBuffer)
                                              : GpuTimerSegmentToken{};

    const glm::mat4& viewProj = usesTemporalProjectionJitter(settings.upscale.type, settings.taa.enabled)
                                    ? ctx.camera.jitteredViewProj
                                    : ctx.camera.viewProj;
    const glm::mat4& previousViewProj = ctx.previousViewProjWithCurrentJitter;
    fallingBlockRenderer->renderToGBuffer(commandList, viewProj, previousViewProj, ctx.animationTime);

    endObjectGBufferRendering(commandList);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, gpuTimer);
    }
    return true;
}
