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
    RhiColorAttachment colorAttachment;
    RhiDepthStencilAttachment depthAttachment;
    RhiRenderingInfo renderingInfo;

    if (compositeInputsEnabled) {
        rhiDevice = ctx.shared->rhiDevice;
        if (!targets.ensureTransparentCompositeTextureViews(*rhiDevice)) {
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

        colorAttachment.view = targets.transparentCompositeTextureViewHandle();
        colorAttachment.loadOp = RhiLoadOp::Load;
        colorAttachment.storeOp = RhiStoreOp::Store;

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
        renderingInfo.colorAttachments = &colorAttachment;
        renderingInfo.colorAttachmentCount = 1u;
        renderingInfo.depthStencilAttachment = &depthAttachment;

        commandList = &beginCommandList(*ctx.shared->commandListPool,
                                        "WaterComposite.TransparentComposite.Commands");
    } else if (deferredFrameActive) {
        if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
            !ctx.sceneCaptureColorView.isValid() || !ctx.sceneCaptureDepthView.isValid()) {
            return false;
        }

        rhiDevice = ctx.shared->rhiDevice;

        colorAttachment.view = ctx.sceneCaptureColorView;
        colorAttachment.loadOp = RhiLoadOp::Load;
        colorAttachment.storeOp = RhiStoreOp::Store;

        depthAttachment.view = ctx.sceneCaptureDepthView;
        depthAttachment.depthLoadOp = RhiLoadOp::Load;
        depthAttachment.depthStoreOp = RhiStoreOp::Store;

        renderingInfo.debugName = "WaterComposite.SceneCapture";
        renderingInfo.renderArea = {
            0,
            0,
            static_cast<uint32_t>(std::max(1, ctx.frameWidth)),
            static_cast<uint32_t>(std::max(1, ctx.frameHeight))
        };
        renderingInfo.colorAttachments = &colorAttachment;
        renderingInfo.colorAttachmentCount = 1u;
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

    const bool useJitteredWater = preTemporalResolve && settings.taa.enabled;
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
    commandList->beginRendering(renderingInfo);
    worldRenderBuffer.recordRhiWater(*commandList, ctx.shared->terrainRhiPipelines->waterPipeline(),
                                     ctx.shared->terrainRhiPipelines->waterBindGroup());
    worldRenderBuffer.mergeSceneWaterFrameStats();

    commandList->endRendering();
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
