#include "WaterCompositePass.h"
#include "../targets/DeferredRenderTargets.h"
#include "../mesh/TerrainRhiPipelineSet.h"
#include "../mesh/WorldRenderBuffer.h"
#include "../core/RenderScene.h"
#include "../debug/RenderDebugService.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiCommandListPool.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../../resource/ResourceMgr.h"

#include <algorithm>
#include <cstdlib>

namespace {
RhiCommandList& beginCommandList(RhiCommandListPool& commandListPool,
                                 const char* const debugName) {
    RhiCommandList* const commandList =
        commandListPool.acquire(RhiCommandListType::Graphics);
    if (commandList == nullptr ||
        !commandList->begin({debugName, RhiCommandListType::Graphics})) {
        std::abort();
    }
    return *commandList;
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
} // namespace

void WaterCompositePass::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
}

void WaterCompositePass::shutdown() {
    m_resourceMgr = nullptr;
}

bool WaterCompositePass::execute(const FrameContext& ctx, const RenderSettings& settings,
                                   DeferredRenderTargets& targets,
                                   bool deferredFrameActive, bool preTemporalResolve,
                                   bool transparentCompositeEnabled,
                                   bool waterEffectsEnabled, bool rainSurfaceRipplesEnabled,
                                   bool volumetricFogActive,
                                   WorldRenderBuffer& worldRenderBuffer,
                                   const std::vector<DrawBatchEntry>& transparentBatch,
                                   const TransparentPassPlan& transparentPlan) {
    if (!waterEffectsEnabled || m_resourceMgr == nullptr) {
        return false;
    }
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        ctx.shared->commandListPool == nullptr ||
        ctx.shared->terrainRhiPipelines == nullptr) {
        return false;
    }

    if (!transparentPlan.hasWater()) {
        return false;
    }

    const bool deferredInputsEnabled = deferredFrameActive && targets.isReady();
    const bool compositeInputsEnabled = deferredInputsEnabled &&
                                        (preTemporalResolve || transparentCompositeEnabled);

    RhiCommandList* commandList = nullptr;
    RhiDevice* rhiDevice = nullptr;
    RhiColorAttachment colorAttachments[3];
    RhiDepthStencilAttachment depthAttachment;
    RhiRenderingInfo renderingInfo;

    if (compositeInputsEnabled) {
        rhiDevice = ctx.shared->rhiDevice;
        if (!targets.ensureTransparentCompositeTextureViews(*rhiDevice) ||
            !targets.ensureReactiveMaskTextureView(*rhiDevice) ||
            !targets.ensureTransparencyMaskTextureView(*rhiDevice)) {
            return false;
        }

        RhiCommandList& copyCommandList = beginCommandList(
            *ctx.shared->commandListPool, "WaterComposite.InputCopy.Commands");
        const GpuTimerSegmentToken copyTimer = ctx.debugService != nullptr
            ? ctx.debugService->beginGpuTimer(copyCommandList, GpuTimerPass::Water)
            : GpuTimerSegmentToken{};
        targets.copySceneResolvedToTransparentComposite(copyCommandList);
        targets.copyDepthToTransparentComposite(copyCommandList);
        if (ctx.debugService != nullptr) {
            ctx.debugService->endGpuTimer(copyCommandList, copyTimer);
        }
        submitCommandList(*rhiDevice, copyCommandList,
                          "WaterComposite.InputCopy.Submit");

        colorAttachments[0].view = targets.transparentCompositeTextureViewHandle();
        colorAttachments[0].loadOp = RhiLoadOp::Load;
        colorAttachments[0].storeOp = RhiStoreOp::Store;
        colorAttachments[1].view = targets.reactiveMaskTextureViewHandle();
        colorAttachments[1].loadOp = RhiLoadOp::Load;
        colorAttachments[1].storeOp = RhiStoreOp::Store;
        colorAttachments[2].view = targets.transparencyMaskTextureViewHandle();
        colorAttachments[2].loadOp = RhiLoadOp::Load;
        colorAttachments[2].storeOp = RhiStoreOp::Store;

        depthAttachment.view = targets.transparentCompositeDepthTextureViewHandle();
        depthAttachment.depthLoadOp = RhiLoadOp::Load;
        depthAttachment.depthStoreOp = RhiStoreOp::Store;

        renderingInfo.debugName = "WaterComposite.TransparentComposite";
        renderingInfo.renderArea = {
            0,
            0,
            static_cast<uint32_t>(std::max(1, targets.width())),
            static_cast<uint32_t>(std::max(1, targets.height()))
        };
        renderingInfo.colorAttachments = colorAttachments;
        renderingInfo.colorAttachmentCount = 3u;
        renderingInfo.depthStencilAttachment = &depthAttachment;

        commandList = &beginCommandList(*ctx.shared->commandListPool,
                                        "WaterComposite.TransparentComposite.Commands");
    } else if (deferredFrameActive) {
        if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
            !ctx.sceneCaptureColorView.isValid() || !ctx.sceneCaptureDepthView.isValid() ||
            !targets.ensureReactiveMaskTextureView(*ctx.shared->rhiDevice) ||
            !targets.ensureTransparencyMaskTextureView(*ctx.shared->rhiDevice)) {
            return false;
        }

        rhiDevice = ctx.shared->rhiDevice;

        colorAttachments[0].view = ctx.sceneCaptureColorView;
        colorAttachments[0].loadOp = RhiLoadOp::Load;
        colorAttachments[0].storeOp = RhiStoreOp::Store;
        colorAttachments[1].view = targets.reactiveMaskTextureViewHandle();
        colorAttachments[1].loadOp = RhiLoadOp::Load;
        colorAttachments[1].storeOp = RhiStoreOp::Store;
        colorAttachments[2].view = targets.transparencyMaskTextureViewHandle();
        colorAttachments[2].loadOp = RhiLoadOp::Load;
        colorAttachments[2].storeOp = RhiStoreOp::Store;

        depthAttachment.view = ctx.sceneCaptureDepthView;
        depthAttachment.depthLoadOp = RhiLoadOp::Load;
        depthAttachment.depthStoreOp = RhiStoreOp::Store;

        renderingInfo.debugName = "WaterComposite.SceneCapture";
        renderingInfo.renderArea = {
            0,
            0,
            ctx.renderExtent.width,
            ctx.renderExtent.height
        };
        renderingInfo.colorAttachments = colorAttachments;
        renderingInfo.colorAttachmentCount = 3u;
        renderingInfo.depthStencilAttachment = &depthAttachment;

        commandList = &beginCommandList(*ctx.shared->commandListPool,
                                        "WaterComposite.SceneCapture.Commands");
    }

    if (commandList == nullptr || rhiDevice == nullptr) {
        return false;
    }
    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(*commandList, GpuTimerPass::Water)
        : GpuTimerSegmentToken{};

    std::vector<const DrawBatchEntry*> waterEntries;
    waterEntries.reserve(transparentBatch.size());
    for (const DrawBatchEntry& entry : transparentBatch) {
        if (entry.kind == TransparentBatchKind::Water) {
            waterEntries.push_back(&entry);
        }
    }
    std::sort(waterEntries.begin(), waterEntries.end(), [](const DrawBatchEntry* lhs, const DrawBatchEntry* rhs) {
        return lhs->distanceSq > rhs->distanceSq;
    });
    if (waterEntries.empty()) {
        if (ctx.debugService != nullptr) {
            ctx.debugService->cancelGpuTimer(gpuTimer);
        }
        submitCommandList(*rhiDevice, *commandList,
                          "WaterComposite.Empty.Submit");
        return false;
    }

    worldRenderBuffer.clearWaterCommands();
    for (const DrawBatchEntry* entry : waterEntries) {
        worldRenderBuffer.addWater(entry->range);
    }

    const bool useJitteredWater = preTemporalResolve &&
        usesTemporalProjectionJitter(settings.upscale.type, settings.taa.enabled);
    TerrainWaterFrameData waterFrame;
    waterFrame.view = ctx.camera.view;
    waterFrame.viewProj = useJitteredWater ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    waterFrame.invViewProj = useJitteredWater ? ctx.camera.jitteredInvViewProj : ctx.camera.invViewProj;
    waterFrame.cameraPos = ctx.camera.position;
    waterFrame.nearPlane = ctx.camera.nearPlane;
    waterFrame.farPlane = ctx.camera.farPlane;
    waterFrame.animationTime = ctx.animationTime;
    waterFrame.shaderTime = ctx.shaderTime;
    waterFrame.sunDirection = ctx.skyColors.sunDirection;
    waterFrame.moonDirection = ctx.skyColors.moonDirection;
    waterFrame.sunLightColor = ctx.skyColors.sunLightColor;
    waterFrame.moonLightColor = ctx.skyColors.moonLightColor;
    waterFrame.skyAmbientColor = ctx.skyColors.skyAmbientColor;
    waterFrame.skyIntensity = ctx.skyIntensity;
    waterFrame.moonVisibility = ctx.skyColors.moonVisibility;
    waterFrame.moonPhaseFlux =
        (std::abs(ctx.skyColors.moonPhaseAngle) / glm::pi<float>() + 0.2f) * 0.0005f;
    waterFrame.weatherWetness = ctx.weather.wetness;
    waterFrame.skyWetness = ctx.weather.skyWetness;
    waterFrame.fogWetness = ctx.weather.fogWetness;
    waterFrame.cloudWetness = ctx.weather.cloudWetness;
    waterFrame.surfaceWetness = ctx.weather.surfaceWetness;
    waterFrame.frameIndex = ctx.frameIndex;
    waterFrame.skyCaptureEnabled = deferredFrameActive;
    waterFrame.compositeInputsEnabled = compositeInputsEnabled;
    waterFrame.depthSofteningEnabled = deferredInputsEnabled;
    waterFrame.volumetricFogActive = volumetricFogActive;
    waterFrame.freezeBias = settings.volumetric.freezeBias;
    waterFrame.rainSurfaceRipplesEnabled = rainSurfaceRipplesEnabled;
    waterFrame.eyeInWater = ctx.eyeInWater;

    if (!ctx.shared->terrainRhiPipelines->prepareWater(*commandList, *m_resourceMgr, targets, waterFrame) ||
        !worldRenderBuffer.prepareRhiWater(*commandList, ctx.shared->terrainRhiPipelines->waterMetadataLayout())) {
        if (ctx.debugService != nullptr) {
            ctx.debugService->cancelGpuTimer(gpuTimer);
        }
        submitCommandList(*rhiDevice, *commandList,
                          "WaterComposite.PreparationFailure.Submit");
        return false;
    }

    if (compositeInputsEnabled) {
        targets.transitionTexture(*commandList,
                                  targets.transparentCompositeTextureHandle(),
                                  RhiResourceState::RenderTarget);
        targets.transitionTexture(*commandList,
                                  targets.transparentCompositeDepthTextureHandle(),
                                  RhiResourceState::DepthWrite);
    } else {
        targets.transitionTexture(*commandList,
                                  ctx.sceneCaptureColorTexture,
                                  RhiResourceState::RenderTarget);
        targets.transitionTexture(*commandList,
                                  ctx.sceneCaptureDepthTexture,
                                  RhiResourceState::DepthWrite);
    }
    targets.transitionTexture(*commandList,
                              targets.reactiveMaskTextureHandle(),
                              RhiResourceState::RenderTarget);
    targets.transitionTexture(*commandList,
                              targets.transparencyMaskTextureHandle(),
                              RhiResourceState::RenderTarget);
    commandList->beginRendering(renderingInfo);
    worldRenderBuffer.recordRhiWater(*commandList, ctx.shared->terrainRhiPipelines->waterPipeline(),
                                     ctx.shared->terrainRhiPipelines->waterBindGroup());
    worldRenderBuffer.mergeSceneWaterFrameStats();

    commandList->endRendering();
    targets.transitionTexture(*commandList,
                              targets.reactiveMaskTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(*commandList,
                              targets.transparencyMaskTextureHandle(),
                              RhiResourceState::ShaderRead);
    if (compositeInputsEnabled) {
        targets.transitionTexture(*commandList,
                                  targets.transparentCompositeTextureHandle(),
                                  RhiResourceState::ShaderRead);
        targets.transitionTexture(*commandList,
                                  targets.transparentCompositeDepthTextureHandle(),
                                  RhiResourceState::DepthRead);
    } else {
        targets.transitionTexture(*commandList,
                                  ctx.sceneCaptureColorTexture,
                                  RhiResourceState::ShaderRead);
        targets.transitionTexture(*commandList,
                                  ctx.sceneCaptureDepthTexture,
                                  RhiResourceState::ShaderRead);
    }
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(*commandList, gpuTimer);
    }
    submitCommandList(*rhiDevice, *commandList, "WaterComposite.Render.Submit");

    if (preTemporalResolve && compositeInputsEnabled) {
        RhiCommandList& copyCommandList = beginCommandList(
            *ctx.shared->commandListPool, "WaterComposite.PreTemporalCopy.Commands");
        const GpuTimerSegmentToken copyTimer = ctx.debugService != nullptr
            ? ctx.debugService->beginGpuTimer(copyCommandList, GpuTimerPass::Water)
            : GpuTimerSegmentToken{};
        targets.copyTransparentCompositeToSceneComposite(copyCommandList);
        targets.copyTransparentCompositeToSceneResolved(copyCommandList);
        if (ctx.debugService != nullptr) {
            ctx.debugService->endGpuTimer(copyCommandList, copyTimer);
        }
        submitCommandList(*rhiDevice, copyCommandList,
                          "WaterComposite.PreTemporalCopy.Submit");
    } else if (compositeInputsEnabled) {
        RhiCommandList& copyCommandList = beginCommandList(
            *ctx.shared->commandListPool, "WaterComposite.SceneCaptureCopy.Commands");
        const GpuTimerSegmentToken copyTimer = ctx.debugService != nullptr
            ? ctx.debugService->beginGpuTimer(copyCommandList, GpuTimerPass::Water)
            : GpuTimerSegmentToken{};
        targets.copyTransparentCompositeToTexture(
            copyCommandList, ctx.sceneCaptureColorTexture);
        if (ctx.debugService != nullptr) {
            ctx.debugService->endGpuTimer(copyCommandList, copyTimer);
        }
        submitCommandList(*rhiDevice, copyCommandList,
                          "WaterComposite.SceneCaptureCopy.Submit");
    }

    // Return whether water was rendered before temporal resolve
    // (caller needs to set m_waterRenderedBeforeTemporal accordingly)
    return preTemporalResolve && compositeInputsEnabled;
}

bool WaterCompositePass::recordGraphPass(
    const FrameContext& ctx,
    const RenderSettings& settings,
    DeferredRenderTargets& targets,
    const bool deferredFrameActive,
    const bool preTemporalResolve,
    const bool transparentCompositeEnabled,
    const bool waterEffectsEnabled,
    const bool rainSurfaceRipplesEnabled,
    const bool volumetricFogActive,
    RhiCommandList& commandList,
    WorldRenderBuffer& worldRenderBuffer,
    const std::vector<DrawBatchEntry>& transparentBatch,
    const TransparentPassPlan& transparentPlan) {
    if (!waterEffectsEnabled || !transparentPlan.hasWater()) {
        return true;
    }
    if (m_resourceMgr == nullptr || ctx.shared == nullptr ||
        ctx.shared->rhiDevice == nullptr ||
        ctx.shared->terrainRhiPipelines == nullptr) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const bool deferredInputsEnabled = deferredFrameActive && targets.isReady();
    const bool compositeInputsEnabled = deferredInputsEnabled &&
                                        (preTemporalResolve || transparentCompositeEnabled);

    RhiColorAttachment colorAttachments[3];
    RhiDepthStencilAttachment depthAttachment;
    RhiRenderingInfo renderingInfo;
    if (compositeInputsEnabled) {
        if (!targets.ensureTransparentCompositeTextureViews(rhiDevice) ||
            !targets.ensureReactiveMaskTextureView(rhiDevice) ||
            !targets.ensureTransparencyMaskTextureView(rhiDevice)) {
            return false;
        }
        colorAttachments[0].view = targets.transparentCompositeTextureViewHandle();
        colorAttachments[1].view = targets.reactiveMaskTextureViewHandle();
        colorAttachments[2].view = targets.transparencyMaskTextureViewHandle();
        depthAttachment.view = targets.transparentCompositeDepthTextureViewHandle();
        renderingInfo.debugName = "WaterComposite.TransparentComposite";
        renderingInfo.renderArea = {
            0,
            0,
            static_cast<uint32_t>(std::max(1, targets.width())),
            static_cast<uint32_t>(std::max(1, targets.height()))
        };
    } else if (deferredFrameActive) {
        if (!ctx.sceneCaptureColorView.isValid() || !ctx.sceneCaptureDepthView.isValid() ||
            !targets.ensureReactiveMaskTextureView(rhiDevice) ||
            !targets.ensureTransparencyMaskTextureView(rhiDevice)) {
            return false;
        }
        colorAttachments[0].view = ctx.sceneCaptureColorView;
        colorAttachments[1].view = targets.reactiveMaskTextureViewHandle();
        colorAttachments[2].view = targets.transparencyMaskTextureViewHandle();
        depthAttachment.view = ctx.sceneCaptureDepthView;
        renderingInfo.debugName = "WaterComposite.SceneCapture";
        renderingInfo.renderArea = {0, 0, ctx.renderExtent.width, ctx.renderExtent.height};
    } else {
        return true;
    }

    for (RhiColorAttachment& attachment : colorAttachments) {
        attachment.loadOp = RhiLoadOp::Load;
        attachment.storeOp = RhiStoreOp::Store;
    }
    depthAttachment.depthLoadOp = RhiLoadOp::Load;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    renderingInfo.colorAttachments = colorAttachments;
    renderingInfo.colorAttachmentCount = 3u;
    renderingInfo.depthStencilAttachment = &depthAttachment;

    std::vector<const DrawBatchEntry*> waterEntries;
    waterEntries.reserve(transparentBatch.size());
    for (const DrawBatchEntry& entry : transparentBatch) {
        if (entry.kind == TransparentBatchKind::Water) {
            waterEntries.push_back(&entry);
        }
    }
    std::sort(waterEntries.begin(), waterEntries.end(),
              [](const DrawBatchEntry* lhs, const DrawBatchEntry* rhs) {
                  return lhs->distanceSq > rhs->distanceSq;
              });
    if (waterEntries.empty()) {
        return true;
    }

    worldRenderBuffer.clearWaterCommands();
    for (const DrawBatchEntry* entry : waterEntries) {
        worldRenderBuffer.addWater(entry->range);
    }

    const bool useJitteredWater = preTemporalResolve &&
        usesTemporalProjectionJitter(settings.upscale.type, settings.taa.enabled);
    TerrainWaterFrameData waterFrame;
    waterFrame.view = ctx.camera.view;
    waterFrame.viewProj = useJitteredWater ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    waterFrame.invViewProj = useJitteredWater ? ctx.camera.jitteredInvViewProj : ctx.camera.invViewProj;
    waterFrame.cameraPos = ctx.camera.position;
    waterFrame.nearPlane = ctx.camera.nearPlane;
    waterFrame.farPlane = ctx.camera.farPlane;
    waterFrame.animationTime = ctx.animationTime;
    waterFrame.shaderTime = ctx.shaderTime;
    waterFrame.sunDirection = ctx.skyColors.sunDirection;
    waterFrame.moonDirection = ctx.skyColors.moonDirection;
    waterFrame.sunLightColor = ctx.skyColors.sunLightColor;
    waterFrame.moonLightColor = ctx.skyColors.moonLightColor;
    waterFrame.skyAmbientColor = ctx.skyColors.skyAmbientColor;
    waterFrame.skyIntensity = ctx.skyIntensity;
    waterFrame.moonVisibility = ctx.skyColors.moonVisibility;
    waterFrame.moonPhaseFlux =
        (std::abs(ctx.skyColors.moonPhaseAngle) / glm::pi<float>() + 0.2f) * 0.0005f;
    waterFrame.weatherWetness = ctx.weather.wetness;
    waterFrame.skyWetness = ctx.weather.skyWetness;
    waterFrame.fogWetness = ctx.weather.fogWetness;
    waterFrame.cloudWetness = ctx.weather.cloudWetness;
    waterFrame.surfaceWetness = ctx.weather.surfaceWetness;
    waterFrame.frameIndex = ctx.frameIndex;
    waterFrame.skyCaptureEnabled = deferredFrameActive;
    waterFrame.compositeInputsEnabled = compositeInputsEnabled;
    waterFrame.depthSofteningEnabled = deferredInputsEnabled;
    waterFrame.volumetricFogActive = volumetricFogActive;
    waterFrame.freezeBias = settings.volumetric.freezeBias;
    waterFrame.rainSurfaceRipplesEnabled = rainSurfaceRipplesEnabled;
    waterFrame.eyeInWater = ctx.eyeInWater;

    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Water)
        : GpuTimerSegmentToken{};
    if (!ctx.shared->terrainRhiPipelines->prepareWater(
            commandList, *m_resourceMgr, targets, waterFrame) ||
        !worldRenderBuffer.prepareRhiWater(
            commandList, ctx.shared->terrainRhiPipelines->waterMetadataLayout())) {
        if (ctx.debugService != nullptr) {
            ctx.debugService->cancelGpuTimer(gpuTimer);
        }
        return false;
    }

    commandList.beginRendering(renderingInfo);
    worldRenderBuffer.recordRhiWater(
        commandList,
        ctx.shared->terrainRhiPipelines->waterPipeline(),
        ctx.shared->terrainRhiPipelines->waterBindGroup());
    worldRenderBuffer.mergeSceneWaterFrameStats();
    commandList.endRendering();
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, gpuTimer);
    }
    return true;
}
