#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/vulkan/VkRhiDevice.h"

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
    if (commandPool == nullptr ||
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
