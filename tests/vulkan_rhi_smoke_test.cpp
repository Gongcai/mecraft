#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiRenderGraph.h"
#include "renderer/rhi/vulkan/VkRhiDevice.h"
#include "renderer/rhi/vulkan/VkRhiInterop.h"
#include "renderer/passes/TemporalUpscalePass.h"
#include "renderer/presentation/PresentationController.h"
#include "thread/ThreadPool.h"

#if defined(MECRAFT_ENABLE_FSR31)
#include "renderer/upscaling/Fsr31VulkanContext.h"
#endif
#if defined(MECRAFT_ENABLE_STREAMLINE)
#include "renderer/upscaling/DlssVulkanContext.h"
#include "renderer/upscaling/StreamlineRuntime.h"
#endif

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

namespace {

enum class FrameAttempt { Success, Retry, Error };

[[nodiscard]] bool validateIndependentUiPresentation(VkRhiDevice& device, RhiCommandListPool& commandPool,
                                                     GLFWwindow* window) {
    std::unique_ptr<PresentationBackend> backend = createNativePresentationBackend(device);
    PresentationController controller(*backend);
    if (!controller.initUiComposition(device)) {
        return false;
    }

    RhiTextureHandle firstUiTexture;
    for (uint32_t frameIndex = 0u; frameIndex < 4u; ++frameIndex) {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        const PresentationFrame frame = controller.beginFrame(width, height);
        if (!frame.shouldRender()) {
            return false;
        }
        const std::optional<PresentationUiFrame> uiFrame = controller.acquireUiFrame(frame);
        if (!uiFrame.has_value() || uiFrame->width != frame.acquired.width ||
            uiFrame->height != frame.acquired.height || uiFrame->colorFormat != device.swapchainColorFormat() ||
            !uiFrame->premultipliedAlpha || !uiFrame->colorTexture.isValid() || !uiFrame->colorView.isValid()) {
            return false;
        }
        const std::optional<PresentationFrameResources> resources = controller.frameResources(frame, *uiFrame);
        if (!resources.has_value() || resources->realFrameNumber != frame.realFrameNumber ||
            resources->width != frame.acquired.width || resources->height != frame.acquired.height ||
            !resources->uiPremultipliedAlpha) {
            return false;
        }
        if (frameIndex == 0u) {
            firstUiTexture = uiFrame->colorTexture;
        } else if (frameIndex == 1u && firstUiTexture.index == uiFrame->colorTexture.index &&
                   firstUiTexture.generation == uiFrame->colorTexture.generation) {
            return false;
        }

        RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Graphics);
        if (commands == nullptr ||
            !commands->begin({"VulkanSmoke.IndependentUiPresentation", RhiCommandListType::Graphics}) ||
            !controller.beginUiRendering(*commands, *uiFrame) ||
            !controller.endUiRenderingAndComposite(*commands, frame, *uiFrame) ||
            !controller.submitUiFrame(*commands, *uiFrame)) {
            return false;
        }
        const PresentationCompleteResult presented = controller.presentFrame(frame);
        if (presented.result != PresentationResult::Presented) {
            return false;
        }
    }

    const PresentationStatistics& statistics = controller.statistics();
    const bool validStatistics = statistics.realFramesAcquired == 4u && statistics.realFramesPresented == 4u &&
                                 statistics.displayedFrames == 4u && statistics.generatedFramesPresented == 0u &&
                                 statistics.failedOperations == 0u;
    controller.shutdownUiComposition();
    return validStatistics;
}

[[nodiscard]] bool validateVulkanInterop(VkRhiDevice& device, RhiCommandListPool& commandPool,
                                         const RhiTextureHandle texture, const RhiTextureViewHandle view,
                                         const uint32_t width, const uint32_t height, const uint32_t depth) {
    const auto deviceInfo = VkRhiInterop::deviceInfo(device);
    const auto textureInfo = VkRhiInterop::textureInfo(device, texture, view);
    if (!deviceInfo.has_value() || deviceInfo->instance == VK_NULL_HANDLE ||
        deviceInfo->physicalDevice == VK_NULL_HANDLE || deviceInfo->device == VK_NULL_HANDLE ||
        deviceInfo->graphicsQueue == VK_NULL_HANDLE || deviceInfo->graphicsQueueFamily == VK_QUEUE_FAMILY_IGNORED ||
        !textureInfo.has_value() || textureInfo->image == VK_NULL_HANDLE || textureInfo->view == VK_NULL_HANDLE ||
        textureInfo->format != VK_FORMAT_R32G32B32A32_SFLOAT || textureInfo->extent.width != width ||
        textureInfo->extent.height != height || textureInfo->extent.depth != depth || textureInfo->arrayLayers != 1u ||
        textureInfo->imageType != VK_IMAGE_TYPE_3D || textureInfo->viewType != VK_IMAGE_VIEW_TYPE_3D ||
        textureInfo->mipCount != 1u || textureInfo->layerCount != 1u ||
        textureInfo->aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
        VkRhiInterop::textureInfo(device, {}, view).has_value() ||
        VkRhiInterop::resourceLayout(RhiResourceState::ShaderRead) != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        return false;
    }

    RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Graphics);
    if (commands == nullptr || !commands->begin({"VulkanSmoke.Interop", RhiCommandListType::Graphics})) {
        return false;
    }
    const auto nativeCommands = VkRhiInterop::commandBuffer(device, *commands);
    if (!nativeCommands.has_value() || *nativeCommands == VK_NULL_HANDLE || !commands->end() ||
        VkRhiInterop::commandBuffer(device, *commands).has_value()) {
        return false;
    }
    return true;
}

[[nodiscard]] bool validateTemporalOutputTarget(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    TemporalUpscalePass pass;
    pass.init(device, commandPool);
    UpscaleSettings settings;
    settings.type = TemporalUpscalerType::Fsr31;
    settings.quality = TemporalUpscaleQuality::Quality;
    constexpr TemporalExtent kInitialExtent{640u, 360u};
    if (!pass.prepareOutputTarget(settings, kInitialExtent, kInitialExtent)) {
        return false;
    }
    const RhiTextureHandle initialTexture = pass.outputTextureHandle();
    const RhiTextureViewHandle initialView = pass.outputTextureViewHandle();
    const auto initialInfo = VkRhiInterop::textureInfo(device, initialTexture, initialView);
    if (!initialInfo.has_value() || initialInfo->format != VK_FORMAT_R16G16B16A16_SFLOAT ||
        initialInfo->extent.width != kInitialExtent.width || initialInfo->extent.height != kInitialExtent.height ||
        (initialInfo->usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0u ||
        (initialInfo->usage & VK_IMAGE_USAGE_STORAGE_BIT) == 0u ||
        !pass.prepareOutputTarget(settings, kInitialExtent, kInitialExtent) ||
        pass.outputTextureHandle().index != initialTexture.index ||
        pass.outputTextureHandle().generation != initialTexture.generation) {
        return false;
    }

    constexpr TemporalExtent kResizedExtent{960u, 540u};
    if (!pass.prepareOutputTarget(settings, kResizedExtent, kResizedExtent)) {
        return false;
    }
    const RhiTextureHandle resizedTexture = pass.outputTextureHandle();
    const bool targetRecreated =
        resizedTexture.index != initialTexture.index || resizedTexture.generation != initialTexture.generation;
    const auto resizedInfo = VkRhiInterop::textureInfo(device, resizedTexture, pass.outputTextureViewHandle());
    settings.type = TemporalUpscalerType::Native;
    if (!targetRecreated || !resizedInfo.has_value() || resizedInfo->extent.width != kResizedExtent.width ||
        resizedInfo->extent.height != kResizedExtent.height ||
        !pass.prepareOutputTarget(settings, kResizedExtent, kResizedExtent) || pass.outputTextureHandle().isValid() ||
        pass.outputTextureViewHandle().isValid()) {
        return false;
    }
    pass.shutdown();
    return true;
}

#if defined(MECRAFT_ENABLE_FSR31)
[[nodiscard]] bool validateFsr31VulkanContext(VkRhiDevice& device) {
    Fsr31VulkanContext context;
    const Fsr31VulkanContextCreateResult invalid = context.initialize(device, {{}, {1920u, 1080u}, false, false});
    if (invalid.status != Fsr31VulkanContextCreateStatus::InvalidRenderExtent) {
        return false;
    }

    constexpr TemporalExtent kRenderExtent{1280u, 720u};
    constexpr TemporalExtent kOutputExtent{1920u, 1080u};
    const Fsr31VulkanContextCreateResult created =
        context.initialize(device, {kRenderExtent, kOutputExtent, false, false});
    if (!created.succeeded() || !context.isInitialized() || context.maxRenderExtent() != kRenderExtent ||
        context.maxOutputExtent() != kOutputExtent || context.scratchMemorySize() == 0u ||
        context.initialize(device, {kRenderExtent, kOutputExtent, false, false}).status !=
            Fsr31VulkanContextCreateStatus::AlreadyInitialized) {
        std::cerr << "FSR 3.1 Vulkan context creation failed: status " << static_cast<uint32_t>(created.status)
                  << ", SDK error " << created.sdkError << '\n';
        return false;
    }
    const Fsr31VulkanContextDestroyResult destroyed = context.shutdown();
    if (!destroyed.succeeded() || context.isInitialized() || context.scratchMemorySize() != 0u ||
        context.shutdown().status != Fsr31VulkanContextDestroyStatus::NotInitialized) {
        return false;
    }
    for (uint32_t iteration = 0u; iteration < 3u; ++iteration) {
        const Fsr31VulkanContextCreateResult recreated =
            context.initialize(device, {kRenderExtent, kOutputExtent, false, iteration == 1u});
        if (!recreated.succeeded() || !context.shutdown().succeeded()) {
            return false;
        }
    }
    return !context.isInitialized() && context.scratchMemorySize() == 0u;
}

struct Fsr31SmokeTexture {
    RhiTextureHandle texture;
    RhiTextureViewHandle view;
};

[[nodiscard]] bool createFsr31SmokeTexture(VkRhiDevice& device, const char* const debugName,
                                           const RhiTextureFormat format, const TemporalExtent extent,
                                           const RhiTextureUsageFlags usage, const void* const pixels,
                                           const size_t sizeBytes, const RhiResourceState finalState,
                                           Fsr31SmokeTexture& output) {
    RhiTextureDesc textureDesc;
    textureDesc.debugName = debugName;
    textureDesc.format = format;
    textureDesc.width = extent.width;
    textureDesc.height = extent.height;
    textureDesc.usage = usage;
    RhiTextureInitialData initialData;
    initialData.pixels = pixels;
    initialData.sizeBytes = sizeBytes;
    initialData.finalState = finalState;
    output.texture = device.createTexture(textureDesc, &initialData);
    if (!output.texture.isValid()) {
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = output.texture;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = format;
    output.view = device.createTextureView(viewDesc);
    if (!output.view.isValid()) {
        device.destroyTexture(output.texture);
        output.texture = {};
        return false;
    }
    return true;
}

void destroyFsr31SmokeTexture(VkRhiDevice& device, Fsr31SmokeTexture& resource) {
    if (resource.view.isValid()) {
        device.destroyTextureView(resource.view);
    }
    if (resource.texture.isValid()) {
        device.destroyTexture(resource.texture);
    }
    resource = {};
}

[[nodiscard]] bool validateFsr31VulkanDispatch(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    constexpr TemporalExtent kRenderExtent{320u, 180u};
    constexpr TemporalExtent kOutputExtent{480u, 270u};
    const size_t renderPixelCount = static_cast<size_t>(kRenderExtent.width) * kRenderExtent.height;
    std::vector<uint16_t> hdrPixels(renderPixelCount * 4u, 0u);
    std::vector<float> depthPixels(renderPixelCount, 1.0f);
    std::vector<uint16_t> velocityPixels(renderPixelCount * 2u, 0u);
    constexpr uint16_t kHalfFloatOne = 0x3c00u;
    const uint16_t exposurePixels[4] = {kHalfFloatOne, 0u, 0u, 0u};
    std::vector<uint8_t> maskPixels(renderPixelCount, 0u);

    Fsr31SmokeTexture hdr;
    Fsr31SmokeTexture depth;
    Fsr31SmokeTexture velocity;
    Fsr31SmokeTexture exposure;
    Fsr31SmokeTexture reactive;
    Fsr31SmokeTexture transparency;
    const auto destroyInputs = [&]() {
        destroyFsr31SmokeTexture(device, transparency);
        destroyFsr31SmokeTexture(device, reactive);
        destroyFsr31SmokeTexture(device, exposure);
        destroyFsr31SmokeTexture(device, velocity);
        destroyFsr31SmokeTexture(device, depth);
        destroyFsr31SmokeTexture(device, hdr);
    };

    const RhiTextureUsageFlags sampledUsage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);
    const bool inputsCreated =
        createFsr31SmokeTexture(device, "VulkanSmoke.FSR31.Hdr", RhiTextureFormat::Rgba16Float, kRenderExtent,
                                sampledUsage, hdrPixels.data(), hdrPixels.size() * sizeof(uint16_t),
                                RhiResourceState::ShaderRead, hdr) &&
        createFsr31SmokeTexture(device, "VulkanSmoke.FSR31.Depth", RhiTextureFormat::Depth32Float, kRenderExtent,
                                sampledUsage | rhiFlag(RhiTextureUsage::DepthStencilAttachment), depthPixels.data(),
                                depthPixels.size() * sizeof(float), RhiResourceState::DepthRead, depth) &&
        createFsr31SmokeTexture(device, "VulkanSmoke.FSR31.Velocity", RhiTextureFormat::Rg16Float, kRenderExtent,
                                sampledUsage, velocityPixels.data(), velocityPixels.size() * sizeof(uint16_t),
                                RhiResourceState::ShaderRead, velocity) &&
        createFsr31SmokeTexture(device, "VulkanSmoke.FSR31.Exposure", RhiTextureFormat::Rgba16Float, {1u, 1u},
                                sampledUsage, exposurePixels, sizeof(exposurePixels), RhiResourceState::ShaderRead,
                                exposure) &&
        createFsr31SmokeTexture(device, "VulkanSmoke.FSR31.Reactive", RhiTextureFormat::R8Unorm, kRenderExtent,
                                sampledUsage, maskPixels.data(), maskPixels.size(), RhiResourceState::ShaderRead,
                                reactive) &&
        createFsr31SmokeTexture(device, "VulkanSmoke.FSR31.Transparency", RhiTextureFormat::R8Unorm, kRenderExtent,
                                sampledUsage, maskPixels.data(), maskPixels.size(), RhiResourceState::ShaderRead,
                                transparency);
    if (!inputsCreated) {
        std::cerr << "FSR 3.1 smoke test failed to create input resources\n";
        destroyInputs();
        return false;
    }

    TemporalUpscalePass pass;
    pass.init(device, commandPool);
    UpscaleSettings settings;
    settings.type = TemporalUpscalerType::Fsr31;
    settings.quality = TemporalUpscaleQuality::Quality;
    settings.debugVisualizationEnabled = true;
    if (!pass.prepareOutputTarget(settings, kRenderExtent, kOutputExtent)) {
        std::cerr << "FSR 3.1 smoke test failed to prepare the output target: texture "
                  << pass.outputTextureHandle().index << ", view " << pass.outputTextureViewHandle().index << '\n';
        pass.shutdown();
        destroyInputs();
        return false;
    }

    TemporalFrameInput frame;
    frame.extents = makeTemporalFrameExtents(kRenderExtent, kRenderExtent, kRenderExtent, kOutputExtent);
    frame.motionVectorScale =
        glm::vec2(static_cast<float>(kRenderExtent.width), static_cast<float>(kRenderExtent.height));
    frame.frameDeltaMilliseconds = 1000.0f / 60.0f;
    frame.preExposure = 1.0f;
    frame.cameraNear = 0.1f;
    frame.cameraFar = 1000.0f;
    frame.verticalFovRadians = 1.0f;
    frame.resetReasons = temporalResetReasonBit(TemporalResetReason::FirstFrame);
    frame.textures.hdrColor = hdr.texture;
    frame.textures.hdrColorView = hdr.view;
    frame.textures.depth = depth.texture;
    frame.textures.depthView = depth.view;
    frame.textures.velocity = velocity.texture;
    frame.textures.velocityView = velocity.view;
    frame.textures.exposure = exposure.texture;
    frame.textures.exposureView = exposure.view;
    frame.textures.reactiveMask = reactive.texture;
    frame.textures.reactiveMaskView = reactive.view;
    frame.textures.transparencyMask = transparency.texture;
    frame.textures.transparencyMaskView = transparency.view;
    frame.textures.outputHdrColor = pass.outputTextureHandle();
    frame.textures.outputHdrColorView = pass.outputTextureViewHandle();

    const TemporalUpscaleResult firstResult = pass.execute(settings, frame);
    TemporalUpscaleResult secondResult;
    if (firstResult.succeeded()) {
        frame.resetReasons = temporalResetReasonBit(TemporalResetReason::None);
        secondResult = pass.execute(settings, frame);
    }
    device.waitIdle();
    const bool dispatched = firstResult.succeeded() && secondResult.succeeded() &&
                            secondResult.outputHdrColor.isValid() && secondResult.outputHdrColorView.isValid() &&
                            secondResult.outputExtent == kOutputExtent;
    if (!dispatched) {
        std::cerr << "FSR 3.1 smoke dispatch failed: "
                  << TemporalUpscalePass::statusText(firstResult.succeeded() ? secondResult.status : firstResult.status)
                  << ", SDK error " << (firstResult.succeeded() ? secondResult.sdkError : firstResult.sdkError) << '\n';
    }
    pass.shutdown();
    destroyInputs();
    return dispatched;
}
#endif

#if defined(MECRAFT_ENABLE_STREAMLINE)
struct DlssSmokeTexture {
    RhiTextureHandle texture;
    RhiTextureViewHandle view;
};

[[nodiscard]] bool renderStableFrame(VkRhiDevice& device, RhiCommandListPool& commandPool, GLFWwindow* window);

[[nodiscard]] bool createDlssSmokeTexture(VkRhiDevice& device, const char* const debugName,
                                          const RhiTextureFormat format, const TemporalExtent extent,
                                          const RhiTextureUsageFlags usage, const void* const pixels,
                                          const size_t sizeBytes, const RhiResourceState finalState,
                                          DlssSmokeTexture& output) {
    RhiTextureDesc textureDesc;
    textureDesc.debugName = debugName;
    textureDesc.format = format;
    textureDesc.width = extent.width;
    textureDesc.height = extent.height;
    textureDesc.usage = usage;
    RhiTextureInitialData initialData;
    initialData.pixels = pixels;
    initialData.sizeBytes = sizeBytes;
    initialData.finalState = finalState;
    output.texture = device.createTexture(textureDesc, &initialData);
    if (!output.texture.isValid()) {
        return false;
    }
    RhiTextureViewDesc viewDesc;
    viewDesc.texture = output.texture;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = format;
    output.view = device.createTextureView(viewDesc);
    if (!output.view.isValid()) {
        device.destroyTexture(output.texture);
        output.texture = {};
        return false;
    }
    return true;
}

void destroyDlssSmokeTexture(VkRhiDevice& device, DlssSmokeTexture& resource) {
    if (resource.view.isValid()) {
        device.destroyTextureView(resource.view);
    }
    if (resource.texture.isValid()) {
        device.destroyTexture(resource.texture);
    }
    resource = {};
}

[[nodiscard]] bool validateDlssVulkanDispatch(VkRhiDevice& device, RhiCommandListPool& commandPool,
                                              GLFWwindow* window) {
    constexpr TemporalExtent kOutputExtent{1280u, 720u};
    const DlssRenderExtentResult queried = queryDlssRenderExtent(TemporalUpscaleQuality::Quality, kOutputExtent);
    if (!queried.succeeded()) {
        std::cerr << "DLSS smoke test failed to query the render extent: " << StreamlineRuntime::instance().lastError()
                  << '\n';
        return false;
    }
    const TemporalExtent renderExtent = queried.extent;
    const size_t renderPixelCount = static_cast<size_t>(renderExtent.width) * renderExtent.height;
    std::vector<uint16_t> hdrPixels(renderPixelCount * 4u, 0u);
    std::vector<float> depthPixels(renderPixelCount, 1.0f);
    std::vector<uint16_t> velocityPixels(renderPixelCount * 2u, 0u);
    constexpr uint16_t kHalfFloatOne = 0x3c00u;
    const uint16_t exposurePixels[4] = {kHalfFloatOne, 0u, 0u, 0u};
    std::vector<uint8_t> maskPixels(renderPixelCount, 0u);

    DlssSmokeTexture hdr;
    DlssSmokeTexture depth;
    DlssSmokeTexture velocity;
    DlssSmokeTexture exposure;
    DlssSmokeTexture reactive;
    DlssSmokeTexture transparency;
    const auto destroyInputs = [&]() {
        destroyDlssSmokeTexture(device, transparency);
        destroyDlssSmokeTexture(device, reactive);
        destroyDlssSmokeTexture(device, exposure);
        destroyDlssSmokeTexture(device, velocity);
        destroyDlssSmokeTexture(device, depth);
        destroyDlssSmokeTexture(device, hdr);
    };
    const RhiTextureUsageFlags sampledUsage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);
    const bool inputsCreated =
        createDlssSmokeTexture(device, "VulkanSmoke.DLSS.Hdr", RhiTextureFormat::Rgba16Float, renderExtent,
                               sampledUsage, hdrPixels.data(), hdrPixels.size() * sizeof(uint16_t),
                               RhiResourceState::ShaderRead, hdr) &&
        createDlssSmokeTexture(device, "VulkanSmoke.DLSS.Depth", RhiTextureFormat::Depth32Float, renderExtent,
                               sampledUsage | rhiFlag(RhiTextureUsage::DepthStencilAttachment), depthPixels.data(),
                               depthPixels.size() * sizeof(float), RhiResourceState::DepthRead, depth) &&
        createDlssSmokeTexture(device, "VulkanSmoke.DLSS.Velocity", RhiTextureFormat::Rg16Float, renderExtent,
                               sampledUsage, velocityPixels.data(), velocityPixels.size() * sizeof(uint16_t),
                               RhiResourceState::ShaderRead, velocity) &&
        createDlssSmokeTexture(device, "VulkanSmoke.DLSS.Exposure", RhiTextureFormat::Rgba16Float, {1u, 1u},
                               sampledUsage, exposurePixels, sizeof(exposurePixels), RhiResourceState::ShaderRead,
                               exposure) &&
        createDlssSmokeTexture(device, "VulkanSmoke.DLSS.Reactive", RhiTextureFormat::R8Unorm, renderExtent,
                               sampledUsage, maskPixels.data(), maskPixels.size(), RhiResourceState::ShaderRead,
                               reactive) &&
        createDlssSmokeTexture(device, "VulkanSmoke.DLSS.Transparency", RhiTextureFormat::R8Unorm, renderExtent,
                               sampledUsage, maskPixels.data(), maskPixels.size(), RhiResourceState::ShaderRead,
                               transparency);
    if (!inputsCreated) {
        std::cerr << "DLSS smoke test failed to create input resources\n";
        destroyInputs();
        return false;
    }

    TemporalUpscalePass pass;
    pass.init(device, commandPool);
    UpscaleSettings settings;
    settings.type = TemporalUpscalerType::Dlss;
    settings.quality = TemporalUpscaleQuality::Quality;
    if (!pass.prepareOutputTarget(settings, renderExtent, kOutputExtent)) {
        std::cerr << "DLSS smoke test failed to prepare the output target: "
                  << StreamlineRuntime::instance().lastError() << '\n';
        pass.shutdown();
        destroyInputs();
        return false;
    }

    TemporalFrameInput frame;
    frame.extents = makeTemporalFrameExtents(renderExtent, renderExtent, renderExtent, kOutputExtent);
    frame.motionVectorScale =
        glm::vec2(static_cast<float>(renderExtent.width), static_cast<float>(renderExtent.height));
    frame.frameDeltaMilliseconds = 1000.0f / 60.0f;
    frame.preExposure = 1.0f;
    frame.cameraNear = 0.1f;
    frame.cameraFar = 1000.0f;
    frame.verticalFovRadians = 1.0f;
    frame.cameraAspectRatio = static_cast<float>(kOutputExtent.width) / static_cast<float>(kOutputExtent.height);
    frame.resetReasons = temporalResetReasonBit(TemporalResetReason::FirstFrame);
    frame.textures.hdrColor = hdr.texture;
    frame.textures.hdrColorView = hdr.view;
    frame.textures.depth = depth.texture;
    frame.textures.depthView = depth.view;
    frame.textures.velocity = velocity.texture;
    frame.textures.velocityView = velocity.view;
    frame.textures.exposure = exposure.texture;
    frame.textures.exposureView = exposure.view;
    frame.textures.reactiveMask = reactive.texture;
    frame.textures.reactiveMaskView = reactive.view;
    frame.textures.transparencyMask = transparency.texture;
    frame.textures.transparencyMaskView = transparency.view;
    frame.textures.outputHdrColor = pass.outputTextureHandle();
    frame.textures.outputHdrColorView = pass.outputTextureViewHandle();

    const DlssJitterResult firstJitter = queryDlssJitter(frame.frameIndex, renderExtent, kOutputExtent);
    frame.jitter = firstJitter.jitter;
    const TemporalUpscaleResult firstResult = pass.execute(settings, frame);
    TemporalUpscaleResult secondResult;
    if (firstResult.succeeded()) {
        frame.frameIndex = 1u;
        frame.jitter = queryDlssJitter(frame.frameIndex, renderExtent, kOutputExtent).jitter;
        frame.resetReasons = temporalResetReasonBit(TemporalResetReason::None);
        secondResult = pass.execute(settings, frame);
    }
    device.waitIdle();
    const bool dispatched = firstResult.succeeded() && secondResult.succeeded() &&
                            secondResult.outputHdrColor.isValid() && secondResult.outputHdrColorView.isValid() &&
                            secondResult.outputExtent == kOutputExtent &&
                            renderStableFrame(device, commandPool, window);
    if (!dispatched) {
        std::cerr << "DLSS smoke dispatch failed: "
                  << TemporalUpscalePass::statusText(firstResult.succeeded() ? secondResult.status : firstResult.status)
                  << ", " << StreamlineRuntime::instance().lastError() << '\n';
    }
    pass.shutdown();
    destroyInputs();
    return dispatched;
}
#endif

[[nodiscard]] FrameAttempt renderFrame(VkRhiDevice& device, RhiCommandListPool& commandPool, const uint32_t width,
                                       const uint32_t height) {
    if (!device.resizeSwapchain(width, height)) {
        return FrameAttempt::Error;
    }
    const RhiFrameAcquireResult frame = device.acquireFrame();
    if (frame.status != RhiFrameStatus::Success && frame.status != RhiFrameStatus::Suboptimal) {
        return frame.status == RhiFrameStatus::OutOfDate || frame.status == RhiFrameStatus::Minimized
                   ? FrameAttempt::Retry
                   : FrameAttempt::Error;
    }

    RhiCommandList* clearCommands = commandPool.acquire(RhiCommandListType::Graphics);
    if (clearCommands == nullptr || !clearCommands->begin({"VulkanSmoke.Clear", RhiCommandListType::Graphics})) {
        return FrameAttempt::Error;
    }
    clearCommands->textureBarrier({frame.colorTexture, RhiResourceState::Present, RhiResourceState::RenderTarget});
    RhiColorAttachment colorAttachment;
    colorAttachment.view = frame.colorView;
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.02f;
    colorAttachment.clearColor[1] = 0.08f;
    colorAttachment.clearColor[2] = 0.16f;
    colorAttachment.clearColor[3] = 1.0f;
    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "VulkanSmoke.Clear";
    renderingInfo.renderArea = {0, 0, frame.width, frame.height};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    clearCommands->beginRendering(renderingInfo);
    clearCommands->endRendering();
    clearCommands->textureBarrier({frame.colorTexture, RhiResourceState::RenderTarget, RhiResourceState::Present});
    if (!clearCommands->end()) {
        return FrameAttempt::Error;
    }
    RhiCommandList* firstSubmission[] = {clearCommands};
    if (!device.submit({"VulkanSmoke.ClearSubmit", firstSubmission, 1u})) {
        return FrameAttempt::Error;
    }

    RhiCommandList* tailCommands = commandPool.acquire(RhiCommandListType::Graphics);
    if (tailCommands == nullptr || !tailCommands->begin({"VulkanSmoke.Tail", RhiCommandListType::Graphics}) ||
        !tailCommands->end()) {
        return FrameAttempt::Error;
    }
    RhiCommandList* secondSubmission[] = {tailCommands};
    RhiSubmissionToken secondToken;
    if (!device.submit({"VulkanSmoke.TailSubmit", secondSubmission, 1u}, &secondToken)) {
        return FrameAttempt::Error;
    }
    const RhiFrameStatus presentStatus = device.presentFrame({frame.frameIndex, frame.imageIndex});
    if (presentStatus != RhiFrameStatus::Success && presentStatus != RhiFrameStatus::Suboptimal) {
        return presentStatus == RhiFrameStatus::OutOfDate || presentStatus == RhiFrameStatus::Minimized
                   ? FrameAttempt::Retry
                   : FrameAttempt::Error;
    }
    return device.waitForSubmission(secondToken) ? FrameAttempt::Success : FrameAttempt::Error;
}

[[nodiscard]] bool renderStableFrame(VkRhiDevice& device, RhiCommandListPool& commandPool, GLFWwindow* window) {
    for (uint32_t attempt = 0u; attempt < 20u; ++attempt) {
        glfwWaitEventsTimeout(0.02);
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth <= 0 || framebufferHeight <= 0) {
            continue;
        }
        const FrameAttempt result = renderFrame(device, commandPool, static_cast<uint32_t>(framebufferWidth),
                                                static_cast<uint32_t>(framebufferHeight));
        if (result == FrameAttempt::Success) {
            return true;
        }
        if (result == FrameAttempt::Error) {
            return false;
        }
    }
    return false;
}

#if defined(MECRAFT_ENABLE_STREAMLINE)
[[nodiscard]] bool validateDlssFrameGenerationSwapchainLifecycle(VkRhiDevice& device, RhiCommandListPool& commandPool,
                                                                 GLFWwindow* window) {
    StreamlineRuntime& streamline = StreamlineRuntime::instance();
    if (!streamline.dlssFrameGenerationSupported()) {
        return !streamline.dlssFrameGenerationLoaded();
    }
    if (streamline.dlssFrameGenerationLoaded()) {
        return false;
    }

    const bool restoreVsync = device.vsyncEnabled();
    if (restoreVsync && !device.setVsyncEnabled(false)) {
        return false;
    }
    if (!VkRhiInterop::recreateFrameGenerationSwapchain(device, true) || !streamline.dlssFrameGenerationLoaded()) {
        return false;
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    StreamlineDlssFrameGenerationOptions options;
    options.enabled = false;
    options.renderWidth = static_cast<uint32_t>(width);
    options.renderHeight = static_cast<uint32_t>(height);
    options.outputWidth = static_cast<uint32_t>(width);
    options.outputHeight = static_cast<uint32_t>(height);
    options.backBufferCount = device.capabilities().swapchainImageCount;
    options.colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    options.depthFormat = VK_FORMAT_D32_SFLOAT;
    options.motionVectorFormat = VK_FORMAT_R16G16_SFLOAT;
    options.hudlessFormat = VK_FORMAT_B8G8R8A8_UNORM;
    options.uiFormat = VK_FORMAT_B8G8R8A8_UNORM;
    if (width <= 0 || height <= 0 || !streamline.configureDlssFrameGeneration(0u, options)) {
        return false;
    }
    const bool loadedFramePresented = renderStableFrame(device, commandPool, window);
    if (!VkRhiInterop::recreateFrameGenerationSwapchain(device, false) || streamline.dlssFrameGenerationLoaded()) {
        return false;
    }
    if (device.vsyncEnabled() != restoreVsync && !device.setVsyncEnabled(restoreVsync)) {
        return false;
    }
    return loadedFramePresented && renderStableFrame(device, commandPool, window);
}
#endif

[[nodiscard]] bool rejectDestroyedResourceSubmission(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "VulkanSmoke.DestroyedBuffer";
    bufferDesc.size = 256u;
    bufferDesc.usage = rhiFlag(RhiBufferUsage::TransferDst);
    const RhiBufferHandle buffer = device.createBuffer(bufferDesc, nullptr, 0u);
    if (!buffer.isValid())
        return false;

    RhiCommandList* referencingCommands = commandPool.acquire(RhiCommandListType::Graphics);
    if (referencingCommands == nullptr ||
        !referencingCommands->begin({"VulkanSmoke.DestroyedResource", RhiCommandListType::Graphics})) {
        device.destroyBuffer(buffer);
        return false;
    }
    referencingCommands->bufferBarrier({buffer, RhiResourceState::Undefined, RhiResourceState::TransferDst});
    if (!referencingCommands->end()) {
        device.destroyBuffer(buffer);
        return false;
    }
    device.destroyBuffer(buffer);

    RhiCommandList* unrelatedCommands = commandPool.acquire(RhiCommandListType::Graphics);
    if (unrelatedCommands == nullptr ||
        !unrelatedCommands->begin({"VulkanSmoke.Unrelated", RhiCommandListType::Graphics}) ||
        !unrelatedCommands->end()) {
        return false;
    }
    RhiCommandList* unrelatedSubmission[] = {unrelatedCommands};
    RhiSubmissionToken unrelatedToken;
    if (!device.submit({"VulkanSmoke.UnrelatedSubmit", unrelatedSubmission, 1u}, &unrelatedToken) ||
        !device.waitForSubmission(unrelatedToken)) {
        return false;
    }
    RhiCommandList* invalidSubmission[] = {referencingCommands};
    return !device.submit({"VulkanSmoke.DestroyedResourceSubmit", invalidSubmission, 1u});
}

[[nodiscard]] bool cancelAcquiredFrame(VkRhiDevice& device, GLFWwindow* window) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0 ||
        !device.resizeSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
        return false;
    }
    const RhiFrameAcquireResult frame = device.acquireFrame();
    if (frame.status != RhiFrameStatus::Success && frame.status != RhiFrameStatus::Suboptimal) {
        return false;
    }
    if (!device.cancelFrame({frame.frameIndex, frame.imageIndex})) {
        std::cerr << "Vulkan cancel-frame smoke check failed\n";
        return false;
    }
    return true;
}

[[nodiscard]] bool cancelSubmittedFrame(VkRhiDevice& device, RhiCommandListPool& commandPool, GLFWwindow* window) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0 ||
        !device.resizeSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
        return false;
    }
    const RhiFrameAcquireResult frame = device.acquireFrame();
    if (frame.status != RhiFrameStatus::Success && frame.status != RhiFrameStatus::Suboptimal) {
        return false;
    }
    RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Graphics);
    if (commands == nullptr || !commands->begin({"VulkanSmoke.CancelSubmitted", RhiCommandListType::Graphics}) ||
        !commands->end()) {
        return false;
    }
    RhiCommandList* submissions[] = {commands};
    if (!device.submit({"VulkanSmoke.CancelSubmitted", submissions, 1u}) ||
        !device.cancelFrame({frame.frameIndex, frame.imageIndex})) {
        std::cerr << "Vulkan submitted cancel-frame smoke check failed\n";
        return false;
    }
    return true;
}

[[nodiscard]] bool createDrawParametersShader(VkRhiDevice& device) {
    constexpr char kVertexShader[] = R"glsl(
#version 450 core
#extension GL_ARB_shader_draw_parameters : require

void main() {
    const float drawOffset = float(gl_DrawIDARB + gl_BaseInstanceARB) * 0.001;
    gl_Position = vec4(drawOffset, 0.0, 0.0, 1.0);
}
)glsl";
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "VulkanSmoke.DrawParameters";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = kVertexShader;
    shaderDesc.sourceSize = sizeof(kVertexShader) - 1u;
    const RhiShaderHandle shader = device.createShader(shaderDesc);
    if (!shader.isValid()) {
        return false;
    }
    device.destroyShader(shader);
    return true;
}

[[nodiscard]] bool validateIndependentBlendPipeline(VkRhiDevice& device) {
    constexpr char kVertexSource[] = R"glsl(
#version 450 core
void main() {
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
)glsl";
    constexpr char kFragmentSource[] = R"glsl(
#version 450 core
layout(location = 0) out vec4 outColor0;
layout(location = 1) out vec4 outColor1;
void main() {
    if (gl_FragCoord.x < 0.0) {
        discard;
    }
    outColor0 = vec4(1.0);
    outColor1 = vec4(0.5);
}
)glsl";

    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "VulkanSmoke.IndependentBlend.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = kVertexSource;
    shaderDesc.sourceSize = sizeof(kVertexSource) - 1u;
    const RhiShaderHandle vertexShader = device.createShader(shaderDesc);
    shaderDesc.debugName = "VulkanSmoke.IndependentBlend.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = kFragmentSource;
    shaderDesc.sourceSize = sizeof(kFragmentSource) - 1u;
    const RhiShaderHandle fragmentShader = device.createShader(shaderDesc);

    RhiPipelineLayoutDesc layoutDesc;
    layoutDesc.debugName = "VulkanSmoke.IndependentBlend.Layout";
    const RhiPipelineLayoutHandle layout = device.createPipelineLayout(layoutDesc);
    if (!vertexShader.isValid() || !fragmentShader.isValid() || !layout.isValid()) {
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "VulkanSmoke.IndependentBlend.Pipeline";
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.layout = layout;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba8Unorm, RhiTextureFormat::Rgba8Unorm};
    pipelineDesc.blend.attachments.resize(2u);
    pipelineDesc.blend.attachments[1].blendEnabled = true;
    pipelineDesc.blend.attachments[1].srcColor = RhiBlendFactor::One;
    pipelineDesc.blend.attachments[1].dstColor = RhiBlendFactor::Zero;
    pipelineDesc.blend.attachments[1].srcAlpha = RhiBlendFactor::One;
    pipelineDesc.blend.attachments[1].dstAlpha = RhiBlendFactor::Zero;

    const uint64_t validationErrorsBefore = device.validationErrorCount();
    const RhiPipelineHandle pipeline = device.createGraphicsPipeline(pipelineDesc);
    const bool valid = pipeline.isValid() && device.validationErrorCount() == validationErrorsBefore;
    if (pipeline.isValid())
        device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(layout);
    device.destroyShader(fragmentShader);
    device.destroyShader(vertexShader);
    return valid;
}

[[nodiscard]] bool validateOffscreenCoordinateContract(VkRhiDevice& device, RhiCommandListPool& commandPool,
                                                       GLFWwindow* window) {
    constexpr uint32_t kWidth = 4u;
    constexpr uint32_t kHeight = 4u;
    constexpr uint32_t kRenderY = 1u;
    constexpr uint32_t kRenderHeight = 2u;
    constexpr uint32_t kBytesPerPixel = 4u;
    constexpr uint32_t kBytesPerRow = kWidth * kBytesPerPixel;
    constexpr uint32_t kReadbackSize = kBytesPerRow * kHeight;

    RhiTextureDesc textureDesc;
    textureDesc.debugName = "VulkanSmoke.OffscreenOrientation";
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = kWidth;
    textureDesc.height = kHeight;
    textureDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment) | rhiFlag(RhiTextureUsage::TransferSrc);
    const RhiTextureHandle texture = device.createTexture(textureDesc, nullptr);
    if (!texture.isValid()) {
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = texture;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = textureDesc.format;
    const RhiTextureViewHandle view = device.createTextureView(viewDesc);
    if (!view.isValid()) {
        return false;
    }

    constexpr char kVertexSource[] = R"glsl(
#version 450 core
layout(location = 0) out vec2 vTexCoord;
void main() {
    const vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    vec2 position = positions[gl_VertexIndex];
    vTexCoord = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
)glsl";
    constexpr char kFragmentSource[] = R"glsl(
#version 450 core
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vTexCoord.y < 0.5
        ? vec4(1.0, 0.0, 0.0, 1.0)
        : vec4(0.0, 0.0, 1.0, 1.0);
}
)glsl";

    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "VulkanSmoke.OffscreenOrientation.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = kVertexSource;
    shaderDesc.sourceSize = sizeof(kVertexSource) - 1u;
    const RhiShaderHandle vertexShader = device.createShader(shaderDesc);
    shaderDesc.debugName = "VulkanSmoke.OffscreenOrientation.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = kFragmentSource;
    shaderDesc.sourceSize = sizeof(kFragmentSource) - 1u;
    const RhiShaderHandle fragmentShader = device.createShader(shaderDesc);
    if (!vertexShader.isValid() || !fragmentShader.isValid()) {
        return false;
    }

    RhiPipelineLayoutDesc layoutDesc;
    layoutDesc.debugName = "VulkanSmoke.OffscreenOrientation.Layout";
    const RhiPipelineLayoutHandle layout = device.createPipelineLayout(layoutDesc);
    if (!layout.isValid()) {
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "VulkanSmoke.OffscreenOrientation.Pipeline";
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.layout = layout;
    pipelineDesc.raster.cullMode = RhiCullMode::Back;
    pipelineDesc.raster.frontFace = RhiFrontFace::CounterClockwise;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(textureDesc.format);
    const RhiPipelineHandle pipeline = device.createGraphicsPipeline(pipelineDesc);
    if (!pipeline.isValid()) {
        return false;
    }

    RhiBufferDesc readbackDesc;
    readbackDesc.debugName = "VulkanSmoke.OffscreenOrientation.Readback";
    readbackDesc.size = kReadbackSize;
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    const RhiBufferHandle readback = device.createBuffer(readbackDesc, nullptr, 0u);
    if (!readback.isValid()) {
        return false;
    }

    RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Graphics);
    if (commands == nullptr ||
        !commands->begin({"VulkanSmoke.OffscreenOrientation.Commands", RhiCommandListType::Graphics})) {
        return false;
    }
    commands->textureBarrier({texture, RhiResourceState::Undefined, RhiResourceState::RenderTarget});
    RhiColorAttachment colorAttachment;
    colorAttachment.view = view;
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "VulkanSmoke.OffscreenOrientation.Rendering";
    renderingInfo.renderArea = {0, static_cast<int32_t>(kRenderY), kWidth, kRenderHeight};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    commands->beginRendering(renderingInfo);
    commands->setGraphicsPipeline(pipeline);
    commands->draw(3u, 1u, 0u, 0u);
    commands->endRendering();
    commands->textureBarrier({texture, RhiResourceState::RenderTarget, RhiResourceState::TransferSrc});
    RhiTextureBufferCopy copy;
    copy.srcTexture = texture;
    copy.dstBuffer = readback;
    copy.bytesPerRow = kBytesPerRow;
    copy.rowsPerImage = kHeight;
    copy.width = kWidth;
    copy.height = kHeight;
    commands->copyTextureToBuffer(copy);
    commands->bufferBarrier({readback, RhiResourceState::TransferDst, RhiResourceState::HostRead});
    if (!commands->end()) {
        return false;
    }
    RhiCommandList* submissions[] = {commands};
    RhiSubmissionToken token;
    if (!device.submit({"VulkanSmoke.OffscreenOrientation.Submit", submissions, 1u}, &token) ||
        !device.waitForSubmission(token)) {
        return false;
    }

    const auto* pixels = static_cast<const uint8_t*>(device.mapBuffer(readback, 0u, kReadbackSize));
    if (pixels == nullptr) {
        return false;
    }
    const auto isRed = [](const uint8_t* pixel) {
        return pixel[0] >= 250u && pixel[1] <= 5u && pixel[2] <= 5u && pixel[3] >= 250u;
    };
    const auto isBlue = [](const uint8_t* pixel) {
        return pixel[0] <= 5u && pixel[1] <= 5u && pixel[2] >= 250u && pixel[3] >= 250u;
    };
    const bool orientationCorrect = isBlue(pixels + static_cast<size_t>(kRenderY) * kBytesPerRow) &&
                                    isRed(pixels + static_cast<size_t>(kRenderY + kRenderHeight - 1u) * kBytesPerRow);
    device.unmapBuffer(readback);
    if (!orientationCorrect) {
        return false;
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth <= 0 || framebufferHeight <= 0 ||
        !device.resizeSwapchain(static_cast<uint32_t>(framebufferWidth), static_cast<uint32_t>(framebufferHeight))) {
        return false;
    }
    const RhiFrameAcquireResult frame = device.acquireFrame();
    if (frame.status != RhiFrameStatus::Success && frame.status != RhiFrameStatus::Suboptimal) {
        return false;
    }

    RhiGraphicsPipelineDesc presentationPipelineDesc = pipelineDesc;
    presentationPipelineDesc.debugName = "VulkanSmoke.PresentationOrientation.Pipeline";
    presentationPipelineDesc.colorFormats.clear();
    presentationPipelineDesc.colorFormats.push_back(device.swapchainColorFormat());
    const RhiPipelineHandle presentationPipeline = device.createGraphicsPipeline(presentationPipelineDesc);
    if (!presentationPipeline.isValid()) {
        return false;
    }

    RhiCommandList* presentationCommands = commandPool.acquire(RhiCommandListType::Graphics);
    if (presentationCommands == nullptr ||
        !presentationCommands->begin({"VulkanSmoke.PresentationOrientation.Commands", RhiCommandListType::Graphics})) {
        return false;
    }
    presentationCommands->bufferBarrier({readback, RhiResourceState::HostRead, RhiResourceState::TransferDst});
    presentationCommands->textureBarrier({texture, RhiResourceState::TransferSrc, RhiResourceState::RenderTarget});
    renderingInfo.renderArea = {0, 0, kWidth, kHeight};
    presentationCommands->beginRendering(renderingInfo);
    presentationCommands->setGraphicsPipeline(pipeline);
    presentationCommands->draw(3u, 1u, 0u, 0u);
    presentationCommands->endRendering();
    presentationCommands->textureBarrier({texture, RhiResourceState::RenderTarget, RhiResourceState::TransferSrc});

    presentationCommands->textureBarrier(
        {frame.colorTexture, RhiResourceState::Present, RhiResourceState::RenderTarget});
    RhiColorAttachment presentationAttachment;
    presentationAttachment.view = frame.colorView;
    presentationAttachment.loadOp = RhiLoadOp::Clear;
    presentationAttachment.storeOp = RhiStoreOp::Store;
    RhiRenderingInfo presentationRendering;
    presentationRendering.debugName = "VulkanSmoke.PresentationOrientation.Rendering";
    presentationRendering.renderArea = {0, 0, frame.width, frame.height};
    presentationRendering.colorAttachments = &presentationAttachment;
    presentationRendering.colorAttachmentCount = 1u;
    presentationCommands->beginRendering(presentationRendering);
    presentationCommands->setGraphicsPipeline(presentationPipeline);
    presentationCommands->draw(3u, 1u, 0u, 0u);
    presentationCommands->endRendering();
    presentationCommands->textureBarrier(
        {frame.colorTexture, RhiResourceState::RenderTarget, RhiResourceState::TransferSrc});

    RhiTextureBufferCopy presentationCopy;
    presentationCopy.srcTexture = frame.colorTexture;
    presentationCopy.dstBuffer = readback;
    presentationCopy.bytesPerRow = kBytesPerPixel;
    presentationCopy.rowsPerImage = 1u;
    presentationCopy.srcX = frame.width / 2u;
    presentationCopy.width = 1u;
    presentationCopy.height = 1u;
    presentationCopy.bufferOffset = 0u;
    presentationCopy.srcY = 0u;
    presentationCommands->copyTextureToBuffer(presentationCopy);
    presentationCopy.bufferOffset = kBytesPerPixel;
    presentationCopy.srcY = frame.height - 1u;
    presentationCommands->copyTextureToBuffer(presentationCopy);

    presentationCommands->textureBarrier(
        {frame.colorTexture, RhiResourceState::TransferSrc, RhiResourceState::TransferDst});
    RhiTextureBlit presentationBlit;
    presentationBlit.src = texture;
    presentationBlit.dstView = frame.colorView;
    presentationCommands->blitTexture(presentationBlit);
    presentationCommands->textureBarrier(
        {frame.colorTexture, RhiResourceState::TransferDst, RhiResourceState::TransferSrc});
    presentationCopy.bufferOffset = kBytesPerPixel * 2u;
    presentationCopy.srcY = 0u;
    presentationCommands->copyTextureToBuffer(presentationCopy);
    presentationCopy.bufferOffset = kBytesPerPixel * 3u;
    presentationCopy.srcY = frame.height - 1u;
    presentationCommands->copyTextureToBuffer(presentationCopy);
    presentationCommands->bufferBarrier({readback, RhiResourceState::TransferDst, RhiResourceState::HostRead});
    presentationCommands->textureBarrier(
        {frame.colorTexture, RhiResourceState::TransferSrc, RhiResourceState::Present});
    if (!presentationCommands->end()) {
        return false;
    }
    RhiCommandList* presentationSubmissions[] = {presentationCommands};
    RhiSubmissionToken presentationToken;
    if (!device.submit({"VulkanSmoke.PresentationOrientation.Submit", presentationSubmissions, 1u},
                       &presentationToken)) {
        return false;
    }
    const RhiFrameStatus presentStatus = device.presentFrame({frame.frameIndex, frame.imageIndex});
    if ((presentStatus != RhiFrameStatus::Success && presentStatus != RhiFrameStatus::Suboptimal) ||
        !device.waitForSubmission(presentationToken)) {
        return false;
    }

    const auto* presentationPixels = static_cast<const uint8_t*>(device.mapBuffer(readback, 0u, kBytesPerPixel * 4u));
    if (presentationPixels == nullptr) {
        return false;
    }
    const auto isBgraRed = [](const uint8_t* pixel) {
        return pixel[0] <= 5u && pixel[1] <= 5u && pixel[2] >= 250u && pixel[3] >= 250u;
    };
    const auto isBgraBlue = [](const uint8_t* pixel) {
        return pixel[0] >= 250u && pixel[1] <= 5u && pixel[2] <= 5u && pixel[3] >= 250u;
    };
    const bool presentationCorrect = isBgraBlue(presentationPixels) && isBgraRed(presentationPixels + kBytesPerPixel) &&
                                     isBgraBlue(presentationPixels + kBytesPerPixel * 2u) &&
                                     isBgraRed(presentationPixels + kBytesPerPixel * 3u);
    device.unmapBuffer(readback);

    device.destroyBuffer(readback);
    device.destroyPipeline(presentationPipeline);
    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(layout);
    device.destroyShader(fragmentShader);
    device.destroyShader(vertexShader);
    device.destroyTextureView(view);
    device.destroyTexture(texture);
    return presentationCorrect;
}

[[nodiscard]] bool validateRenderGraphMultiQueue(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    constexpr std::array<uint32_t, 4u> kPayload{0x13579BDFu, 0x2468ACE0u, 0x10203040u, 0x55667788u};
    constexpr uint64_t kPayloadSize = sizeof(kPayload);

    RhiBufferDesc readbackDesc;
    readbackDesc.debugName = "VulkanSmoke.RenderGraph.Readback";
    readbackDesc.size = kPayloadSize;
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    const RhiBufferHandle readback = device.createBuffer(readbackDesc, nullptr, 0u);
    if (!readback.isValid()) {
        return false;
    }

    const uint64_t validationErrorsBefore = device.validationErrorCount();
    RenderGraph graph;
    RhiBufferHandle firstTransientBuffer;
    RhiTextureHandle firstTransientTexture;
    const auto executeFrame = [&](const RhiResourceState readbackInitialState, const bool expectReuse,
                                  const size_t expectedSubmissionCount) {
        graph.reset();

        RhiBufferDesc transientBufferDesc;
        transientBufferDesc.debugName = "VulkanSmoke.RenderGraph.TransientBuffer";
        transientBufferDesc.size = kPayloadSize;
        transientBufferDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::TransferSrc) |
                                    rhiFlag(RhiBufferUsage::Storage);
        const RgBufferHandle transientBuffer =
            graph.createBuffer({"MultiQueueBuffer", transientBufferDesc, RhiResourceState::Undefined});

        RhiTextureDesc transientTextureDesc;
        transientTextureDesc.debugName = "VulkanSmoke.RenderGraph.TransientTexture";
        transientTextureDesc.format = RhiTextureFormat::Rgba8Unorm;
        transientTextureDesc.width = 8u;
        transientTextureDesc.height = 8u;
        transientTextureDesc.usage = rhiFlag(RhiTextureUsage::TransferDst) | rhiFlag(RhiTextureUsage::Storage) |
                                     rhiFlag(RhiTextureUsage::Sampled);
        const RgTextureHandle transientTexture =
            graph.createTexture({"MultiQueueTexture", transientTextureDesc, RhiResourceState::Undefined});

        RhiBufferDesc importedReadbackDesc = readbackDesc;
        importedReadbackDesc.initialState = readbackInitialState;
        const RgBufferHandle importedReadback =
            graph.importBuffer({"Readback", readback, importedReadbackDesc, readbackInitialState,
                                RhiResourceState::HostRead, RhiQueueType::Graphics, RhiQueueType::Graphics});

        RhiBufferHandle resolvedBuffer;
        RhiTextureHandle resolvedTexture;
        graph.addPass({"MultiQueue.Transfer", RgPassType::Copy, RhiQueueType::Transfer})
            .writeBuffer(transientBuffer, RhiResourceState::TransferDst)
            .writeTexture(transientTexture, RhiResourceState::TransferDst)
            .setExecute([&](RgPassContext& context) {
                resolvedBuffer = context.buffer(transientBuffer);
                resolvedTexture = context.texture(transientTexture);
                context.commandList().updateBuffer(resolvedBuffer, 0u, kPayload.data(), sizeof(kPayload));
                return resolvedBuffer.isValid() && resolvedTexture.isValid();
            });
        graph.addPass({"MultiQueue.Compute", RgPassType::Compute, RhiQueueType::Compute})
            .readWriteBuffer(transientBuffer, RhiResourceState::StorageBuffer)
            .readWriteTexture(transientTexture, RhiResourceState::ShaderWrite)
            .setExecute([](RgPassContext&) { return true; });
        graph.addPass({"MultiQueue.Graphics", RgPassType::Copy, RhiQueueType::Graphics})
            .readBuffer(transientBuffer, RhiResourceState::TransferSrc)
            .readTexture(transientTexture, RhiResourceState::ShaderRead)
            .writeBuffer(importedReadback, RhiResourceState::TransferDst)
            .setExecute([&](RgPassContext& context) {
                context.commandList().copyBuffer(
                    {context.buffer(transientBuffer), context.buffer(importedReadback), 0u, 0u, kPayloadSize});
                return true;
            });

        const RgCompileResult compiled = graph.compile();
        if (!compiled.succeeded()) {
            std::cerr << "Render Graph multi-queue compile failed: " << compiled.message << '\n';
            return false;
        }
        const auto& batches = graph.submissionBatches();
        if (batches.size() != 3u || batches[0].queue != RhiQueueType::Transfer ||
            batches[1].queue != RhiQueueType::Compute || batches[2].queue != RhiQueueType::Graphics ||
            batches[1].dependencies.size() != 1u || batches[1].dependencies[0] != 0u ||
            batches[2].dependencies.size() != 1u || batches[2].dependencies[0] != 1u) {
            std::cerr << "Render Graph multi-queue batch plan is invalid\n";
            return false;
        }

        const RgExecuteResult executed = graph.execute(device, commandPool);
        if (!executed.succeeded()) {
            std::cerr << "Render Graph multi-queue execution failed: " << executed.message << '\n';
            return false;
        }
        if (executed.submissions.size() != expectedSubmissionCount) {
            std::cerr << "Render Graph multi-queue submission count mismatch\n";
            return false;
        }
        const size_t queueTokenOffset = expectedSubmissionCount - 3u;
        if (executed.submissions[queueTokenOffset].queue != RhiQueueType::Transfer ||
            executed.submissions[queueTokenOffset + 1u].queue != RhiQueueType::Compute ||
            executed.submissions[queueTokenOffset + 2u].queue != RhiQueueType::Graphics ||
            executed.submissions[queueTokenOffset].timelineValue() == 0u ||
            executed.submissions[queueTokenOffset + 1u].timelineValue() == 0u ||
            executed.submissions[queueTokenOffset + 2u].timelineValue() == 0u) {
            std::cerr << "Render Graph multi-queue timeline chain is invalid\n";
            return false;
        }
        if (!expectReuse) {
            firstTransientBuffer = resolvedBuffer;
            firstTransientTexture = resolvedTexture;
            return true;
        }
        if (!device.waitForSubmission(executed.completionToken())) {
            std::cerr << "Render Graph multi-frame timeline chain is invalid\n";
            return false;
        }
        if (graph.pollTimings(device) != RgTimingPollResult::Available) {
            std::cerr << "Render Graph multi-queue timings are unavailable\n";
            return false;
        }
        const RgTimingSnapshot& timings = graph.latestTimings();
        if (!timings.isValid() || timings.execution != 2u || timings.passes.size() != 3u ||
            timings.passes[0].name != "MultiQueue.Transfer" || timings.passes[0].queue != RhiQueueType::Transfer ||
            timings.passes[1].name != "MultiQueue.Compute" || timings.passes[1].queue != RhiQueueType::Compute ||
            timings.passes[2].name != "MultiQueue.Graphics" || timings.passes[2].queue != RhiQueueType::Graphics) {
            std::cerr << "Render Graph multi-queue timing snapshot is invalid\n";
            return false;
        }

        const auto* mapped = static_cast<const uint32_t*>(device.mapBuffer(readback, 0u, kPayloadSize));
        if (mapped == nullptr) {
            return false;
        }
        const bool payloadMatches = std::equal(kPayload.begin(), kPayload.end(), mapped);
        device.unmapBuffer(readback);
        if (!payloadMatches) {
            std::cerr << "Render Graph multi-queue payload mismatch\n";
            return false;
        }

        return resolvedBuffer.index == firstTransientBuffer.index &&
               resolvedBuffer.generation == firstTransientBuffer.generation &&
               resolvedTexture.index == firstTransientTexture.index &&
               resolvedTexture.generation == firstTransientTexture.generation;
    };

    const bool valid =
        executeFrame(RhiResourceState::TransferDst, false, 3u) && executeFrame(RhiResourceState::HostRead, true, 4u);
    graph.releaseTransientResources(device);
    device.destroyBuffer(readback);
    return valid && device.validationErrorCount() == validationErrorsBefore;
}

// Builds a graph of independent thread-safe copy passes spanning multiple
// submission batches and executes it with worker-thread recording enabled.
// Validates payload correctness through a readback and that at least one
// batch was actually recorded off the calling thread.
[[nodiscard]] bool validateRenderGraphMultithreadedRecord(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    constexpr uint32_t kPassCount = 12u;
    constexpr uint64_t kChunkSize = 16u;
    constexpr uint64_t kPayloadSize = kPassCount * kChunkSize;

    std::array<uint32_t, kPayloadSize / sizeof(uint32_t)> payload{};
    for (uint32_t index = 0u; index < payload.size(); ++index) {
        payload[index] = 0x9E3779B9u * (index + 1u);
    }

    RhiBufferDesc sourceDesc;
    sourceDesc.debugName = "VulkanSmoke.MtRecord.Source";
    sourceDesc.size = kPayloadSize;
    sourceDesc.usage = rhiFlag(RhiBufferUsage::TransferSrc) | rhiFlag(RhiBufferUsage::TransferDst);
    sourceDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    sourceDesc.initialState = RhiResourceState::TransferSrc;
    const RhiBufferHandle source = device.createBuffer(sourceDesc, payload.data(), kPayloadSize);
    if (!source.isValid()) {
        return false;
    }

    RhiBufferDesc readbackDesc;
    readbackDesc.debugName = "VulkanSmoke.MtRecord.Readback";
    readbackDesc.size = kPayloadSize;
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    const RhiBufferHandle readback = device.createBuffer(readbackDesc, nullptr, 0u);
    if (!readback.isValid()) {
        device.destroyBuffer(source);
        return false;
    }

    const uint64_t validationErrorsBefore = device.validationErrorCount();
    ThreadPool workerPool(2);
    workerPool.start();
    bool valid = true;
    {
        RenderGraph graph;
        graph.setRecordThreading(&workerPool);

        // Two frames: the second one exercises worker command-list pool
        // reuse and the cross-thread completion recycling path.
        const auto executeFrame = [&](const RhiResourceState readbackInitialState) {
            graph.reset();
            const RgBufferHandle importedSource =
                graph.importBuffer({"MtSource", source, sourceDesc, RhiResourceState::TransferSrc,
                                    RhiResourceState::TransferSrc, RhiQueueType::Graphics, RhiQueueType::Graphics});
            RhiBufferDesc importedReadbackDesc = readbackDesc;
            importedReadbackDesc.initialState = readbackInitialState;
            const RgBufferHandle importedReadback =
                graph.importBuffer({"MtReadback", readback, importedReadbackDesc, readbackInitialState,
                                    RhiResourceState::HostRead, RhiQueueType::Graphics, RhiQueueType::Graphics});

            // Twelve independent chunk copies exceed the per-batch pass cap,
            // producing at least two worker-eligible submission batches.
            char passNames[kPassCount][32];
            for (uint32_t pass = 0u; pass < kPassCount; ++pass) {
                std::snprintf(passNames[pass], sizeof(passNames[pass]), "MtRecord.Copy%u", pass);
                const uint64_t offset = pass * kChunkSize;
                graph
                    .addPass({passNames[pass], RgPassType::Copy, RhiQueueType::Graphics,
                              /*threadSafeRecord=*/true})
                    .readBuffer(importedSource, RhiResourceState::TransferSrc, {offset, kChunkSize})
                    .writeBuffer(importedReadback, RhiResourceState::TransferDst, {offset, kChunkSize})
                    .setExecute([&, offset](RgPassContext& context) {
                        context.commandList().copyBuffer({context.buffer(importedSource),
                                                          context.buffer(importedReadback), offset, offset,
                                                          kChunkSize});
                        return true;
                    });
            }

            const RgCompileResult compiled = graph.compile();
            if (!compiled.succeeded()) {
                std::cerr << "Render Graph MT record compile failed: " << compiled.message << '\n';
                return false;
            }
            const size_t batchCount = graph.submissionBatches().size();
            if (batchCount < 2u) {
                std::cerr << "Render Graph MT record produced too few "
                             "batches\n";
                return false;
            }
            const RgExecuteResult executed = graph.execute(device, commandPool);
            if (!executed.succeeded()) {
                std::cerr << "Render Graph MT record execution failed: " << executed.message << '\n';
                return false;
            }
            if (executed.workerRecordedBatchCount != static_cast<uint32_t>(batchCount)) {
                std::cerr << "Render Graph MT record worker batch count " << executed.workerRecordedBatchCount
                          << " does not match batch count " << batchCount << '\n';
                return false;
            }
            if (!device.waitForSubmission(executed.completionToken())) {
                std::cerr << "Render Graph MT record wait failed\n";
                return false;
            }
            // Collecting timings forces submission-completion polling, which
            // marks worker-recorded lists for owner-thread recycling.
            if (graph.pollTimings(device) == RgTimingPollResult::Error) {
                std::cerr << "Render Graph MT record timing poll failed\n";
                return false;
            }
            const auto* mapped = static_cast<const uint32_t*>(device.mapBuffer(readback, 0u, kPayloadSize));
            if (mapped == nullptr) {
                return false;
            }
            const bool payloadMatches = std::equal(payload.begin(), payload.end(), mapped);
            device.unmapBuffer(readback);
            if (!payloadMatches) {
                std::cerr << "Render Graph MT record payload mismatch\n";
                return false;
            }
            return true;
        };

        valid = executeFrame(RhiResourceState::TransferDst) && executeFrame(RhiResourceState::HostRead);
        graph.releaseTransientResources(device);
    }
    workerPool.shutdown();
    device.destroyBuffer(readback);
    device.destroyBuffer(source);
    return valid && device.validationErrorCount() == validationErrorsBefore;
}

[[nodiscard]] bool validateRenderGraphTextureAliasing(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    constexpr uint32_t kSizeA = 64u;
    constexpr uint32_t kSizeB = 32u;
    constexpr uint64_t kPixelBytesA = kSizeA * kSizeA * 4u;
    constexpr uint64_t kPixelBytesB = kSizeB * kSizeB * 4u;
    constexpr uint8_t kFillA = 0xA5u;
    constexpr uint8_t kFillB = 0x3Cu;
    const std::vector<uint8_t> payloadA(kPixelBytesA, kFillA);
    const std::vector<uint8_t> payloadB(kPixelBytesB, kFillB);

    const uint64_t validationErrorsBefore = device.validationErrorCount();

    RhiBufferDesc uploadDesc;
    uploadDesc.debugName = "VulkanSmoke.GraphAlias.Upload";
    uploadDesc.size = kPixelBytesA;
    uploadDesc.usage = rhiFlag(RhiBufferUsage::TransferSrc);
    uploadDesc.memoryUsage = RhiMemoryUsage::CpuToGpu;
    uploadDesc.initialState = RhiResourceState::TransferSrc;
    const RhiBufferHandle uploadA = device.createBuffer(uploadDesc, payloadA.data(), payloadA.size());
    uploadDesc.size = kPixelBytesB;
    const RhiBufferHandle uploadB = device.createBuffer(uploadDesc, payloadB.data(), payloadB.size());
    RhiBufferDesc readbackDesc;
    readbackDesc.debugName = "VulkanSmoke.GraphAlias.Readback";
    readbackDesc.size = kPixelBytesA + kPixelBytesB;
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    const RhiBufferHandle readback = device.createBuffer(readbackDesc, nullptr, 0u);
    if (!uploadA.isValid() || !uploadB.isValid() || !readback.isValid()) {
        return false;
    }

    RenderGraph graph;
    RhiTextureHandle resolvedFirst;
    RhiTextureHandle resolvedSecond;
    const auto executeFrame = [&]() {
        graph.reset();
        RhiTextureDesc transientDesc;
        transientDesc.format = RhiTextureFormat::Rgba8Unorm;
        transientDesc.width = kSizeA;
        transientDesc.height = kSizeA;
        transientDesc.usage = rhiFlag(RhiTextureUsage::TransferDst) | rhiFlag(RhiTextureUsage::TransferSrc) |
                              rhiFlag(RhiTextureUsage::Sampled);
        // Two graphics-only transients with different descriptions and
        // disjoint pass intervals: the aliasing allocator must land them on
        // one shared page as distinct placed images while their payloads
        // stay isolated by the serial barrier chain.
        const RgTextureHandle first = graph.createTexture({"AliasFirst", transientDesc, RhiResourceState::Undefined});
        transientDesc.width = kSizeB;
        transientDesc.height = kSizeB;
        const RgTextureHandle second = graph.createTexture({"AliasSecond", transientDesc, RhiResourceState::Undefined});
        RhiBufferDesc importedUploadDesc = uploadDesc;
        const RgBufferHandle importedA =
            graph.importBuffer({"UploadA", uploadA, importedUploadDesc, RhiResourceState::TransferSrc,
                                RhiResourceState::TransferSrc, RhiQueueType::Graphics, RhiQueueType::Graphics});
        const RgBufferHandle importedB =
            graph.importBuffer({"UploadB", uploadB, importedUploadDesc, RhiResourceState::TransferSrc,
                                RhiResourceState::TransferSrc, RhiQueueType::Graphics, RhiQueueType::Graphics});
        RhiBufferDesc importedReadbackDesc = readbackDesc;
        const RgBufferHandle importedReadback =
            graph.importBuffer({"Readback", readback, importedReadbackDesc, RhiResourceState::TransferDst,
                                RhiResourceState::HostRead, RhiQueueType::Graphics, RhiQueueType::Graphics});

        const auto fillPass = [&](const char* name, const RgTextureHandle target, const RgBufferHandle source,
                                  const uint32_t extent, RhiTextureHandle* resolved) {
            graph.addPass({name, RgPassType::Copy, RhiQueueType::Graphics})
                .readBuffer(source, RhiResourceState::TransferSrc)
                .writeTexture(target, RhiResourceState::TransferDst)
                .setExecute([&, target, source, extent, resolved](RgPassContext& context) {
                    if (resolved != nullptr) {
                        *resolved = context.texture(target);
                    }
                    RhiBufferTextureCopy copy;
                    copy.srcBuffer = context.buffer(source);
                    copy.dstTexture = context.texture(target);
                    copy.width = extent;
                    copy.height = extent;
                    context.commandList().copyBufferToTexture(copy);
                    return true;
                });
        };
        const auto drainPass = [&](const char* name, const RgTextureHandle target, const uint32_t extent,
                                   const uint64_t readbackOffset) {
            graph.addPass({name, RgPassType::Copy, RhiQueueType::Graphics})
                .readTexture(target, RhiResourceState::TransferSrc)
                .writeBuffer(importedReadback, RhiResourceState::TransferDst)
                .setExecute([&, target, extent, readbackOffset](RgPassContext& context) {
                    RhiTextureBufferCopy copy;
                    copy.srcTexture = context.texture(target);
                    copy.dstBuffer = context.buffer(importedReadback);
                    copy.bufferOffset = readbackOffset;
                    copy.width = extent;
                    copy.height = extent;
                    context.commandList().copyTextureToBuffer(copy);
                    return true;
                });
        };
        fillPass("Alias.FillFirst", first, importedA, kSizeA, &resolvedFirst);
        drainPass("Alias.DrainFirst", first, kSizeA, 0u);
        fillPass("Alias.FillSecond", second, importedB, kSizeB, &resolvedSecond);
        drainPass("Alias.DrainSecond", second, kSizeB, kPixelBytesA);

        const RgCompileResult compiled = graph.compile();
        if (!compiled.succeeded()) {
            std::cerr << "Alias graph compile failed: " << compiled.message << '\n';
            return false;
        }
        const RgExecuteResult executed = graph.execute(device, commandPool);
        if (!executed.succeeded()) {
            std::cerr << "Alias graph execution failed: " << executed.message << '\n';
            return false;
        }
        if (!device.waitForSubmission(executed.completionToken())) {
            return false;
        }
        const auto* mapped = static_cast<const uint8_t*>(device.mapBuffer(readback, 0u, kPixelBytesA + kPixelBytesB));
        if (mapped == nullptr) {
            return false;
        }
        const bool isolated =
            std::all_of(mapped, mapped + kPixelBytesA, [](const uint8_t value) { return value == kFillA; }) &&
            std::all_of(mapped + kPixelBytesA, mapped + kPixelBytesA + kPixelBytesB,
                        [](const uint8_t value) { return value == kFillB; });
        device.unmapBuffer(readback);
        if (!isolated) {
            std::cerr << "Aliased transient payloads were not isolated\n";
            return false;
        }
        return true;
    };

    bool valid = executeFrame();
    const RgTransientMemoryStats statsFirstFrame = graph.transientMemoryStats();
    if (valid && (statsFirstFrame.aliasedTextureCount != 2u || statsFirstFrame.pageCount != 1u ||
                  statsFirstFrame.aliasedPageBytes >= statsFirstFrame.aliasedRequestBytes)) {
        std::cerr << "Alias stats mismatch: textures=" << statsFirstFrame.aliasedTextureCount
                  << " pages=" << statsFirstFrame.pageCount << '\n';
        valid = false;
    }
    const RhiTextureHandle firstFrameFirst = resolvedFirst;
    const RhiTextureHandle firstFrameSecond = resolvedSecond;
    if (valid && (firstFrameFirst.index == firstFrameSecond.index &&
                  firstFrameFirst.generation == firstFrameSecond.generation)) {
        std::cerr << "Aliased transients resolved to one texture\n";
        valid = false;
    }
    // A second identical frame must reuse the same placed textures and page.
    valid = valid && executeFrame();
    if (valid &&
        (graph.transientMemoryStats().pageCount != 1u || resolvedFirst.index != firstFrameFirst.index ||
         resolvedFirst.generation != firstFrameFirst.generation || resolvedSecond.index != firstFrameSecond.index ||
         resolvedSecond.generation != firstFrameSecond.generation)) {
        std::cerr << "Alias page reuse across frames failed\n";
        valid = false;
    }

    graph.releaseTransientResources(device);
    device.destroyBuffer(readback);
    device.destroyBuffer(uploadB);
    device.destroyBuffer(uploadA);
    device.waitIdle();
    return valid && device.validationErrorCount() == validationErrorsBefore;
}

[[nodiscard]] bool validateResourceDescriptionNameOwnership(VkRhiDevice& device) {
    constexpr const char* kBufferDebugName = "VulkanSmoke.DescriptionQuery.Buffer";
    std::array<char, 64u> bufferDebugName{};
    std::strcpy(bufferDebugName.data(), kBufferDebugName);
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = bufferDebugName.data();
    bufferDesc.size = 256u;
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Storage);
    bufferDesc.memoryUsage = RhiMemoryUsage::CpuToGpu;
    bufferDesc.initialState = RhiResourceState::StorageBuffer;
    const RhiBufferHandle buffer = device.createBuffer(bufferDesc, nullptr, 0u);

    constexpr const char* kTextureDebugName = "VulkanSmoke.DescriptionQuery.Texture";
    std::array<char, 64u> textureDebugName{};
    std::strcpy(textureDebugName.data(), kTextureDebugName);
    RhiTextureDesc textureDesc;
    textureDesc.debugName = textureDebugName.data();
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = 8u;
    textureDesc.height = 8u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled);
    const RhiTextureHandle texture = device.createTexture(textureDesc, nullptr);

    bufferDebugName.fill('B');
    bufferDebugName.back() = '\0';
    textureDebugName.fill('T');
    textureDebugName.back() = '\0';

    RhiBufferDesc queriedBuffer;
    RhiTextureDesc queriedTexture;
    const bool valid = buffer.isValid() && texture.isValid() && device.getBufferDesc(buffer, queriedBuffer) &&
                       queriedBuffer.debugName != nullptr &&
                       std::strcmp(queriedBuffer.debugName, kBufferDebugName) == 0 &&
                       device.getTextureDesc(texture, queriedTexture) && queriedTexture.debugName != nullptr &&
                       std::strcmp(queriedTexture.debugName, kTextureDebugName) == 0;
    device.destroyBuffer(buffer);
    device.destroyTexture(texture);
    return valid;
}

[[nodiscard]] bool validateTextureAliasing(VkRhiDevice& device) {
    if (!device.capabilities().textureAliasing) {
        std::cerr << "Vulkan device must report textureAliasing\n";
        return false;
    }
    const uint64_t validationErrorsBefore = device.validationErrorCount();

    RhiTextureDesc colorDesc;
    colorDesc.debugName = "VulkanSmoke.Aliasing.Color";
    colorDesc.format = RhiTextureFormat::Rgba16Float;
    colorDesc.width = 256u;
    colorDesc.height = 256u;
    colorDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment) | rhiFlag(RhiTextureUsage::Sampled);
    colorDesc.memoryCategory = RhiMemoryCategory::Transient;
    RhiTextureDesc storageDesc;
    storageDesc.debugName = "VulkanSmoke.Aliasing.Storage";
    storageDesc.format = RhiTextureFormat::R32Float;
    storageDesc.width = 128u;
    storageDesc.height = 128u;
    storageDesc.usage = rhiFlag(RhiTextureUsage::Storage) | rhiFlag(RhiTextureUsage::Sampled);
    storageDesc.memoryCategory = RhiMemoryCategory::Transient;

    RhiTextureMemoryRequirements colorRequirements;
    RhiTextureMemoryRequirements storageRequirements;
    if (!device.getTextureMemoryRequirements(colorDesc, colorRequirements) ||
        !device.getTextureMemoryRequirements(storageDesc, storageRequirements)) {
        std::cerr << "Texture memory requirements query failed\n";
        return false;
    }
    RhiTextureMemoryRequirements blockRequirements;
    blockRequirements.sizeBytes = std::max(colorRequirements.sizeBytes, storageRequirements.sizeBytes);
    blockRequirements.alignment = std::max(colorRequirements.alignment, storageRequirements.alignment);
    blockRequirements.memoryTypeBits = colorRequirements.memoryTypeBits & storageRequirements.memoryTypeBits;
    if (blockRequirements.memoryTypeBits == 0u) {
        // No shared memory type on this device; aliasing simply won't group
        // these formats, which is a valid outcome.
        return true;
    }

    const RhiMemoryStats memoryStatsBefore = device.memoryStats();
    const RhiMemoryCategoryStats transientBefore =
        memoryStatsBefore.categories[static_cast<size_t>(RhiMemoryCategory::Transient)];
    const RhiMemoryHandle memory =
        device.allocateTextureMemory(blockRequirements, RhiMemoryCategory::Transient, "VulkanSmoke.Aliasing.Block");
    if (!memory.isValid()) {
        std::cerr << "Texture memory allocation failed\n";
        return false;
    }
    const RhiTextureHandle colorTexture = device.createPlacedTexture(colorDesc, memory);
    const RhiTextureHandle storageTexture = device.createPlacedTexture(storageDesc, memory);
    bool valid = colorTexture.isValid() && storageTexture.isValid();
    const RhiMemoryStats memoryStatsPlaced = device.memoryStats();
    const RhiMemoryCategoryStats transientPlaced =
        memoryStatsPlaced.categories[static_cast<size_t>(RhiMemoryCategory::Transient)];
    if (!memoryStatsPlaced.valid || memoryStatsPlaced.accuracy != RhiMemoryStatsAccuracy::Exact ||
        transientPlaced.allocationCount != transientBefore.allocationCount + 1u ||
        transientPlaced.resourceCount != transientBefore.resourceCount + 2u ||
        transientPlaced.bytes < transientBefore.bytes + blockRequirements.sizeBytes) {
        std::cerr << "Placed textures must count one exact shared allocation\n";
        valid = false;
    }

    // Views must be creatable on placed textures like any other texture.
    RhiTextureViewHandle colorView;
    RhiTextureViewHandle storageView;
    if (valid) {
        RhiTextureViewDesc viewDesc;
        viewDesc.texture = colorTexture;
        colorView = device.createTextureView(viewDesc);
        viewDesc.texture = storageTexture;
        storageView = device.createTextureView(viewDesc);
        valid = colorView.isValid() && storageView.isValid();
    }

    // A description larger than the block must be rejected, not bound.
    RhiTextureDesc oversizedDesc = colorDesc;
    oversizedDesc.debugName = "VulkanSmoke.Aliasing.Oversized";
    oversizedDesc.width = 4096u;
    oversizedDesc.height = 4096u;
    if (device.createPlacedTexture(oversizedDesc, memory).isValid()) {
        std::cerr << "Oversized placed texture was not rejected\n";
        valid = false;
    }

    if (colorView.isValid())
        device.destroyTextureView(colorView);
    if (storageView.isValid())
        device.destroyTextureView(storageView);
    if (colorTexture.isValid())
        device.destroyTexture(colorTexture);
    if (storageTexture.isValid())
        device.destroyTexture(storageTexture);
    device.destroyTextureMemory(memory);
    device.waitIdle();
    const RhiMemoryCategoryStats transientAfter =
        device.memoryStats().categories[static_cast<size_t>(RhiMemoryCategory::Transient)];
    if (transientAfter.bytes != transientBefore.bytes ||
        transientAfter.allocationCount != transientBefore.allocationCount ||
        transientAfter.resourceCount != transientBefore.resourceCount) {
        std::cerr << "Released shared allocation remained in the live memory snapshot\n";
        valid = false;
    }
    return valid && device.validationErrorCount() == validationErrorsBefore;
}

[[nodiscard]] bool validateRg32UintAttachmentClear(VkRhiDevice& device, RhiCommandListPool& commandPool) {
    constexpr std::array<uint32_t, 2> kClearValue{0x13579bdfu, 0x2468ace0u};
    const uint64_t validationErrorsBefore = device.validationErrorCount();

    RhiTextureDesc textureDesc;
    textureDesc.debugName = "VulkanSmoke.Rg32UintClear.Target";
    textureDesc.format = RhiTextureFormat::Rg32Uint;
    textureDesc.width = 1u;
    textureDesc.height = 1u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment) | rhiFlag(RhiTextureUsage::TransferSrc);
    const RhiTextureHandle texture = device.createTexture(textureDesc, nullptr);
    RhiTextureViewDesc viewDesc;
    viewDesc.texture = texture;
    viewDesc.format = textureDesc.format;
    const RhiTextureViewHandle view = device.createTextureView(viewDesc);

    RhiBufferDesc readbackDesc;
    readbackDesc.debugName = "VulkanSmoke.Rg32UintClear.Readback";
    readbackDesc.size = sizeof(kClearValue);
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    const RhiBufferHandle readback = device.createBuffer(readbackDesc, nullptr, 0u);

    bool valid = texture.isValid() && view.isValid() && readback.isValid();
    RhiCommandList* commands = valid ? commandPool.acquire(RhiCommandListType::Graphics) : nullptr;
    valid = valid && commands != nullptr &&
            commands->begin({"VulkanSmoke.Rg32UintClear.Commands", RhiCommandListType::Graphics});
    if (valid) {
        commands->textureBarrier({texture, RhiResourceState::Undefined, RhiResourceState::RenderTarget});
        RhiColorAttachment attachment;
        attachment.view = view;
        attachment.loadOp = RhiLoadOp::Clear;
        attachment.storeOp = RhiStoreOp::Store;
        attachment.clearValueType = RhiColorClearValueType::Uint;
        attachment.clearColorUint[0] = kClearValue[0];
        attachment.clearColorUint[1] = kClearValue[1];
        RhiRenderingInfo renderingInfo;
        renderingInfo.debugName = "VulkanSmoke.Rg32UintClear.Rendering";
        renderingInfo.renderArea = {0, 0, 1u, 1u};
        renderingInfo.colorAttachments = &attachment;
        renderingInfo.colorAttachmentCount = 1u;
        commands->beginRendering(renderingInfo);
        commands->endRendering();
        commands->textureBarrier({texture, RhiResourceState::RenderTarget, RhiResourceState::TransferSrc});
        RhiTextureBufferCopy copy;
        copy.srcTexture = texture;
        copy.dstBuffer = readback;
        copy.bytesPerRow = static_cast<uint32_t>(sizeof(kClearValue));
        copy.rowsPerImage = 1u;
        copy.width = 1u;
        copy.height = 1u;
        commands->copyTextureToBuffer(copy);
        commands->bufferBarrier({readback, RhiResourceState::TransferDst, RhiResourceState::HostRead});
        valid = commands->end();
    }

    RhiSubmissionToken token;
    if (valid) {
        RhiCommandList* submissions[] = {commands};
        valid = device.submit({"VulkanSmoke.Rg32UintClear.Submit", submissions, 1u}, &token) &&
                device.waitForSubmission(token);
    }
    if (valid) {
        const auto* mapped = static_cast<const uint32_t*>(device.mapBuffer(readback, 0u, sizeof(kClearValue)));
        valid = mapped != nullptr && mapped[0] == kClearValue[0] && mapped[1] == kClearValue[1];
        if (mapped != nullptr) {
            device.unmapBuffer(readback);
        }
    }

    if (readback.isValid())
        device.destroyBuffer(readback);
    if (view.isValid())
        device.destroyTextureView(view);
    if (texture.isValid())
        device.destroyTexture(texture);
    device.waitIdle();
    return valid && device.validationErrorCount() == validationErrorsBefore;
}

} // namespace

[[nodiscard]] bool validateCubeArrayViews(VkRhiDevice& device) {
    const uint64_t validationErrorsBefore = device.validationErrorCount();
    RhiTextureDesc textureDesc;
    textureDesc.debugName = "VulkanSmoke.CubeArray";
    textureDesc.dimension = RhiTextureDimension::CubeArray;
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = 4u;
    textureDesc.height = 4u;
    textureDesc.depthOrLayers = 12u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment);
    const RhiTextureHandle texture = device.createTexture(textureDesc, nullptr);
    if (!texture.isValid()) {
        return false;
    }

    RhiTextureViewDesc cubeArrayDesc;
    cubeArrayDesc.texture = texture;
    cubeArrayDesc.viewType = RhiTextureViewType::CubeArray;
    cubeArrayDesc.layerCount = 12u;
    const RhiTextureViewHandle cubeArrayView = device.createTextureView(cubeArrayDesc);

    RhiTextureViewDesc cubeDesc;
    cubeDesc.texture = texture;
    cubeDesc.viewType = RhiTextureViewType::Cube;
    cubeDesc.baseLayer = 6u;
    cubeDesc.layerCount = 6u;
    const RhiTextureViewHandle cubeView = device.createTextureView(cubeDesc);

    RhiTextureViewDesc faceDesc;
    faceDesc.texture = texture;
    faceDesc.viewType = RhiTextureViewType::Texture2D;
    faceDesc.baseLayer = 7u;
    faceDesc.layerCount = 1u;
    const RhiTextureViewHandle faceView = device.createTextureView(faceDesc);

    RhiTextureDesc invalidTextureDesc = textureDesc;
    invalidTextureDesc.depthOrLayers = 10u;
    const RhiTextureHandle invalidTexture = device.createTexture(invalidTextureDesc, nullptr);
    RhiTextureViewDesc invalidViewDesc = cubeArrayDesc;
    invalidViewDesc.baseLayer = 1u;
    invalidViewDesc.layerCount = 6u;
    const RhiTextureViewHandle invalidView = device.createTextureView(invalidViewDesc);

    const bool valid = cubeArrayView.isValid() && cubeView.isValid() && faceView.isValid() &&
                       !invalidTexture.isValid() && !invalidView.isValid();
    if (faceView.isValid())
        device.destroyTextureView(faceView);
    if (cubeView.isValid())
        device.destroyTextureView(cubeView);
    if (cubeArrayView.isValid())
        device.destroyTextureView(cubeArrayView);
    device.destroyTexture(texture);
    return valid && device.validationErrorCount() == validationErrorsBefore;
}

int main() {
#if defined(MECRAFT_ENABLE_STREAMLINE)
    StreamlineRuntime& streamline = StreamlineRuntime::instance();
    if (!streamline.initialize()) {
        std::cerr << streamline.lastError() << '\n';
        return 1;
    }
#endif
    if (glfwInit() != GLFW_TRUE) {
#if defined(MECRAFT_ENABLE_STREAMLINE)
        streamline.shutdown();
#endif
        return 1;
    }

    VkRhiDevice device;
    if (!device.prepareWindowCreation()) {
        glfwTerminate();
#if defined(MECRAFT_ENABLE_STREAMLINE)
        streamline.shutdown();
#endif
        return 1;
    }
    GLFWwindow* window = glfwCreateWindow(320, 240, "vulkan_rhi_smoke_test", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "vulkan_rhi_smoke_test: window creation failed\n";
        glfwTerminate();
#if defined(MECRAFT_ENABLE_STREAMLINE)
        streamline.shutdown();
#endif
        return 1;
    }
    glfwShowWindow(window);
    glfwWaitEventsTimeout(0.1);

    RhiDeviceDesc desc;
    desc.debugName = "VulkanRhiSmokeTest";
    desc.nativeWindow = window;
    desc.width = 320;
    desc.height = 240;
    desc.enableDebugMarkers = true;
    desc.enableDebugOutput = true;
    if (!device.init(desc)) {
        std::cerr << "vulkan_rhi_smoke_test: device init failed\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (device.capabilities().vulkanApiVersion < VK_API_VERSION_1_3 ||
        !device.capabilities().shaderDemoteToHelperInvocation) {
        std::cerr << "vulkan_rhi_smoke_test: Vulkan 1.3 shader target features are unavailable\n";
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (device.swapchainColorFormat() != RhiTextureFormat::Bgra8Unorm ||
        !validateResourceDescriptionNameOwnership(device)) {
        std::cerr << "vulkan_rhi_smoke_test: swapchain format or resource "
                     "name ownership check failed\n";
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (!validateTextureAliasing(device) || !validateCubeArrayViews(device)) {
        std::cerr << "vulkan_rhi_smoke_test: texture aliasing or cube-array check failed\n";
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (!device.vsyncEnabled() || device.capabilities().swapchainPresentMode != RhiPresentMode::Fifo) {
        std::cerr << "vulkan_rhi_smoke_test: default present mode is not "
                     "vsync Fifo\n";
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (device.capabilities().vsyncControl) {
        if (!device.setVsyncEnabled(false) || device.vsyncEnabled()) {
            std::cerr << "vulkan_rhi_smoke_test: vsync toggle failed\n";
            device.shutdown();
            glfwDestroyWindow(window);
            glfwTerminate();
            return 1;
        }
    }
    if (!createDrawParametersShader(device)) {
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (!validateIndependentBlendPipeline(device)) {
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    constexpr uint32_t textureWidth = 256u;
    constexpr uint32_t textureHeight = 128u;
    constexpr uint32_t textureDepth = 33u;
    std::vector<float> texturePixels(static_cast<size_t>(textureWidth) * textureHeight * textureDepth * 4u, 0.25f);
    RhiTextureDesc textureDesc;
    textureDesc.debugName = "VulkanSmoke.Texture3D";
    textureDesc.dimension = RhiTextureDimension::Texture3D;
    textureDesc.format = RhiTextureFormat::Rgba32Float;
    textureDesc.width = textureWidth;
    textureDesc.height = textureHeight;
    textureDesc.depthOrLayers = textureDepth;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);
    RhiTextureInitialData textureInitialData;
    textureInitialData.pixels = texturePixels.data();
    textureInitialData.sizeBytes = texturePixels.size() * sizeof(float);
    textureInitialData.layerCount = textureDepth;
    textureInitialData.finalState = RhiResourceState::ShaderRead;
    const RhiTextureHandle texture = device.createTexture(textureDesc, &textureInitialData);
    if (!texture.isValid()) {
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    RhiTextureViewDesc textureViewDesc;
    textureViewDesc.texture = texture;
    textureViewDesc.viewType = RhiTextureViewType::Texture3D;
    textureViewDesc.format = RhiTextureFormat::Rgba32Float;
    textureViewDesc.baseMip = 0u;
    textureViewDesc.mipCount = 1u;
    textureViewDesc.baseLayer = 0u;
    textureViewDesc.layerCount = textureDepth;
    const RhiTextureViewHandle textureView = device.createTextureView(textureViewDesc);
    if (!textureView.isValid()) {
        device.destroyTexture(texture);
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    std::unique_ptr<RhiCommandListPool> commandPool =
        device.createCommandListPool({"VulkanSmoke.CommandPool", 4u, 64u * 1024u});
    const bool immediateModeValidated =
        !device.capabilities().vsyncControl ||
        (commandPool != nullptr && renderStableFrame(device, *commandPool, window) &&
         device.capabilities().swapchainPresentMode == RhiPresentMode::Immediate && device.setVsyncEnabled(true) &&
         device.vsyncEnabled() && renderStableFrame(device, *commandPool, window) &&
         device.capabilities().swapchainPresentMode == RhiPresentMode::Fifo);
    if (commandPool == nullptr || !immediateModeValidated || !validateRg32UintAttachmentClear(device, *commandPool) ||
#if defined(MECRAFT_ENABLE_FSR31)
        !validateFsr31VulkanDispatch(device, *commandPool) || !validateFsr31VulkanContext(device) ||
#endif
#if defined(MECRAFT_ENABLE_STREAMLINE)
        !validateDlssVulkanDispatch(device, *commandPool, window) ||
        !validateDlssFrameGenerationSwapchainLifecycle(device, *commandPool, window) ||
#endif
        !validateTemporalOutputTarget(device, *commandPool) ||
        !validateVulkanInterop(device, *commandPool, texture, textureView, textureWidth, textureHeight, textureDepth) ||
        !validateOffscreenCoordinateContract(device, *commandPool, window) ||
        !validateRenderGraphMultiQueue(device, *commandPool) ||
        !validateRenderGraphMultithreadedRecord(device, *commandPool) ||
        !validateRenderGraphTextureAliasing(device, *commandPool) ||
        !validateIndependentUiPresentation(device, *commandPool, window) ||
        !rejectDestroyedResourceSubmission(device, *commandPool) || !cancelAcquiredFrame(device, window) ||
        !cancelSubmittedFrame(device, *commandPool, window) || !renderStableFrame(device, *commandPool, window) ||
        !renderStableFrame(device, *commandPool, window)) {
        commandPool.reset();
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    glfwSetWindowSize(window, 480, 320);
    const bool resized = renderStableFrame(device, *commandPool, window);

    device.destroyTextureView(textureView);
    device.destroyTexture(texture);
    commandPool.reset();
    device.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return resized ? 0 : 1;
}
