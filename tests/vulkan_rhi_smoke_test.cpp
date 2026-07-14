#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/vulkan/VkRhiDevice.h"
#include "renderer/rhi/vulkan/VkRhiInterop.h"
#include "renderer/passes/TemporalUpscalePass.h"

#if defined(MECRAFT_ENABLE_FSR31)
#include "renderer/upscaling/Fsr31VulkanContext.h"
#endif

#include <GLFW/glfw3.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {

enum class FrameAttempt {
    Success,
    Retry,
    Error
};

[[nodiscard]] bool validateVulkanInterop(VkRhiDevice& device,
                                         RhiCommandListPool& commandPool,
                                         const RhiTextureHandle texture,
                                         const RhiTextureViewHandle view,
                                         const uint32_t width,
                                         const uint32_t height,
                                         const uint32_t depth) {
    const auto deviceInfo = VkRhiInterop::deviceInfo(device);
    const auto textureInfo = VkRhiInterop::textureInfo(device, texture, view);
    if (!deviceInfo.has_value() || deviceInfo->instance == VK_NULL_HANDLE ||
        deviceInfo->physicalDevice == VK_NULL_HANDLE ||
        deviceInfo->device == VK_NULL_HANDLE ||
        deviceInfo->graphicsQueue == VK_NULL_HANDLE ||
        deviceInfo->graphicsQueueFamily == VK_QUEUE_FAMILY_IGNORED ||
        !textureInfo.has_value() || textureInfo->image == VK_NULL_HANDLE ||
        textureInfo->view == VK_NULL_HANDLE ||
        textureInfo->format != VK_FORMAT_R32G32B32A32_SFLOAT ||
        textureInfo->extent.width != width || textureInfo->extent.height != height ||
        textureInfo->extent.depth != depth || textureInfo->arrayLayers != 1u ||
        textureInfo->imageType != VK_IMAGE_TYPE_3D ||
        textureInfo->viewType != VK_IMAGE_VIEW_TYPE_3D ||
        textureInfo->mipCount != 1u || textureInfo->layerCount != 1u ||
        textureInfo->aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
        VkRhiInterop::textureInfo(device, {}, view).has_value() ||
        VkRhiInterop::resourceLayout(RhiResourceState::ShaderRead) !=
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        return false;
    }

    RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Graphics);
    if (commands == nullptr ||
        !commands->begin({"VulkanSmoke.Interop", RhiCommandListType::Graphics})) {
        return false;
    }
    const auto nativeCommands = VkRhiInterop::commandBuffer(device, *commands);
    if (!nativeCommands.has_value() || *nativeCommands == VK_NULL_HANDLE ||
        !commands->end() || VkRhiInterop::commandBuffer(device, *commands).has_value()) {
        return false;
    }
    return true;
}

[[nodiscard]] bool validateTemporalOutputTarget(
    VkRhiDevice& device,
    RhiCommandListPool& commandPool) {
    TemporalUpscalePass pass;
    pass.init(device, commandPool);
    UpscaleSettings settings;
    settings.type = TemporalUpscalerType::Fsr31;
    settings.quality = TemporalUpscaleQuality::Quality;
    constexpr TemporalExtent kInitialExtent{640u, 360u};
    if (!pass.prepareOutputTarget(
            settings, kInitialExtent, kInitialExtent)) {
        return false;
    }
    const RhiTextureHandle initialTexture = pass.outputTextureHandle();
    const RhiTextureViewHandle initialView = pass.outputTextureViewHandle();
    const auto initialInfo = VkRhiInterop::textureInfo(
        device, initialTexture, initialView);
    if (!initialInfo.has_value() ||
        initialInfo->format != VK_FORMAT_R16G16B16A16_SFLOAT ||
        initialInfo->extent.width != kInitialExtent.width ||
        initialInfo->extent.height != kInitialExtent.height ||
        (initialInfo->usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0u ||
        (initialInfo->usage & VK_IMAGE_USAGE_STORAGE_BIT) == 0u ||
        !pass.prepareOutputTarget(settings, kInitialExtent, kInitialExtent) ||
        pass.outputTextureHandle().index != initialTexture.index ||
        pass.outputTextureHandle().generation != initialTexture.generation) {
        return false;
    }

    constexpr TemporalExtent kResizedExtent{960u, 540u};
    if (!pass.prepareOutputTarget(
            settings, kResizedExtent, kResizedExtent)) {
        return false;
    }
    const RhiTextureHandle resizedTexture = pass.outputTextureHandle();
    const bool targetRecreated = resizedTexture.index != initialTexture.index ||
                                 resizedTexture.generation != initialTexture.generation;
    const auto resizedInfo = VkRhiInterop::textureInfo(
        device, resizedTexture, pass.outputTextureViewHandle());
    settings.type = TemporalUpscalerType::Native;
    if (!targetRecreated || !resizedInfo.has_value() ||
        resizedInfo->extent.width != kResizedExtent.width ||
        resizedInfo->extent.height != kResizedExtent.height ||
        !pass.prepareOutputTarget(settings, kResizedExtent, kResizedExtent) ||
        pass.outputTextureHandle().isValid() ||
        pass.outputTextureViewHandle().isValid()) {
        return false;
    }
    pass.shutdown();
    return true;
}

#if defined(MECRAFT_ENABLE_FSR31)
[[nodiscard]] bool validateFsr31VulkanContext(VkRhiDevice& device) {
    Fsr31VulkanContext context;
    const Fsr31VulkanContextCreateResult invalid = context.initialize(
        device, {{}, {1920u, 1080u}, false, false});
    if (invalid.status != Fsr31VulkanContextCreateStatus::InvalidRenderExtent) {
        return false;
    }

    constexpr TemporalExtent kRenderExtent{1280u, 720u};
    constexpr TemporalExtent kOutputExtent{1920u, 1080u};
    const Fsr31VulkanContextCreateResult created = context.initialize(
        device, {kRenderExtent, kOutputExtent, false, false});
    if (!created.succeeded() || !context.isInitialized() ||
        context.maxRenderExtent() != kRenderExtent ||
        context.maxOutputExtent() != kOutputExtent ||
        context.scratchMemorySize() == 0u ||
        context.initialize(device, {kRenderExtent, kOutputExtent, false, false}).status !=
            Fsr31VulkanContextCreateStatus::AlreadyInitialized) {
        std::cerr << "FSR 3.1 Vulkan context creation failed: status "
                  << static_cast<uint32_t>(created.status)
                  << ", SDK error " << created.sdkError << '\n';
        return false;
    }
    const Fsr31VulkanContextDestroyResult destroyed = context.shutdown();
    return destroyed.succeeded() && !context.isInitialized() &&
           context.scratchMemorySize() == 0u &&
           context.shutdown().status ==
               Fsr31VulkanContextDestroyStatus::NotInitialized;
}

struct Fsr31SmokeTexture {
    RhiTextureHandle texture;
    RhiTextureViewHandle view;
};

[[nodiscard]] bool createFsr31SmokeTexture(
    VkRhiDevice& device,
    const char* const debugName,
    const RhiTextureFormat format,
    const TemporalExtent extent,
    const RhiTextureUsageFlags usage,
    const void* const pixels,
    const size_t sizeBytes,
    const RhiResourceState finalState,
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

void destroyFsr31SmokeTexture(
    VkRhiDevice& device,
    Fsr31SmokeTexture& resource) {
    if (resource.view.isValid()) {
        device.destroyTextureView(resource.view);
    }
    if (resource.texture.isValid()) {
        device.destroyTexture(resource.texture);
    }
    resource = {};
}

[[nodiscard]] bool validateFsr31VulkanDispatch(
    VkRhiDevice& device,
    RhiCommandListPool& commandPool) {
    constexpr TemporalExtent kRenderExtent{320u, 180u};
    constexpr TemporalExtent kOutputExtent{480u, 270u};
    const size_t renderPixelCount =
        static_cast<size_t>(kRenderExtent.width) * kRenderExtent.height;
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

    const RhiTextureUsageFlags sampledUsage =
        rhiFlag(RhiTextureUsage::Sampled) |
        rhiFlag(RhiTextureUsage::TransferDst);
    const bool inputsCreated = createFsr31SmokeTexture(
            device, "VulkanSmoke.FSR31.Hdr", RhiTextureFormat::Rgba16Float,
            kRenderExtent, sampledUsage, hdrPixels.data(),
            hdrPixels.size() * sizeof(uint16_t), RhiResourceState::ShaderRead, hdr) &&
        createFsr31SmokeTexture(
            device, "VulkanSmoke.FSR31.Depth", RhiTextureFormat::Depth32Float,
            kRenderExtent,
            sampledUsage | rhiFlag(RhiTextureUsage::DepthStencilAttachment),
            depthPixels.data(), depthPixels.size() * sizeof(float),
            RhiResourceState::DepthRead, depth) &&
        createFsr31SmokeTexture(
            device, "VulkanSmoke.FSR31.Velocity", RhiTextureFormat::Rg16Float,
            kRenderExtent, sampledUsage, velocityPixels.data(),
            velocityPixels.size() * sizeof(uint16_t),
            RhiResourceState::ShaderRead, velocity) &&
        createFsr31SmokeTexture(
            device, "VulkanSmoke.FSR31.Exposure", RhiTextureFormat::Rgba16Float,
            {1u, 1u}, sampledUsage, exposurePixels, sizeof(exposurePixels),
            RhiResourceState::ShaderRead, exposure) &&
        createFsr31SmokeTexture(
            device, "VulkanSmoke.FSR31.Reactive", RhiTextureFormat::R8Unorm,
            kRenderExtent, sampledUsage, maskPixels.data(), maskPixels.size(),
            RhiResourceState::ShaderRead, reactive) &&
        createFsr31SmokeTexture(
            device, "VulkanSmoke.FSR31.Transparency", RhiTextureFormat::R8Unorm,
            kRenderExtent, sampledUsage, maskPixels.data(), maskPixels.size(),
            RhiResourceState::ShaderRead, transparency);
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
    if (!pass.prepareOutputTarget(settings, kRenderExtent, kOutputExtent)) {
        std::cerr << "FSR 3.1 smoke test failed to prepare the output target: texture "
                  << pass.outputTextureHandle().index << ", view "
                  << pass.outputTextureViewHandle().index << '\n';
        pass.shutdown();
        destroyInputs();
        return false;
    }

    TemporalFrameInput frame;
    frame.renderExtent = kRenderExtent;
    frame.outputExtent = kOutputExtent;
    frame.motionVectorScale = glm::vec2(
        static_cast<float>(kRenderExtent.width),
        static_cast<float>(kRenderExtent.height));
    frame.frameDeltaMilliseconds = 1000.0f / 60.0f;
    frame.preExposure = 1.0f;
    frame.cameraNear = 0.1f;
    frame.cameraFar = 1000.0f;
    frame.verticalFovRadians = 1.0f;
    frame.reset = true;
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
        frame.reset = false;
        secondResult = pass.execute(settings, frame);
    }
    device.waitIdle();
    const bool dispatched = firstResult.succeeded() &&
        secondResult.succeeded() && secondResult.outputHdrColor.isValid() &&
        secondResult.outputHdrColorView.isValid() &&
        secondResult.outputExtent == kOutputExtent;
    if (!dispatched) {
        std::cerr << "FSR 3.1 smoke dispatch failed: "
                  << TemporalUpscalePass::statusText(
                         firstResult.succeeded() ? secondResult.status
                                                 : firstResult.status)
                  << ", SDK error "
                  << (firstResult.succeeded() ? secondResult.sdkError
                                              : firstResult.sdkError)
                  << '\n';
    }
    pass.shutdown();
    destroyInputs();
    return dispatched;
}
#endif

[[nodiscard]] FrameAttempt renderFrame(VkRhiDevice& device,
                                       RhiCommandListPool& commandPool,
                                       const uint32_t width,
                                       const uint32_t height) {
    if (!device.resizeSwapchain(width, height)) {
        return FrameAttempt::Error;
    }
    const RhiFrameAcquireResult frame = device.acquireFrame();
    if (frame.status != RhiFrameStatus::Success &&
        frame.status != RhiFrameStatus::Suboptimal) {
        return frame.status == RhiFrameStatus::OutOfDate ||
                       frame.status == RhiFrameStatus::Minimized
            ? FrameAttempt::Retry : FrameAttempt::Error;
    }

    RhiCommandList* clearCommands = commandPool.acquire(RhiCommandListType::Graphics);
    if (clearCommands == nullptr ||
        !clearCommands->begin({"VulkanSmoke.Clear", RhiCommandListType::Graphics})) {
        return FrameAttempt::Error;
    }
    clearCommands->textureBarrier({frame.colorTexture,
                                   RhiResourceState::Present,
                                   RhiResourceState::RenderTarget});
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
    clearCommands->textureBarrier({frame.colorTexture,
                                   RhiResourceState::RenderTarget,
                                   RhiResourceState::Present});
    if (!clearCommands->end()) {
        return FrameAttempt::Error;
    }
    RhiCommandList* firstSubmission[] = {clearCommands};
    if (!device.submit({"VulkanSmoke.ClearSubmit", firstSubmission, 1u})) {
        return FrameAttempt::Error;
    }

    RhiCommandList* tailCommands = commandPool.acquire(RhiCommandListType::Graphics);
    if (tailCommands == nullptr ||
        !tailCommands->begin({"VulkanSmoke.Tail", RhiCommandListType::Graphics}) ||
        !tailCommands->end()) {
        return FrameAttempt::Error;
    }
    RhiCommandList* secondSubmission[] = {tailCommands};
    RhiSubmissionToken secondToken;
    if (!device.submit({"VulkanSmoke.TailSubmit", secondSubmission, 1u}, &secondToken)) {
        return FrameAttempt::Error;
    }
    const RhiFrameStatus presentStatus = device.presentFrame(
        {frame.frameIndex, frame.imageIndex});
    if (presentStatus != RhiFrameStatus::Success &&
        presentStatus != RhiFrameStatus::Suboptimal) {
        return presentStatus == RhiFrameStatus::OutOfDate ||
                       presentStatus == RhiFrameStatus::Minimized
            ? FrameAttempt::Retry : FrameAttempt::Error;
    }
    return device.waitForSubmission(secondToken)
        ? FrameAttempt::Success : FrameAttempt::Error;
}

[[nodiscard]] bool renderStableFrame(VkRhiDevice& device,
                                     RhiCommandListPool& commandPool,
                                     GLFWwindow* window) {
    for (uint32_t attempt = 0u; attempt < 20u; ++attempt) {
        glfwWaitEventsTimeout(0.02);
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth <= 0 || framebufferHeight <= 0) {
            continue;
        }
        const FrameAttempt result = renderFrame(
            device, commandPool,
            static_cast<uint32_t>(framebufferWidth),
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

[[nodiscard]] bool rejectDestroyedResourceSubmission(VkRhiDevice& device,
                                                      RhiCommandListPool& commandPool) {
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "VulkanSmoke.DestroyedBuffer";
    bufferDesc.size = 256u;
    bufferDesc.usage = rhiFlag(RhiBufferUsage::TransferDst);
    const RhiBufferHandle buffer = device.createBuffer(bufferDesc, nullptr, 0u);
    if (!buffer.isValid()) return false;

    RhiCommandList* referencingCommands = commandPool.acquire(RhiCommandListType::Graphics);
    if (referencingCommands == nullptr ||
        !referencingCommands->begin(
            {"VulkanSmoke.DestroyedResource", RhiCommandListType::Graphics})) {
        device.destroyBuffer(buffer);
        return false;
    }
    referencingCommands->bufferBarrier(
        {buffer, RhiResourceState::Undefined, RhiResourceState::TransferDst});
    if (!referencingCommands->end()) {
        device.destroyBuffer(buffer);
        return false;
    }
    device.destroyBuffer(buffer);

    RhiCommandList* unrelatedCommands = commandPool.acquire(RhiCommandListType::Graphics);
    if (unrelatedCommands == nullptr ||
        !unrelatedCommands->begin(
            {"VulkanSmoke.Unrelated", RhiCommandListType::Graphics}) ||
        !unrelatedCommands->end()) {
        return false;
    }
    RhiCommandList* unrelatedSubmission[] = {unrelatedCommands};
    RhiSubmissionToken unrelatedToken;
    if (!device.submit({"VulkanSmoke.UnrelatedSubmit", unrelatedSubmission, 1u},
                       &unrelatedToken) ||
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
        !device.resizeSwapchain(static_cast<uint32_t>(width),
                                static_cast<uint32_t>(height))) {
        return false;
    }
    const RhiFrameAcquireResult frame = device.acquireFrame();
    if (frame.status != RhiFrameStatus::Success &&
        frame.status != RhiFrameStatus::Suboptimal) {
        return false;
    }
    return device.presentFrame({frame.frameIndex, frame.imageIndex}) ==
           RhiFrameStatus::OutOfDate;
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

[[nodiscard]] bool validateOffscreenCoordinateContract(VkRhiDevice& device,
                                                       RhiCommandListPool& commandPool,
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
    textureDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment) |
                        rhiFlag(RhiTextureUsage::TransferSrc);
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
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) |
                         rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    const RhiBufferHandle readback = device.createBuffer(readbackDesc, nullptr, 0u);
    if (!readback.isValid()) {
        return false;
    }

    RhiCommandList* commands = commandPool.acquire(RhiCommandListType::Graphics);
    if (commands == nullptr ||
        !commands->begin({"VulkanSmoke.OffscreenOrientation.Commands",
                          RhiCommandListType::Graphics})) {
        return false;
    }
    commands->textureBarrier({texture, RhiResourceState::Undefined,
                              RhiResourceState::RenderTarget});
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
    commands->textureBarrier({texture, RhiResourceState::RenderTarget,
                              RhiResourceState::TransferSrc});
    RhiTextureBufferCopy copy;
    copy.srcTexture = texture;
    copy.dstBuffer = readback;
    copy.bytesPerRow = kBytesPerRow;
    copy.rowsPerImage = kHeight;
    copy.width = kWidth;
    copy.height = kHeight;
    commands->copyTextureToBuffer(copy);
    commands->bufferBarrier({readback, RhiResourceState::TransferDst,
                             RhiResourceState::HostRead});
    if (!commands->end()) {
        return false;
    }
    RhiCommandList* submissions[] = {commands};
    RhiSubmissionToken token;
    if (!device.submit({"VulkanSmoke.OffscreenOrientation.Submit", submissions, 1u},
                       &token) ||
        !device.waitForSubmission(token)) {
        return false;
    }

    const auto* pixels = static_cast<const uint8_t*>(
        device.mapBuffer(readback, 0u, kReadbackSize));
    if (pixels == nullptr) {
        return false;
    }
    const auto isRed = [](const uint8_t* pixel) {
        return pixel[0] >= 250u && pixel[1] <= 5u && pixel[2] <= 5u && pixel[3] >= 250u;
    };
    const auto isBlue = [](const uint8_t* pixel) {
        return pixel[0] <= 5u && pixel[1] <= 5u && pixel[2] >= 250u && pixel[3] >= 250u;
    };
    const bool orientationCorrect =
        isBlue(pixels + static_cast<size_t>(kRenderY) * kBytesPerRow) &&
        isRed(pixels + static_cast<size_t>(kRenderY + kRenderHeight - 1u) * kBytesPerRow);
    device.unmapBuffer(readback);
    if (!orientationCorrect) {
        return false;
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth <= 0 || framebufferHeight <= 0 ||
        !device.resizeSwapchain(static_cast<uint32_t>(framebufferWidth),
                                static_cast<uint32_t>(framebufferHeight))) {
        return false;
    }
    const RhiFrameAcquireResult frame = device.acquireFrame();
    if (frame.status != RhiFrameStatus::Success &&
        frame.status != RhiFrameStatus::Suboptimal) {
        return false;
    }

    RhiGraphicsPipelineDesc presentationPipelineDesc = pipelineDesc;
    presentationPipelineDesc.debugName = "VulkanSmoke.PresentationOrientation.Pipeline";
    presentationPipelineDesc.colorFormats.clear();
    presentationPipelineDesc.colorFormats.push_back(device.swapchainColorFormat());
    const RhiPipelineHandle presentationPipeline =
        device.createGraphicsPipeline(presentationPipelineDesc);
    if (!presentationPipeline.isValid()) {
        return false;
    }

    RhiCommandList* presentationCommands = commandPool.acquire(RhiCommandListType::Graphics);
    if (presentationCommands == nullptr ||
        !presentationCommands->begin({"VulkanSmoke.PresentationOrientation.Commands",
                                      RhiCommandListType::Graphics})) {
        return false;
    }
    presentationCommands->bufferBarrier({readback, RhiResourceState::HostRead,
                                         RhiResourceState::TransferDst});
    presentationCommands->textureBarrier({texture, RhiResourceState::TransferSrc,
                                           RhiResourceState::RenderTarget});
    renderingInfo.renderArea = {0, 0, kWidth, kHeight};
    presentationCommands->beginRendering(renderingInfo);
    presentationCommands->setGraphicsPipeline(pipeline);
    presentationCommands->draw(3u, 1u, 0u, 0u);
    presentationCommands->endRendering();
    presentationCommands->textureBarrier({texture, RhiResourceState::RenderTarget,
                                           RhiResourceState::TransferSrc});

    presentationCommands->textureBarrier({frame.colorTexture, RhiResourceState::Present,
                                           RhiResourceState::RenderTarget});
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
    presentationCommands->textureBarrier({frame.colorTexture, RhiResourceState::RenderTarget,
                                           RhiResourceState::TransferSrc});

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

    presentationCommands->textureBarrier({frame.colorTexture, RhiResourceState::TransferSrc,
                                           RhiResourceState::TransferDst});
    RhiTextureBlit presentationBlit;
    presentationBlit.src = texture;
    presentationBlit.dstView = frame.colorView;
    presentationCommands->blitTexture(presentationBlit);
    presentationCommands->textureBarrier({frame.colorTexture, RhiResourceState::TransferDst,
                                           RhiResourceState::TransferSrc});
    presentationCopy.bufferOffset = kBytesPerPixel * 2u;
    presentationCopy.srcY = 0u;
    presentationCommands->copyTextureToBuffer(presentationCopy);
    presentationCopy.bufferOffset = kBytesPerPixel * 3u;
    presentationCopy.srcY = frame.height - 1u;
    presentationCommands->copyTextureToBuffer(presentationCopy);
    presentationCommands->bufferBarrier({readback, RhiResourceState::TransferDst,
                                         RhiResourceState::HostRead});
    presentationCommands->textureBarrier({frame.colorTexture, RhiResourceState::TransferSrc,
                                           RhiResourceState::Present});
    if (!presentationCommands->end()) {
        return false;
    }
    RhiCommandList* presentationSubmissions[] = {presentationCommands};
    RhiSubmissionToken presentationToken;
    if (!device.submit({"VulkanSmoke.PresentationOrientation.Submit",
                        presentationSubmissions, 1u},
                       &presentationToken)) {
        return false;
    }
    const RhiFrameStatus presentStatus = device.presentFrame(
        {frame.frameIndex, frame.imageIndex});
    if ((presentStatus != RhiFrameStatus::Success &&
         presentStatus != RhiFrameStatus::Suboptimal) ||
        !device.waitForSubmission(presentationToken)) {
        return false;
    }

    const auto* presentationPixels = static_cast<const uint8_t*>(
        device.mapBuffer(readback, 0u, kBytesPerPixel * 4u));
    if (presentationPixels == nullptr) {
        return false;
    }
    const auto isBgraRed = [](const uint8_t* pixel) {
        return pixel[0] <= 5u && pixel[1] <= 5u && pixel[2] >= 250u && pixel[3] >= 250u;
    };
    const auto isBgraBlue = [](const uint8_t* pixel) {
        return pixel[0] >= 250u && pixel[1] <= 5u && pixel[2] <= 5u && pixel[3] >= 250u;
    };
    const bool presentationCorrect =
        isBgraBlue(presentationPixels) &&
        isBgraRed(presentationPixels + kBytesPerPixel) &&
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

} // namespace

int main() {
    if (glfwInit() != GLFW_TRUE) {
        return 1;
    }

    VkRhiDevice device;
    if (!device.prepareWindowCreation()) {
        glfwTerminate();
        return 1;
    }
    GLFWwindow* window = glfwCreateWindow(320, 240, "vulkan_rhi_smoke_test", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
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
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (device.swapchainColorFormat() != RhiTextureFormat::Bgra8Unorm) {
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (!device.vsyncEnabled() ||
        device.capabilities().swapchainPresentMode != RhiPresentMode::Fifo) {
        device.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (device.capabilities().vsyncControl) {
        if (!device.setVsyncEnabled(false) || device.vsyncEnabled()) {
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
    constexpr uint32_t textureWidth = 256u;
    constexpr uint32_t textureHeight = 128u;
    constexpr uint32_t textureDepth = 33u;
    std::vector<float> texturePixels(
        static_cast<size_t>(textureWidth) * textureHeight * textureDepth * 4u, 0.25f);
    RhiTextureDesc textureDesc;
    textureDesc.debugName = "VulkanSmoke.Texture3D";
    textureDesc.dimension = RhiTextureDimension::Texture3D;
    textureDesc.format = RhiTextureFormat::Rgba32Float;
    textureDesc.width = textureWidth;
    textureDesc.height = textureHeight;
    textureDesc.depthOrLayers = textureDepth;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                        rhiFlag(RhiTextureUsage::TransferDst);
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
    std::unique_ptr<RhiCommandListPool> commandPool = device.createCommandListPool(
        {"VulkanSmoke.CommandPool", 4u, 64u * 1024u});
    const bool immediateModeValidated =
        !device.capabilities().vsyncControl ||
        (commandPool != nullptr &&
         renderStableFrame(device, *commandPool, window) &&
         device.capabilities().swapchainPresentMode == RhiPresentMode::Immediate &&
         device.setVsyncEnabled(true) && device.vsyncEnabled() &&
         renderStableFrame(device, *commandPool, window) &&
         device.capabilities().swapchainPresentMode == RhiPresentMode::Fifo);
    if (commandPool == nullptr || !immediateModeValidated ||
#if defined(MECRAFT_ENABLE_FSR31)
        !validateFsr31VulkanDispatch(device, *commandPool) ||
        !validateFsr31VulkanContext(device) ||
#endif
        !validateTemporalOutputTarget(device, *commandPool) ||
        !validateVulkanInterop(device, *commandPool, texture, textureView,
                               textureWidth, textureHeight, textureDepth) ||
        !validateOffscreenCoordinateContract(device, *commandPool, window) ||
        !rejectDestroyedResourceSubmission(device, *commandPool) ||
        !cancelAcquiredFrame(device, window) ||
        !renderStableFrame(device, *commandPool, window) ||
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
