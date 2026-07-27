#include "PostProcessPass.h"

#include "../../Diagnostics.h"
#include "../../resource/ResourceMgr.h"
#include "../debug/RenderDebugService.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiCommandListPool.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

namespace {
struct ExposureDownsamplePushConstants {
    glm::vec4 sourceSize;
    glm::ivec4 flags;
};
static_assert(sizeof(ExposureDownsamplePushConstants) == 32u);

struct ExposureResolvePushConstants {
    glm::vec4 exposure;
    glm::ivec4 flags;
};
static_assert(sizeof(ExposureResolvePushConstants) == 32u);

[[nodiscard]] bool sameTextureHandle(const RhiTextureHandle lhs, const RhiTextureHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

bool createTextureAndView(RhiDevice& rhiDevice,
                          const char* debugName,
                          const RhiTextureFormat format,
                          const uint32_t width,
                          const uint32_t height,
                          const RhiTextureUsageFlags usage,
                          RhiTextureHandle& texture,
                          RhiTextureViewHandle& view) {
    RhiTextureDesc textureDesc;
    textureDesc.debugName = debugName;
    textureDesc.dimension = RhiTextureDimension::Texture2D;
    textureDesc.format = format;
    textureDesc.width = std::max(1u, width);
    textureDesc.height = std::max(1u, height);
    textureDesc.depthOrLayers = 1u;
    textureDesc.mipLevels = 1u;
    textureDesc.sampleCount = 1u;
    textureDesc.usage = usage;
    texture = rhiDevice.createTexture(textureDesc, nullptr);
    if (!texture.isValid()) {
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = texture;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = format;
    viewDesc.baseMip = 0u;
    viewDesc.mipCount = 1u;
    viewDesc.baseLayer = 0u;
    viewDesc.layerCount = 1u;
    view = rhiDevice.createTextureView(viewDesc);
    if (!view.isValid()) {
        rhiDevice.destroyTexture(texture);
        texture = {};
        return false;
    }
    return true;
}

void beginPostProcessColorOutput(RhiCommandList& commandList,
                                 const char* debugName,
                                 const RhiTextureViewHandle view,
                                 const int width,
                                 const int height,
                                 const bool clearColor) {
    RhiColorAttachment colorAttachment;
    colorAttachment.view = view;
    colorAttachment.loadOp = clearColor ? RhiLoadOp::Clear : RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = debugName;
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, width)),
        static_cast<uint32_t>(std::max(1, height))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    commandList.beginRendering(renderingInfo);
}
} // namespace

PostProcessPass::~PostProcessPass() {
    shutdown();
}

void PostProcessPass::init(ResourceMgr& resourceMgr,
                           RhiCommandListPool& commandListPool) {
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_bayer256");
    m_commandListPool = &commandListPool;
}

void PostProcessPass::shutdown() {
    destroyRhiResources();
    m_noiseTexture = {};
    m_sceneCaptured = false;
    m_captureWidth = 0;
    m_captureHeight = 0;
    m_hdrInputWidth = 0;
    m_hdrInputHeight = 0;
    m_processingWidth = 0;
    m_processingHeight = 0;
    m_autoExposureSampleAccumulator = 0.0;
    m_commandListPool = nullptr;
}

RhiCommandList& PostProcessPass::beginCommandList(const char* const debugName) const {
    if (m_commandListPool == nullptr) {
        std::abort();
    }
    RhiCommandList* const commandList =
        m_commandListPool->acquire(RhiCommandListType::Graphics);
    if (commandList == nullptr ||
        !commandList->begin({debugName, RhiCommandListType::Graphics})) {
        std::abort();
    }
    return *commandList;
}

void PostProcessPass::submitCommandList(RhiDevice& rhiDevice,
                                        RhiCommandList& commandList,
                                        const char* const debugName) const {
    if (!commandList.end()) {
        std::abort();
    }
    RhiCommandList* commandLists[] = {&commandList};
    const RhiSubmitInfo submitInfo{debugName, commandLists, 1u};
    if (!rhiDevice.submit(submitInfo)) {
        std::abort();
    }
}

void PostProcessPass::setFrameEffects(const PostProcessEffects& effects) {
    m_effects = effects;
    m_effects.underwaterStrength = std::clamp(m_effects.underwaterStrength, 0.0f, 1.0f);
    m_effects.bloomThreshold = std::clamp(m_effects.bloomThreshold, 0.0f, 4.0f);
    m_effects.bloomStrength = std::clamp(m_effects.bloomStrength, 0.0f, 20.0f);
    m_effects.bloomMipCount = std::clamp(m_effects.bloomMipCount, 1, kBloomMipCount);
    m_effects.autoExposureMin = std::clamp(m_effects.autoExposureMin, 0.001f, 64.0f);
    m_effects.autoExposureMax = std::clamp(m_effects.autoExposureMax, m_effects.autoExposureMin, 64.0f);
    m_effects.autoExposureSpeed = std::clamp(m_effects.autoExposureSpeed, 0.05f, 12.0f);
    m_effects.autoExposureBias = std::clamp(m_effects.autoExposureBias, -3.0f, 3.0f);
    m_effects.autoExposureDayFactor = std::clamp(m_effects.autoExposureDayFactor, 0.0f, 1.0f);
    m_effects.sunScreenPos.x = std::clamp(m_effects.sunScreenPos.x, -1.0f, 2.0f);
    m_effects.sunScreenPos.y = std::clamp(m_effects.sunScreenPos.y, -1.0f, 2.0f);
    m_effects.sunVisibility = std::clamp(m_effects.sunVisibility, 0.0f, 1.0f);
    m_effects.sunRayStrength = std::clamp(m_effects.sunRayStrength, 0.0f, 1.0f);
    m_effects.tonemapMode = std::clamp(m_effects.tonemapMode, 0, 5);
    m_effects.colorTemperature = std::clamp(m_effects.colorTemperature, 0.0f, 2.0f);
    m_effects.vibrance = std::clamp(m_effects.vibrance, -1.0f, 1.0f);
    m_effects.highlightCompression = std::clamp(m_effects.highlightCompression, 0.0f, 1.5f);
    m_effects.filmEmulationStrength = std::clamp(m_effects.filmEmulationStrength, 0.0f, 1.0f);
    m_effects.redModifierStrength = std::clamp(m_effects.redModifierStrength, 0.0f, 1.0f);
    m_effects.colorLuma.x = std::clamp(m_effects.colorLuma.x, 0.5f, 1.5f);
    m_effects.colorLuma.y = std::clamp(m_effects.colorLuma.y, 0.5f, 1.5f);
    m_effects.colorLuma.z = std::clamp(m_effects.colorLuma.z, 0.5f, 1.5f);
    m_effects.splitToneStrength = std::clamp(m_effects.splitToneStrength, 0.0f, 1.0f);
    m_effects.vignetteStrength = std::clamp(m_effects.vignetteStrength, 0.0f, 0.5f);
    m_effects.noiseDitherStrength = std::clamp(m_effects.noiseDitherStrength, 0.0f, 0.08f);
    m_effects.sharpenStrength = std::clamp(m_effects.sharpenStrength, 0.0f, 1.0f);
    m_effects.exposure = std::clamp(m_effects.exposure, 0.1f, 50.0f);
    m_effects.gamma = std::clamp(m_effects.gamma, 1.0f, 3.5f);
    m_effects.saturation = std::clamp(m_effects.saturation, 0.0f, 3.0f);
    m_effects.contrast = std::clamp(m_effects.contrast, 0.25f, 3.0f);
    m_effects.weatherWetness = std::clamp(m_effects.weatherWetness, 0.0f, 1.0f);
    m_effects.weatherStorm = std::clamp(m_effects.weatherStorm, 0.0f, 1.0f);
    m_effects.skyWetness = std::clamp(m_effects.skyWetness, 0.0f, 1.0f);
    m_effects.fogWetness = std::clamp(m_effects.fogWetness, 0.0f, 1.0f);
    m_effects.cloudWetness = std::clamp(m_effects.cloudWetness, 0.0f, 1.0f);
}

bool PostProcessPass::beginSceneCapture(RhiDevice& rhiDevice,
                                        const int requestedWidth,
                                        const int requestedHeight) {
    m_sceneCaptured = false;
    if (requestedWidth <= 0 || requestedHeight <= 0 ||
        !ensureSceneCaptureTargets(rhiDevice, requestedWidth, requestedHeight)) {
        return false;
    }
    if (!setHdrInput(m_sceneColorHandle,
                     m_sceneColorView,
                     requestedWidth,
                     requestedHeight)) {
        return false;
    }
    m_sceneCaptured = true;
    return true;
}

bool PostProcessPass::setHdrInput(const RhiTextureHandle texture,
                                  const RhiTextureViewHandle view,
                                  const int width,
                                  const int height) {
    if (m_rhiDevice == nullptr || !texture.isValid() || !view.isValid() ||
        width <= 0 || height <= 0) {
        return false;
    }
    // Temporal upscaling changes the HDR input extent within a frame. Preserve
    // exposure history here and resize processing targets only at composition.
    const bool processingTargetsMissing =
        !m_bloomHandle[0][0].isValid() ||
        !m_exposureStateHandle[0].isValid();
    if (processingTargetsMissing &&
        !ensureProcessingTargets(*m_rhiDevice, width, height)) {
        return false;
    }
    if (m_hdrInputHandle.index != texture.index ||
        m_hdrInputHandle.generation != texture.generation ||
        m_hdrInputView.index != view.index ||
        m_hdrInputView.generation != view.generation) {
        destroyCompositeBindGroups();
        destroyTargetBindGroups();
    }
    m_hdrInputHandle = texture;
    m_hdrInputView = view;
    m_hdrInputWidth = width;
    m_hdrInputHeight = height;
    return true;
}

bool PostProcessPass::prepareTextureOutput(RhiDevice& rhiDevice,
                                           const int width,
                                           const int height) {
    return width > 0 && height > 0 &&
           ensureProcessingTargets(rhiDevice, width, height) &&
           ensureCompositeTarget(rhiDevice, width, height);
}

bool PostProcessPass::compositeToBackbuffer(
    RhiDevice& rhiDevice,
    const RhiTextureViewHandle swapchainColorView,
    const RhiTextureFormat swapchainColorFormat,
    const int outputWidth,
    const int outputHeight,
    const float frameTime,
    const RhiTextureHandle gbufferDepthTexture,
    RenderDebugService& debugService) {
    if (!m_sceneCaptured || !swapchainColorView.isValid() ||
        !ensureProcessingTargets(rhiDevice, m_hdrInputWidth, m_hdrInputHeight) ||
        !ensureRhiPipelines(rhiDevice) || !ensureNoiseTextureView(rhiDevice) ||
        !ensureSwapchainCompositePipeline(rhiDevice, swapchainColorFormat) ||
        !ensureGbufferDepthTextureView(rhiDevice, gbufferDepthTexture) ||
        !rebuildTargetBindGroups() || !rebuildCompositeBindGroups()) {
        return false;
    }
    return executeCompositeGraph(rhiDevice, CompositeDestination::Backbuffer,
                                 swapchainColorView,
                                 outputWidth, outputHeight, frameTime,
                                 debugService);
}

RhiTextureHandle PostProcessPass::compositeToTexture(
    RhiDevice& rhiDevice,
    const float frameTime,
    const RhiTextureHandle gbufferDepthTexture,
    RenderDebugService& debugService) {
    if (!m_sceneCaptured ||
        !ensureProcessingTargets(rhiDevice, m_hdrInputWidth, m_hdrInputHeight) ||
        !ensureCompositeTarget(rhiDevice, m_hdrInputWidth, m_hdrInputHeight) ||
        !ensureRhiPipelines(rhiDevice) || !ensureNoiseTextureView(rhiDevice) ||
        !ensureGbufferDepthTextureView(rhiDevice, gbufferDepthTexture) ||
        !rebuildTargetBindGroups() || !rebuildCompositeBindGroups() ||
        !executeCompositeGraph(rhiDevice, CompositeDestination::Texture,
                                m_compositeView, m_processingWidth,
                                m_processingHeight, frameTime, debugService)) {
        return {};
    }
    return m_compositeHandle;
}

bool PostProcessPass::blitSceneCaptureToBackbuffer(
    RhiDevice& rhiDevice,
    const RhiTextureViewHandle swapchainColorView,
    RenderDebugService& debugService) {
    if (!m_sceneCaptured || !m_sceneColorHandle.isValid()) {
        return false;
    }
    return executeBlitGraph(rhiDevice, m_sceneColorHandle,
                            swapchainColorView, debugService);
}

bool PostProcessPass::blitCompositeToBackbuffer(
    RhiDevice& rhiDevice,
    const RhiTextureViewHandle swapchainColorView,
    RenderDebugService& debugService) {
    if (!m_compositeHandle.isValid()) {
        return false;
    }
    return executeBlitGraph(rhiDevice, m_compositeHandle,
                            swapchainColorView, debugService);
}

bool PostProcessPass::executeCompositeGraph(
    RhiDevice& rhiDevice,
    const CompositeDestination destination,
    const RhiTextureViewHandle outputView,
    const int outputWidth,
    const int outputHeight,
    const float frameTime,
    RenderDebugService& debugService) {
    if (m_commandListPool == nullptr || m_rhiDevice != &rhiDevice ||
        !m_hdrInputHandle.isValid() || !m_hdrInputView.isValid() ||
        !m_sceneDepthHandle.isValid() || !m_sceneDepthView.isValid() ||
        !m_noiseTexture.isValid() || !m_noiseTextureView.isValid() ||
        !m_gbufferDepthViewTexture.isValid() ||
        !m_gbufferDepthTextureView.isValid() ||
        !m_compositeParamsBuffer.isValid() || !outputView.isValid()) {
        return false;
    }

    struct GraphResources {
        RgTextureHandle hdrInput;
        RgTextureHandle sceneDepth;
        RgTextureHandle noise;
        RgTextureHandle gbufferDepth;
        RgTextureHandle output;
        std::array<std::array<RgTextureHandle, 2>, kBloomMipCount> bloom{};
        std::array<RgTextureHandle, kExposureMipCount> exposure{};
        std::array<RgTextureHandle, 2> exposureState{};
        RgBufferHandle compositeParams;
    } resources;

    m_renderGraph.reset();
    const auto importTexture = [&](const RhiTextureHandle texture,
                                   const RhiTextureViewHandle view,
                                   const RhiResourceState stableState,
                                   RgTextureHandle& graphTexture) {
        RhiTextureDesc desc;
        if (!rhiDevice.getTextureDesc(texture, desc)) {
            return false;
        }
        RgImportedTextureDesc imported;
        imported.name = desc.debugName;
        imported.texture = texture;
        imported.desc = desc;
        imported.initialState = stableState;
        imported.finalState = stableState;
        imported.defaultView = view;
        graphTexture = m_renderGraph.importTexture(imported);
        return graphTexture.isValid();
    };
    const auto importBuffer = [&](const RhiBufferHandle buffer,
                                  const RhiResourceState stableState,
                                  RgBufferHandle& graphBuffer) {
        RhiBufferDesc desc;
        if (!rhiDevice.getBufferDesc(buffer, desc)) {
            return false;
        }
        RgImportedBufferDesc imported;
        imported.name = desc.debugName;
        imported.buffer = buffer;
        imported.desc = desc;
        imported.initialState = stableState;
        imported.finalState = stableState;
        graphBuffer = m_renderGraph.importBuffer(imported);
        return graphBuffer.isValid();
    };

    if (!importTexture(m_hdrInputHandle, m_hdrInputView,
                       RhiResourceState::ShaderRead, resources.hdrInput) ||
        !importTexture(m_sceneDepthHandle, m_sceneDepthView,
                       RhiResourceState::DepthRead, resources.sceneDepth) ||
        !importTexture(m_noiseTexture, m_noiseTextureView,
                       RhiResourceState::ShaderRead, resources.noise) ||
        !importTexture(m_gbufferDepthViewTexture, m_gbufferDepthTextureView,
                       RhiResourceState::DepthRead, resources.gbufferDepth) ||
        !importBuffer(m_compositeParamsBuffer, RhiResourceState::UniformBuffer,
                      resources.compositeParams)) {
        return false;
    }
    for (int mip = 0; mip < kBloomMipCount; ++mip) {
        for (int ping = 0; ping < 2; ++ping) {
            if (!importTexture(m_bloomHandle[mip][ping], m_bloomView[mip][ping],
                               RhiResourceState::ShaderRead,
                               resources.bloom[mip][ping])) {
                return false;
            }
        }
    }
    for (int mip = 0; mip < m_exposureMipCount; ++mip) {
        if (!importTexture(m_exposureHandle[mip], m_exposureView[mip],
                           RhiResourceState::ShaderRead,
                           resources.exposure[mip])) {
            return false;
        }
    }
    for (int index = 0; index < 2; ++index) {
        if (!importTexture(m_exposureStateHandle[index],
                           m_exposureStateView[index],
                           RhiResourceState::ShaderRead,
                           resources.exposureState[index])) {
            return false;
        }
    }

    const RhiTextureHandle outputTexture = destination == CompositeDestination::Texture
        ? m_compositeHandle
        : rhiDevice.currentSwapchainColorTexture();
    const RhiResourceState outputStableState =
        destination == CompositeDestination::Texture
            ? RhiResourceState::ShaderRead
            : RhiResourceState::Present;
    if (!outputTexture.isValid() ||
        !importTexture(outputTexture, outputView, outputStableState,
                       resources.output)) {
        return false;
    }

    const float manualExposure = 0.8f / std::max(m_effects.exposure, 0.0001f);
    const float elapsedFrameTime = std::max(frameTime, 0.0f);
    const bool autoExposureEnabled = m_effects.autoExposureEnabled;
    const bool initializeExposure = autoExposureEnabled &&
                                    !m_autoExposureInitialized;
    const bool shouldSampleExposure = autoExposureEnabled &&
        (initializeExposure ||
         m_autoExposureSampleAccumulator + elapsedFrameTime >=
             kAutoExposureSampleIntervalSeconds);
    const int previousExposureReadIndex = initializeExposure
        ? 0
        : m_exposureStateReadIndex;
    const int finalExposureReadIndex = autoExposureEnabled
        ? 1 - previousExposureReadIndex
        : m_exposureStateReadIndex;
    const double nextExposureAccumulator =
        shouldSampleExposure
            ? 0.0
            : m_autoExposureSampleAccumulator + elapsedFrameTime;
    if (!autoExposureEnabled) {
        m_autoExposureInitialized = false;
        m_adaptedExposure = manualExposure;
        m_autoExposureSampleAccumulator = 0.0;
    } else if (initializeExposure) {
        m_adaptedExposure = manualExposure;
    }

    GpuTimerSegmentToken exposureTimer;
    GpuTimerSegmentToken bloomTimer;
    GpuTimerSegmentToken compositeTimer;
    RgPassHandle graphTail;

    if (initializeExposure) {
        RenderGraphPassBuilder initialize = m_renderGraph.addPass(
            {"PostProcess.ExposureStateInitialization",
             RgPassType::Graphics, RhiQueueType::Graphics});
        initialize.writeTexture(resources.exposureState[0],
                                 RhiResourceState::RenderTarget)
            .writeTexture(resources.exposureState[1],
                          RhiResourceState::RenderTarget)
            .setExecute([this, &debugService, &exposureTimer, manualExposure](
                            RgPassContext& pass) {
                exposureTimer = debugService.beginGpuTimer(
                    pass.commandList(), GpuTimerPass::Post);
                recordExposureStateInitialization(pass.commandList(),
                                                  manualExposure);
                return true;
            });
        graphTail = initialize.handle();
    }

    if (shouldSampleExposure) {
        const int exposureLod = std::min(
            kAutoExposureLod,
            std::max(0, static_cast<int>(std::floor(std::log2(static_cast<float>(
                std::max(m_processingWidth, m_processingHeight)))))));
        glm::ivec2 sourceSize(std::max(1, m_processingWidth >> exposureLod),
                              std::max(1, m_processingHeight >> exposureLod));
        bool sourceIsScene = true;
        for (int mip = 0; mip < m_exposureMipCount; ++mip) {
            const glm::ivec2 mipSourceSize = sourceSize;
            const bool mipSourceIsScene = sourceIsScene;
            const std::string passName =
                "PostProcess.ExposureDownsample." + std::to_string(mip);
            RenderGraphPassBuilder downsample = m_renderGraph.addPass(
                {passName.c_str(), RgPassType::Graphics, RhiQueueType::Graphics});
            if (graphTail.isValid()) {
                downsample.dependsOn(graphTail);
            }
            if (mipSourceIsScene) {
                downsample.readTexture(resources.hdrInput,
                                       RhiResourceState::ShaderRead);
            } else {
                downsample.readTexture(resources.exposure[mip - 1],
                                       RhiResourceState::ShaderRead);
            }
            downsample.writeTexture(resources.exposure[mip],
                                    RhiResourceState::RenderTarget)
                .setExecute([this, &debugService, &exposureTimer, mip,
                             mipSourceSize, mipSourceIsScene,
                             exposureLod](RgPassContext& pass) {
                    if (mip == 0 && !exposureTimer.valid) {
                        exposureTimer = debugService.beginGpuTimer(
                            pass.commandList(), GpuTimerPass::Post);
                    }
                    recordExposureDownsample(pass.commandList(), mip,
                                             mipSourceSize, mipSourceIsScene,
                                             exposureLod);
                    return true;
                });
            graphTail = downsample.handle();
            sourceSize = m_exposureMipSize[mip];
            sourceIsScene = false;
        }
    }

    if (autoExposureEnabled) {
        RenderGraphPassBuilder resolve = m_renderGraph.addPass(
            {"PostProcess.ExposureResolve", RgPassType::Graphics,
             RhiQueueType::Graphics});
        if (graphTail.isValid()) {
            resolve.dependsOn(graphTail);
        }
        resolve.readTexture(resources.exposure[m_exposureMipCount - 1],
                            RhiResourceState::ShaderRead)
            .readTexture(resources.exposureState[previousExposureReadIndex],
                         RhiResourceState::ShaderRead)
            .writeTexture(resources.exposureState[finalExposureReadIndex],
                          RhiResourceState::RenderTarget)
            .setExecute([this, &debugService, &exposureTimer,
                         previousExposureReadIndex, finalExposureReadIndex,
                         elapsedFrameTime, manualExposure,
                         historyAvailable = !initializeExposure,
                         reuseExposure = !shouldSampleExposure](
                            RgPassContext& pass) {
                if (!exposureTimer.valid) {
                    exposureTimer = debugService.beginGpuTimer(
                        pass.commandList(), GpuTimerPass::Post);
                }
                recordExposureResolve(pass.commandList(),
                                      previousExposureReadIndex,
                                      finalExposureReadIndex,
                                      elapsedFrameTime, manualExposure,
                                      historyAvailable, reuseExposure);
                debugService.endGpuTimer(pass.commandList(), exposureTimer);
                return true;
            });
        graphTail = resolve.handle();
    }

    const bool bloomReady = m_effects.bloomEnabled &&
                            m_effects.bloomStrength > 0.001f;
    const int bloomMipCount = std::clamp(m_effects.bloomMipCount,
                                         1, kBloomMipCount);
    if (bloomReady) {
        for (int mip = 0; mip < bloomMipCount; ++mip) {
            const std::string extractName =
                "PostProcess.Bloom.Extract." + std::to_string(mip);
            RenderGraphPassBuilder extract = m_renderGraph.addPass(
                {extractName.c_str(), RgPassType::Graphics,
                 RhiQueueType::Graphics});
            if (graphTail.isValid()) {
                extract.dependsOn(graphTail);
            }
            extract.readTexture(resources.hdrInput, RhiResourceState::ShaderRead)
                .writeTexture(resources.bloom[mip][0],
                               RhiResourceState::RenderTarget)
                .setExecute([this, &debugService, &bloomTimer, mip](
                                RgPassContext& pass) {
                    if (mip == 0) {
                        bloomTimer = debugService.beginGpuTimer(
                            pass.commandList(), GpuTimerPass::Post);
                    }
                    recordBloomExtract(pass.commandList(), mip);
                    return true;
                });
            const RgPassHandle extractHandle = extract.handle();

            const std::string horizontalName =
                "PostProcess.Bloom.BlurHorizontal." + std::to_string(mip);
            RenderGraphPassBuilder horizontal = m_renderGraph.addPass(
                {horizontalName.c_str(), RgPassType::Graphics,
                 RhiQueueType::Graphics});
            horizontal.dependsOn(extractHandle)
                .readTexture(resources.bloom[mip][0],
                             RhiResourceState::ShaderRead)
                .writeTexture(resources.bloom[mip][1],
                               RhiResourceState::RenderTarget)
                .setExecute([this, mip](RgPassContext& pass) {
                    recordBloomBlur(pass.commandList(), mip, true);
                    return true;
                });
            const RgPassHandle horizontalHandle = horizontal.handle();

            const std::string verticalName =
                "PostProcess.Bloom.BlurVertical." + std::to_string(mip);
            RenderGraphPassBuilder vertical = m_renderGraph.addPass(
                {verticalName.c_str(), RgPassType::Graphics,
                 RhiQueueType::Graphics});
            vertical.dependsOn(horizontalHandle)
                .readTexture(resources.bloom[mip][1],
                             RhiResourceState::ShaderRead)
                .writeTexture(resources.bloom[mip][0],
                               RhiResourceState::RenderTarget)
                .setExecute([this, &debugService, &bloomTimer, mip,
                             lastMip = mip == bloomMipCount - 1](
                                RgPassContext& pass) {
                    recordBloomBlur(pass.commandList(), mip, false);
                    if (lastMip) {
                        debugService.endGpuTimer(pass.commandList(), bloomTimer);
                    }
                    return true;
                });
            graphTail = vertical.handle();
        }
    }

    const bool useAutoExposureTexture = autoExposureEnabled;
    const PostProcessCompositeParams compositeParams =
        buildCompositeParams(useAutoExposureTexture, bloomReady);
    RenderGraphPassBuilder params = m_renderGraph.addPass(
        {"PostProcess.CompositeParameters", RgPassType::Copy,
         RhiQueueType::Graphics});
    if (graphTail.isValid()) {
        params.dependsOn(graphTail);
    }
    params.writeBuffer(resources.compositeParams, RhiResourceState::TransferDst)
        .setExecute([this, &debugService, &compositeTimer, compositeParams](
                        RgPassContext& pass) {
            compositeTimer = debugService.beginGpuTimer(
                pass.commandList(), GpuTimerPass::Post);
            uploadCompositeParams(pass.commandList(), compositeParams);
            return true;
        });
    const RgPassHandle paramsHandle = params.handle();

    RenderGraphPassBuilder composite = m_renderGraph.addPass(
        {destination == CompositeDestination::Texture
             ? "PostProcess.CompositeTexture"
             : "PostProcess.CompositeBackbuffer",
         RgPassType::Graphics, RhiQueueType::Graphics});
    composite.dependsOn(paramsHandle)
        .readTexture(resources.hdrInput, RhiResourceState::ShaderRead)
        .readTexture(resources.noise, RhiResourceState::ShaderRead)
        .readTexture(resources.gbufferDepth, RhiResourceState::DepthRead)
        .readTexture(resources.sceneDepth, RhiResourceState::DepthRead)
        .readTexture(resources.exposureState[finalExposureReadIndex],
                     RhiResourceState::ShaderRead)
        .readBuffer(resources.compositeParams, RhiResourceState::UniformBuffer)
        .writeTexture(resources.output, RhiResourceState::RenderTarget);
    for (int mip = 0; mip < kBloomMipCount; ++mip) {
        composite.readTexture(resources.bloom[mip][0],
                              RhiResourceState::ShaderRead);
    }
    composite.setExecute([this, &debugService, &compositeTimer, destination,
                          outputView, outputWidth, outputHeight,
                          finalExposureReadIndex](RgPassContext& pass) {
        if (destination == CompositeDestination::Texture) {
            bindCompositeOutput(pass.commandList(), m_processingWidth,
                                m_processingHeight);
            renderComposite(pass.commandList(), m_compositeTexturePipeline,
                            finalExposureReadIndex);
        } else {
            bindBackbufferOutput(pass.commandList(), outputView,
                                 std::max(1, outputWidth),
                                 std::max(1, outputHeight), false);
            renderComposite(pass.commandList(), m_compositeSwapchainPipeline,
                            finalExposureReadIndex);
        }
        pass.commandList().endRendering();
        debugService.endGpuTimer(pass.commandList(), compositeTimer);
        return true;
    });

    const RgCompileResult compiled = m_renderGraph.compile();
    if (!compiled.succeeded()) {
        MECRAFT_LOG_STREAM(
            std::cerr << "[PostProcessPass] Render Graph compile failed: "
                      << compiled.message << '\n');
        return false;
    }
    const GpuTimerCheckpoint timerCheckpoint = debugService.gpuTimerCheckpoint();
    const RgExecuteResult executed = m_renderGraph.execute(
        rhiDevice, *m_commandListPool);
    if (!executed.succeeded()) {
        debugService.cancelGpuTimersSince(timerCheckpoint);
        MECRAFT_LOG_STREAM(
            std::cerr << "[PostProcessPass] Render Graph execution failed: "
                      << executed.message << '\n');
        return false;
    }

    if (autoExposureEnabled) {
        m_exposureStateReadIndex = finalExposureReadIndex;
        m_autoExposureInitialized = true;
        m_autoExposureSampleAccumulator = nextExposureAccumulator;
    }
    return true;
}

bool PostProcessPass::executeBlitGraph(
    RhiDevice& rhiDevice,
    const RhiTextureHandle sourceTexture,
    const RhiTextureViewHandle swapchainColorView,
    RenderDebugService& debugService) {
    if (m_commandListPool == nullptr || m_rhiDevice != &rhiDevice ||
        !sourceTexture.isValid() || !swapchainColorView.isValid()) {
        return false;
    }
    const RhiTextureHandle swapchainTexture =
        rhiDevice.currentSwapchainColorTexture();
    if (!swapchainTexture.isValid()) {
        return false;
    }

    m_renderGraph.reset();
    const auto importTexture = [&](const RhiTextureHandle texture,
                                   const RhiTextureViewHandle view,
                                   const RhiResourceState stableState,
                                   RgTextureHandle& graphTexture) {
        RhiTextureDesc desc;
        if (!rhiDevice.getTextureDesc(texture, desc)) {
            return false;
        }
        RgImportedTextureDesc imported;
        imported.name = desc.debugName;
        imported.texture = texture;
        imported.desc = desc;
        imported.initialState = stableState;
        imported.finalState = stableState;
        imported.defaultView = view;
        graphTexture = m_renderGraph.importTexture(imported);
        return graphTexture.isValid();
    };
    RgTextureHandle source;
    RgTextureHandle swapchain;
    if (!importTexture(sourceTexture, {}, RhiResourceState::ShaderRead, source) ||
        !importTexture(swapchainTexture, swapchainColorView,
                       RhiResourceState::Present, swapchain)) {
        return false;
    }

    RenderGraphPassBuilder blit = m_renderGraph.addPass(
        {"PostProcess.Blit", RgPassType::Copy, RhiQueueType::Graphics});
    blit.readTexture(source, RhiResourceState::TransferSrc)
        .writeTexture(swapchain, RhiResourceState::TransferDst)
        .setExecute([&debugService, sourceTexture, swapchainColorView](
                        RgPassContext& pass) {
            const GpuTimerSegmentToken timerToken = debugService.beginGpuTimer(
                pass.commandList(), GpuTimerPass::Post);
            RhiTextureBlit blitInfo;
            blitInfo.src = sourceTexture;
            blitInfo.dstView = swapchainColorView;
            pass.commandList().blitTexture(blitInfo);
            debugService.endGpuTimer(pass.commandList(), timerToken);
            return true;
        });

    const RgCompileResult compiled = m_renderGraph.compile();
    if (!compiled.succeeded()) {
        MECRAFT_LOG_STREAM(
            std::cerr << "[PostProcessPass] Blit graph compile failed: "
                      << compiled.message << '\n');
        return false;
    }
    const GpuTimerCheckpoint timerCheckpoint = debugService.gpuTimerCheckpoint();
    const RgExecuteResult executed = m_renderGraph.execute(
        rhiDevice, *m_commandListPool);
    if (!executed.succeeded()) {
        debugService.cancelGpuTimersSince(timerCheckpoint);
        MECRAFT_LOG_STREAM(
            std::cerr << "[PostProcessPass] Blit graph execution failed: "
                      << executed.message << '\n');
        return false;
    }
    return true;
}

void PostProcessPass::recordExposureStateInitialization(
    RhiCommandList& commandList,
    const float manualExposure) {
    const float initialState[4] = {
        std::max(manualExposure, 0.001f),
        m_lastAverageLum,
        m_lastTargetExposure,
        1.0f
    };
    for (int index = 0; index < 2; ++index) {
        RhiColorAttachment colorAttachment;
        colorAttachment.view = m_exposureStateView[index];
        colorAttachment.loadOp = RhiLoadOp::Clear;
        colorAttachment.storeOp = RhiStoreOp::Store;
        colorAttachment.clearColor[0] = initialState[0];
        colorAttachment.clearColor[1] = initialState[1];
        colorAttachment.clearColor[2] = initialState[2];
        colorAttachment.clearColor[3] = initialState[3];

        RhiRenderingInfo renderingInfo;
        renderingInfo.debugName = "ExposureStateInit";
        renderingInfo.renderArea = {0, 0, 1u, 1u};
        renderingInfo.colorAttachments = &colorAttachment;
        renderingInfo.colorAttachmentCount = 1u;
        commandList.beginRendering(renderingInfo);
        commandList.endRendering();
    }
}

void PostProcessPass::recordExposureDownsample(
    RhiCommandList& commandList,
    const int mip,
    const glm::ivec2& sourceSize,
    const bool sourceIsScene,
    const int sourceLod) {
    beginPostProcessColorOutput(commandList,
                                "ExposureDownsample",
                                m_exposureView[mip],
                                m_exposureMipSize[mip].x,
                                m_exposureMipSize[mip].y,
                                true);
    commandList.setGraphicsPipeline(m_exposureDownsamplePipeline);
    commandList.setBindGroup(0u, m_exposureDownsampleBindGroup[mip]);
    const ExposureDownsamplePushConstants pushConstants{
        glm::vec4(static_cast<float>(sourceSize.x),
                  static_cast<float>(sourceSize.y), 0.0f, 0.0f),
        glm::ivec4(sourceIsScene ? 1 : 0,
                   sourceIsScene ? sourceLod : 0,
                   0,
                   0)
    };
    commandList.pushConstants(&pushConstants,
                              sizeof(pushConstants),
                              rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
}

void PostProcessPass::recordExposureResolve(
    RhiCommandList& commandList,
    const int readIndex,
    const int writeIndex,
    const float elapsedFrameTime,
    const float manualExposure,
    const bool historyAvailable,
    const bool reuseExposure) {
    beginPostProcessColorOutput(commandList,
                                "ExposureResolve",
                                m_exposureStateView[writeIndex],
                                1,
                                1,
                                false);
    commandList.setGraphicsPipeline(m_exposureResolvePipeline);
    commandList.setBindGroup(0u, m_exposureResolveBindGroup[readIndex]);
    const ExposureResolvePushConstants resolvePushConstants{
        glm::vec4(elapsedFrameTime,
                  m_effects.autoExposureSpeed,
                  m_effects.autoExposureBias,
                  manualExposure),
        glm::ivec4(historyAvailable ? 1 : 0,
                   reuseExposure ? 1 : 0,
                   0,
                   0)
    };
    commandList.pushConstants(&resolvePushConstants,
                              sizeof(resolvePushConstants),
                              rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
}

void PostProcessPass::recordBloomExtract(RhiCommandList& commandList,
                                         const int mip) {
    beginPostProcessColorOutput(commandList,
                                "BloomExtract",
                                m_bloomView[mip][0],
                                m_bloomMipSize[mip].x,
                                m_bloomMipSize[mip].y,
                                true);
    commandList.setGraphicsPipeline(m_bloomExtractPipeline);
    commandList.setBindGroup(0u, m_bloomExtractBindGroup);
    const glm::ivec4 pushConstants(mip + 1, 0, 0, 0);
    commandList.pushConstants(&pushConstants,
                              sizeof(pushConstants),
                              rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
}

void PostProcessPass::recordBloomBlur(RhiCommandList& commandList,
                                      const int mip,
                                      const bool horizontal) {
    beginPostProcessColorOutput(commandList,
                                horizontal ? "BloomBlurHorizontal"
                                            : "BloomBlurVertical",
                                m_bloomView[mip][horizontal ? 1 : 0],
                                m_bloomMipSize[mip].x,
                                m_bloomMipSize[mip].y,
                                true);
    commandList.setGraphicsPipeline(m_bloomBlurPipeline);
    commandList.setBindGroup(0u, m_bloomBlurBindGroup[mip][horizontal ? 0 : 1]);
    const glm::vec4 pushConstants = horizontal
        ? glm::vec4(1.0f, 0.0f, 1.0f, 0.0f)
        : glm::vec4(0.0f, 1.0f, 1.0f, 0.0f);
    commandList.pushConstants(&pushConstants,
                              sizeof(pushConstants),
                              rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
}

void PostProcessPass::renderComposite(RhiCommandList& commandList,
                                      const RhiPipelineHandle pipeline,
                                      const int exposureStateIndex) {
    commandList.setGraphicsPipeline(pipeline);
    commandList.setBindGroup(0u, m_compositeBindGroup[exposureStateIndex]);
    commandList.draw(3u, 1u, 0u, 0u);
}

void PostProcessPass::uploadCompositeParams(
    RhiCommandList& commandList,
    const PostProcessCompositeParams& params) {
    commandList.updateBuffer(m_compositeParamsBuffer, 0u,
                             &params, sizeof(params));
}

PostProcessPass::PostProcessCompositeParams PostProcessPass::buildCompositeParams(
    const bool useAutoExposureTexture,
    const bool hasBloom) const {
    const bool sunRaysEnabled = m_effects.sunRaysEnabled && hasBloom;
    const float noiseDitherStrength = m_effects.shaderpackGradingEnabled
        ? m_effects.noiseDitherStrength
        : 0.0f;
    return {
        glm::ivec4(hasBloom ? 1 : 0,
                   useAutoExposureTexture ? 1 : 0,
                   sunRaysEnabled ? 1 : 0,
                   m_effects.shaderpackGradingEnabled ? 1 : 0),
        glm::ivec4(std::clamp(m_effects.bloomMipCount, 1, kBloomMipCount),
                   m_effects.tonemapMode,
                   m_effects.postprocessDebugMode,
                   0),
        glm::ivec4(m_effects.underwaterEnabled ? 1 : 0,
                   m_effects.purkinjeShiftEnabled ? 1 : 0,
                   m_effects.bloomyFogEnabled ? 1 : 0,
                   0),
        glm::vec4(m_effects.bloomStrength,
                  m_adaptedExposure,
                  m_effects.gamma,
                  m_effects.screenRollRadians),
        glm::vec4(m_effects.sunScreenPos,
                  m_effects.sunVisibility,
                  m_effects.sunRayStrength),
        glm::vec4(m_effects.colorTemperature,
                  m_effects.vibrance,
                  m_effects.splitToneStrength,
                  m_effects.vignetteStrength),
        glm::vec4(noiseDitherStrength,
                  m_effects.sharpenStrength,
                  m_effects.saturation,
                  m_effects.contrast),
        glm::vec4(m_effects.underwaterTint,
                  m_effects.underwaterStrength),
        glm::vec4(m_effects.weatherWetness,
                  m_effects.weatherStorm,
                  m_effects.snowStrength,
                  m_effects.skyWetness),
        glm::vec4(m_effects.fogWetness,
                  m_effects.cloudWetness,
                  m_effects.cameraRainVisibility,
                  m_effects.weatherExposureBias),
        glm::vec4(m_effects.weatherPostRainFog,
                  m_effects.gameTime,
                  0.0f,
                  0.0f)
    };
}

bool PostProcessPass::ensureSceneCaptureTargets(RhiDevice& rhiDevice,
                                                const int width,
                                                const int height) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyRhiResources();
    }
    m_rhiDevice = &rhiDevice;
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (m_sceneColorHandle.isValid() && m_sceneDepthHandle.isValid() &&
        m_captureWidth == width && m_captureHeight == height) {
        return true;
    }

    destroySceneCaptureTargets();
    const RhiTextureUsageFlags colorUsage =
        rhiFlag(RhiTextureUsage::Sampled) |
        rhiFlag(RhiTextureUsage::ColorAttachment) |
        rhiFlag(RhiTextureUsage::TransferSrc) |
        rhiFlag(RhiTextureUsage::TransferDst);
    const RhiTextureUsageFlags depthUsage =
        rhiFlag(RhiTextureUsage::Sampled) |
        rhiFlag(RhiTextureUsage::DepthStencilAttachment) |
        rhiFlag(RhiTextureUsage::TransferSrc) |
        rhiFlag(RhiTextureUsage::TransferDst);
    if (!createTextureAndView(rhiDevice,
                              "PostProcess.SceneColor",
                              RhiTextureFormat::Rgba16Float,
                              static_cast<uint32_t>(width),
                              static_cast<uint32_t>(height),
                              colorUsage,
                              m_sceneColorHandle,
                              m_sceneColorView) ||
        !createTextureAndView(rhiDevice,
                              "PostProcess.SceneDepth",
                              RhiTextureFormat::Depth32Float,
                              static_cast<uint32_t>(width),
                              static_cast<uint32_t>(height),
                              depthUsage,
                              m_sceneDepthHandle,
                              m_sceneDepthView)) {
        destroySceneCaptureTargets();
        return false;
    }

    RhiCommandList& initializeCommandList = beginCommandList(
        "PostProcess.SceneCaptureInitialization.Commands");
    initializeCommandList.textureBarrier({
        m_sceneColorHandle,
        RhiResourceState::Undefined,
        RhiResourceState::ShaderRead
    });
    initializeCommandList.textureBarrier({
        m_sceneDepthHandle,
        RhiResourceState::Undefined,
        RhiResourceState::DepthRead
    });
    submitCommandList(rhiDevice, initializeCommandList,
                      "PostProcess.SceneCaptureInitialization.Submit");
    m_captureWidth = width;
    m_captureHeight = height;
    return true;
}

bool PostProcessPass::ensureProcessingTargets(RhiDevice& rhiDevice,
                                              const int width,
                                              const int height) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyRhiResources();
    }
    m_rhiDevice = &rhiDevice;
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (m_bloomHandle[0][0].isValid() && m_exposureStateHandle[0].isValid() &&
        m_processingWidth == width && m_processingHeight == height) {
        return true;
    }

    destroyProcessingTargets();

    for (int mip = 0; mip < kBloomMipCount; ++mip) {
        const int divisor = 1 << (mip + 1);
        m_bloomMipSize[mip] = glm::ivec2(std::max(1, width / divisor),
                                         std::max(1, height / divisor));
        for (int ping = 0; ping < 2; ++ping) {
            if (!createTextureAndView(rhiDevice,
                                      "PostProcess.Bloom",
                                      RhiTextureFormat::Rgba16Float,
                                      static_cast<uint32_t>(m_bloomMipSize[mip].x),
                                      static_cast<uint32_t>(m_bloomMipSize[mip].y),
                                      rhiFlag(RhiTextureUsage::Sampled) |
                                          rhiFlag(RhiTextureUsage::ColorAttachment),
                                      m_bloomHandle[mip][ping],
                                      m_bloomView[mip][ping])) {
                destroyProcessingTargets();
                return false;
            }
        }
    }

    const int exposureBaseLod = std::min(
        kAutoExposureLod,
        std::max(0, static_cast<int>(std::floor(std::log2(static_cast<float>(
            std::max(width, height)))))));
    const glm::ivec2 exposureBaseSize(std::max(1, width >> exposureBaseLod),
                                      std::max(1, height >> exposureBaseLod));
    glm::ivec2 exposureSize(std::max(1, exposureBaseSize.x / 2),
                            std::max(1, exposureBaseSize.y / 2));
    m_exposureMipCount = 0;
    for (int mip = 0; mip < kExposureMipCount; ++mip) {
        m_exposureMipSize[mip] = exposureSize;
        if (!createTextureAndView(rhiDevice,
                                  "PostProcess.Exposure",
                                  RhiTextureFormat::Rg16Float,
                                  static_cast<uint32_t>(exposureSize.x),
                                  static_cast<uint32_t>(exposureSize.y),
                                  rhiFlag(RhiTextureUsage::Sampled) |
                                      rhiFlag(RhiTextureUsage::ColorAttachment),
                                  m_exposureHandle[mip],
                                  m_exposureView[mip])) {
            destroyProcessingTargets();
            return false;
        }
        ++m_exposureMipCount;
        if (exposureSize.x == 1 && exposureSize.y == 1) {
            break;
        }
        exposureSize = glm::ivec2(std::max(1, exposureSize.x / 2),
                                  std::max(1, exposureSize.y / 2));
    }

    for (int index = 0; index < 2; ++index) {
        if (!createTextureAndView(rhiDevice,
                                  "PostProcess.ExposureState",
                                  RhiTextureFormat::Rgba16Float,
                                  1u,
                                  1u,
                                  rhiFlag(RhiTextureUsage::Sampled) |
                                      rhiFlag(RhiTextureUsage::ColorAttachment),
                                  m_exposureStateHandle[index],
                                  m_exposureStateView[index])) {
            destroyProcessingTargets();
            return false;
        }
    }

    RhiCommandList& initializeCommandList = beginCommandList(
        "PostProcess.ProcessingInitialization.Commands");
    for (int mip = 0; mip < kBloomMipCount; ++mip) {
        for (int ping = 0; ping < 2; ++ping) {
            initializeCommandList.textureBarrier({
                m_bloomHandle[mip][ping],
                RhiResourceState::Undefined,
                RhiResourceState::ShaderRead
            });
        }
    }
    for (int mip = 0; mip < m_exposureMipCount; ++mip) {
        initializeCommandList.textureBarrier({
            m_exposureHandle[mip],
            RhiResourceState::Undefined,
            RhiResourceState::ShaderRead
        });
    }
    for (int index = 0; index < 2; ++index) {
        initializeCommandList.textureBarrier({
            m_exposureStateHandle[index],
            RhiResourceState::Undefined,
            RhiResourceState::RenderTarget
        });
        RhiColorAttachment exposureStateAttachment;
        exposureStateAttachment.view = m_exposureStateView[index];
        exposureStateAttachment.loadOp = RhiLoadOp::Clear;
        exposureStateAttachment.storeOp = RhiStoreOp::Store;
        exposureStateAttachment.clearColor[0] = 1.0f;
        exposureStateAttachment.clearColor[1] = 0.0f;
        exposureStateAttachment.clearColor[2] = 1.0f;
        exposureStateAttachment.clearColor[3] = 1.0f;
        RhiRenderingInfo exposureStateRendering;
        exposureStateRendering.debugName = "PostProcess.ExposureStateInitialization";
        exposureStateRendering.renderArea = {0, 0, 1u, 1u};
        exposureStateRendering.colorAttachments = &exposureStateAttachment;
        exposureStateRendering.colorAttachmentCount = 1u;
        initializeCommandList.beginRendering(exposureStateRendering);
        initializeCommandList.endRendering();
        initializeCommandList.textureBarrier({
            m_exposureStateHandle[index],
            RhiResourceState::RenderTarget,
            RhiResourceState::ShaderRead
        });
    }
    submitCommandList(rhiDevice, initializeCommandList,
                      "PostProcess.ProcessingInitialization.Submit");

    m_processingWidth = width;
    m_processingHeight = height;
    m_autoExposureInitialized = false;
    m_autoExposureSampleAccumulator = 0.0;
    return true;
}

bool PostProcessPass::ensureCompositeTarget(RhiDevice& rhiDevice,
                                            const int width,
                                            const int height) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyRhiResources();
    }
    m_rhiDevice = &rhiDevice;
    if (m_compositeHandle.isValid() && m_compositeView.isValid()) {
        return true;
    }
    destroyCompositeBindGroups();
    if (!createTextureAndView(rhiDevice,
                              "PostProcess.Composite",
                              RhiTextureFormat::Rgba8Unorm,
                              static_cast<uint32_t>(std::max(1, width)),
                              static_cast<uint32_t>(std::max(1, height)),
                              rhiFlag(RhiTextureUsage::Sampled) |
                                  rhiFlag(RhiTextureUsage::ColorAttachment) |
                                  rhiFlag(RhiTextureUsage::TransferSrc),
                              m_compositeHandle,
                              m_compositeView)) {
        return false;
    }
    RhiCommandList& commandList = beginCommandList(
        "PostProcess.CompositeTargetInitialization.Commands");
    commandList.textureBarrier({
        m_compositeHandle,
        RhiResourceState::Undefined,
        RhiResourceState::ShaderRead
    });
    submitCommandList(rhiDevice, commandList,
                      "PostProcess.CompositeTargetInitialization.Submit");
    return true;
}

bool PostProcessPass::ensureRhiPipelines(RhiDevice& rhiDevice) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyRhiResources();
    }
    m_rhiDevice = &rhiDevice;
    if (m_compositeTexturePipeline.isValid()) {
        return true;
    }

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> postProcessSource =
        renderer::rhi::loadShaderSource("assets/shaders/postprocess.frag");
    const std::optional<std::string> bloomExtractSource =
        renderer::rhi::loadShaderSource("assets/shaders/bloom_extract.frag");
    const std::optional<std::string> bloomBlurSource =
        renderer::rhi::loadShaderSource("assets/shaders/bloom_blur.frag");
    const std::optional<std::string> exposureDownsampleSource =
        renderer::rhi::loadShaderSource("assets/shaders/exposure_downsample.frag");
    const std::optional<std::string> exposureResolveSource =
        renderer::rhi::loadShaderSource("assets/shaders/exposure_resolve.frag");
    if (!vertexSource.has_value() || !postProcessSource.has_value() ||
        !bloomExtractSource.has_value() || !bloomBlurSource.has_value() ||
        !exposureDownsampleSource.has_value() || !exposureResolveSource.has_value()) {
        return false;
    }

    auto createShader = [&](const char* debugName,
                            const RhiShaderStage stage,
                            const std::string& source) {
        RhiShaderDesc desc;
        desc.debugName = debugName;
        desc.stage = stage;
        desc.source = source.c_str();
        desc.sourceSize = source.size();
        return rhiDevice.createShader(desc);
    };
    m_fullscreenVertexShader = createShader("PostProcess.Vertex",
                                             RhiShaderStage::Vertex,
                                             *vertexSource);
    m_postProcessFragmentShader = createShader("PostProcess.Composite.Fragment",
                                                RhiShaderStage::Fragment,
                                                *postProcessSource);
    m_bloomExtractFragmentShader = createShader("PostProcess.BloomExtract.Fragment",
                                                 RhiShaderStage::Fragment,
                                                 *bloomExtractSource);
    m_bloomBlurFragmentShader = createShader("PostProcess.BloomBlur.Fragment",
                                              RhiShaderStage::Fragment,
                                              *bloomBlurSource);
    m_exposureDownsampleFragmentShader = createShader(
        "PostProcess.ExposureDownsample.Fragment",
        RhiShaderStage::Fragment,
        *exposureDownsampleSource);
    m_exposureResolveFragmentShader = createShader("PostProcess.ExposureResolve.Fragment",
                                                    RhiShaderStage::Fragment,
                                                    *exposureResolveSource);
    if (!m_fullscreenVertexShader.isValid() || !m_postProcessFragmentShader.isValid() ||
        !m_bloomExtractFragmentShader.isValid() || !m_bloomBlurFragmentShader.isValid() ||
        !m_exposureDownsampleFragmentShader.isValid() ||
        !m_exposureResolveFragmentShader.isValid()) {
        destroyRhiResources();
        return false;
    }

    auto createSampler = [&](const RhiFilter filter, const RhiAddressMode addressMode) {
        RhiSamplerDesc desc;
        desc.minFilter = filter;
        desc.magFilter = filter;
        desc.mipmapMode = RhiMipmapMode::Nearest;
        desc.addressU = addressMode;
        desc.addressV = addressMode;
        desc.addressW = addressMode;
        return rhiDevice.createSampler(desc);
    };
    m_linearClampSampler = createSampler(RhiFilter::Linear, RhiAddressMode::ClampToEdge);
    m_nearestClampSampler = createSampler(RhiFilter::Nearest, RhiAddressMode::ClampToEdge);
    m_nearestRepeatSampler = createSampler(RhiFilter::Nearest, RhiAddressMode::Repeat);
    if (!m_linearClampSampler.isValid() || !m_nearestClampSampler.isValid() ||
        !m_nearestRepeatSampler.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "PostProcess.Params";
    bufferDesc.size = sizeof(PostProcessCompositeParams);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Uniform) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    bufferDesc.initialState = RhiResourceState::UniformBuffer;
    m_compositeParamsBuffer = rhiDevice.createBuffer(bufferDesc, nullptr, 0u);
    if (!m_compositeParamsBuffer.isValid()) {
        destroyRhiResources();
        return false;
    }

    auto createTextureLayout = [&](const char* debugName, const uint32_t textureCount) {
        RhiBindGroupLayoutDesc desc;
        desc.debugName = debugName;
        for (uint32_t binding = 0u; binding < textureCount; ++binding) {
            desc.entries.push_back({
                binding,
                RhiBindingType::CombinedTextureSampler,
                rhiFlag(RhiShaderStage::Fragment),
                1u
            });
        }
        return rhiDevice.createBindGroupLayout(desc);
    };
    m_singleTextureBindGroupLayout = createTextureLayout(
        "PostProcess.SingleTexture.BindGroupLayout",
        1u);
    m_twoTextureBindGroupLayout = createTextureLayout(
        "PostProcess.TwoTexture.BindGroupLayout",
        2u);

    RhiBindGroupLayoutDesc compositeLayoutDesc;
    compositeLayoutDesc.debugName = "PostProcess.Composite.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 12u; ++binding) {
        compositeLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    compositeLayoutDesc.entries.push_back({
        12u,
        RhiBindingType::UniformBuffer,
        rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    m_compositeBindGroupLayout = rhiDevice.createBindGroupLayout(compositeLayoutDesc);
    if (!m_singleTextureBindGroupLayout.isValid() ||
        !m_twoTextureBindGroupLayout.isValid() ||
        !m_compositeBindGroupLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    auto createPipelineLayout = [&](const char* debugName,
                                    const RhiBindGroupLayoutHandle bindGroupLayout,
                                    const uint32_t pushConstantBytes) {
        RhiPipelineLayoutDesc desc;
        desc.debugName = debugName;
        desc.bindGroupLayouts.push_back(bindGroupLayout);
        desc.pushConstantBytes = pushConstantBytes;
        desc.pushConstantStages = pushConstantBytes > 0u
            ? rhiFlag(RhiShaderStage::Fragment)
            : 0u;
        return rhiDevice.createPipelineLayout(desc);
    };
    m_exposureDownsamplePipelineLayout = createPipelineLayout(
        "PostProcess.ExposureDownsample.PipelineLayout",
        m_singleTextureBindGroupLayout,
        sizeof(ExposureDownsamplePushConstants));
    m_exposureResolvePipelineLayout = createPipelineLayout(
        "PostProcess.ExposureResolve.PipelineLayout",
        m_twoTextureBindGroupLayout,
        sizeof(ExposureResolvePushConstants));
    m_bloomExtractPipelineLayout = createPipelineLayout(
        "PostProcess.BloomExtract.PipelineLayout",
        m_singleTextureBindGroupLayout,
        sizeof(glm::ivec4));
    m_bloomBlurPipelineLayout = createPipelineLayout(
        "PostProcess.BloomBlur.PipelineLayout",
        m_singleTextureBindGroupLayout,
        sizeof(glm::vec4));
    m_compositePipelineLayout = createPipelineLayout(
        "PostProcess.Composite.PipelineLayout",
        m_compositeBindGroupLayout,
        0u);
    if (!m_exposureDownsamplePipelineLayout.isValid() ||
        !m_exposureResolvePipelineLayout.isValid() ||
        !m_bloomExtractPipelineLayout.isValid() ||
        !m_bloomBlurPipelineLayout.isValid() ||
        !m_compositePipelineLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    auto createPipeline = [&](const char* debugName,
                              const RhiShaderHandle fragmentShader,
                              const RhiPipelineLayoutHandle layout,
                              const RhiTextureFormat colorFormat) {
        RhiGraphicsPipelineDesc desc;
        desc.debugName = debugName;
        desc.vertexShader = m_fullscreenVertexShader;
        desc.fragmentShader = fragmentShader;
        desc.layout = layout;
        desc.topology = RhiPrimitiveTopology::TriangleList;
        desc.raster.cullMode = RhiCullMode::None;
        desc.depthStencil.depthTestEnabled = false;
        desc.depthStencil.depthWriteEnabled = false;
        desc.colorFormats.push_back(colorFormat);
        desc.blend.attachments.push_back({});
        return rhiDevice.createGraphicsPipeline(desc);
    };
    m_exposureDownsamplePipeline = createPipeline(
        "PostProcess.ExposureDownsample.Pipeline",
        m_exposureDownsampleFragmentShader,
        m_exposureDownsamplePipelineLayout,
        RhiTextureFormat::Rg16Float);
    m_exposureResolvePipeline = createPipeline(
        "PostProcess.ExposureResolve.Pipeline",
        m_exposureResolveFragmentShader,
        m_exposureResolvePipelineLayout,
        RhiTextureFormat::Rgba16Float);
    m_bloomExtractPipeline = createPipeline(
        "PostProcess.BloomExtract.Pipeline",
        m_bloomExtractFragmentShader,
        m_bloomExtractPipelineLayout,
        RhiTextureFormat::Rgba16Float);
    m_bloomBlurPipeline = createPipeline(
        "PostProcess.BloomBlur.Pipeline",
        m_bloomBlurFragmentShader,
        m_bloomBlurPipelineLayout,
        RhiTextureFormat::Rgba16Float);
    m_compositeTexturePipeline = createPipeline(
        "PostProcess.CompositeTexture.Pipeline",
        m_postProcessFragmentShader,
        m_compositePipelineLayout,
        RhiTextureFormat::Rgba8Unorm);
    if (!m_exposureDownsamplePipeline.isValid() ||
        !m_exposureResolvePipeline.isValid() ||
        !m_bloomExtractPipeline.isValid() || !m_bloomBlurPipeline.isValid() ||
        !m_compositeTexturePipeline.isValid()) {
        destroyRhiResources();
        return false;
    }
    return true;
}

bool PostProcessPass::ensureSwapchainCompositePipeline(
    RhiDevice& rhiDevice,
    const RhiTextureFormat colorFormat) {
    if (colorFormat == RhiTextureFormat::Undefined ||
        !m_fullscreenVertexShader.isValid() || !m_postProcessFragmentShader.isValid() ||
        !m_compositePipelineLayout.isValid()) {
        return false;
    }
    if (m_compositeSwapchainPipeline.isValid() &&
        m_compositeSwapchainFormat == colorFormat) {
        return true;
    }
    if (m_compositeSwapchainPipeline.isValid()) {
        rhiDevice.destroyPipeline(m_compositeSwapchainPipeline);
        m_compositeSwapchainPipeline = {};
    }

    RhiGraphicsPipelineDesc desc;
    desc.debugName = "PostProcess.CompositeSwapchain.Pipeline";
    desc.vertexShader = m_fullscreenVertexShader;
    desc.fragmentShader = m_postProcessFragmentShader;
    desc.layout = m_compositePipelineLayout;
    desc.topology = RhiPrimitiveTopology::TriangleList;
    desc.raster.cullMode = RhiCullMode::None;
    desc.depthStencil.depthTestEnabled = false;
    desc.depthStencil.depthWriteEnabled = false;
    desc.colorFormats.push_back(colorFormat);
    desc.blend.attachments.push_back({});
    m_compositeSwapchainPipeline = rhiDevice.createGraphicsPipeline(desc);
    if (!m_compositeSwapchainPipeline.isValid()) {
        return false;
    }
    m_compositeSwapchainFormat = colorFormat;
    return true;
}

bool PostProcessPass::ensureNoiseTextureView(RhiDevice& rhiDevice) {
    if (m_noiseTextureView.isValid() &&
        sameTextureHandle(m_noiseViewTexture, m_noiseTexture)) {
        return true;
    }
    destroyCompositeBindGroups();
    if (m_noiseTextureView.isValid()) {
        rhiDevice.destroyTextureView(m_noiseTextureView);
        m_noiseTextureView = {};
        m_noiseViewTexture = {};
    }
    if (!m_noiseTexture.isValid()) {
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = m_noiseTexture;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = RhiTextureFormat::Rgba8Unorm;
    viewDesc.baseMip = 0u;
    viewDesc.mipCount = 1u;
    viewDesc.baseLayer = 0u;
    viewDesc.layerCount = 1u;
    m_noiseTextureView = rhiDevice.createTextureView(viewDesc);
    if (!m_noiseTextureView.isValid()) {
        return false;
    }
    m_noiseViewTexture = m_noiseTexture;
    return true;
}

bool PostProcessPass::ensureGbufferDepthTextureView(RhiDevice& rhiDevice,
                                                    const RhiTextureHandle texture) {
    if (m_gbufferDepthTextureView.isValid() &&
        sameTextureHandle(m_gbufferDepthViewTexture, texture)) {
        return true;
    }
    destroyCompositeBindGroups();
    if (m_gbufferDepthTextureView.isValid()) {
        rhiDevice.destroyTextureView(m_gbufferDepthTextureView);
        m_gbufferDepthTextureView = {};
        m_gbufferDepthViewTexture = {};
    }
    if (!texture.isValid()) {
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = texture;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = RhiTextureFormat::Depth32Float;
    viewDesc.baseMip = 0u;
    viewDesc.mipCount = 1u;
    viewDesc.baseLayer = 0u;
    viewDesc.layerCount = 1u;
    m_gbufferDepthTextureView = rhiDevice.createTextureView(viewDesc);
    if (!m_gbufferDepthTextureView.isValid()) {
        return false;
    }
    m_gbufferDepthViewTexture = texture;
    return true;
}

bool PostProcessPass::rebuildTargetBindGroups() {
    if (m_bloomExtractBindGroup.isValid() && m_exposureMipCount > 0 &&
        m_exposureDownsampleBindGroup[m_exposureMipCount - 1].isValid() &&
        m_exposureResolveBindGroup[0].isValid() &&
        m_exposureResolveBindGroup[1].isValid()) {
        return true;
    }
    if (m_rhiDevice == nullptr || !m_singleTextureBindGroupLayout.isValid() ||
        !m_twoTextureBindGroupLayout.isValid() || m_exposureMipCount <= 0) {
        return false;
    }
    destroyTargetBindGroups();

    auto createSingleTextureBindGroup = [&](const RhiTextureViewHandle view,
                                            const RhiSamplerHandle sampler) {
        RhiBindGroupDesc desc;
        desc.layout = m_singleTextureBindGroupLayout;
        RhiBindGroupEntry entry;
        entry.binding = 0u;
        entry.resource.combinedTextureSampler.textureView = view;
        entry.resource.combinedTextureSampler.sampler = sampler;
        desc.entries.push_back(entry);
        return m_rhiDevice->createBindGroup(desc);
    };
    m_bloomExtractBindGroup = createSingleTextureBindGroup(m_hdrInputView,
                                                            m_linearClampSampler);
    if (!m_bloomExtractBindGroup.isValid()) {
        destroyTargetBindGroups();
        return false;
    }
    for (int mip = 0; mip < kBloomMipCount; ++mip) {
        m_bloomBlurBindGroup[mip][0] = createSingleTextureBindGroup(
            m_bloomView[mip][0],
            m_linearClampSampler);
        m_bloomBlurBindGroup[mip][1] = createSingleTextureBindGroup(
            m_bloomView[mip][1],
            m_linearClampSampler);
        if (!m_bloomBlurBindGroup[mip][0].isValid() ||
            !m_bloomBlurBindGroup[mip][1].isValid()) {
            destroyTargetBindGroups();
            return false;
        }
    }
    for (int mip = 0; mip < m_exposureMipCount; ++mip) {
        const RhiTextureViewHandle source = mip == 0
            ? m_hdrInputView
            : m_exposureView[mip - 1];
        m_exposureDownsampleBindGroup[mip] = createSingleTextureBindGroup(
            source,
            m_nearestClampSampler);
        if (!m_exposureDownsampleBindGroup[mip].isValid()) {
            destroyTargetBindGroups();
            return false;
        }
    }

    const RhiTextureViewHandle finalExposureView =
        m_exposureView[m_exposureMipCount - 1];
    for (int readIndex = 0; readIndex < 2; ++readIndex) {
        RhiBindGroupDesc desc;
        desc.layout = m_twoTextureBindGroupLayout;
        const RhiTextureViewHandle views[2] = {
            finalExposureView,
            m_exposureStateView[readIndex]
        };
        for (uint32_t binding = 0u; binding < 2u; ++binding) {
            RhiBindGroupEntry entry;
            entry.binding = binding;
            entry.resource.combinedTextureSampler.textureView = views[binding];
            entry.resource.combinedTextureSampler.sampler = m_nearestClampSampler;
            desc.entries.push_back(entry);
        }
        m_exposureResolveBindGroup[readIndex] = m_rhiDevice->createBindGroup(desc);
        if (!m_exposureResolveBindGroup[readIndex].isValid()) {
            destroyTargetBindGroups();
            return false;
        }
    }
    return true;
}

bool PostProcessPass::rebuildCompositeBindGroups() {
    if (m_compositeBindGroup[0].isValid() && m_compositeBindGroup[1].isValid()) {
        return true;
    }
    if (m_rhiDevice == nullptr || !m_compositeBindGroupLayout.isValid() ||
        !m_hdrInputView.isValid() || !m_noiseTextureView.isValid() ||
        !m_gbufferDepthTextureView.isValid() || !m_sceneDepthView.isValid()) {
        return false;
    }
    destroyCompositeBindGroups();

    const RhiTextureViewHandle commonViews[12] = {
        m_hdrInputView,
        m_bloomView[0][0],
        m_bloomView[1][0],
        m_bloomView[2][0],
        m_bloomView[3][0],
        m_bloomView[4][0],
        m_bloomView[5][0],
        m_bloomView[6][0],
        m_noiseTextureView,
        m_gbufferDepthTextureView,
        {},
        m_sceneDepthView
    };
    const RhiSamplerHandle samplers[12] = {
        m_linearClampSampler,
        m_linearClampSampler,
        m_linearClampSampler,
        m_linearClampSampler,
        m_linearClampSampler,
        m_linearClampSampler,
        m_linearClampSampler,
        m_linearClampSampler,
        m_nearestRepeatSampler,
        m_nearestClampSampler,
        m_nearestClampSampler,
        m_nearestClampSampler
    };
    for (int exposureIndex = 0; exposureIndex < 2; ++exposureIndex) {
        RhiBindGroupDesc desc;
        desc.layout = m_compositeBindGroupLayout;
        for (uint32_t binding = 0u; binding < 12u; ++binding) {
            RhiBindGroupEntry entry;
            entry.binding = binding;
            entry.resource.combinedTextureSampler.textureView = binding == 10u
                ? m_exposureStateView[exposureIndex]
                : commonViews[binding];
            entry.resource.combinedTextureSampler.sampler = samplers[binding];
            desc.entries.push_back(entry);
        }
        RhiBindGroupEntry uniformEntry;
        uniformEntry.binding = 12u;
        uniformEntry.resource.buffer.buffer = m_compositeParamsBuffer;
        uniformEntry.resource.buffer.offset = 0u;
        uniformEntry.resource.buffer.range = sizeof(PostProcessCompositeParams);
        desc.entries.push_back(uniformEntry);
        m_compositeBindGroup[exposureIndex] = m_rhiDevice->createBindGroup(desc);
        if (!m_compositeBindGroup[exposureIndex].isValid()) {
            destroyCompositeBindGroups();
            return false;
        }
    }
    return true;
}

void PostProcessPass::bindCompositeOutput(RhiCommandList& commandList,
                                          const int width,
                                          const int height) {
    beginPostProcessColorOutput(commandList,
                                "PostProcessCompositeTexture",
                                m_compositeView,
                                width,
                                height,
                                false);
}

void PostProcessPass::bindBackbufferOutput(
    RhiCommandList& commandList,
    const RhiTextureViewHandle swapchainColorView,
    const int width,
    const int height,
    const bool clearColor) {
    beginPostProcessColorOutput(commandList,
                                "PostProcessBackbuffer",
                                swapchainColorView,
                                width,
                                height,
                                clearColor);
}

void PostProcessPass::destroyTargetBindGroups() {
    if (m_rhiDevice != nullptr) {
        if (m_bloomExtractBindGroup.isValid()) {
            m_rhiDevice->destroyBindGroup(m_bloomExtractBindGroup);
        }
        for (int mip = 0; mip < kBloomMipCount; ++mip) {
            for (RhiBindGroupHandle& bindGroup : m_bloomBlurBindGroup[mip]) {
                if (bindGroup.isValid()) {
                    m_rhiDevice->destroyBindGroup(bindGroup);
                }
                bindGroup = {};
            }
        }
        for (RhiBindGroupHandle& bindGroup : m_exposureDownsampleBindGroup) {
            if (bindGroup.isValid()) {
                m_rhiDevice->destroyBindGroup(bindGroup);
            }
            bindGroup = {};
        }
        for (RhiBindGroupHandle& bindGroup : m_exposureResolveBindGroup) {
            if (bindGroup.isValid()) {
                m_rhiDevice->destroyBindGroup(bindGroup);
            }
            bindGroup = {};
        }
    }
    m_bloomExtractBindGroup = {};
}

void PostProcessPass::destroyCompositeBindGroups() {
    if (m_rhiDevice != nullptr) {
        for (RhiBindGroupHandle& bindGroup : m_compositeBindGroup) {
            if (bindGroup.isValid()) {
                m_rhiDevice->destroyBindGroup(bindGroup);
            }
            bindGroup = {};
        }
    }
}

void PostProcessPass::destroySceneCaptureTargets() {
    destroyCompositeBindGroups();
    destroyTargetBindGroups();
    if (m_rhiDevice != nullptr) {
        auto destroyTextureAndView = [&](RhiTextureHandle& texture,
                                         RhiTextureViewHandle& view) {
            if (view.isValid()) {
                m_rhiDevice->destroyTextureView(view);
            }
            if (texture.isValid()) {
                m_rhiDevice->destroyTexture(texture);
            }
            view = {};
            texture = {};
        };
        destroyTextureAndView(m_sceneColorHandle, m_sceneColorView);
        destroyTextureAndView(m_sceneDepthHandle, m_sceneDepthView);
    }
    m_hdrInputHandle = {};
    m_hdrInputView = {};
    m_hdrInputWidth = 0;
    m_hdrInputHeight = 0;
    m_captureWidth = 0;
    m_captureHeight = 0;
}

void PostProcessPass::destroyProcessingTargets() {
    destroyCompositeBindGroups();
    destroyTargetBindGroups();
    if (m_rhiDevice != nullptr) {
        auto destroyTextureAndView = [&](RhiTextureHandle& texture,
                                         RhiTextureViewHandle& view) {
            if (view.isValid()) {
                m_rhiDevice->destroyTextureView(view);
            }
            if (texture.isValid()) {
                m_rhiDevice->destroyTexture(texture);
            }
            view = {};
            texture = {};
        };
        destroyTextureAndView(m_compositeHandle, m_compositeView);
        for (int mip = 0; mip < kBloomMipCount; ++mip) {
            m_bloomMipSize[mip] = glm::ivec2(0);
            for (int ping = 0; ping < 2; ++ping) {
                destroyTextureAndView(m_bloomHandle[mip][ping],
                                      m_bloomView[mip][ping]);
            }
        }
        for (int mip = 0; mip < kExposureMipCount; ++mip) {
            m_exposureMipSize[mip] = glm::ivec2(0);
            destroyTextureAndView(m_exposureHandle[mip], m_exposureView[mip]);
        }
        for (int index = 0; index < 2; ++index) {
            destroyTextureAndView(m_exposureStateHandle[index],
                                  m_exposureStateView[index]);
        }
    }
    m_exposureMipCount = 0;
    m_exposureStateReadIndex = 0;
    m_autoExposureInitialized = false;
    m_autoExposureSampleAccumulator = 0.0;
    m_processingWidth = 0;
    m_processingHeight = 0;
}

void PostProcessPass::destroyRhiResources() {
    if (m_rhiDevice != nullptr) {
        m_renderGraph.releaseTransientResources(*m_rhiDevice);
    }
    destroyCompositeBindGroups();
    destroySceneCaptureTargets();
    destroyProcessingTargets();
    if (m_rhiDevice != nullptr) {
        if (m_noiseTextureView.isValid()) {
            m_rhiDevice->destroyTextureView(m_noiseTextureView);
        }
        if (m_gbufferDepthTextureView.isValid()) {
            m_rhiDevice->destroyTextureView(m_gbufferDepthTextureView);
        }
        const RhiPipelineHandle pipelines[] = {
            m_exposureDownsamplePipeline,
            m_exposureResolvePipeline,
            m_bloomExtractPipeline,
            m_bloomBlurPipeline,
            m_compositeTexturePipeline,
            m_compositeSwapchainPipeline
        };
        for (const RhiPipelineHandle pipeline : pipelines) {
            if (pipeline.isValid()) {
                m_rhiDevice->destroyPipeline(pipeline);
            }
        }
        const RhiShaderHandle shaders[] = {
            m_fullscreenVertexShader,
            m_postProcessFragmentShader,
            m_bloomExtractFragmentShader,
            m_bloomBlurFragmentShader,
            m_exposureDownsampleFragmentShader,
            m_exposureResolveFragmentShader
        };
        for (const RhiShaderHandle shader : shaders) {
            if (shader.isValid()) {
                m_rhiDevice->destroyShader(shader);
            }
        }
        const RhiPipelineLayoutHandle pipelineLayouts[] = {
            m_exposureDownsamplePipelineLayout,
            m_exposureResolvePipelineLayout,
            m_bloomExtractPipelineLayout,
            m_bloomBlurPipelineLayout,
            m_compositePipelineLayout
        };
        for (const RhiPipelineLayoutHandle layout : pipelineLayouts) {
            if (layout.isValid()) {
                m_rhiDevice->destroyPipelineLayout(layout);
            }
        }
        const RhiBindGroupLayoutHandle bindGroupLayouts[] = {
            m_singleTextureBindGroupLayout,
            m_twoTextureBindGroupLayout,
            m_compositeBindGroupLayout
        };
        for (const RhiBindGroupLayoutHandle layout : bindGroupLayouts) {
            if (layout.isValid()) {
                m_rhiDevice->destroyBindGroupLayout(layout);
            }
        }
        if (m_compositeParamsBuffer.isValid()) {
            m_rhiDevice->destroyBuffer(m_compositeParamsBuffer);
        }
        const RhiSamplerHandle samplers[] = {
            m_linearClampSampler,
            m_nearestClampSampler,
            m_nearestRepeatSampler
        };
        for (const RhiSamplerHandle sampler : samplers) {
            if (sampler.isValid()) {
                m_rhiDevice->destroySampler(sampler);
            }
        }
    }

    m_noiseViewTexture = {};
    m_noiseTextureView = {};
    m_gbufferDepthViewTexture = {};
    m_gbufferDepthTextureView = {};
    m_compositeParamsBuffer = {};
    m_linearClampSampler = {};
    m_nearestClampSampler = {};
    m_nearestRepeatSampler = {};
    m_singleTextureBindGroupLayout = {};
    m_twoTextureBindGroupLayout = {};
    m_compositeBindGroupLayout = {};
    m_exposureDownsamplePipelineLayout = {};
    m_exposureResolvePipelineLayout = {};
    m_bloomExtractPipelineLayout = {};
    m_bloomBlurPipelineLayout = {};
    m_compositePipelineLayout = {};
    m_fullscreenVertexShader = {};
    m_postProcessFragmentShader = {};
    m_bloomExtractFragmentShader = {};
    m_bloomBlurFragmentShader = {};
    m_exposureDownsampleFragmentShader = {};
    m_exposureResolveFragmentShader = {};
    m_exposureDownsamplePipeline = {};
    m_exposureResolvePipeline = {};
    m_bloomExtractPipeline = {};
    m_bloomBlurPipeline = {};
    m_compositeTexturePipeline = {};
    m_compositeSwapchainPipeline = {};
    m_compositeSwapchainFormat = RhiTextureFormat::Undefined;
    m_rhiDevice = nullptr;
}
