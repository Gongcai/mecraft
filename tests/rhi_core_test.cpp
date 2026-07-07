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
    if (!testGlRhiFullscreenTriangle()) {
        return 1;
    }
    return 0;
}
