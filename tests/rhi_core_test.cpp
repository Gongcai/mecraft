#include "renderer/rhi/RhiHandleAllocator.h"
#include "renderer/rhi/RhiHash.h"
#include "renderer/rhi/gl/GlRhiDevice.h"
#include "renderer/rhi/gl/GlRhiTextureRegistry.h"
#include "resource/RhiTextureResourceUtils.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <array>
#include <cstdint>
#include <iostream>

namespace {
bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

class GlTestContext {
public:
    ~GlTestContext() {
        shutdown();
    }

    [[nodiscard]] bool init() {
        if (!glfwInit()) {
            std::cerr << "GLFW must initialize for OpenGL RHI backend tests\n";
            return false;
        }
        m_glfwInitialized = true;

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        m_window = glfwCreateWindow(32, 32, "rhi_core_test", nullptr, nullptr);
        if (m_window == nullptr) {
            std::cerr << "hidden GLFW OpenGL test window must be created\n";
            shutdown();
            return false;
        }

        glfwMakeContextCurrent(m_window);
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
            std::cerr << "GLAD must load OpenGL symbols for RHI backend tests\n";
            shutdown();
            return false;
        }
        if (!GLAD_GL_VERSION_4_5) {
            std::cerr << "OpenGL 4.5 must be available for RHI backend tests\n";
            shutdown();
            return false;
        }
        return true;
    }

private:
    void shutdown() {
        if (m_window != nullptr) {
            glfwDestroyWindow(m_window);
            m_window = nullptr;
        }
        if (m_glfwInitialized) {
            glfwTerminate();
            m_glfwInitialized = false;
        }
    }

    GLFWwindow* m_window = nullptr;
    bool m_glfwInitialized = false;
};

bool testHandleGeneration() {
    RhiHandleAllocator<RhiTextureHandle> allocator;

    const RhiTextureHandle first = allocator.allocate();
    if (!requireTrue(first.isValid(), "allocated texture handle must be valid")) {
        return false;
    }
    if (!requireTrue(allocator.isAlive(first), "allocated texture handle must be alive")) {
        return false;
    }
    if (!requireTrue(allocator.release(first), "release must accept a live texture handle")) {
        return false;
    }
    if (!requireTrue(!allocator.isAlive(first), "released texture handle must not stay alive")) {
        return false;
    }

    const RhiTextureHandle second = allocator.allocate();
    if (!requireTrue(second.index == first.index, "allocator must reuse released handle slots")) {
        return false;
    }
    if (!requireTrue(second.generation != first.generation, "reused handle generation must change")) {
        return false;
    }
    return requireTrue(!allocator.release(first), "release must reject stale texture handles");
}

bool testDescHashStability() {
    RhiTextureDesc desc;
    desc.dimension = RhiTextureDimension::Texture2DArray;
    desc.format = RhiTextureFormat::Rgba8Unorm;
    desc.width = 16;
    desc.height = 16;
    desc.depthOrLayers = 128;
    desc.mipLevels = 5;
    desc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);

    const uint64_t firstHash = rhiHashTextureDesc(desc);
    const uint64_t secondHash = rhiHashTextureDesc(desc);
    if (!requireTrue(firstHash == secondHash, "texture desc hash must be stable")) {
        return false;
    }

    desc.mipLevels = 1;
    return requireTrue(firstHash != rhiHashTextureDesc(desc),
                       "texture desc hash must change when semantic fields change");
}

bool testGlTextureRegistry() {
    renderer::rhi::gl::GlRhiTextureRegistration registration;
    registration.textureId = 42;
    registration.dimension = RhiTextureDimension::Texture2D;
    registration.format = RhiTextureFormat::Rgba8Unorm;
    registration.width = 16;
    registration.height = 16;
    registration.depthOrLayers = 1;
    registration.mipLevels = 1;
    registration.sampleCount = 1;
    registration.usage = rhiFlag(RhiTextureUsage::Sampled);

    const RhiTextureHandle handle = renderer::rhi::gl::registerTexture(registration);
    if (!requireTrue(handle.isValid(), "registered GL texture must return a valid RHI handle")) {
        return false;
    }
    if (!requireTrue(renderer::rhi::gl::isTextureRegistered(handle),
                     "registered GL texture handle must be alive")) {
        return false;
    }
    if (!requireTrue(renderer::rhi::gl::textureId(handle) == registration.textureId,
                     "registered GL texture handle must resolve to the native texture id")) {
        return false;
    }

    renderer::rhi::gl::GlRhiTextureRegistration resolved;
    if (!requireTrue(renderer::rhi::gl::textureRegistration(handle, resolved),
                     "registered GL texture handle must expose its copied metadata")) {
        return false;
    }
    if (!requireTrue(resolved.width == registration.width &&
                         resolved.height == registration.height &&
                         resolved.format == registration.format,
                     "registered GL texture metadata must round-trip")) {
        return false;
    }

    renderer::rhi::gl::unregisterTexture(handle);
    return requireTrue(!renderer::rhi::gl::isTextureRegistered(handle),
                       "unregistered GL texture handle must not stay alive");
}

bool testResourceTextureRegistration() {
    TextureAtlas atlas;
    const uint32_t atlasTextureId = 77;
    atlas.atlasWidth = 32;
    atlas.atlasHeight = 16;
    if (!requireTrue(resource::registerTextureAtlas(atlas, atlasTextureId),
                     "texture atlas registration must create an RHI texture handle")) {
        return false;
    }
    if (!requireTrue(renderer::rhi::gl::textureId(atlas.texture) == atlasTextureId,
                     "texture atlas handle must resolve to its native texture id")) {
        return false;
    }
    resource::unregisterTextureAtlas(atlas);
    if (!requireTrue(!atlas.texture.isValid(),
                     "texture atlas unregister must clear the RHI texture handle")) {
        return false;
    }

    TextureArray textureArray;
    const uint32_t textureArrayId = 88;
    textureArray.tileSize = 16;
    textureArray.layerCount = 4;
    if (!requireTrue(resource::registerTextureArray(textureArray, textureArrayId),
                     "texture array registration must create an RHI texture handle")) {
        return false;
    }
    if (!requireTrue(renderer::rhi::gl::textureId(textureArray.texture) == textureArrayId,
                     "texture array handle must resolve to its native texture id")) {
        return false;
    }
    resource::unregisterTextureArray(textureArray);
    return requireTrue(!textureArray.texture.isValid(),
                       "texture array unregister must clear the RHI texture handle");
}

bool testGlRhiDeviceHandles() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize")) {
        return false;
    }

    GlRhiDevice device;
    RhiDeviceDesc deviceDesc;
    deviceDesc.debugName = "rhi_core_test";
    if (!requireTrue(device.init(deviceDesc), "OpenGL RHI device must initialize")) {
        return false;
    }

    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "test-buffer";
    bufferDesc.size = 256;
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) | rhiFlag(RhiBufferUsage::TransferDst);
    const RhiBufferHandle buffer = device.createBuffer(bufferDesc, nullptr, 0);
    if (!requireTrue(buffer.isValid(), "OpenGL RHI device must create buffer handles")) {
        return false;
    }

    RhiTextureDesc textureDesc;
    textureDesc.debugName = "test-texture";
    textureDesc.width = 4;
    textureDesc.height = 4;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);
    const RhiTextureHandle texture = device.createTexture(textureDesc, nullptr);
    if (!requireTrue(texture.isValid(), "OpenGL RHI device must create texture handles")) {
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = texture;
    const RhiTextureViewHandle view = device.createTextureView(viewDesc);
    if (!requireTrue(view.isValid(), "OpenGL RHI device must create texture view handles")) {
        return false;
    }

    device.destroyTextureView(view);
    device.destroyTexture(texture);
    device.destroyBuffer(buffer);
    device.shutdown();
    return true;
}

bool testGlRhiSwapchainBackbuffer() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for swapchain draw")) {
        return false;
    }

    GlRhiDevice device;
    RhiDeviceDesc deviceDesc;
    deviceDesc.debugName = "rhi_swapchain_backbuffer_test";
    deviceDesc.width = 32;
    deviceDesc.height = 32;
    if (!requireTrue(device.init(deviceDesc), "OpenGL RHI device must initialize for swapchain draw")) {
        return false;
    }
    if (!requireTrue(device.resizeSwapchain(32u, 32u), "swapchain resize must accept live framebuffer dimensions")) {
        device.shutdown();
        return false;
    }

    const RhiTextureViewHandle swapchainView = device.currentSwapchainColorView();
    if (!requireTrue(swapchainView.isValid(), "swapchain color view must be valid")) {
        device.shutdown();
        return false;
    }
    const RhiTextureViewHandle swapchainDepthView = device.currentSwapchainDepthStencilView();
    if (!requireTrue(swapchainDepthView.isValid(), "swapchain depth-stencil view must be valid")) {
        device.shutdown();
        return false;
    }
    if (!requireTrue(device.swapchainColorFormat() == RhiTextureFormat::Rgba8Unorm,
                     "OpenGL swapchain format must be RGBA8 unorm")) {
        device.shutdown();
        return false;
    }
    if (!requireTrue(device.swapchainDepthStencilFormat() == RhiTextureFormat::Depth24,
                     "OpenGL swapchain depth-stencil format must be depth24")) {
        device.shutdown();
        return false;
    }

    constexpr char kFullscreenVertexShader[] = R"glsl(
#version 450 core
const vec2 kPositions[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2(3.0, -1.0),
    vec2(-1.0, 3.0)
);

void main() {
    gl_Position = vec4(kPositions[gl_VertexID], 0.0, 1.0);
}
)glsl";
    constexpr char kWhiteFragmentShader[] = R"glsl(
#version 450 core
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(1.0);
}
)glsl";

    RhiShaderDesc vertexShaderDesc;
    vertexShaderDesc.debugName = "swapchain-test-vertex";
    vertexShaderDesc.stage = RhiShaderStage::Vertex;
    vertexShaderDesc.source = kFullscreenVertexShader;
    vertexShaderDesc.sourceSize = sizeof(kFullscreenVertexShader) - 1u;
    const RhiShaderHandle vertexShader = device.createShader(vertexShaderDesc);
    if (!requireTrue(vertexShader.isValid(), "swapchain vertex shader must compile")) {
        device.shutdown();
        return false;
    }

    RhiShaderDesc fragmentShaderDesc;
    fragmentShaderDesc.debugName = "swapchain-test-fragment";
    fragmentShaderDesc.stage = RhiShaderStage::Fragment;
    fragmentShaderDesc.source = kWhiteFragmentShader;
    fragmentShaderDesc.sourceSize = sizeof(kWhiteFragmentShader) - 1u;
    const RhiShaderHandle fragmentShader = device.createShader(fragmentShaderDesc);
    if (!requireTrue(fragmentShader.isValid(), "swapchain fragment shader must compile")) {
        device.shutdown();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "swapchain-test-layout";
    const RhiPipelineLayoutHandle pipelineLayout = device.createPipelineLayout(pipelineLayoutDesc);
    if (!requireTrue(pipelineLayout.isValid(), "swapchain pipeline layout must be created")) {
        device.shutdown();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "swapchain-test-pipeline";
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.colorFormats.push_back(device.swapchainColorFormat());
    pipelineDesc.depthFormat = device.swapchainDepthStencilFormat();
    const RhiPipelineHandle pipeline = device.createGraphicsPipeline(pipelineDesc);
    if (!requireTrue(pipeline.isValid(), "swapchain graphics pipeline must be created")) {
        device.shutdown();
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = swapchainView;
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = swapchainDepthView;
    depthAttachment.depthLoadOp = RhiLoadOp::Clear;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    depthAttachment.clearDepth = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "swapchain-test-rendering";
    renderingInfo.renderArea = {0, 0, 32u, 32u};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    renderingInfo.depthStencilAttachment = &depthAttachment;

    RhiCommandList& cmd = device.beginFrame();
    cmd.beginRendering(renderingInfo);
    cmd.setViewport({0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 1.0f});
    cmd.setGraphicsPipeline(pipeline);
    cmd.draw(3u, 1u, 0u, 0u);

    std::array<uint8_t, 4> pixel{};
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    float centerDepth = 1.0f;
    glReadPixels(16, 16, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &centerDepth);
    cmd.endRendering();
    device.submitFrame(cmd);

    const bool whitePixel = pixel[0] >= 250u && pixel[1] >= 250u && pixel[2] >= 250u && pixel[3] >= 250u;
    if (!requireTrue(whitePixel, "swapchain fullscreen draw must produce a white center pixel")) {
        std::cerr << "center pixel rgba=("
                  << static_cast<int>(pixel[0]) << ", "
                  << static_cast<int>(pixel[1]) << ", "
                  << static_cast<int>(pixel[2]) << ", "
                  << static_cast<int>(pixel[3]) << ")\n";
        device.shutdown();
        return false;
    }
    if (!requireTrue(centerDepth > 0.49f && centerDepth < 0.51f,
                     "swapchain depth attachment must receive fullscreen triangle depth")) {
        std::cerr << "center depth=" << centerDepth << '\n';
        device.shutdown();
        return false;
    }

    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(pipelineLayout);
    device.destroyShader(fragmentShader);
    device.destroyShader(vertexShader);
    device.shutdown();
    return true;
}

bool testGlRhiFullscreenTriangle() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for fullscreen draw")) {
        return false;
    }

    GlRhiDevice device;
    RhiDeviceDesc deviceDesc;
    deviceDesc.debugName = "rhi_fullscreen_triangle_test";
    if (!requireTrue(device.init(deviceDesc), "OpenGL RHI device must initialize for fullscreen draw")) {
        return false;
    }

    RhiTextureDesc textureDesc;
    textureDesc.debugName = "fullscreen-target";
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = 4;
    textureDesc.height = 4;
    textureDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment) | rhiFlag(RhiTextureUsage::TransferSrc);
    const RhiTextureHandle target = device.createTexture(textureDesc, nullptr);
    if (!requireTrue(target.isValid(), "fullscreen draw target texture must be created")) {
        device.shutdown();
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = target;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    const RhiTextureViewHandle targetView = device.createTextureView(viewDesc);
    if (!requireTrue(targetView.isValid(), "fullscreen draw target view must be created")) {
        device.shutdown();
        return false;
    }

    constexpr char kFullscreenVertexShader[] = R"glsl(
#version 450 core
const vec2 kPositions[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2(3.0, -1.0),
    vec2(-1.0, 3.0)
);

void main() {
    gl_Position = vec4(kPositions[gl_VertexID], 0.0, 1.0);
}
)glsl";
    constexpr char kSolidFragmentShader[] = R"glsl(
#version 450 core
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)glsl";

    RhiShaderDesc vertexShaderDesc;
    vertexShaderDesc.debugName = "fullscreen-test-vertex";
    vertexShaderDesc.stage = RhiShaderStage::Vertex;
    vertexShaderDesc.source = kFullscreenVertexShader;
    vertexShaderDesc.sourceSize = sizeof(kFullscreenVertexShader) - 1u;
    const RhiShaderHandle vertexShader = device.createShader(vertexShaderDesc);
    if (!requireTrue(vertexShader.isValid(), "fullscreen vertex shader must compile")) {
        device.shutdown();
        return false;
    }

    RhiShaderDesc fragmentShaderDesc;
    fragmentShaderDesc.debugName = "fullscreen-test-fragment";
    fragmentShaderDesc.stage = RhiShaderStage::Fragment;
    fragmentShaderDesc.source = kSolidFragmentShader;
    fragmentShaderDesc.sourceSize = sizeof(kSolidFragmentShader) - 1u;
    const RhiShaderHandle fragmentShader = device.createShader(fragmentShaderDesc);
    if (!requireTrue(fragmentShader.isValid(), "fullscreen fragment shader must compile")) {
        device.shutdown();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "fullscreen-test-layout";
    const RhiPipelineLayoutHandle pipelineLayout = device.createPipelineLayout(pipelineLayoutDesc);
    if (!requireTrue(pipelineLayout.isValid(), "fullscreen pipeline layout must be created")) {
        device.shutdown();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "fullscreen-test-pipeline";
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba8Unorm);
    const RhiPipelineHandle pipeline = device.createGraphicsPipeline(pipelineDesc);
    if (!requireTrue(pipeline.isValid(), "fullscreen graphics pipeline must be created")) {
        device.shutdown();
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targetView;
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 1.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "fullscreen-test-rendering";
    renderingInfo.renderArea = {0, 0, 4, 4};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1;

    RhiCommandList& cmd = device.beginFrame();
    cmd.beginRendering(renderingInfo);
    cmd.setViewport({0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f});
    cmd.setGraphicsPipeline(pipeline);
    cmd.draw(3, 1, 0, 0);

    std::array<uint8_t, 4> pixel{};
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    cmd.endRendering();
    device.submitFrame(cmd);

    const bool redPixel = pixel[0] >= 250u && pixel[1] <= 5u && pixel[2] <= 5u && pixel[3] >= 250u;
    if (!requireTrue(redPixel, "fullscreen triangle must render a red center pixel")) {
        std::cerr << "center pixel rgba=("
                  << static_cast<int>(pixel[0]) << ", "
                  << static_cast<int>(pixel[1]) << ", "
                  << static_cast<int>(pixel[2]) << ", "
                  << static_cast<int>(pixel[3]) << ")\n";
        device.shutdown();
        return false;
    }

    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(pipelineLayout);
    device.destroyShader(fragmentShader);
    device.destroyShader(vertexShader);
    device.destroyTextureView(targetView);
    device.destroyTexture(target);
    device.shutdown();
    return true;
}

bool readCenterPixel(GlRhiDevice& device,
                     const RhiTextureViewHandle targetView,
                     const uint32_t width,
                     const uint32_t height,
                     std::array<uint8_t, 4>& outPixel) {
    RhiColorAttachment colorAttachment;
    colorAttachment.view = targetView;
    colorAttachment.loadOp = RhiLoadOp::Load;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "readback-rendering";
    renderingInfo.renderArea = {0, 0, width, height};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1;

    RhiCommandList& cmd = device.beginFrame();
    cmd.beginRendering(renderingInfo);
    glReadPixels(static_cast<GLint>(width / 2u),
                 static_cast<GLint>(height / 2u),
                 1,
                 1,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 outPixel.data());
    cmd.endRendering();
    device.submitFrame(cmd);
    return true;
}

bool testGlRhiBufferCopyToTexture() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for buffer copy")) {
        return false;
    }

    GlRhiDevice device;
    RhiDeviceDesc deviceDesc;
    deviceDesc.debugName = "rhi_buffer_copy_test";
    if (!requireTrue(device.init(deviceDesc), "OpenGL RHI device must initialize for buffer copy")) {
        return false;
    }

    constexpr uint32_t kWidth = 4u;
    constexpr uint32_t kHeight = 4u;
    std::array<uint8_t, kWidth * kHeight * 4u> sourcePixels{};
    for (uint32_t i = 0u; i < kWidth * kHeight; ++i) {
        sourcePixels[i * 4u + 0u] = 0u;
        sourcePixels[i * 4u + 1u] = 0u;
        sourcePixels[i * 4u + 2u] = 255u;
        sourcePixels[i * 4u + 3u] = 255u;
    }

    RhiBufferDesc srcBufferDesc;
    srcBufferDesc.debugName = "copy-source-buffer";
    srcBufferDesc.size = sourcePixels.size();
    srcBufferDesc.usage = rhiFlag(RhiBufferUsage::TransferSrc);
    srcBufferDesc.memoryUsage = RhiMemoryUsage::CpuToGpu;
    const RhiBufferHandle srcBuffer =
        device.createBuffer(srcBufferDesc, sourcePixels.data(), sourcePixels.size());
    if (!requireTrue(srcBuffer.isValid(), "source buffer must be created for copy test")) {
        device.shutdown();
        return false;
    }

    RhiBufferDesc dstBufferDesc;
    dstBufferDesc.debugName = "copy-destination-buffer";
    dstBufferDesc.size = sourcePixels.size();
    dstBufferDesc.usage = rhiFlag(RhiBufferUsage::TransferSrc) | rhiFlag(RhiBufferUsage::TransferDst);
    const RhiBufferHandle dstBuffer = device.createBuffer(dstBufferDesc, nullptr, 0);
    if (!requireTrue(dstBuffer.isValid(), "destination buffer must be created for copy test")) {
        device.shutdown();
        return false;
    }

    RhiTextureDesc textureDesc;
    textureDesc.debugName = "copy-target-texture";
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = kWidth;
    textureDesc.height = kHeight;
    textureDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment) | rhiFlag(RhiTextureUsage::TransferDst);
    const RhiTextureHandle target = device.createTexture(textureDesc, nullptr);
    if (!requireTrue(target.isValid(), "copy target texture must be created")) {
        device.shutdown();
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = target;
    const RhiTextureViewHandle targetView = device.createTextureView(viewDesc);
    if (!requireTrue(targetView.isValid(), "copy target texture view must be created")) {
        device.shutdown();
        return false;
    }

    RhiCommandList& cmd = device.beginFrame();
    RhiBufferCopy bufferCopy;
    bufferCopy.src = srcBuffer;
    bufferCopy.dst = dstBuffer;
    bufferCopy.size = sourcePixels.size();
    cmd.copyBuffer(bufferCopy);

    RhiBufferTextureCopy textureCopy;
    textureCopy.srcBuffer = dstBuffer;
    textureCopy.dstTexture = target;
    textureCopy.width = kWidth;
    textureCopy.height = kHeight;
    cmd.copyBufferToTexture(textureCopy);
    cmd.textureBarrier({target, RhiResourceState::TransferDst, RhiResourceState::RenderTarget});
    device.submitFrame(cmd);

    std::array<uint8_t, 4> pixel{};
    if (!readCenterPixel(device, targetView, kWidth, kHeight, pixel)) {
        device.shutdown();
        return false;
    }

    const bool bluePixel = pixel[0] <= 5u && pixel[1] <= 5u && pixel[2] >= 250u && pixel[3] >= 250u;
    if (!requireTrue(bluePixel, "buffer copy path must produce a blue center pixel")) {
        std::cerr << "center pixel rgba=("
                  << static_cast<int>(pixel[0]) << ", "
                  << static_cast<int>(pixel[1]) << ", "
                  << static_cast<int>(pixel[2]) << ", "
                  << static_cast<int>(pixel[3]) << ")\n";
        device.shutdown();
        return false;
    }

    device.destroyTextureView(targetView);
    device.destroyTexture(target);
    device.destroyBuffer(dstBuffer);
    device.destroyBuffer(srcBuffer);
    device.shutdown();
    return true;
}

bool testGlRhiComputeStorageTexture() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for compute dispatch")) {
        return false;
    }

    GlRhiDevice device;
    RhiDeviceDesc deviceDesc;
    deviceDesc.debugName = "rhi_compute_storage_texture_test";
    if (!requireTrue(device.init(deviceDesc), "OpenGL RHI device must initialize for compute dispatch")) {
        return false;
    }

    constexpr uint32_t kWidth = 4u;
    constexpr uint32_t kHeight = 4u;
    RhiTextureDesc textureDesc;
    textureDesc.debugName = "compute-storage-target";
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = kWidth;
    textureDesc.height = kHeight;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Storage) | rhiFlag(RhiTextureUsage::ColorAttachment);
    const RhiTextureHandle target = device.createTexture(textureDesc, nullptr);
    if (!requireTrue(target.isValid(), "compute storage texture must be created")) {
        device.shutdown();
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = target;
    const RhiTextureViewHandle targetView = device.createTextureView(viewDesc);
    if (!requireTrue(targetView.isValid(), "compute storage texture view must be created")) {
        device.shutdown();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "compute-storage-layout";
    RhiBindGroupLayoutEntry storageImageEntry;
    storageImageEntry.binding = 0u;
    storageImageEntry.type = RhiBindingType::StorageTexture;
    storageImageEntry.stages = rhiFlag(RhiShaderStage::Compute);
    bindGroupLayoutDesc.entries.push_back(storageImageEntry);
    const RhiBindGroupLayoutHandle bindGroupLayout =
        device.createBindGroupLayout(bindGroupLayoutDesc);
    if (!requireTrue(bindGroupLayout.isValid(), "compute bind group layout must be created")) {
        device.shutdown();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "compute-pipeline-layout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(bindGroupLayout);
    const RhiPipelineLayoutHandle pipelineLayout = device.createPipelineLayout(pipelineLayoutDesc);
    if (!requireTrue(pipelineLayout.isValid(), "compute pipeline layout must be created")) {
        device.shutdown();
        return false;
    }

    constexpr char kComputeShader[] = R"glsl(
#version 450 core
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(rgba8, binding = 0) uniform writeonly image2D outImage;

void main() {
    imageStore(outImage, ivec2(gl_GlobalInvocationID.xy), vec4(0.0, 1.0, 0.0, 1.0));
}
)glsl";

    RhiShaderDesc computeShaderDesc;
    computeShaderDesc.debugName = "compute-storage-shader";
    computeShaderDesc.stage = RhiShaderStage::Compute;
    computeShaderDesc.source = kComputeShader;
    computeShaderDesc.sourceSize = sizeof(kComputeShader) - 1u;
    const RhiShaderHandle computeShader = device.createShader(computeShaderDesc);
    if (!requireTrue(computeShader.isValid(), "compute shader must compile")) {
        device.shutdown();
        return false;
    }

    RhiComputePipelineDesc pipelineDesc;
    pipelineDesc.debugName = "compute-storage-pipeline";
    pipelineDesc.computeShader = computeShader;
    pipelineDesc.layout = pipelineLayout;
    const RhiPipelineHandle pipeline = device.createComputePipeline(pipelineDesc);
    if (!requireTrue(pipeline.isValid(), "compute pipeline must be created")) {
        device.shutdown();
        return false;
    }

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = bindGroupLayout;
    RhiBindGroupEntry bindGroupEntry;
    bindGroupEntry.binding = 0u;
    bindGroupEntry.resource.textureView = targetView;
    bindGroupDesc.entries.push_back(bindGroupEntry);
    const RhiBindGroupHandle bindGroup = device.createBindGroup(bindGroupDesc);
    if (!requireTrue(bindGroup.isValid(), "compute bind group must be created")) {
        device.shutdown();
        return false;
    }

    RhiCommandList& cmd = device.beginFrame();
    cmd.setComputePipeline(pipeline);
    cmd.setBindGroup(0u, bindGroup);
    cmd.dispatch(kWidth, kHeight, 1u);
    cmd.textureBarrier({target, RhiResourceState::ShaderWrite, RhiResourceState::RenderTarget});
    device.submitFrame(cmd);

    std::array<uint8_t, 4> pixel{};
    if (!readCenterPixel(device, targetView, kWidth, kHeight, pixel)) {
        device.shutdown();
        return false;
    }

    const bool greenPixel = pixel[0] <= 5u && pixel[1] >= 250u && pixel[2] <= 5u && pixel[3] >= 250u;
    if (!requireTrue(greenPixel, "compute dispatch must write a green center pixel")) {
        std::cerr << "center pixel rgba=("
                  << static_cast<int>(pixel[0]) << ", "
                  << static_cast<int>(pixel[1]) << ", "
                  << static_cast<int>(pixel[2]) << ", "
                  << static_cast<int>(pixel[3]) << ")\n";
        device.shutdown();
        return false;
    }

    device.destroyBindGroup(bindGroup);
    device.destroyPipeline(pipeline);
    device.destroyShader(computeShader);
    device.destroyPipelineLayout(pipelineLayout);
    device.destroyBindGroupLayout(bindGroupLayout);
    device.destroyTextureView(targetView);
    device.destroyTexture(target);
    device.shutdown();
    return true;
}

bool testGlRhiCombinedTextureSampler() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for sampled texture draw")) {
        return false;
    }

    GlRhiDevice device;
    RhiDeviceDesc deviceDesc;
    deviceDesc.debugName = "rhi_sampled_texture_test";
    if (!requireTrue(device.init(deviceDesc), "OpenGL RHI device must initialize for sampled texture draw")) {
        return false;
    }

    constexpr uint32_t kWidth = 4u;
    constexpr uint32_t kHeight = 4u;
    constexpr std::array<uint8_t, 4> kYellowPixel = {255u, 255u, 0u, 255u};

    RhiTextureDesc sourceDesc;
    sourceDesc.debugName = "sampled-source-texture";
    sourceDesc.format = RhiTextureFormat::Rgba8Unorm;
    sourceDesc.width = 1u;
    sourceDesc.height = 1u;
    sourceDesc.usage = rhiFlag(RhiTextureUsage::Sampled);

    RhiTextureInitialData sourceInitialData;
    sourceInitialData.pixels = kYellowPixel.data();
    sourceInitialData.sizeBytes = kYellowPixel.size();
    const RhiTextureHandle sourceTexture = device.createTexture(sourceDesc, &sourceInitialData);
    if (!requireTrue(sourceTexture.isValid(), "sampled source texture must be created")) {
        device.shutdown();
        return false;
    }

    RhiTextureViewDesc sourceViewDesc;
    sourceViewDesc.texture = sourceTexture;
    const RhiTextureViewHandle sourceView = device.createTextureView(sourceViewDesc);
    if (!requireTrue(sourceView.isValid(), "sampled source texture view must be created")) {
        device.shutdown();
        return false;
    }

    RhiSamplerDesc samplerDesc;
    samplerDesc.minFilter = RhiFilter::Nearest;
    samplerDesc.magFilter = RhiFilter::Nearest;
    samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    const RhiSamplerHandle sampler = device.createSampler(samplerDesc);
    if (!requireTrue(sampler.isValid(), "sampler must be created for sampled texture draw")) {
        device.shutdown();
        return false;
    }

    RhiTextureDesc targetDesc;
    targetDesc.debugName = "sampled-draw-target";
    targetDesc.format = RhiTextureFormat::Rgba8Unorm;
    targetDesc.width = kWidth;
    targetDesc.height = kHeight;
    targetDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment) | rhiFlag(RhiTextureUsage::TransferSrc);
    const RhiTextureHandle target = device.createTexture(targetDesc, nullptr);
    if (!requireTrue(target.isValid(), "sampled draw target texture must be created")) {
        device.shutdown();
        return false;
    }

    RhiTextureViewDesc targetViewDesc;
    targetViewDesc.texture = target;
    const RhiTextureViewHandle targetView = device.createTextureView(targetViewDesc);
    if (!requireTrue(targetView.isValid(), "sampled draw target texture view must be created")) {
        device.shutdown();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "sampled-texture-layout";
    RhiBindGroupLayoutEntry textureEntry;
    textureEntry.binding = 0u;
    textureEntry.type = RhiBindingType::CombinedTextureSampler;
    textureEntry.stages = rhiFlag(RhiShaderStage::Fragment);
    bindGroupLayoutDesc.entries.push_back(textureEntry);
    const RhiBindGroupLayoutHandle bindGroupLayout =
        device.createBindGroupLayout(bindGroupLayoutDesc);
    if (!requireTrue(bindGroupLayout.isValid(), "sampled bind group layout must be created")) {
        device.shutdown();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "sampled-pipeline-layout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(bindGroupLayout);
    const RhiPipelineLayoutHandle pipelineLayout = device.createPipelineLayout(pipelineLayoutDesc);
    if (!requireTrue(pipelineLayout.isValid(), "sampled pipeline layout must be created")) {
        device.shutdown();
        return false;
    }

    constexpr char kFullscreenVertexShader[] = R"glsl(
#version 450 core
const vec2 kPositions[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2(3.0, -1.0),
    vec2(-1.0, 3.0)
);

void main() {
    gl_Position = vec4(kPositions[gl_VertexID], 0.0, 1.0);
}
)glsl";
    constexpr char kSampledFragmentShader[] = R"glsl(
#version 450 core
layout(binding = 0) uniform sampler2D uSource;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(uSource, vec2(0.5));
}
)glsl";

    RhiShaderDesc vertexShaderDesc;
    vertexShaderDesc.debugName = "sampled-fullscreen-vertex";
    vertexShaderDesc.stage = RhiShaderStage::Vertex;
    vertexShaderDesc.source = kFullscreenVertexShader;
    vertexShaderDesc.sourceSize = sizeof(kFullscreenVertexShader) - 1u;
    const RhiShaderHandle vertexShader = device.createShader(vertexShaderDesc);
    if (!requireTrue(vertexShader.isValid(), "sampled vertex shader must compile")) {
        device.shutdown();
        return false;
    }

    RhiShaderDesc fragmentShaderDesc;
    fragmentShaderDesc.debugName = "sampled-fragment";
    fragmentShaderDesc.stage = RhiShaderStage::Fragment;
    fragmentShaderDesc.source = kSampledFragmentShader;
    fragmentShaderDesc.sourceSize = sizeof(kSampledFragmentShader) - 1u;
    const RhiShaderHandle fragmentShader = device.createShader(fragmentShaderDesc);
    if (!requireTrue(fragmentShader.isValid(), "sampled fragment shader must compile")) {
        device.shutdown();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "sampled-texture-pipeline";
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba8Unorm);
    const RhiPipelineHandle pipeline = device.createGraphicsPipeline(pipelineDesc);
    if (!requireTrue(pipeline.isValid(), "sampled graphics pipeline must be created")) {
        device.shutdown();
        return false;
    }

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = bindGroupLayout;
    RhiBindGroupEntry bindGroupEntry;
    bindGroupEntry.binding = 0u;
    bindGroupEntry.resource.combinedTextureSampler.textureView = sourceView;
    bindGroupEntry.resource.combinedTextureSampler.sampler = sampler;
    bindGroupDesc.entries.push_back(bindGroupEntry);
    const RhiBindGroupHandle bindGroup = device.createBindGroup(bindGroupDesc);
    if (!requireTrue(bindGroup.isValid(), "sampled bind group must be created")) {
        device.shutdown();
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targetView;
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "sampled-texture-rendering";
    renderingInfo.renderArea = {0, 0, kWidth, kHeight};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiCommandList& cmd = device.beginFrame();
    cmd.beginRendering(renderingInfo);
    cmd.setViewport({0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight), 0.0f, 1.0f});
    cmd.setGraphicsPipeline(pipeline);
    cmd.setBindGroup(0u, bindGroup);
    cmd.draw(3u, 1u, 0u, 0u);
    std::array<uint8_t, 4> pixel{};
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    cmd.endRendering();
    device.submitFrame(cmd);

    const bool yellowPixel = pixel[0] >= 250u && pixel[1] >= 250u && pixel[2] <= 5u && pixel[3] >= 250u;
    if (!requireTrue(yellowPixel, "sampled texture draw must produce a yellow center pixel")) {
        std::cerr << "center pixel rgba=("
                  << static_cast<int>(pixel[0]) << ", "
                  << static_cast<int>(pixel[1]) << ", "
                  << static_cast<int>(pixel[2]) << ", "
                  << static_cast<int>(pixel[3]) << ")\n";
        device.shutdown();
        return false;
    }

    device.destroyBindGroup(bindGroup);
    device.destroyPipeline(pipeline);
    device.destroyShader(fragmentShader);
    device.destroyShader(vertexShader);
    device.destroyPipelineLayout(pipelineLayout);
    device.destroyBindGroupLayout(bindGroupLayout);
    device.destroyTextureView(targetView);
    device.destroyTexture(target);
    device.destroySampler(sampler);
    device.destroyTextureView(sourceView);
    device.destroyTexture(sourceTexture);
    device.shutdown();
    return true;
}

bool testGlRhiIndexedTriangle() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for indexed draw")) {
        return false;
    }

    GlRhiDevice device;
    RhiDeviceDesc deviceDesc;
    deviceDesc.debugName = "rhi_indexed_triangle_test";
    if (!requireTrue(device.init(deviceDesc), "OpenGL RHI device must initialize for indexed draw")) {
        return false;
    }

    struct Vertex {
        float position[2];
    };
    constexpr std::array<Vertex, 3> kVertices = {{
        {{-1.0f, -1.0f}},
        {{3.0f, -1.0f}},
        {{-1.0f, 3.0f}}
    }};
    constexpr std::array<uint16_t, 3> kIndices = {0u, 1u, 2u};
    constexpr uint32_t kWidth = 4u;
    constexpr uint32_t kHeight = 4u;

    RhiBufferDesc vertexBufferDesc;
    vertexBufferDesc.debugName = "indexed-triangle-vertices";
    vertexBufferDesc.size = sizeof(Vertex) * kVertices.size();
    vertexBufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex);
    vertexBufferDesc.memoryUsage = RhiMemoryUsage::CpuToGpu;
    const RhiBufferHandle vertexBuffer =
        device.createBuffer(vertexBufferDesc, kVertices.data(), vertexBufferDesc.size);
    if (!requireTrue(vertexBuffer.isValid(), "vertex buffer must be created for indexed draw")) {
        device.shutdown();
        return false;
    }

    RhiBufferDesc indexBufferDesc;
    indexBufferDesc.debugName = "indexed-triangle-indices";
    indexBufferDesc.size = sizeof(uint16_t) * kIndices.size();
    indexBufferDesc.usage = rhiFlag(RhiBufferUsage::Index);
    indexBufferDesc.memoryUsage = RhiMemoryUsage::CpuToGpu;
    const RhiBufferHandle indexBuffer =
        device.createBuffer(indexBufferDesc, kIndices.data(), indexBufferDesc.size);
    if (!requireTrue(indexBuffer.isValid(), "index buffer must be created for indexed draw")) {
        device.shutdown();
        return false;
    }

    RhiTextureDesc targetDesc;
    targetDesc.debugName = "indexed-triangle-target";
    targetDesc.format = RhiTextureFormat::Rgba8Unorm;
    targetDesc.width = kWidth;
    targetDesc.height = kHeight;
    targetDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment) | rhiFlag(RhiTextureUsage::TransferSrc);
    const RhiTextureHandle target = device.createTexture(targetDesc, nullptr);
    if (!requireTrue(target.isValid(), "indexed draw target texture must be created")) {
        device.shutdown();
        return false;
    }

    RhiTextureViewDesc targetViewDesc;
    targetViewDesc.texture = target;
    const RhiTextureViewHandle targetView = device.createTextureView(targetViewDesc);
    if (!requireTrue(targetView.isValid(), "indexed draw target view must be created")) {
        device.shutdown();
        return false;
    }

    constexpr char kVertexShader[] = R"glsl(
#version 450 core
layout(location = 0) in vec2 inPosition;

void main() {
    gl_Position = vec4(inPosition, 0.0, 1.0);
}
)glsl";
    constexpr char kFragmentShader[] = R"glsl(
#version 450 core
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(1.0, 0.0, 1.0, 1.0);
}
)glsl";

    RhiShaderDesc vertexShaderDesc;
    vertexShaderDesc.debugName = "indexed-triangle-vertex";
    vertexShaderDesc.stage = RhiShaderStage::Vertex;
    vertexShaderDesc.source = kVertexShader;
    vertexShaderDesc.sourceSize = sizeof(kVertexShader) - 1u;
    const RhiShaderHandle vertexShader = device.createShader(vertexShaderDesc);
    if (!requireTrue(vertexShader.isValid(), "indexed vertex shader must compile")) {
        device.shutdown();
        return false;
    }

    RhiShaderDesc fragmentShaderDesc;
    fragmentShaderDesc.debugName = "indexed-triangle-fragment";
    fragmentShaderDesc.stage = RhiShaderStage::Fragment;
    fragmentShaderDesc.source = kFragmentShader;
    fragmentShaderDesc.sourceSize = sizeof(kFragmentShader) - 1u;
    const RhiShaderHandle fragmentShader = device.createShader(fragmentShaderDesc);
    if (!requireTrue(fragmentShader.isValid(), "indexed fragment shader must compile")) {
        device.shutdown();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "indexed-triangle-layout";
    const RhiPipelineLayoutHandle pipelineLayout = device.createPipelineLayout(pipelineLayoutDesc);
    if (!requireTrue(pipelineLayout.isValid(), "indexed pipeline layout must be created")) {
        device.shutdown();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "indexed-triangle-pipeline";
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.layout = pipelineLayout;
    RhiVertexBinding vertexBinding;
    vertexBinding.binding = 0u;
    vertexBinding.stride = sizeof(Vertex);
    pipelineDesc.vertexInput.bindings.push_back(vertexBinding);
    RhiVertexAttribute positionAttribute;
    positionAttribute.location = 0u;
    positionAttribute.binding = 0u;
    positionAttribute.format = RhiVertexFormat::Float2;
    positionAttribute.offset = 0u;
    pipelineDesc.vertexInput.attributes.push_back(positionAttribute);
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba8Unorm);
    const RhiPipelineHandle pipeline = device.createGraphicsPipeline(pipelineDesc);
    if (!requireTrue(pipeline.isValid(), "indexed graphics pipeline must be created")) {
        device.shutdown();
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targetView;
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "indexed-triangle-rendering";
    renderingInfo.renderArea = {0, 0, kWidth, kHeight};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiCommandList& cmd = device.beginFrame();
    cmd.beginRendering(renderingInfo);
    cmd.setViewport({0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight), 0.0f, 1.0f});
    cmd.setGraphicsPipeline(pipeline);
    cmd.setVertexBuffer(0u, vertexBuffer, 0u);
    cmd.setIndexBuffer(indexBuffer, RhiIndexFormat::Uint16, 0u);
    cmd.drawIndexed(3u, 1u, 0u, 0, 0u);
    std::array<uint8_t, 4> pixel{};
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    cmd.endRendering();
    device.submitFrame(cmd);

    const bool magentaPixel = pixel[0] >= 250u && pixel[1] <= 5u && pixel[2] >= 250u && pixel[3] >= 250u;
    if (!requireTrue(magentaPixel, "indexed draw must produce a magenta center pixel")) {
        std::cerr << "center pixel rgba=("
                  << static_cast<int>(pixel[0]) << ", "
                  << static_cast<int>(pixel[1]) << ", "
                  << static_cast<int>(pixel[2]) << ", "
                  << static_cast<int>(pixel[3]) << ")\n";
        device.shutdown();
        return false;
    }

    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(pipelineLayout);
    device.destroyShader(fragmentShader);
    device.destroyShader(vertexShader);
    device.destroyTextureView(targetView);
    device.destroyTexture(target);
    device.destroyBuffer(indexBuffer);
    device.destroyBuffer(vertexBuffer);
    device.shutdown();
    return true;
}

bool testGlRhiDrawIndirect() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for indirect draw")) {
        return false;
    }

    GlRhiDevice device;
    RhiDeviceDesc deviceDesc;
    deviceDesc.debugName = "rhi_draw_indirect_test";
    if (!requireTrue(device.init(deviceDesc), "OpenGL RHI device must initialize for indirect draw")) {
        return false;
    }

    struct DrawArraysIndirectCommand {
        uint32_t count;
        uint32_t instanceCount;
        uint32_t first;
        uint32_t baseInstance;
    };
    constexpr DrawArraysIndirectCommand kDrawCommand = {3u, 1u, 0u, 0u};
    constexpr uint32_t kWidth = 4u;
    constexpr uint32_t kHeight = 4u;

    RhiBufferDesc indirectBufferDesc;
    indirectBufferDesc.debugName = "fullscreen-indirect-command";
    indirectBufferDesc.size = sizeof(DrawArraysIndirectCommand);
    indirectBufferDesc.usage = rhiFlag(RhiBufferUsage::Indirect);
    indirectBufferDesc.memoryUsage = RhiMemoryUsage::CpuToGpu;
    const RhiBufferHandle indirectBuffer =
        device.createBuffer(indirectBufferDesc, &kDrawCommand, sizeof(kDrawCommand));
    if (!requireTrue(indirectBuffer.isValid(), "indirect buffer must be created")) {
        device.shutdown();
        return false;
    }

    RhiTextureDesc targetDesc;
    targetDesc.debugName = "indirect-draw-target";
    targetDesc.format = RhiTextureFormat::Rgba8Unorm;
    targetDesc.width = kWidth;
    targetDesc.height = kHeight;
    targetDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment) | rhiFlag(RhiTextureUsage::TransferSrc);
    const RhiTextureHandle target = device.createTexture(targetDesc, nullptr);
    if (!requireTrue(target.isValid(), "indirect draw target texture must be created")) {
        device.shutdown();
        return false;
    }

    RhiTextureViewDesc targetViewDesc;
    targetViewDesc.texture = target;
    const RhiTextureViewHandle targetView = device.createTextureView(targetViewDesc);
    if (!requireTrue(targetView.isValid(), "indirect draw target view must be created")) {
        device.shutdown();
        return false;
    }

    constexpr char kVertexShader[] = R"glsl(
#version 450 core
const vec2 kPositions[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2(3.0, -1.0),
    vec2(-1.0, 3.0)
);

void main() {
    gl_Position = vec4(kPositions[gl_VertexID], 0.0, 1.0);
}
)glsl";
    constexpr char kFragmentShader[] = R"glsl(
#version 450 core
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(0.0, 1.0, 1.0, 1.0);
}
)glsl";

    RhiShaderDesc vertexShaderDesc;
    vertexShaderDesc.debugName = "indirect-fullscreen-vertex";
    vertexShaderDesc.stage = RhiShaderStage::Vertex;
    vertexShaderDesc.source = kVertexShader;
    vertexShaderDesc.sourceSize = sizeof(kVertexShader) - 1u;
    const RhiShaderHandle vertexShader = device.createShader(vertexShaderDesc);
    if (!requireTrue(vertexShader.isValid(), "indirect vertex shader must compile")) {
        device.shutdown();
        return false;
    }

    RhiShaderDesc fragmentShaderDesc;
    fragmentShaderDesc.debugName = "indirect-fragment";
    fragmentShaderDesc.stage = RhiShaderStage::Fragment;
    fragmentShaderDesc.source = kFragmentShader;
    fragmentShaderDesc.sourceSize = sizeof(kFragmentShader) - 1u;
    const RhiShaderHandle fragmentShader = device.createShader(fragmentShaderDesc);
    if (!requireTrue(fragmentShader.isValid(), "indirect fragment shader must compile")) {
        device.shutdown();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "indirect-layout";
    const RhiPipelineLayoutHandle pipelineLayout = device.createPipelineLayout(pipelineLayoutDesc);
    if (!requireTrue(pipelineLayout.isValid(), "indirect pipeline layout must be created")) {
        device.shutdown();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "indirect-pipeline";
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba8Unorm);
    const RhiPipelineHandle pipeline = device.createGraphicsPipeline(pipelineDesc);
    if (!requireTrue(pipeline.isValid(), "indirect graphics pipeline must be created")) {
        device.shutdown();
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targetView;
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "indirect-rendering";
    renderingInfo.renderArea = {0, 0, kWidth, kHeight};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiCommandList& cmd = device.beginFrame();
    cmd.beginRendering(renderingInfo);
    cmd.setViewport({0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight), 0.0f, 1.0f});
    cmd.setGraphicsPipeline(pipeline);
    cmd.drawIndirect(indirectBuffer, 0u, 1u, 0u);
    std::array<uint8_t, 4> pixel{};
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    cmd.endRendering();
    device.submitFrame(cmd);

    const bool cyanPixel = pixel[0] <= 5u && pixel[1] >= 250u && pixel[2] >= 250u && pixel[3] >= 250u;
    if (!requireTrue(cyanPixel, "indirect draw must produce a cyan center pixel")) {
        std::cerr << "center pixel rgba=("
                  << static_cast<int>(pixel[0]) << ", "
                  << static_cast<int>(pixel[1]) << ", "
                  << static_cast<int>(pixel[2]) << ", "
                  << static_cast<int>(pixel[3]) << ")\n";
        device.shutdown();
        return false;
    }

    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(pipelineLayout);
    device.destroyShader(fragmentShader);
    device.destroyShader(vertexShader);
    device.destroyTextureView(targetView);
    device.destroyTexture(target);
    device.destroyBuffer(indirectBuffer);
    device.shutdown();
    return true;
}
} // namespace

int main() {
    if (!testHandleGeneration()) {
        return 1;
    }
    if (!testDescHashStability()) {
        return 1;
    }
    if (!testGlTextureRegistry()) {
        return 1;
    }
    if (!testResourceTextureRegistration()) {
        return 1;
    }
    if (!testGlRhiDeviceHandles()) {
        return 1;
    }
    if (!testGlRhiSwapchainBackbuffer()) {
        return 1;
    }
    if (!testGlRhiFullscreenTriangle()) {
        return 1;
    }
    if (!testGlRhiBufferCopyToTexture()) {
        return 1;
    }
    if (!testGlRhiComputeStorageTexture()) {
        return 1;
    }
    if (!testGlRhiCombinedTextureSampler()) {
        return 1;
    }
    if (!testGlRhiIndexedTriangle()) {
        return 1;
    }
    if (!testGlRhiDrawIndirect()) {
        return 1;
    }
    return 0;
}
