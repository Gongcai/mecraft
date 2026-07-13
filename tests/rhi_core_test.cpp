#include "renderer/rhi/RhiHandleAllocator.h"
#include "renderer/rhi/RhiGrowableBuffer.h"
#include "renderer/rhi/RhiHash.h"
#include "renderer/rhi/RhiShaderSourceLoader.h"
#include "renderer/rhi/gl/GlRhiDevice.h"
#include "renderer/debug/RenderDebugService.h"
#include "renderer/mesh/WorldRenderBuffer.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <array>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace {
GLFWwindow* g_testWindow = nullptr;

RhiDeviceDesc makeDeviceDesc() {
    RhiDeviceDesc desc;
    desc.nativeWindow = g_testWindow;
    return desc;
}

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

class ScopedErrorCapture {
public:
    ScopedErrorCapture()
        : m_previous(std::cerr.rdbuf(m_stream.rdbuf())) {}

    ~ScopedErrorCapture() {
        std::cerr.rdbuf(m_previous);
    }

    [[nodiscard]] std::string output() const {
        return m_stream.str();
    }

private:
    std::ostringstream m_stream;
    std::streambuf* m_previous = nullptr;
};

RhiCommandList& beginTestCommands(
    RhiDevice& device,
    std::unique_ptr<RhiCommandListPool>& commandPool) {
    commandPool = device.createCommandListPool({"rhi-core-test-command-pool", 1u, 64u * 1024u});
    if (commandPool == nullptr) {
        std::cerr << "test command list pool must be created\n";
        std::abort();
    }
    RhiCommandList* commandList = commandPool->acquire(RhiCommandListType::Graphics);
    if (commandList == nullptr ||
        !commandList->begin({"rhi-core-test-command-list", RhiCommandListType::Graphics})) {
        std::cerr << "test graphics command list must be acquired and begun\n";
        std::abort();
    }
    return *commandList;
}

void submitTestCommands(
    RhiDevice& device,
    std::unique_ptr<RhiCommandListPool>& commandPool,
    RhiCommandList& commandList) {
    RhiCommandList* commandLists[] = {&commandList};
    if (!commandList.end() ||
        !device.submit({"rhi-core-test-submit", commandLists, 1u})) {
        std::cerr << "test graphics command list must end and submit\n";
        std::abort();
    }
    device.waitIdle();
    if (!commandPool->reset()) {
        std::cerr << "test command list pool must reset after completion\n";
        std::abort();
    }
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
        g_testWindow = m_window;
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
            g_testWindow = nullptr;
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
    if (!requireTrue(!allocator.release(first), "release must reject stale texture handles")) {
        return false;
    }

    RhiHandleAllocator<RhiTextureHandle> externalAllocator{0x80000000u};
    const RhiTextureHandle external = externalAllocator.allocate();
    if (!requireTrue(external.index == 0x80000000u,
                     "allocator must honor its configured first handle index")) {
        return false;
    }
    if (!requireTrue(external.index != second.index,
                     "allocators with disjoint index ranges must not collide")) {
        return false;
    }
    return requireTrue(externalAllocator.slotForHandle(external) == 0u,
                       "allocator must resolve a live handle to its local slot");
}

bool testVertexRangeAllocator() {
    VertexRangeAllocator allocator;
    allocator.init(16u);

    GpuMeshRange first;
    GpuMeshRange second;
    if (!requireTrue(allocator.allocate(6u, first) && first.firstVertex == 0u,
                     "vertex range allocator must allocate the first range at zero")) {
        return false;
    }
    if (!requireTrue(allocator.allocate(4u, second) && second.firstVertex == 6u,
                     "vertex range allocator must append the second range")) {
        return false;
    }

    allocator.free(first);
    GpuMeshRange reused;
    if (!requireTrue(allocator.allocate(5u, reused) && reused.firstVertex == 0u,
                     "vertex range allocator must reuse a released range")) {
        return false;
    }
    const size_t usedBeforeStaleFree = allocator.usedVertices();
    allocator.free(first);
    if (!requireTrue(allocator.usedVertices() == usedBeforeStaleFree,
                     "vertex range allocator must reject stale generations")) {
        return false;
    }

    allocator.grow(32u);
    GpuMeshRange expanded;
    if (!requireTrue(allocator.allocate(20u, expanded) && expanded.firstVertex == 10u,
                     "vertex range allocator must merge newly grown trailing capacity")) {
        return false;
    }
    if (!requireTrue(allocator.capacityVertices() == 32u && allocator.usedVertices() == 29u,
                     "vertex range allocator must track grown capacity and usage")) {
        return false;
    }

    allocator.free(reused);
    allocator.free(second);
    allocator.free(expanded);
    if (!requireTrue(allocator.usedVertices() == 0u && allocator.fragmentationRatio() == 0.0f,
                     "vertex range allocator must fully coalesce released ranges")) {
        return false;
    }
    allocator.shutdown();
    return true;
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
    if (!requireTrue(firstHash != rhiHashTextureDesc(desc),
                     "texture desc hash must change when semantic fields change")) {
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    const uint64_t pipelineHash = rhiHashGraphicsPipelineDesc(pipelineDesc);
    pipelineDesc.raster.depthBiasEnabled = true;
    pipelineDesc.raster.depthBiasConstantFactor = 4.0f;
    pipelineDesc.raster.depthBiasSlopeFactor = 2.0f;
    return requireTrue(pipelineHash != rhiHashGraphicsPipelineDesc(pipelineDesc),
                       "graphics pipeline hash must include depth bias state");
}

bool testGlRhiDeviceHandles() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
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
    textureDesc.mipLevels = 3u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);
    const RhiTextureHandle texture = device.createTexture(textureDesc, nullptr);
    if (!requireTrue(texture.isValid(), "OpenGL RHI device must create texture handles")) {
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = texture;
    viewDesc.mipCount = kRhiRemainingMipLevels;
    viewDesc.layerCount = kRhiRemainingArrayLayers;
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

bool testGlRhiDeferredResourceRetirement() {
    GlTestContext context;
    if (!requireTrue(context.init(),
                     "OpenGL test context must initialize for deferred resource retirement")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_deferred_resource_retirement_test";
    if (!requireTrue(device.init(deviceDesc),
                     "OpenGL RHI device must initialize for deferred resource retirement")) {
        return false;
    }

    constexpr std::array<uint32_t, 8> sourceData = {
        0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f000u,
        0x13579bdfu, 0x2468ace0u, 0xdeadbeefu, 0xc001d00du
    };
    RhiBufferDesc sourceDesc;
    sourceDesc.debugName = "deferred-retirement-source";
    sourceDesc.size = sizeof(sourceData);
    sourceDesc.usage = rhiFlag(RhiBufferUsage::TransferSrc) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    sourceDesc.initialState = RhiResourceState::TransferSrc;
    const RhiBufferHandle source = device.createBuffer(
        sourceDesc, sourceData.data(), sizeof(sourceData));

    RhiBufferDesc destinationDesc;
    destinationDesc.debugName = "deferred-retirement-destination";
    destinationDesc.size = sizeof(sourceData);
    destinationDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) |
                            rhiFlag(RhiBufferUsage::MapRead);
    destinationDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    destinationDesc.initialState = RhiResourceState::TransferDst;
    const RhiBufferHandle destination = device.createBuffer(destinationDesc, nullptr, 0u);
    if (!requireTrue(source.isValid() && destination.isValid(),
                     "deferred resource retirement test buffers must be created")) {
        device.shutdown();
        return false;
    }

    RhiCommandList& commandList = beginTestCommands(device, commandPool);
    commandList.copyBuffer({source, destination, 0u, 0u, sizeof(sourceData)});
    commandList.bufferBarrier({destination,
                               RhiResourceState::TransferDst,
                               RhiResourceState::HostRead});
    RhiCommandList* commandLists[] = {&commandList};
    if (!requireTrue(commandList.end() &&
                     device.submit({"deferred-resource-retirement-submit",
                                    commandLists, 1u}),
                     "resource retirement commands must submit")) {
        device.shutdown();
        return false;
    }
    device.destroyBuffer(source);

    const RhiBufferHandle replacement = device.createBuffer(sourceDesc, nullptr, 0u);
    if (!requireTrue(replacement.isValid() && replacement.index == source.index &&
                     replacement.generation != source.generation,
                     "destroyBuffer must invalidate the logical handle after submission")) {
        device.shutdown();
        return false;
    }

    device.waitIdle();
    if (!requireTrue(commandPool->reset(),
                     "resource retirement command pool must reset after completion")) {
        device.shutdown();
        return false;
    }
    const void* mapped = device.mapBuffer(destination, 0u, sizeof(sourceData));
    if (!requireTrue(mapped != nullptr &&
                     std::memcmp(mapped, sourceData.data(), sizeof(sourceData)) == 0,
                     "destroyed source storage must remain alive through its submission fence")) {
        if (mapped != nullptr) {
            device.unmapBuffer(destination);
        }
        device.shutdown();
        return false;
    }
    device.unmapBuffer(destination);
    device.destroyBuffer(replacement);
    device.destroyBuffer(destination);
    device.waitIdle();
    device.shutdown();
    return true;
}

bool testGlRhiTextureDescriptorUsageValidation() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for descriptor validation")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_texture_descriptor_usage_test";
    if (!requireTrue(device.init(deviceDesc),
                     "OpenGL RHI device must initialize for descriptor validation")) {
        return false;
    }

    RhiTextureDesc textureDesc;
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = 4u;
    textureDesc.height = 4u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment);
    const RhiTextureHandle texture = device.createTexture(textureDesc, nullptr);
    RhiTextureViewDesc viewDesc;
    viewDesc.texture = texture;
    viewDesc.format = textureDesc.format;
    const RhiTextureViewHandle view = device.createTextureView(viewDesc);
    const RhiSamplerHandle sampler = device.createSampler({});

    RhiBindGroupLayoutDesc sampledLayoutDesc;
    sampledLayoutDesc.entries.push_back({
        0u,
        RhiBindingType::CombinedTextureSampler,
        rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    const RhiBindGroupLayoutHandle sampledLayout = device.createBindGroupLayout(sampledLayoutDesc);
    RhiBindGroupDesc sampledGroupDesc;
    sampledGroupDesc.layout = sampledLayout;
    RhiBindGroupEntry sampledEntry;
    sampledEntry.binding = 0u;
    sampledEntry.resource.combinedTextureSampler.textureView = view;
    sampledEntry.resource.combinedTextureSampler.sampler = sampler;
    sampledGroupDesc.entries.push_back(sampledEntry);
    const RhiBindGroupHandle sampledGroup = device.createBindGroup(sampledGroupDesc);
    if (!requireTrue(!sampledGroup.isValid(),
                     "sampled descriptors must reject textures without sampled usage")) {
        device.shutdown();
        return false;
    }

    RhiBindGroupLayoutDesc storageLayoutDesc;
    storageLayoutDesc.entries.push_back({
        0u,
        RhiBindingType::StorageTexture,
        rhiFlag(RhiShaderStage::Compute),
        1u
    });
    const RhiBindGroupLayoutHandle storageLayout = device.createBindGroupLayout(storageLayoutDesc);
    RhiBindGroupDesc storageGroupDesc;
    storageGroupDesc.layout = storageLayout;
    RhiBindGroupEntry storageEntry;
    storageEntry.binding = 0u;
    storageEntry.resource.textureView = view;
    storageGroupDesc.entries.push_back(storageEntry);
    const RhiBindGroupHandle storageGroup = device.createBindGroup(storageGroupDesc);
    if (!requireTrue(!storageGroup.isValid(),
                     "storage descriptors must reject textures without storage usage")) {
        device.shutdown();
        return false;
    }

    device.destroyBindGroupLayout(storageLayout);
    device.destroyBindGroupLayout(sampledLayout);
    device.destroySampler(sampler);
    device.destroyTextureView(view);
    device.destroyTexture(texture);
    device.shutdown();
    return true;
}

bool testGlRhiShaderLayoutContracts() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for shader layout contracts")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_shader_layout_contract_test";
    if (!requireTrue(device.init(deviceDesc),
                     "OpenGL RHI device must initialize for shader layout contracts")) {
        return false;
    }

    constexpr char kVertexShader[] = R"glsl(
#version 450 core
const vec2 positions[3] = vec2[3](vec2(-1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
void main() { gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0); }
)glsl";
    constexpr char kFragmentShader[] = R"glsl(
#version 450 core
layout(set = 0, binding = 0) uniform sampler2D firstTexture;
layout(set = 1, binding = 0) uniform sampler2D secondTexture;
layout(set = 1, binding = 15, std140) uniform MaterialParams { vec4 factor; } material;
layout(push_constant) uniform DrawConstants { vec4 tint; } drawConstants;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = (texture(firstTexture, vec2(0.5)) + texture(secondTexture, vec2(0.5))) *
               material.factor * drawConstants.tint;
}
)glsl";

    RhiShaderDesc vertexDesc;
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = kVertexShader;
    vertexDesc.sourceSize = sizeof(kVertexShader) - 1u;
    const RhiShaderHandle vertexShader = device.createShader(vertexDesc);
    RhiShaderDesc fragmentDesc;
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = kFragmentShader;
    fragmentDesc.sourceSize = sizeof(kFragmentShader) - 1u;
    const RhiShaderHandle fragmentShader = device.createShader(fragmentDesc);
    if (!requireTrue(vertexShader.isValid() && fragmentShader.isValid(),
                     "canonical multi-set shaders must compile")) {
        device.shutdown();
        return false;
    }

    RhiBindGroupLayoutDesc firstSetDesc;
    firstSetDesc.entries.push_back({
        0u,
        RhiBindingType::CombinedTextureSampler,
        rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    const RhiBindGroupLayoutHandle firstSet = device.createBindGroupLayout(firstSetDesc);
    RhiBindGroupLayoutDesc secondSetDesc;
    secondSetDesc.entries.push_back({
        0u,
        RhiBindingType::CombinedTextureSampler,
        rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    secondSetDesc.entries.push_back({
        15u,
        RhiBindingType::UniformBuffer,
        rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    const RhiBindGroupLayoutHandle secondSet = device.createBindGroupLayout(secondSetDesc);

    RhiPipelineLayoutDesc layoutDesc;
    layoutDesc.bindGroupLayouts = {firstSet, secondSet};
    layoutDesc.pushConstantBytes = sizeof(glm::vec4);
    layoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    const RhiPipelineLayoutHandle layout = device.createPipelineLayout(layoutDesc);
    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.layout = layout;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba8Unorm);
    const RhiPipelineHandle pipeline = device.createGraphicsPipeline(pipelineDesc);
    if (!requireTrue(pipeline.isValid(),
                     "same-type binding zero in two sets and binding fifteen with push constants must coexist")) {
        device.shutdown();
        return false;
    }

    RhiPipelineLayoutDesc mismatchLayoutDesc;
    mismatchLayoutDesc.bindGroupLayouts = {firstSet, firstSet};
    mismatchLayoutDesc.pushConstantBytes = sizeof(glm::vec4);
    mismatchLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    const RhiPipelineLayoutHandle mismatchLayout = device.createPipelineLayout(mismatchLayoutDesc);
    pipelineDesc.layout = mismatchLayout;
    const RhiPipelineHandle mismatchPipeline = device.createGraphicsPipeline(pipelineDesc);
    if (!requireTrue(!mismatchPipeline.isValid(),
                     "pipeline creation must reject descriptor reflection and layout mismatches")) {
        device.shutdown();
        return false;
    }

    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(mismatchLayout);
    device.destroyPipelineLayout(layout);
    device.destroyBindGroupLayout(secondSet);
    device.destroyBindGroupLayout(firstSet);
    device.destroyShader(fragmentShader);
    device.destroyShader(vertexShader);
    device.shutdown();
    return true;
}

bool testGlRhiUiSharedPipelines() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for shared UI pipelines")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_ui_shared_pipeline_test";
    if (!requireTrue(device.init(deviceDesc),
                     "OpenGL RHI device must initialize for shared UI pipelines")) {
        return false;
    }

    const auto solidVertexSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.vert");
    const auto solidFragmentSource = renderer::rhi::loadShaderSource("assets/shaders/ui_color_rhi.frag");
    const auto glassVertexSource = renderer::rhi::loadShaderSource("assets/shaders/ui_glass_rhi.vert");
    const auto glassFragmentSource = renderer::rhi::loadShaderSource("assets/shaders/ui_glass_rhi.frag");
    const auto imageVertexSource = renderer::rhi::loadShaderSource("assets/shaders/ui_image_rhi.vert");
    const auto imageFragmentSource = renderer::rhi::loadShaderSource("assets/shaders/ui_image_rhi.frag");
    const auto dashboardVertexSource = renderer::rhi::loadShaderSource("assets/shaders/imgui_rhi.vert");
    const auto dashboardFragmentSource = renderer::rhi::loadShaderSource("assets/shaders/imgui_rhi.frag");
    if (!requireTrue(solidVertexSource && solidFragmentSource && glassVertexSource &&
                     glassFragmentSource && imageVertexSource && imageFragmentSource &&
                     dashboardVertexSource && dashboardFragmentSource,
                     "shared UI canonical shader sources must load")) {
        device.shutdown();
        return false;
    }

    auto createShader = [&](const RhiShaderStage stage, const std::string& source) {
        RhiShaderDesc desc;
        desc.stage = stage;
        desc.source = source.c_str();
        desc.sourceSize = source.size();
        return device.createShader(desc);
    };
    const RhiShaderHandle solidVertex = createShader(RhiShaderStage::Vertex, *solidVertexSource);
    const RhiShaderHandle solidFragment = createShader(RhiShaderStage::Fragment, *solidFragmentSource);
    const RhiShaderHandle glassVertex = createShader(RhiShaderStage::Vertex, *glassVertexSource);
    const RhiShaderHandle glassFragment = createShader(RhiShaderStage::Fragment, *glassFragmentSource);
    const RhiShaderHandle imageVertex = createShader(RhiShaderStage::Vertex, *imageVertexSource);
    const RhiShaderHandle imageFragment = createShader(RhiShaderStage::Fragment, *imageFragmentSource);
    const RhiShaderHandle dashboardVertex = createShader(
        RhiShaderStage::Vertex, *dashboardVertexSource);
    const RhiShaderHandle dashboardFragment = createShader(
        RhiShaderStage::Fragment, *dashboardFragmentSource);
    if (!requireTrue(solidVertex.isValid() && solidFragment.isValid() &&
                     glassVertex.isValid() && glassFragment.isValid() &&
                     imageVertex.isValid() && imageFragment.isValid() &&
                     dashboardVertex.isValid() && dashboardFragment.isValid(),
                     "shared UI canonical shaders must compile")) {
        device.shutdown();
        return false;
    }

    RhiBindGroupLayoutDesc textureLayoutDesc;
    textureLayoutDesc.entries.push_back({
        0u,
        RhiBindingType::CombinedTextureSampler,
        rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    const RhiBindGroupLayoutHandle textureLayout = device.createBindGroupLayout(textureLayoutDesc);
    RhiPipelineLayoutDesc solidLayoutDesc;
    solidLayoutDesc.pushConstantBytes = 48u;
    solidLayoutDesc.pushConstantStages =
        rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    const RhiPipelineLayoutHandle solidLayout = device.createPipelineLayout(solidLayoutDesc);
    RhiPipelineLayoutDesc texturedLayoutDesc;
    texturedLayoutDesc.bindGroupLayouts.push_back(textureLayout);
    texturedLayoutDesc.pushConstantBytes = 64u;
    texturedLayoutDesc.pushConstantStages =
        rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    const RhiPipelineLayoutHandle texturedLayout = device.createPipelineLayout(texturedLayoutDesc);
    RhiPipelineLayoutDesc dashboardLayoutDesc;
    dashboardLayoutDesc.bindGroupLayouts.push_back(textureLayout);
    dashboardLayoutDesc.pushConstantBytes = 16u;
    dashboardLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex);
    const RhiPipelineLayoutHandle dashboardLayout =
        device.createPipelineLayout(dashboardLayoutDesc);

    auto createPipeline = [&](const RhiShaderHandle vertex,
                              const RhiShaderHandle fragment,
                              const RhiPipelineLayoutHandle layout) {
        RhiGraphicsPipelineDesc desc;
        desc.vertexShader = vertex;
        desc.fragmentShader = fragment;
        desc.layout = layout;
        desc.vertexInput.bindings.push_back({0u, sizeof(float) * 2u, RhiVertexInputRate::Vertex});
        desc.vertexInput.attributes.push_back({0u, 0u, RhiVertexFormat::Float2, 0u});
        desc.raster.cullMode = RhiCullMode::None;
        desc.raster.scissorEnabled = true;
        desc.depthStencil.depthTestEnabled = false;
        desc.depthStencil.depthWriteEnabled = false;
        desc.colorFormats.push_back(RhiTextureFormat::Rgba8Unorm);
        return device.createGraphicsPipeline(desc);
    };
    const RhiPipelineHandle solidPipeline = createPipeline(solidVertex, solidFragment, solidLayout);
    const RhiPipelineHandle glassPipeline = createPipeline(glassVertex, glassFragment, texturedLayout);
    const RhiPipelineHandle imagePipeline = createPipeline(imageVertex, imageFragment, texturedLayout);
    RhiGraphicsPipelineDesc dashboardPipelineDesc;
    dashboardPipelineDesc.vertexShader = dashboardVertex;
    dashboardPipelineDesc.fragmentShader = dashboardFragment;
    dashboardPipelineDesc.layout = dashboardLayout;
    dashboardPipelineDesc.vertexInput.bindings.push_back({
        0u, 20u, RhiVertexInputRate::Vertex
    });
    dashboardPipelineDesc.vertexInput.attributes.push_back({
        0u, 0u, RhiVertexFormat::Float2, 0u
    });
    dashboardPipelineDesc.vertexInput.attributes.push_back({
        1u, 0u, RhiVertexFormat::Float2, 8u
    });
    dashboardPipelineDesc.vertexInput.attributes.push_back({
        2u, 0u, RhiVertexFormat::Uint, 16u
    });
    dashboardPipelineDesc.raster.cullMode = RhiCullMode::None;
    dashboardPipelineDesc.raster.scissorEnabled = true;
    dashboardPipelineDesc.depthStencil.depthTestEnabled = false;
    dashboardPipelineDesc.depthStencil.depthWriteEnabled = false;
    RhiBlendAttachmentState dashboardBlend;
    dashboardBlend.blendEnabled = true;
    dashboardBlend.srcColor = RhiBlendFactor::SrcAlpha;
    dashboardBlend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    dashboardBlend.srcAlpha = RhiBlendFactor::One;
    dashboardBlend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    dashboardPipelineDesc.blend.attachments.push_back(dashboardBlend);
    dashboardPipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba8Unorm);
    const RhiPipelineHandle dashboardPipeline =
        device.createGraphicsPipeline(dashboardPipelineDesc);
    if (!requireTrue(solidPipeline.isValid() && glassPipeline.isValid() &&
                     imagePipeline.isValid() && dashboardPipeline.isValid(),
                     "shared UI and dashboard pipelines must compile from canonical shaders")) {
        device.shutdown();
        return false;
    }

    auto requireInvalidSampler = [&](const RhiSamplerDesc& desc, const char* message) {
        return requireTrue(!device.createSampler(desc).isValid(), message);
    };
    RhiSamplerDesc invalidSampler;
    invalidSampler.minFilter = static_cast<RhiFilter>(255);
    if (!requireInvalidSampler(invalidSampler, "sampler creation must reject an invalid min filter")) return false;
    invalidSampler = {};
    invalidSampler.magFilter = static_cast<RhiFilter>(255);
    if (!requireInvalidSampler(invalidSampler, "sampler creation must reject an invalid mag filter")) return false;
    invalidSampler = {};
    invalidSampler.mipmapMode = static_cast<RhiMipmapMode>(255);
    if (!requireInvalidSampler(invalidSampler, "sampler creation must reject an invalid mipmap mode")) return false;
    invalidSampler = {};
    invalidSampler.addressU = static_cast<RhiAddressMode>(255);
    if (!requireInvalidSampler(invalidSampler, "sampler creation must reject an invalid address mode")) return false;
    invalidSampler = {};
    invalidSampler.borderColor = static_cast<RhiBorderColor>(255);
    if (!requireInvalidSampler(invalidSampler, "sampler creation must reject an invalid border color")) return false;
    invalidSampler = {};
    invalidSampler.compareOp = static_cast<RhiCompareOp>(255);
    if (!requireInvalidSampler(invalidSampler, "sampler creation must reject an invalid compare operation")) return false;

    RhiGraphicsPipelineDesc invalidPipelineDesc;
    invalidPipelineDesc.vertexShader = solidVertex;
    invalidPipelineDesc.fragmentShader = solidFragment;
    invalidPipelineDesc.layout = solidLayout;
    invalidPipelineDesc.vertexInput.bindings.push_back({0u, sizeof(float) * 2u, RhiVertexInputRate::Vertex});
    invalidPipelineDesc.vertexInput.attributes.push_back({0u, 0u, RhiVertexFormat::Float2, 0u});
    invalidPipelineDesc.raster.cullMode = RhiCullMode::None;
    invalidPipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba8Unorm);
    auto requireInvalidPipeline = [&](const RhiGraphicsPipelineDesc& desc, const char* message) {
        return requireTrue(!device.createGraphicsPipeline(desc).isValid(), message);
    };
    RhiGraphicsPipelineDesc invalidEnumPipeline = invalidPipelineDesc;
    invalidEnumPipeline.topology = static_cast<RhiPrimitiveTopology>(255);
    if (!requireInvalidPipeline(invalidEnumPipeline, "pipeline creation must reject an invalid topology")) return false;
    invalidEnumPipeline = invalidPipelineDesc;
    invalidEnumPipeline.raster.cullMode = static_cast<RhiCullMode>(255);
    if (!requireInvalidPipeline(invalidEnumPipeline, "pipeline creation must reject an invalid cull mode")) return false;
    invalidEnumPipeline = invalidPipelineDesc;
    invalidEnumPipeline.raster.frontFace = static_cast<RhiFrontFace>(255);
    if (!requireInvalidPipeline(invalidEnumPipeline, "pipeline creation must reject an invalid front face")) return false;
    invalidEnumPipeline = invalidPipelineDesc;
    invalidEnumPipeline.depthStencil.depthCompare = static_cast<RhiCompareOp>(255);
    if (!requireInvalidPipeline(invalidEnumPipeline, "pipeline creation must reject an invalid depth compare operation")) return false;
    invalidEnumPipeline = invalidPipelineDesc;
    invalidEnumPipeline.vertexInput.bindings[0].inputRate = static_cast<RhiVertexInputRate>(255);
    if (!requireInvalidPipeline(invalidEnumPipeline, "pipeline creation must reject an invalid vertex input rate")) return false;
    invalidEnumPipeline = invalidPipelineDesc;
    invalidEnumPipeline.vertexInput.attributes[0].format = static_cast<RhiVertexFormat>(255);
    if (!requireInvalidPipeline(invalidEnumPipeline, "pipeline creation must reject an invalid vertex format")) return false;
    invalidEnumPipeline = invalidPipelineDesc;
    invalidEnumPipeline.blend.attachments.push_back({});
    invalidEnumPipeline.blend.attachments[0].srcColor = static_cast<RhiBlendFactor>(255);
    if (!requireInvalidPipeline(invalidEnumPipeline, "pipeline creation must reject an invalid blend factor")) return false;
    invalidEnumPipeline = invalidPipelineDesc;
    invalidEnumPipeline.blend.attachments.push_back({});
    invalidEnumPipeline.blend.attachments[0].colorOp = static_cast<RhiBlendOp>(255);
    if (!requireInvalidPipeline(invalidEnumPipeline, "pipeline creation must reject an invalid blend operation")) return false;

    device.destroyPipeline(dashboardPipeline);
    device.destroyPipeline(imagePipeline);
    device.destroyPipeline(glassPipeline);
    device.destroyPipeline(solidPipeline);
    device.destroyPipelineLayout(dashboardLayout);
    device.destroyPipelineLayout(texturedLayout);
    device.destroyPipelineLayout(solidLayout);
    device.destroyBindGroupLayout(textureLayout);
    device.destroyShader(dashboardFragment);
    device.destroyShader(dashboardVertex);
    device.destroyShader(imageFragment);
    device.destroyShader(imageVertex);
    device.destroyShader(glassFragment);
    device.destroyShader(glassVertex);
    device.destroyShader(solidFragment);
    device.destroyShader(solidVertex);
    device.shutdown();
    return true;
}

bool testGlRhiTimestampQueryPool() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for timestamp queries")) {
        return false;
    }
    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_timestamp_query_test";
    if (!requireTrue(device.init(deviceDesc), "OpenGL RHI device must initialize for timestamp queries")) {
        return false;
    }
    RhiQueryPoolDesc poolDesc;
    poolDesc.debugName = "timestamp-pool";
    poolDesc.queryCount = 2u;
    const RhiQueryPoolHandle pool = device.createQueryPool(poolDesc);
    if (!requireTrue(pool.isValid(), "RHI device must create timestamp query pools")) return false;

    RhiCommandList& commandList = beginTestCommands(device, commandPool);
    commandList.writeTimestamp(pool, 0u);
    commandList.writeTimestamp(pool, 1u);
    submitTestCommands(device, commandPool, commandList);
    glFinish();

    std::array<uint64_t, 2> timestamps{};
    if (!requireTrue(device.areQueryResultsAvailable(pool, 0u, 2u),
                     "submitted timestamp query results must become available")) return false;
    if (!requireTrue(device.getQueryResults(pool, 0u, 2u, timestamps.data()),
                     "RHI device must read timestamp query results")) return false;
    if (!requireTrue(timestamps[1] >= timestamps[0],
                     "timestamp query results must preserve command order")) return false;

    RhiCommandList& resetCommandList = beginTestCommands(device, commandPool);
    resetCommandList.resetQueryPool(pool, 0u, 2u);
    submitTestCommands(device, commandPool, resetCommandList);
    if (!requireTrue(!device.areQueryResultsAvailable(pool, 0u, 2u),
                     "reset timestamp queries must become unavailable until rewritten")) return false;

    RhiCommandList& reuseCommandList = beginTestCommands(device, commandPool);
    reuseCommandList.writeTimestamp(pool, 0u);
    reuseCommandList.writeTimestamp(pool, 1u);
    submitTestCommands(device, commandPool, reuseCommandList);
    glFinish();
    if (!requireTrue(device.areQueryResultsAvailable(pool, 0u, 2u),
                     "reset timestamp queries must support ring-slot reuse")) return false;

    device.destroyQueryPool(pool);
    if (!requireTrue(!device.areQueryResultsAvailable(pool, 0u, 1u),
                     "destroyed query pool handles must become stale")) return false;
    device.shutdown();
    return true;
}

bool testRenderDebugServiceTimestampSegments() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for debug timestamps")) {
        return false;
    }
    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "render_debug_timestamp_test";
    if (!requireTrue(device.init(deviceDesc),
                     "OpenGL RHI device must initialize for debug timestamps")) {
        return false;
    }

    RenderDebugService service;
    service.init(device);
    RhiCommandList& initialResetCommandList = beginTestCommands(device, commandPool);
    service.beginFrame(initialResetCommandList);
    submitTestCommands(device, commandPool, initialResetCommandList);

    RhiCommandList& firstCommandList = beginTestCommands(device, commandPool);
    const GpuTimerSegmentToken firstSegment =
        service.beginGpuTimer(firstCommandList, GpuTimerPass::GBuffer);
    service.endGpuTimer(firstCommandList, firstSegment);
    submitTestCommands(device, commandPool, firstCommandList);

    RhiCommandList& secondCommandList = beginTestCommands(device, commandPool);
    const GpuTimerSegmentToken secondSegment =
        service.beginGpuTimer(secondCommandList, GpuTimerPass::GBuffer);
    service.endGpuTimer(secondCommandList, secondSegment);
    submitTestCommands(device, commandPool, secondCommandList);

    RhiCommandList& discardedCommandList = beginTestCommands(device, commandPool);
    const GpuTimerSegmentToken discardedSegment =
        service.beginGpuTimer(discardedCommandList, GpuTimerPass::Cloud);
    service.cancelGpuTimer(discardedSegment);
    submitTestCommands(device, commandPool, discardedCommandList);
    glFinish();

    for (int frame = 0; frame < 4; ++frame) {
        RhiCommandList& frameResetCommandList = beginTestCommands(device, commandPool);
        service.beginFrame(frameResetCommandList);
        submitTestCommands(device, commandPool, frameResetCommandList);
    }

    const GpuFrameStats& stats = service.getGpuFrameStats();
    if (!requireTrue(stats.supported && stats.valid,
                     "debug timestamp segments must publish valid frame statistics")) return false;
    if (!requireTrue(stats.gbufferMs >= 0.0,
                     "debug timestamp segments must aggregate non-negative duration")) return false;
    if (!requireTrue(stats.cloudMs == 0.0,
                     "discarded timestamp segments must not contribute duration")) return false;

    service.shutdown();
    device.shutdown();
    return true;
}

bool testGlRhiGrowableBuffer() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for growable buffer")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_growable_buffer_test";
    if (!requireTrue(device.init(deviceDesc), "OpenGL RHI device must initialize for growable buffer")) {
        return false;
    }

    RhiGrowableBuffer buffer;
    if (!requireTrue(buffer.init(device,
                                 16u,
                                 rhiFlag(RhiBufferUsage::Indirect),
                                 "growable-indirect-buffer"),
                     "growable RHI buffer must initialize")) {
        device.shutdown();
        return false;
    }

    constexpr std::array<uint32_t, 4> kCommand = {3u, 1u, 0u, 0u};
    constexpr uint64_t kOffset = 65536u;
    RhiCommandList& commandList = beginTestCommands(device, commandPool);
    const bool writeSucceeded = buffer.write(
        commandList,
        kOffset,
        kCommand.data(),
        sizeof(kCommand));
    submitTestCommands(device, commandPool, commandList);
    if (!requireTrue(writeSucceeded && buffer.capacity() == kOffset + sizeof(kCommand),
                     "growable RHI buffer must expand and write beyond its initial capacity")) {
        buffer.shutdown();
        device.shutdown();
        return false;
    }

    buffer.shutdown();
    device.shutdown();
    return true;
}

bool testGlRhiSwapchainBackbuffer() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for swapchain draw")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
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
    const RhiFrameAcquireResult frame = device.acquireFrame();
    if (!requireTrue(frame.status == RhiFrameStatus::Success &&
                     frame.width == 32u && frame.height == 32u &&
                     frame.colorTexture.isValid() && frame.colorView.isValid() &&
                     frame.depthStencilView.isValid(),
                     "swapchain frame acquisition must expose the current frame resources")) {
        device.shutdown();
        return false;
    }
    if (!requireTrue(device.acquireFrame().status == RhiFrameStatus::Error,
                     "frame acquisition must reject a second acquire before presentation") ||
        !requireTrue(device.presentFrame({frame.frameIndex + 1u, frame.imageIndex}) ==
                         RhiFrameStatus::Error,
                     "frame presentation must reject a mismatched frame identity")) {
        device.shutdown();
        return false;
    }

    const RhiTextureViewHandle swapchainView = device.currentSwapchainColorView();
    if (!requireTrue(swapchainView.isValid(), "swapchain color view must be valid")) {
        device.shutdown();
        return false;
    }
    const RhiTextureHandle swapchainTexture = device.currentSwapchainColorTexture();
    if (!requireTrue(swapchainTexture.isValid(), "swapchain color texture must be valid")) {
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
    gl_Position = vec4(kPositions[gl_VertexIndex], 0.0, 1.0);
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

    RhiCommandList& cmd = beginTestCommands(device, commandPool);
    cmd.textureBarrier({
        swapchainTexture,
        RhiResourceState::Present,
        RhiResourceState::RenderTarget
    });
    cmd.textureBarrier({
        swapchainTexture,
        RhiResourceState::RenderTarget,
        RhiResourceState::TransferSrc
    });
    cmd.textureBarrier({
        swapchainTexture,
        RhiResourceState::TransferSrc,
        RhiResourceState::RenderTarget
    });
    cmd.beginRendering(renderingInfo);
    cmd.setViewport({0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 1.0f});
    cmd.setGraphicsPipeline(pipeline);
    cmd.draw(3u, 1u, 0u, 0u);

    cmd.endRendering();
    submitTestCommands(device, commandPool, cmd);

    std::array<uint8_t, 4> pixel{};
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    float centerDepth = 1.0f;
    glReadPixels(16, 16, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &centerDepth);

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

    RhiBufferDesc readbackDesc;
    readbackDesc.debugName = "swapchain-readback-buffer";
    readbackDesc.size = 4u;
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) |
                         rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    const RhiBufferHandle readbackBuffer = device.createBuffer(readbackDesc, nullptr, 0u);
    if (!requireTrue(readbackBuffer.isValid(),
                     "swapchain readback buffer must be created")) {
        device.shutdown();
        return false;
    }

    RhiTextureBufferCopy readbackCopy;
    readbackCopy.srcTexture = swapchainTexture;
    readbackCopy.dstBuffer = readbackBuffer;
    readbackCopy.bytesPerRow = 4u;
    readbackCopy.rowsPerImage = 1u;
    readbackCopy.srcX = 16u;
    readbackCopy.srcY = 16u;
    readbackCopy.width = 1u;
    readbackCopy.height = 1u;
    RhiCommandList& readbackCommandList = beginTestCommands(device, commandPool);
    readbackCommandList.textureBarrier({
        swapchainTexture,
        RhiResourceState::RenderTarget,
        RhiResourceState::TransferSrc
    });
    readbackCommandList.copyTextureToBuffer(readbackCopy);
    readbackCommandList.bufferBarrier({
        readbackBuffer,
        RhiResourceState::TransferDst,
        RhiResourceState::HostRead
    });
    readbackCommandList.textureBarrier({
        swapchainTexture,
        RhiResourceState::TransferSrc,
        RhiResourceState::RenderTarget
    });
    submitTestCommands(device, commandPool, readbackCommandList);
    device.waitIdle();

    const auto* mappedPixel = static_cast<const uint8_t*>(
        device.mapBuffer(readbackBuffer, 0u, 4u));
    if (!requireTrue(mappedPixel != nullptr,
                     "swapchain readback buffer must map for host access")) {
        device.shutdown();
        return false;
    }
    const bool rhiReadbackWhite = mappedPixel[0] >= 250u && mappedPixel[1] >= 250u &&
                                  mappedPixel[2] >= 250u && mappedPixel[3] >= 250u;
    device.unmapBuffer(readbackBuffer);
    if (!requireTrue(rhiReadbackWhite,
                     "RHI swapchain texture readback must preserve the rendered pixel")) {
        device.shutdown();
        return false;
    }

    RhiCommandList& presentCommandList = beginTestCommands(device, commandPool);
    presentCommandList.textureBarrier({
        swapchainTexture,
        RhiResourceState::RenderTarget,
        RhiResourceState::Present
    });
    submitTestCommands(device, commandPool, presentCommandList);
    if (!requireTrue(device.presentFrame({frame.frameIndex, frame.imageIndex}) ==
                         RhiFrameStatus::Success,
                     "the acquired frame identity must present successfully") ||
        !requireTrue(!device.currentSwapchainColorTexture().isValid() &&
                         !device.currentSwapchainColorView().isValid(),
                     "swapchain frame handles must expire after presentation")) {
        device.shutdown();
        return false;
    }

    device.destroyBuffer(readbackBuffer);
    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(pipelineLayout);
    device.destroyShader(fragmentShader);
    device.destroyShader(vertexShader);
    device.shutdown();
    return true;
}

bool testGlRhiBlitToSwapchainBackbuffer() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for swapchain blit")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_swapchain_blit_test";
    deviceDesc.width = 32;
    deviceDesc.height = 32;
    if (!requireTrue(device.init(deviceDesc), "OpenGL RHI device must initialize for swapchain blit")) {
        return false;
    }
    if (!requireTrue(device.resizeSwapchain(32u, 32u), "swapchain resize must accept blit dimensions")) {
        device.shutdown();
        return false;
    }
    const RhiFrameAcquireResult frame = device.acquireFrame();
    if (!requireTrue(frame.status == RhiFrameStatus::Success,
                     "swapchain frame must be acquired before blitting")) {
        device.shutdown();
        return false;
    }

    constexpr std::array<uint8_t, 4> kMagentaPixel = {255u, 0u, 255u, 255u};
    RhiTextureDesc sourceDesc;
    sourceDesc.debugName = "swapchain-blit-source";
    sourceDesc.format = RhiTextureFormat::Rgba8Unorm;
    sourceDesc.width = 1u;
    sourceDesc.height = 1u;
    sourceDesc.usage = rhiFlag(RhiTextureUsage::TransferSrc) |
                       rhiFlag(RhiTextureUsage::TransferDst);

    RhiTextureInitialData initialData;
    initialData.pixels = kMagentaPixel.data();
    initialData.sizeBytes = kMagentaPixel.size();
    initialData.finalState = RhiResourceState::TransferSrc;
    const RhiTextureHandle source = device.createTexture(sourceDesc, &initialData);
    if (!requireTrue(source.isValid(), "swapchain blit source texture must be created")) {
        device.shutdown();
        return false;
    }

    const RhiTextureViewHandle swapchainView = device.currentSwapchainColorView();
    if (!requireTrue(swapchainView.isValid(), "swapchain blit target view must be valid")) {
        device.shutdown();
        return false;
    }

    RhiTextureBlit blit;
    blit.src = source;
    blit.dstView = swapchainView;

    RhiCommandList& cmd = beginTestCommands(device, commandPool);
    cmd.textureBarrier({device.currentSwapchainColorTexture(),
                        RhiResourceState::Present,
                        RhiResourceState::TransferDst});
    cmd.blitTexture(blit);
    cmd.textureBarrier({device.currentSwapchainColorTexture(),
                        RhiResourceState::TransferDst,
                        RhiResourceState::Present});
    submitTestCommands(device, commandPool, cmd);

    std::array<uint8_t, 4> pixel{};
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0u);
    glReadBuffer(GL_BACK);
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());

    const bool magentaPixel = pixel[0] >= 250u && pixel[1] <= 5u && pixel[2] >= 250u && pixel[3] >= 250u;
    if (!requireTrue(magentaPixel, "swapchain blit must produce a magenta center pixel")) {
        std::cerr << "center pixel rgba=("
                  << static_cast<int>(pixel[0]) << ", "
                  << static_cast<int>(pixel[1]) << ", "
                  << static_cast<int>(pixel[2]) << ", "
                  << static_cast<int>(pixel[3]) << ")\n";
        device.shutdown();
        return false;
    }

    device.destroyTexture(source);
    device.shutdown();
    return true;
}

bool testGlRhiTexture3DInitialData() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for 3D texture upload")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_texture_3d_initial_data_test";
    if (!requireTrue(device.init(deviceDesc), "OpenGL RHI device must initialize for 3D texture upload")) {
        return false;
    }

    std::array<float, 32> source{};
    for (size_t index = 0; index < source.size(); ++index) {
        source[index] = static_cast<float>(index) * 0.25f;
    }

    RhiTextureDesc textureDesc;
    textureDesc.debugName = "texture-3d-initial-data";
    textureDesc.dimension = RhiTextureDimension::Texture3D;
    textureDesc.format = RhiTextureFormat::Rgba32Float;
    textureDesc.width = 2u;
    textureDesc.height = 2u;
    textureDesc.depthOrLayers = 2u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                        rhiFlag(RhiTextureUsage::TransferDst);

    RhiTextureInitialData initialData;
    initialData.pixels = source.data();
    initialData.sizeBytes = source.size() * sizeof(float);
    initialData.layerCount = 2u;
    initialData.finalState = RhiResourceState::ShaderRead;
    const RhiTextureHandle texture = device.createTexture(textureDesc, &initialData);
    if (!requireTrue(texture.isValid(), "3D texture with initial data must be created")) {
        device.shutdown();
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = texture;
    viewDesc.viewType = RhiTextureViewType::Texture3D;
    viewDesc.format = RhiTextureFormat::Rgba32Float;
    viewDesc.layerCount = 2u;
    const RhiTextureViewHandle view = device.createTextureView(viewDesc);
    if (!requireTrue(view.isValid(), "3D texture view must be created after initial data upload") ||
        !requireTrue(glGetError() == GL_NO_ERROR, "3D texture initial data upload must not report a GL error")) {
        if (view.isValid()) {
            device.destroyTextureView(view);
        }
        device.destroyTexture(texture);
        device.shutdown();
        return false;
    }

    device.destroyTextureView(view);
    device.destroyTexture(texture);
    device.shutdown();
    return true;
}

bool testGlRhiCubemapInitialData() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for cubemap upload")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_cubemap_initial_data_test";
    if (!requireTrue(device.init(deviceDesc), "OpenGL RHI device must initialize for cubemap upload")) {
        return false;
    }

    std::array<uint8_t, 24> pixels{};
    for (size_t face = 0; face < 6u; ++face) {
        pixels[face * 4u] = static_cast<uint8_t>(32u + face * 24u);
        pixels[face * 4u + 3u] = 255u;
    }

    RhiTextureDesc textureDesc;
    textureDesc.debugName = "cubemap-initial-data";
    textureDesc.dimension = RhiTextureDimension::Cube;
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = 1u;
    textureDesc.height = 1u;
    textureDesc.depthOrLayers = 6u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                        rhiFlag(RhiTextureUsage::TransferDst);

    RhiTextureInitialData initialData;
    initialData.pixels = pixels.data();
    initialData.sizeBytes = pixels.size();
    initialData.layerCount = 6u;
    initialData.finalState = RhiResourceState::ShaderRead;
    const RhiTextureHandle texture = device.createTexture(textureDesc, &initialData);
    if (!requireTrue(texture.isValid(), "cubemap with six initial layers must be created") ||
        !requireTrue(glGetError() == GL_NO_ERROR, "cubemap initial data upload must not report a GL error")) {
        device.shutdown();
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = texture;
    viewDesc.viewType = RhiTextureViewType::Cube;
    viewDesc.layerCount = 6u;
    const RhiTextureViewHandle view = device.createTextureView(viewDesc);
    if (!requireTrue(view.isValid(), "cubemap view must be created after initial data upload")) {
        device.destroyTexture(texture);
        device.shutdown();
        return false;
    }

    device.destroyTextureView(view);
    device.destroyTexture(texture);
    device.shutdown();
    return true;
}

bool testGlRhiGenerateMipmaps() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for mipmap generation")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_generate_mipmaps_test";
    if (!requireTrue(device.init(deviceDesc), "OpenGL RHI device must initialize for mipmap generation")) {
        return false;
    }

    std::array<uint8_t, 128> pixels{};
    for (size_t index = 0; index < pixels.size(); index += 4u) {
        pixels[index] = 255u;
        pixels[index + 3u] = 255u;
    }

    RhiTextureDesc textureDesc;
    textureDesc.debugName = "mipmap-array";
    textureDesc.dimension = RhiTextureDimension::Texture2DArray;
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = 4u;
    textureDesc.height = 4u;
    textureDesc.depthOrLayers = 2u;
    textureDesc.mipLevels = 3u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                        rhiFlag(RhiTextureUsage::TransferDst);

    RhiTextureInitialData initialData;
    initialData.pixels = pixels.data();
    initialData.sizeBytes = pixels.size();
    initialData.layerCount = 2u;
    initialData.finalState = RhiResourceState::TransferDst;
    const RhiTextureHandle texture = device.createTexture(textureDesc, &initialData);
    if (!requireTrue(texture.isValid(), "texture array for mipmap generation must be created")) {
        device.shutdown();
        return false;
    }

    RhiCommandList& commandList = beginTestCommands(device, commandPool);
    commandList.generateMipmaps(texture);
    commandList.textureBarrier({texture,
                                RhiResourceState::TransferDst,
                                RhiResourceState::ShaderRead});
    submitTestCommands(device, commandPool, commandList);
    if (!requireTrue(glGetError() == GL_NO_ERROR, "RHI mipmap generation must not report a GL error")) {
        device.destroyTexture(texture);
        device.shutdown();
        return false;
    }

    device.destroyTexture(texture);
    device.shutdown();
    return true;
}

bool testGlRhiRejectsInvalidTransferTextureStates() {
    GlTestContext context;
    if (!requireTrue(context.init(),
                     "OpenGL test context must initialize for transfer state validation")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_transfer_state_validation_test";
    if (!requireTrue(device.init(deviceDesc),
                     "OpenGL RHI device must initialize for transfer state validation")) {
        return false;
    }

    RhiBufferDesc bufferDesc;
    bufferDesc.size = 64u;
    bufferDesc.usage = rhiFlag(RhiBufferUsage::TransferSrc) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    bufferDesc.initialState = RhiResourceState::TransferSrc;
    const RhiBufferHandle buffer = device.createBuffer(bufferDesc, nullptr, 0u);

    RhiTextureDesc textureDesc;
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = 4u;
    textureDesc.height = 4u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::TransferSrc) |
                        rhiFlag(RhiTextureUsage::TransferDst);
    const RhiTextureHandle source = device.createTexture(textureDesc, nullptr);
    const RhiTextureHandle destination = device.createTexture(textureDesc, nullptr);
    textureDesc.mipLevels = 2u;
    const RhiTextureHandle mipTexture = device.createTexture(textureDesc, nullptr);
    if (!requireTrue(buffer.isValid() && source.isValid() && destination.isValid() &&
                     mipTexture.isValid(),
                     "transfer state validation resources must be created")) {
        device.shutdown();
        return false;
    }

    RhiBufferTextureCopy upload;
    upload.srcBuffer = buffer;
    upload.dstTexture = destination;
    upload.width = 1u;
    upload.height = 1u;

    RhiTextureBufferCopy readback;
    readback.srcTexture = source;
    readback.dstBuffer = buffer;
    readback.width = 1u;
    readback.height = 1u;

    RhiTextureCopy copy;
    copy.src = source;
    copy.dst = destination;
    copy.extent = {1u, 1u, 1u};

    RhiTextureBlit blit;
    blit.src = source;
    blit.dst = destination;

    const auto rejectsInvalidState = [&](const auto& recordCommands,
                                         const char* expectedDiagnostic) {
        RhiCommandList& commandList = beginTestCommands(device, commandPool);
        recordCommands(commandList);
        RhiCommandList* submitted[] = {&commandList};
        std::string diagnostics;
        bool rejected = false;
        {
            ScopedErrorCapture capture;
            rejected = commandList.end() &&
                !device.submit({"InvalidTransferState.Submit", submitted, 1u});
            diagnostics = capture.output();
        }
        const bool reset = commandPool->reset();
        return rejected && reset &&
               diagnostics.find(expectedDiagnostic) != std::string::npos;
    };
    const bool rejectedEveryInvalidState =
        rejectsInvalidState(
            [&](RhiCommandList& commands) { commands.copyBufferToTexture(upload); },
            "copyBufferToTexture requires destination subresources in TransferDst state") &&
        rejectsInvalidState(
            [&](RhiCommandList& commands) {
                commands.bufferBarrier({buffer, RhiResourceState::TransferSrc,
                                        RhiResourceState::TransferDst});
                commands.copyTextureToBuffer(readback);
            },
            "copyTextureToBuffer requires source subresources in TransferSrc state") &&
        rejectsInvalidState(
            [&](RhiCommandList& commands) { commands.copyTexture(copy); },
            "copyTexture requires TransferSrc and TransferDst subresource states") &&
        rejectsInvalidState(
            [&](RhiCommandList& commands) { commands.blitTexture(blit); },
            "blitTexture requires TransferSrc and TransferDst subresource states") &&
        rejectsInvalidState(
            [&](RhiCommandList& commands) { commands.generateMipmaps(mipTexture); },
            "generateMipmaps requires every texture subresource in TransferDst state");

    device.destroyTexture(mipTexture);
    device.destroyTexture(destination);
    device.destroyTexture(source);
    device.destroyBuffer(buffer);
    device.shutdown();
    return requireTrue(rejectedEveryInvalidState,
                       "transfer commands must reject texture subresources in invalid states");
}

bool testGlRhiBufferStateContracts() {
    GlTestContext context;
    if (!requireTrue(context.init(),
                     "OpenGL test context must initialize for buffer state validation")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_buffer_state_validation_test";
    if (!requireTrue(device.init(deviceDesc),
                     "OpenGL RHI device must initialize for buffer state validation")) {
        return false;
    }

    constexpr std::array<uint32_t, 4> kData{1u, 2u, 3u, 4u};
    RhiBufferDesc invalidDesc;
    invalidDesc.size = sizeof(kData);
    invalidDesc.usage = rhiFlag(RhiBufferUsage::Vertex);
    invalidDesc.initialState = RhiResourceState::TransferSrc;
    const RhiBufferHandle invalidState = device.createBuffer(invalidDesc, nullptr, 0u);
    invalidDesc.initialState = RhiResourceState::VertexBuffer;
    const RhiBufferHandle invalidInitialData =
        device.createBuffer(invalidDesc, kData.data(), sizeof(kData));

    RhiBufferDesc sourceDesc;
    sourceDesc.debugName = "buffer-state-source";
    sourceDesc.size = sizeof(kData);
    sourceDesc.usage = rhiFlag(RhiBufferUsage::TransferSrc) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    sourceDesc.initialState = RhiResourceState::TransferSrc;
    const RhiBufferHandle source =
        device.createBuffer(sourceDesc, kData.data(), sizeof(kData));

    RhiBufferDesc destinationDesc;
    destinationDesc.debugName = "buffer-state-destination";
    destinationDesc.size = sizeof(kData);
    destinationDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) |
                            rhiFlag(RhiBufferUsage::MapRead);
    destinationDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    destinationDesc.initialState = RhiResourceState::TransferDst;
    const RhiBufferHandle destination = device.createBuffer(destinationDesc, nullptr, 0u);
    if (!requireTrue(!invalidState.isValid() && !invalidInitialData.isValid() &&
                     source.isValid() && destination.isValid(),
                     "buffer creation must enforce initial state and upload usage contracts")) {
        device.shutdown();
        return false;
    }

    RhiCommandList& commands = beginTestCommands(device, commandPool);
    commands.bufferBarrier({source, RhiResourceState::VertexBuffer,
                            RhiResourceState::TransferDst});
    commands.copyBuffer({source, destination, 0u, 0u, sizeof(kData)});
    commands.bufferBarrier({destination, RhiResourceState::TransferDst,
                            RhiResourceState::HostRead});
    std::string diagnostics;
    bool rejected = false;
    {
        ScopedErrorCapture capture;
        RhiCommandList* submitted[] = {&commands};
        rejected = commands.end() &&
            !device.submit({"InvalidBufferBarrier.Submit", submitted, 1u});
        diagnostics = capture.output();
    }
    if (!requireTrue(rejected && commandPool->reset(),
                     "stale buffer barriers must reject the complete submission")) {
        device.shutdown();
        return false;
    }

    RhiCommandList& validCommands = beginTestCommands(device, commandPool);
    validCommands.copyBuffer({source, destination, 0u, 0u, sizeof(kData)});
    validCommands.bufferBarrier({destination, RhiResourceState::TransferDst,
                                 RhiResourceState::HostRead});
    submitTestCommands(device, commandPool, validCommands);
    device.waitIdle();
    const auto* mapped = static_cast<const uint32_t*>(
        device.mapBuffer(destination, 0u, sizeof(kData)));
    const bool copied = mapped != nullptr &&
        std::memcmp(mapped, kData.data(), sizeof(kData)) == 0;
    if (mapped != nullptr) {
        device.unmapBuffer(destination);
    }

    const bool diagnosedMismatch = diagnostics.find(
        "bufferBarrier oldState does not match the tracked buffer state") != std::string::npos;
    device.destroyBuffer(destination);
    device.destroyBuffer(source);
    device.shutdown();
    return requireTrue(diagnosedMismatch && copied,
                       "buffer state tracking must atomically reject stale barriers and preserve later valid copies");
}

bool testGlRhiFullscreenTriangle() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for fullscreen draw")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
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
    gl_Position = vec4(kPositions[gl_VertexIndex], 0.0, 1.0);
}
)glsl";
constexpr char kSolidFragmentShader[] = R"glsl(
#version 450 core
layout(location = 0) out vec4 outColor;
layout(push_constant, std430) uniform FullscreenPushConstants {
    vec4 color;
} pc;

void main() {
    outColor = pc.color;
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
    pipelineLayoutDesc.pushConstantBytes = sizeof(glm::vec4);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
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

    RhiCommandList& cmd = beginTestCommands(device, commandPool);
    cmd.textureBarrier({target, RhiResourceState::Undefined,
                        RhiResourceState::RenderTarget});
    cmd.beginRendering(renderingInfo);
    cmd.setViewport({0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f});
    cmd.setGraphicsPipeline(pipeline);
    const glm::vec4 color(1.0f, 0.0f, 0.0f, 1.0f);
    cmd.pushConstants(&color, sizeof(color), rhiFlag(RhiShaderStage::Fragment));
    cmd.draw(3, 1, 0, 0);
    cmd.endRendering();
    submitTestCommands(device, commandPool, cmd);

    std::array<uint8_t, 4> pixel{};
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());

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
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiColorAttachment colorAttachment;
    colorAttachment.view = targetView;
    colorAttachment.loadOp = RhiLoadOp::Load;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "readback-rendering";
    renderingInfo.renderArea = {0, 0, width, height};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1;

    RhiCommandList& cmd = beginTestCommands(device, commandPool);
    cmd.beginRendering(renderingInfo);
    cmd.endRendering();
    submitTestCommands(device, commandPool, cmd);
    glReadPixels(static_cast<GLint>(width / 2u),
                 static_cast<GLint>(height / 2u),
                 1,
                 1,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 outPixel.data());
    return true;
}

bool testGlRhiDepthLoadClearIgnoresPreviousWriteMask() {
    GlTestContext context;
    if (!requireTrue(context.init(),
                     "OpenGL test context must initialize for depth load clear")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_depth_load_clear_test";
    if (!requireTrue(device.init(deviceDesc),
                     "OpenGL RHI device must initialize for depth load clear")) {
        return false;
    }

    constexpr char kVertexShader[] = R"glsl(
#version 450 core
void main() {
    const vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0));
    gl_Position = vec4(positions[gl_VertexIndex], 0.5, 1.0);
}
)glsl";
    constexpr char kFragmentShader[] = R"glsl(
#version 450 core
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(1.0);
}
)glsl";

    RhiShaderDesc shaderDesc;
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = kVertexShader;
    shaderDesc.sourceSize = sizeof(kVertexShader) - 1u;
    const RhiShaderHandle vertexShader = device.createShader(shaderDesc);
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = kFragmentShader;
    shaderDesc.sourceSize = sizeof(kFragmentShader) - 1u;
    const RhiShaderHandle fragmentShader = device.createShader(shaderDesc);
    const RhiPipelineLayoutHandle pipelineLayout =
        device.createPipelineLayout(RhiPipelineLayoutDesc{});

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba8Unorm};
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    const RhiPipelineHandle pipeline = device.createGraphicsPipeline(pipelineDesc);

    RhiTextureDesc colorDesc;
    colorDesc.format = RhiTextureFormat::Rgba8Unorm;
    colorDesc.width = 4u;
    colorDesc.height = 4u;
    colorDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment);
    const RhiTextureHandle colorTexture = device.createTexture(colorDesc, nullptr);
    RhiTextureViewDesc colorViewDesc;
    colorViewDesc.texture = colorTexture;
    const RhiTextureViewHandle colorView = device.createTextureView(colorViewDesc);

    RhiTextureDesc depthDesc;
    depthDesc.format = RhiTextureFormat::Depth32Float;
    depthDesc.width = 4u;
    depthDesc.height = 4u;
    depthDesc.usage = rhiFlag(RhiTextureUsage::DepthStencilAttachment);
    const RhiTextureHandle depthTexture = device.createTexture(depthDesc, nullptr);
    RhiTextureViewDesc depthViewDesc;
    depthViewDesc.texture = depthTexture;
    const RhiTextureViewHandle depthView = device.createTextureView(depthViewDesc);

    if (!requireTrue(vertexShader.isValid() && fragmentShader.isValid() &&
                     pipelineLayout.isValid() && pipeline.isValid() &&
                     colorTexture.isValid() && colorView.isValid() &&
                     depthTexture.isValid() && depthView.isValid(),
                     "depth load clear resources must be created")) {
        device.shutdown();
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = colorView;
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = depthView;
    depthAttachment.depthLoadOp = RhiLoadOp::Clear;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    depthAttachment.clearDepth = 0.0f;
    RhiRenderingInfo renderingInfo;
    renderingInfo.renderArea = {0, 0, 4u, 4u};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    renderingInfo.depthStencilAttachment = &depthAttachment;

    RhiCommandList& maskCommands = beginTestCommands(device, commandPool);
    maskCommands.textureBarrier({colorTexture, RhiResourceState::Undefined,
                                 RhiResourceState::RenderTarget});
    maskCommands.textureBarrier({depthTexture, RhiResourceState::Undefined,
                                 RhiResourceState::DepthWrite});
    maskCommands.beginRendering(renderingInfo);
    maskCommands.setGraphicsPipeline(pipeline);
    maskCommands.endRendering();
    submitTestCommands(device, commandPool, maskCommands);

    depthAttachment.clearDepth = 0.75f;
    RhiCommandList& clearCommands = beginTestCommands(device, commandPool);
    clearCommands.beginRendering(renderingInfo);
    clearCommands.endRendering();
    submitTestCommands(device, commandPool, clearCommands);

    float depth = 0.0f;
    glReadPixels(2, 2, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    const bool passed = requireTrue(std::abs(depth - 0.75f) <= 0.0001f,
                                    "depth load clear must ignore the previous pipeline write mask");

    device.destroyTextureView(depthView);
    device.destroyTexture(depthTexture);
    device.destroyTextureView(colorView);
    device.destroyTexture(colorTexture);
    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(pipelineLayout);
    device.destroyShader(fragmentShader);
    device.destroyShader(vertexShader);
    device.shutdown();
    return passed;
}

bool testGlRhiBufferCopyToTexture() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for buffer copy")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
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
    srcBufferDesc.usage = rhiFlag(RhiBufferUsage::TransferSrc) |
                          rhiFlag(RhiBufferUsage::TransferDst);
    srcBufferDesc.memoryUsage = RhiMemoryUsage::CpuToGpu;
    srcBufferDesc.initialState = RhiResourceState::TransferSrc;
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
    dstBufferDesc.initialState = RhiResourceState::TransferDst;
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

    RhiCommandList& cmd = beginTestCommands(device, commandPool);
    RhiBufferCopy bufferCopy;
    bufferCopy.src = srcBuffer;
    bufferCopy.dst = dstBuffer;
    bufferCopy.size = sourcePixels.size();
    cmd.copyBuffer(bufferCopy);
    cmd.bufferBarrier({dstBuffer, RhiResourceState::TransferDst,
                       RhiResourceState::TransferSrc});

    RhiBufferTextureCopy textureCopy;
    textureCopy.srcBuffer = dstBuffer;
    textureCopy.dstTexture = target;
    textureCopy.width = kWidth;
    textureCopy.height = kHeight;
    cmd.textureBarrier({target, RhiResourceState::Undefined,
                        RhiResourceState::TransferDst});
    cmd.copyBufferToTexture(textureCopy);
    cmd.textureBarrier({target, RhiResourceState::TransferDst, RhiResourceState::RenderTarget});
    submitTestCommands(device, commandPool, cmd);

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

bool testGlRhiTightlyPackedR8BufferCopy() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for R8 texture copy")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_r8_buffer_copy_test";
    if (!requireTrue(device.init(deviceDesc), "OpenGL RHI device must initialize for R8 texture copy")) {
        return false;
    }

    constexpr uint32_t kWidth = 3u;
    constexpr uint32_t kHeight = 2u;
    constexpr std::array<uint8_t, kWidth * kHeight> kPixels = {
        11u, 22u, 33u,
        44u, 55u, 66u
    };

    RhiBufferDesc bufferDesc;
    bufferDesc.size = 8u;
    bufferDesc.usage = rhiFlag(RhiBufferUsage::TransferSrc) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    bufferDesc.memoryUsage = RhiMemoryUsage::CpuToGpu;
    bufferDesc.initialState = RhiResourceState::TransferSrc;
    std::array<uint8_t, 8u> paddedPixels{};
    std::copy(kPixels.begin(), kPixels.end(), paddedPixels.begin());
    const RhiBufferHandle buffer = device.createBuffer(
        bufferDesc, paddedPixels.data(), paddedPixels.size());

    RhiTextureDesc textureDesc;
    textureDesc.format = RhiTextureFormat::R8Unorm;
    textureDesc.width = kWidth;
    textureDesc.height = kHeight;
    textureDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment) |
                        rhiFlag(RhiTextureUsage::TransferDst);
    const RhiTextureHandle texture = device.createTexture(textureDesc, nullptr);
    RhiTextureViewDesc viewDesc;
    viewDesc.texture = texture;
    const RhiTextureViewHandle view = device.createTextureView(viewDesc);
    if (!requireTrue(buffer.isValid() && texture.isValid() && view.isValid(),
                     "R8 texture copy resources must be created")) {
        device.shutdown();
        return false;
    }

    RhiBufferTextureCopy copy;
    copy.srcBuffer = buffer;
    copy.dstTexture = texture;
    copy.width = kWidth;
    copy.height = kHeight;
    RhiCommandList& uploadCommandList = beginTestCommands(device, commandPool);
    uploadCommandList.textureBarrier({
        texture,
        RhiResourceState::Undefined,
        RhiResourceState::TransferDst
    });
    uploadCommandList.copyBufferToTexture(copy);
    uploadCommandList.textureBarrier({
        texture,
        RhiResourceState::TransferDst,
        RhiResourceState::RenderTarget
    });
    submitTestCommands(device, commandPool, uploadCommandList);

    RhiColorAttachment attachment;
    attachment.view = view;
    attachment.loadOp = RhiLoadOp::Load;
    attachment.storeOp = RhiStoreOp::Store;
    RhiRenderingInfo renderingInfo;
    renderingInfo.renderArea = {0, 0, kWidth, kHeight};
    renderingInfo.colorAttachments = &attachment;
    renderingInfo.colorAttachmentCount = 1u;
    std::array<uint8_t, kWidth * kHeight> readback{};
    RhiCommandList& readCommandList = beginTestCommands(device, commandPool);
    readCommandList.beginRendering(renderingInfo);
    readCommandList.endRendering();
    submitTestCommands(device, commandPool, readCommandList);
    GLint previousPackAlignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, kWidth, kHeight, GL_RED, GL_UNSIGNED_BYTE, readback.data());
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);

    if (!requireTrue(readback == kPixels,
                     "tightly packed odd-width R8 rows must preserve every texel")) {
        device.shutdown();
        return false;
    }

    device.destroyTextureView(view);
    device.destroyTexture(texture);
    device.destroyBuffer(buffer);
    device.shutdown();
    return true;
}

bool testGlRhiTextureCopyToPaddedReadbackBuffer() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for texture readback")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_texture_readback_test";
    if (!requireTrue(device.init(deviceDesc),
                     "OpenGL RHI device must initialize for texture readback")) {
        return false;
    }

    constexpr uint32_t kWidth = 3u;
    constexpr uint32_t kHeight = 2u;
    constexpr uint32_t kBytesPerRow = 16u;
    constexpr uint8_t kPaddingValue = 0xCDu;
    constexpr std::array<uint8_t, kWidth * kHeight * 4u> kPixels = {
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u,
        21u, 22u, 23u, 24u, 25u, 26u, 27u, 28u, 29u, 30u, 31u, 32u
    };

    RhiTextureDesc textureDesc;
    textureDesc.debugName = "texture-readback-source";
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = kWidth;
    textureDesc.height = kHeight;
    textureDesc.usage = rhiFlag(RhiTextureUsage::TransferSrc) |
                        rhiFlag(RhiTextureUsage::TransferDst);
    RhiTextureInitialData textureData;
    textureData.pixels = kPixels.data();
    textureData.sizeBytes = kPixels.size();
    textureData.finalState = RhiResourceState::TransferSrc;
    const RhiTextureHandle texture = device.createTexture(textureDesc, &textureData);

    constexpr uint64_t kReadbackSize = static_cast<uint64_t>(kBytesPerRow) * kHeight;
    std::array<uint8_t, kReadbackSize> initialReadback{};
    initialReadback.fill(kPaddingValue);
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "texture-readback-destination";
    bufferDesc.size = kReadbackSize;
    bufferDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) |
                       rhiFlag(RhiBufferUsage::MapRead);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    bufferDesc.initialState = RhiResourceState::TransferDst;
    const RhiBufferHandle buffer = device.createBuffer(
        bufferDesc, initialReadback.data(), initialReadback.size());
    if (!requireTrue(texture.isValid() && buffer.isValid(),
                     "texture readback resources must be created")) {
        device.shutdown();
        return false;
    }

    RhiTextureBufferCopy copy;
    copy.srcTexture = texture;
    copy.dstBuffer = buffer;
    copy.bytesPerRow = kBytesPerRow;
    copy.rowsPerImage = kHeight;
    copy.width = kWidth;
    copy.height = kHeight;
    RhiCommandList& commandList = beginTestCommands(device, commandPool);
    commandList.copyTextureToBuffer(copy);
    commandList.bufferBarrier({
        buffer,
        RhiResourceState::TransferDst,
        RhiResourceState::HostRead
    });
    submitTestCommands(device, commandPool, commandList);
    device.waitIdle();

    const auto* readback = static_cast<const uint8_t*>(
        device.mapBuffer(buffer, 0u, kReadbackSize));
    if (!requireTrue(readback != nullptr, "texture readback buffer must map for host access")) {
        device.shutdown();
        return false;
    }

    bool matches = true;
    for (uint32_t y = 0u; y < kHeight; ++y) {
        matches = matches && std::memcmp(
            readback + static_cast<uint64_t>(y) * kBytesPerRow,
            kPixels.data() + static_cast<size_t>(y) * kWidth * 4u,
            kWidth * 4u) == 0;
        for (uint32_t byte = kWidth * 4u; byte < kBytesPerRow; ++byte) {
            matches = matches &&
                      readback[static_cast<uint64_t>(y) * kBytesPerRow + byte] == kPaddingValue;
        }
    }
    device.unmapBuffer(buffer);

    if (!requireTrue(matches,
                     "texture readback must preserve pixels and explicit row padding")) {
        device.shutdown();
        return false;
    }

    device.destroyBuffer(buffer);
    device.destroyTexture(texture);
    device.shutdown();
    return true;
}

bool testGlRhiBufferCopyToTexture3DRegion() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for 3D texture region copy")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_buffer_copy_3d_region_test";
    if (!requireTrue(device.init(deviceDesc), "OpenGL RHI device must initialize for 3D texture region copy")) {
        return false;
    }

    constexpr std::array<float, 16> kRegionPixels = {
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f
    };
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "copy-3d-region-source";
    bufferDesc.size = sizeof(kRegionPixels);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::TransferSrc) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    bufferDesc.memoryUsage = RhiMemoryUsage::CpuToGpu;
    bufferDesc.initialState = RhiResourceState::TransferSrc;
    const RhiBufferHandle buffer = device.createBuffer(
        bufferDesc, kRegionPixels.data(), sizeof(kRegionPixels));

    RhiTextureDesc textureDesc;
    textureDesc.debugName = "copy-3d-region-target";
    textureDesc.dimension = RhiTextureDimension::Texture3D;
    textureDesc.format = RhiTextureFormat::Rgba32Float;
    textureDesc.width = 4u;
    textureDesc.height = 4u;
    textureDesc.depthOrLayers = 4u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::TransferDst);
    const RhiTextureHandle texture = device.createTexture(textureDesc, nullptr);
    if (!requireTrue(buffer.isValid() && texture.isValid(),
                     "3D texture region copy resources must be created")) {
        device.shutdown();
        return false;
    }

    RhiBufferTextureCopy copy;
    copy.srcBuffer = buffer;
    copy.dstTexture = texture;
    copy.dstX = 1u;
    copy.dstY = 1u;
    copy.dstZ = 2u;
    copy.width = 2u;
    copy.height = 2u;
    copy.depth = 1u;
    RhiCommandList& commandList = beginTestCommands(device, commandPool);
    commandList.textureBarrier({texture,
                                RhiResourceState::Undefined,
                                RhiResourceState::TransferDst,
                                0u,
                                1u,
                                2u,
                                1u});
    commandList.copyBufferToTexture(copy);
    submitTestCommands(device, commandPool, commandList);
    const bool noError = requireTrue(glGetError() == GL_NO_ERROR,
                                     "3D texture region copy must not report a GL error");

    device.destroyTexture(texture);
    device.destroyBuffer(buffer);
    device.shutdown();
    return noError;
}

bool testGlRhiComputeStorageTexture() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for compute dispatch")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
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
layout(set = 0, binding = 0, rgba8) uniform writeonly image2D outImage;

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

    RhiCommandList& cmd = beginTestCommands(device, commandPool);
    cmd.setComputePipeline(pipeline);
    cmd.setBindGroup(0u, bindGroup);
    cmd.dispatch(kWidth, kHeight, 1u);
    std::string diagnostics;
    bool rejected = false;
    {
        ScopedErrorCapture capture;
        RhiCommandList* submitted[] = {&cmd};
        rejected = cmd.end() &&
            !device.submit({"InvalidStorageTextureState.Submit", submitted, 1u});
        diagnostics = capture.output();
    }
    if (!requireTrue(rejected && commandPool->reset() &&
                     diagnostics.find("dispatch texture descriptor state does not match its binding type") !=
                         std::string::npos,
            "compute dispatch must reject a storage texture outside ShaderWrite state")) {
        device.shutdown();
        return false;
    }

    RhiCommandList& validCmd = beginTestCommands(device, commandPool);
    validCmd.textureBarrier({target, RhiResourceState::Undefined,
                             RhiResourceState::ShaderWrite});
    validCmd.setComputePipeline(pipeline);
    validCmd.setBindGroup(0u, bindGroup);
    validCmd.dispatch(kWidth, kHeight, 1u);
    validCmd.textureBarrier({target, RhiResourceState::ShaderWrite,
                             RhiResourceState::RenderTarget});
    submitTestCommands(device, commandPool, validCmd);

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
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
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
    sourceDesc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                       rhiFlag(RhiTextureUsage::TransferSrc) |
                       rhiFlag(RhiTextureUsage::TransferDst);

    RhiTextureInitialData sourceInitialData;
    sourceInitialData.pixels = kYellowPixel.data();
    sourceInitialData.sizeBytes = kYellowPixel.size();
    sourceInitialData.finalState = RhiResourceState::TransferSrc;
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
    gl_Position = vec4(kPositions[gl_VertexIndex], 0.0, 1.0);
}
)glsl";
    constexpr char kSampledFragmentShader[] = R"glsl(
#version 450 core
layout(set = 0, binding = 0) uniform sampler2D uSource;
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

    RhiCommandList& cmd = beginTestCommands(device, commandPool);
    cmd.textureBarrier({target, RhiResourceState::Undefined,
                        RhiResourceState::RenderTarget});
    cmd.beginRendering(renderingInfo);
    cmd.setViewport({0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight), 0.0f, 1.0f});
    cmd.setGraphicsPipeline(pipeline);
    cmd.setBindGroup(0u, bindGroup);
    cmd.draw(3u, 1u, 0u, 0u);
    cmd.endRendering();
    std::string diagnostics;
    bool rejected = false;
    {
        ScopedErrorCapture capture;
        RhiCommandList* submitted[] = {&cmd};
        rejected = cmd.end() &&
            !device.submit({"InvalidSampledTextureState.Submit", submitted, 1u});
        diagnostics = capture.output();
    }
    if (!requireTrue(rejected && commandPool->reset() &&
                     diagnostics.find("graphics draw texture descriptor state does not match its binding type") !=
                         std::string::npos,
            "graphics draw must reject a sampled texture outside ShaderRead state")) {
        device.shutdown();
        return false;
    }

    RhiCommandList& validCmd = beginTestCommands(device, commandPool);
    validCmd.textureBarrier({target, RhiResourceState::Undefined,
                             RhiResourceState::RenderTarget});
    validCmd.textureBarrier({sourceTexture, RhiResourceState::TransferSrc,
                             RhiResourceState::ShaderRead});
    validCmd.beginRendering(renderingInfo);
    validCmd.setViewport({0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight), 0.0f, 1.0f});
    validCmd.setGraphicsPipeline(pipeline);
    validCmd.setBindGroup(0u, bindGroup);
    validCmd.draw(3u, 1u, 0u, 0u);
    validCmd.endRendering();
    submitTestCommands(device, commandPool, validCmd);

    std::array<uint8_t, 4> pixel{};
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());

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
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
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
    vertexBufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                             rhiFlag(RhiBufferUsage::TransferDst);
    vertexBufferDesc.memoryUsage = RhiMemoryUsage::CpuToGpu;
    vertexBufferDesc.initialState = RhiResourceState::VertexBuffer;
    const RhiBufferHandle vertexBuffer =
        device.createBuffer(vertexBufferDesc, kVertices.data(), vertexBufferDesc.size);
    if (!requireTrue(vertexBuffer.isValid(), "vertex buffer must be created for indexed draw")) {
        device.shutdown();
        return false;
    }

    RhiBufferDesc indexBufferDesc;
    indexBufferDesc.debugName = "indexed-triangle-indices";
    indexBufferDesc.size = sizeof(uint16_t) * kIndices.size();
    indexBufferDesc.usage = rhiFlag(RhiBufferUsage::Index) |
                            rhiFlag(RhiBufferUsage::TransferDst);
    indexBufferDesc.memoryUsage = RhiMemoryUsage::CpuToGpu;
    indexBufferDesc.initialState = RhiResourceState::IndexBuffer;
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

    RhiCommandList& cmd = beginTestCommands(device, commandPool);
    cmd.textureBarrier({target, RhiResourceState::Undefined,
                        RhiResourceState::RenderTarget});
    cmd.beginRendering(renderingInfo);
    cmd.setViewport({0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight), 0.0f, 1.0f});
    cmd.setGraphicsPipeline(pipeline);
    cmd.setVertexBuffer(0u, vertexBuffer, 0u);
    cmd.setIndexBuffer(indexBuffer, RhiIndexFormat::Uint16, 0u);
    cmd.drawIndexed(3u, 1u, 0u, 0, 0u);
    cmd.endRendering();
    submitTestCommands(device, commandPool, cmd);

    std::array<uint8_t, 4> pixel{};
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());

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

    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 1.0f;
    colorAttachment.clearColor[2] = 0.0f;
    RhiCommandList& isolatedCmd = beginTestCommands(device, commandPool);
    isolatedCmd.beginRendering(renderingInfo);
    isolatedCmd.setViewport({0.0f, 0.0f, static_cast<float>(kWidth),
                             static_cast<float>(kHeight), 0.0f, 1.0f});
    isolatedCmd.setGraphicsPipeline(pipeline);
    isolatedCmd.drawIndexed(3u, 1u, 0u, 0, 0u);
    isolatedCmd.endRendering();
    RhiCommandList* isolatedSubmission[] = {&isolatedCmd};
    bool isolatedRejected = false;
    {
        ScopedErrorCapture capture;
        isolatedRejected = isolatedCmd.end() &&
            !device.submit({"IsolatedBindings.Submit", isolatedSubmission, 1u});
    }

    std::array<uint8_t, 4> isolatedPixel{};
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, isolatedPixel.data());

    const bool preservedPixel = isolatedPixel[0] >= 250u && isolatedPixel[1] <= 5u &&
        isolatedPixel[2] >= 250u && isolatedPixel[3] >= 250u;
    if (!requireTrue(isolatedRejected && commandPool->reset() && preservedPixel,
                     "missing bindings must reject the complete list without inheriting or executing prior state")) {
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
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
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
    constexpr DrawArraysIndirectCommand kDrawCommand = {3u, 1u, 0u, 1u};
    constexpr std::array<std::array<float, 4>, 2> kMetadata = {{
        {{4.0f, 0.0f, 0.0f, 0.0f}},
        {{0.0f, 0.0f, 0.0f, 0.0f}}
    }};
    constexpr std::array<float, 4> kDrawColor = {0.0f, 1.0f, 1.0f, 1.0f};
    constexpr uint64_t kCommandOffset = 65536u;
    constexpr uint64_t kIndirectBufferSize = kCommandOffset + sizeof(DrawArraysIndirectCommand);
    constexpr uint32_t kWidth = 4u;
    constexpr uint32_t kHeight = 4u;

    RhiBufferDesc indirectBufferDesc;
    indirectBufferDesc.debugName = "fullscreen-indirect-command";
    indirectBufferDesc.size = kIndirectBufferSize;
    indirectBufferDesc.usage = rhiFlag(RhiBufferUsage::Indirect) |
                               rhiFlag(RhiBufferUsage::TransferDst);
    indirectBufferDesc.memoryUsage = RhiMemoryUsage::CpuToGpu;
    indirectBufferDesc.initialState = RhiResourceState::TransferDst;
    const RhiBufferHandle indirectBuffer = device.createBuffer(indirectBufferDesc, nullptr, 0u);
    if (!requireTrue(indirectBuffer.isValid(), "indirect buffer must be created")) {
        device.shutdown();
        return false;
    }

    RhiBufferDesc metadataBufferDesc;
    metadataBufferDesc.debugName = "terrain-contract-metadata";
    metadataBufferDesc.size = sizeof(kMetadata);
    metadataBufferDesc.usage = rhiFlag(RhiBufferUsage::Storage) |
                               rhiFlag(RhiBufferUsage::TransferDst);
    metadataBufferDesc.initialState = RhiResourceState::StorageBuffer;
    const RhiBufferHandle metadataBuffer =
        device.createBuffer(metadataBufferDesc, kMetadata.data(), sizeof(kMetadata));
    if (!requireTrue(metadataBuffer.isValid(), "terrain metadata buffer must be created")) {
        device.shutdown();
        return false;
    }

    RhiBufferDesc drawParamsBufferDesc;
    drawParamsBufferDesc.debugName = "terrain-contract-draw-params";
    drawParamsBufferDesc.size = sizeof(kDrawColor);
    drawParamsBufferDesc.usage = rhiFlag(RhiBufferUsage::Uniform) |
                                 rhiFlag(RhiBufferUsage::TransferDst);
    drawParamsBufferDesc.initialState = RhiResourceState::UniformBuffer;
    const RhiBufferHandle drawParamsBuffer =
        device.createBuffer(drawParamsBufferDesc, kDrawColor.data(), sizeof(kDrawColor));
    if (!requireTrue(drawParamsBuffer.isValid(), "terrain draw parameter buffer must be created")) {
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
#extension GL_ARB_shader_draw_parameters : require
layout(location = 11) in uint aVertexIndex;
layout(set = 0, binding = 0, std430) readonly buffer TerrainMetadata {
    vec4 offsets[];
};

const vec2 kPositions[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2(3.0, -1.0),
    vec2(-1.0, 3.0)
);

void main() {
    gl_Position = vec4(kPositions[aVertexIndex] + offsets[gl_BaseInstanceARB].xy, 0.0, 1.0);
}
)glsl";
    constexpr char kFragmentShader[] = R"glsl(
#version 450 core
layout(location = 0) out vec4 outColor;
layout(set = 1, binding = 13, std140) uniform DrawParams {
    vec4 drawColor;
};

void main() {
    outColor = drawColor;
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

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "terrain-contract-bind-group-layout";
    bindGroupLayoutDesc.entries.push_back({
        0u,
        RhiBindingType::StorageBuffer,
        rhiFlag(RhiShaderStage::Vertex),
        1u
    });
    const RhiBindGroupLayoutHandle bindGroupLayout =
        device.createBindGroupLayout(bindGroupLayoutDesc);
    if (!requireTrue(bindGroupLayout.isValid(), "terrain bind group layout must be created")) {
        device.shutdown();
        return false;
    }

    RhiBindGroupLayoutDesc drawParamsLayoutDesc;
    drawParamsLayoutDesc.debugName = "terrain-contract-draw-params-layout";
    drawParamsLayoutDesc.entries.push_back({
        13u,
        RhiBindingType::UniformBuffer,
        rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    const RhiBindGroupLayoutHandle drawParamsLayout =
        device.createBindGroupLayout(drawParamsLayoutDesc);
    if (!requireTrue(drawParamsLayout.isValid(), "terrain draw parameter layout must be created")) {
        device.shutdown();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "indirect-layout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(bindGroupLayout);
    pipelineLayoutDesc.bindGroupLayouts.push_back(drawParamsLayout);
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
    pipelineDesc.vertexInput.bindings.push_back({
        0u,
        static_cast<uint32_t>(sizeof(PackedBlockVertex)),
        RhiVertexInputRate::Vertex
    });
    pipelineDesc.vertexInput.attributes.push_back({11u, 0u, RhiVertexFormat::Uint, 0u});
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba8Unorm);
    const RhiPipelineHandle pipeline = device.createGraphicsPipeline(pipelineDesc);
    if (!requireTrue(pipeline.isValid(), "indirect graphics pipeline must be created")) {
        device.shutdown();
        return false;
    }

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = bindGroupLayout;
    RhiBindGroupEntry metadataEntry;
    metadataEntry.binding = 0u;
    metadataEntry.resource.buffer.buffer = metadataBuffer;
    metadataEntry.resource.buffer.range = sizeof(kMetadata);
    bindGroupDesc.entries.push_back(metadataEntry);
    const RhiBindGroupHandle metadataBindGroup = device.createBindGroup(bindGroupDesc);
    if (!requireTrue(metadataBindGroup.isValid(), "terrain metadata bind group must be created")) {
        device.shutdown();
        return false;
    }

    RhiBindGroupDesc drawParamsBindGroupDesc;
    drawParamsBindGroupDesc.layout = drawParamsLayout;
    RhiBindGroupEntry drawParamsEntry;
    drawParamsEntry.binding = 13u;
    drawParamsEntry.resource.buffer.buffer = drawParamsBuffer;
    drawParamsEntry.resource.buffer.range = sizeof(kDrawColor);
    drawParamsBindGroupDesc.entries.push_back(drawParamsEntry);
    const RhiBindGroupHandle drawParamsBindGroup = device.createBindGroup(drawParamsBindGroupDesc);
    if (!requireTrue(drawParamsBindGroup.isValid(), "terrain draw parameter bind group must be created")) {
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

    RhiCommandList& cmd = beginTestCommands(device, commandPool);
    RhiVertexPoolAllocator vertexPool;
    if (!requireTrue(vertexPool.init(device, 2u, "terrain-contract-vertices"),
                     "terrain RHI vertex pool must initialize")) {
        device.shutdown();
        return false;
    }
    GpuMeshRange vertexRange;
    std::vector<PackedBlockVertex> terrainVertices(3u);
    terrainVertices[0].posPacked = 0u;
    terrainVertices[1].posPacked = 1u;
    terrainVertices[2].posPacked = 2u;
    if (!requireTrue(vertexPool.allocate(cmd, 3u, vertexRange) &&
                     vertexPool.capacityVertices() == 5u &&
                     vertexPool.upload(cmd, vertexRange, terrainVertices),
                     "terrain RHI vertex pool must expand and upload packed vertices")) {
        vertexPool.shutdown();
        device.shutdown();
        return false;
    }
    std::vector<uint8_t> indirectUpload(kIndirectBufferSize, 0u);
    std::memcpy(indirectUpload.data() + kCommandOffset, &kDrawCommand, sizeof(kDrawCommand));
    cmd.updateBuffer(indirectBuffer, 0u, indirectUpload.data(), indirectUpload.size());
    cmd.bufferBarrier({indirectBuffer, RhiResourceState::TransferDst,
                       RhiResourceState::IndirectArgument});
    cmd.textureBarrier({target, RhiResourceState::Undefined,
                        RhiResourceState::RenderTarget});
    cmd.beginRendering(renderingInfo);
    cmd.setViewport({0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight), 0.0f, 1.0f});
    cmd.setGraphicsPipeline(pipeline);
    cmd.setBindGroup(0u, metadataBindGroup);
    cmd.setBindGroup(1u, drawParamsBindGroup);
    cmd.setVertexBuffer(0u, vertexPool.buffer(), 0u);
    cmd.drawIndirect(indirectBuffer, kCommandOffset, 1u, 0u);
    cmd.endRendering();
    submitTestCommands(device, commandPool, cmd);

    std::array<uint8_t, 4> pixel{};
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());

    const bool cyanPixel = pixel[0] <= 5u && pixel[1] >= 250u && pixel[2] >= 250u && pixel[3] >= 250u;
    if (!requireTrue(cyanPixel, "indirect draw must produce a cyan center pixel")) {
        std::cerr << "center pixel rgba=("
                  << static_cast<int>(pixel[0]) << ", "
                  << static_cast<int>(pixel[1]) << ", "
                  << static_cast<int>(pixel[2]) << ", "
                  << static_cast<int>(pixel[3]) << ")\n";
        vertexPool.shutdown();
        device.shutdown();
        return false;
    }

    vertexPool.shutdown();
    device.destroyBindGroup(drawParamsBindGroup);
    device.destroyBindGroup(metadataBindGroup);
    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(pipelineLayout);
    device.destroyBindGroupLayout(drawParamsLayout);
    device.destroyBindGroupLayout(bindGroupLayout);
    device.destroyShader(fragmentShader);
    device.destroyShader(vertexShader);
    device.destroyTextureView(targetView);
    device.destroyTexture(target);
    device.destroyBuffer(drawParamsBuffer);
    device.destroyBuffer(metadataBuffer);
    device.destroyBuffer(indirectBuffer);
    device.shutdown();
    return true;
}

bool testGlRhiTerrainGBufferPipeline() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for terrain pipeline")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_terrain_gbuffer_pipeline_test";
    if (!requireTrue(device.init(deviceDesc), "OpenGL RHI device must initialize for terrain pipeline")) {
        return false;
    }

    renderer::rhi::RhiShaderSourceOptions sourceOptions;
    sourceOptions.preprocessorDefinitions.push_back("RHI_TERRAIN_MDI");
    sourceOptions.preprocessorDefinitions.push_back("RHI_TERRAIN_NORMAL_MAPS");
    sourceOptions.preprocessorDefinitions.push_back("RHI_TERRAIN_SPECULAR_MAPS");
    const auto vertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/chunk_gbuffer.vert",
        sourceOptions);
    const auto fragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/chunk_gbuffer.frag",
        sourceOptions);
    if (!requireTrue(vertexSource.has_value() && fragmentSource.has_value(),
                     "terrain GBuffer RHI shader sources must load")) {
        device.shutdown();
        return false;
    }

    RhiShaderDesc vertexShaderDesc;
    vertexShaderDesc.debugName = "TerrainGBuffer.Vertex";
    vertexShaderDesc.stage = RhiShaderStage::Vertex;
    vertexShaderDesc.source = vertexSource->c_str();
    vertexShaderDesc.sourceSize = vertexSource->size();
    const RhiShaderHandle vertexShader = device.createShader(vertexShaderDesc);

    RhiShaderDesc fragmentShaderDesc;
    fragmentShaderDesc.debugName = "TerrainGBuffer.Fragment";
    fragmentShaderDesc.stage = RhiShaderStage::Fragment;
    fragmentShaderDesc.source = fragmentSource->c_str();
    fragmentShaderDesc.sourceSize = fragmentSource->size();
    const RhiShaderHandle fragmentShader = device.createShader(fragmentShaderDesc);
    if (!requireTrue(vertexShader.isValid() && fragmentShader.isValid(),
                     "terrain GBuffer RHI shaders must compile")) {
        device.shutdown();
        return false;
    }

    RhiBindGroupLayoutDesc metadataLayoutDesc;
    metadataLayoutDesc.debugName = "Terrain.MetadataLayout";
    metadataLayoutDesc.entries.push_back({
        0u,
        RhiBindingType::StorageBuffer,
        rhiFlag(RhiShaderStage::Vertex),
        1u
    });
    const RhiBindGroupLayoutHandle metadataLayout = device.createBindGroupLayout(metadataLayoutDesc);

    RhiBindGroupLayoutDesc materialLayoutDesc;
    materialLayoutDesc.debugName = "Terrain.GBufferMaterialLayout";
    constexpr std::array<uint32_t, 7> kTextureBindings = {0u, 3u, 4u, 9u, 10u, 11u, 12u};
    for (const uint32_t binding : kTextureBindings) {
        materialLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    materialLayoutDesc.entries.push_back({
        13u,
        RhiBindingType::UniformBuffer,
        rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    const RhiBindGroupLayoutHandle materialLayout = device.createBindGroupLayout(materialLayoutDesc);
    if (!requireTrue(metadataLayout.isValid() && materialLayout.isValid(),
                     "terrain GBuffer bind group layouts must be created")) {
        device.shutdown();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "Terrain.GBufferPipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(metadataLayout);
    pipelineLayoutDesc.bindGroupLayouts.push_back(materialLayout);
    const RhiPipelineLayoutHandle pipelineLayout = device.createPipelineLayout(pipelineLayoutDesc);
    if (!requireTrue(pipelineLayout.isValid(), "terrain GBuffer pipeline layout must be created")) {
        device.shutdown();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "Terrain.GBufferPipeline";
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertexInput.bindings.push_back({0u, sizeof(uint32_t) * 4u, RhiVertexInputRate::Vertex});
    for (uint32_t attribute = 0u; attribute < 4u; ++attribute) {
        pipelineDesc.vertexInput.attributes.push_back({
            11u + attribute,
            0u,
            RhiVertexFormat::Uint,
            attribute * static_cast<uint32_t>(sizeof(uint32_t))
        });
    }
    pipelineDesc.raster.cullMode = RhiCullMode::Back;
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::Less;
    pipelineDesc.colorFormats = {
        RhiTextureFormat::Rgba8Unorm,
        RhiTextureFormat::Rgba16Float,
        RhiTextureFormat::Rg8Unorm,
        RhiTextureFormat::Rgba8Unorm,
        RhiTextureFormat::Rgba8Unorm
    };
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    const RhiPipelineHandle pipeline = device.createGraphicsPipeline(pipelineDesc);
    if (!requireTrue(pipeline.isValid(), "terrain GBuffer RHI pipeline must be created")) {
        device.shutdown();
        return false;
    }

    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(pipelineLayout);
    device.destroyBindGroupLayout(materialLayout);
    device.destroyBindGroupLayout(metadataLayout);
    device.destroyShader(fragmentShader);
    device.destroyShader(vertexShader);
    device.shutdown();
    return true;
}

bool testGlRhiTerrainTransparentPipeline() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for transparent terrain pipeline")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_terrain_transparent_pipeline_test";
    if (!requireTrue(device.init(deviceDesc),
                     "OpenGL RHI device must initialize for transparent terrain pipeline")) {
        return false;
    }

    renderer::rhi::RhiShaderSourceOptions sourceOptions;
    sourceOptions.preprocessorDefinitions.push_back("RHI_TERRAIN_LIT_MDI");
    const auto vertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/chunk_lit.vert",
        sourceOptions);
    const auto fragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/transparent_composite.frag",
        sourceOptions);
    if (!requireTrue(vertexSource.has_value() && fragmentSource.has_value(),
                     "transparent terrain RHI shader sources must load")) {
        device.shutdown();
        return false;
    }

    RhiShaderDesc vertexShaderDesc;
    vertexShaderDesc.debugName = "TerrainTransparent.Vertex";
    vertexShaderDesc.stage = RhiShaderStage::Vertex;
    vertexShaderDesc.source = vertexSource->c_str();
    vertexShaderDesc.sourceSize = vertexSource->size();
    const RhiShaderHandle vertexShader = device.createShader(vertexShaderDesc);

    RhiShaderDesc fragmentShaderDesc;
    fragmentShaderDesc.debugName = "TerrainTransparent.Fragment";
    fragmentShaderDesc.stage = RhiShaderStage::Fragment;
    fragmentShaderDesc.source = fragmentSource->c_str();
    fragmentShaderDesc.sourceSize = fragmentSource->size();
    const RhiShaderHandle fragmentShader = device.createShader(fragmentShaderDesc);
    if (!requireTrue(vertexShader.isValid() && fragmentShader.isValid(),
                     "transparent terrain RHI shaders must compile")) {
        device.shutdown();
        return false;
    }

    RhiBindGroupLayoutDesc metadataLayoutDesc;
    metadataLayoutDesc.entries.push_back({
        0u,
        RhiBindingType::StorageBuffer,
        rhiFlag(RhiShaderStage::Vertex),
        1u
    });
    const RhiBindGroupLayoutHandle metadataLayout = device.createBindGroupLayout(metadataLayoutDesc);

    RhiBindGroupLayoutDesc materialLayoutDesc;
    constexpr std::array<uint32_t, 10> kTextureBindings = {
        0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 14u
    };
    for (const uint32_t binding : kTextureBindings) {
        materialLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    materialLayoutDesc.entries.push_back({
        15u,
        RhiBindingType::UniformBuffer,
        rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    const RhiBindGroupLayoutHandle materialLayout = device.createBindGroupLayout(materialLayoutDesc);
    if (!requireTrue(metadataLayout.isValid() && materialLayout.isValid(),
                     "transparent terrain bind group layouts must be created")) {
        device.shutdown();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.bindGroupLayouts.push_back(metadataLayout);
    pipelineLayoutDesc.bindGroupLayouts.push_back(materialLayout);
    const RhiPipelineLayoutHandle pipelineLayout = device.createPipelineLayout(pipelineLayoutDesc);
    if (!requireTrue(pipelineLayout.isValid(),
                     "transparent terrain pipeline layout must be created")) {
        device.shutdown();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertexInput.bindings.push_back({0u, sizeof(uint32_t) * 4u, RhiVertexInputRate::Vertex});
    for (uint32_t attribute = 0u; attribute < 4u; ++attribute) {
        pipelineDesc.vertexInput.attributes.push_back({
            11u + attribute,
            0u,
            RhiVertexFormat::Uint,
            attribute * static_cast<uint32_t>(sizeof(uint32_t))
        });
    }
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::LessOrEqual;
    RhiBlendAttachmentState blend;
    blend.blendEnabled = true;
    blend.srcColor = RhiBlendFactor::SrcAlpha;
    blend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    blend.srcAlpha = RhiBlendFactor::SrcAlpha;
    blend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    pipelineDesc.blend.attachments.push_back(blend);
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba16Float};
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    const RhiPipelineHandle pipeline = device.createGraphicsPipeline(pipelineDesc);
    if (!requireTrue(pipeline.isValid(), "transparent terrain RHI pipeline must be created")) {
        device.shutdown();
        return false;
    }

    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(pipelineLayout);
    device.destroyBindGroupLayout(materialLayout);
    device.destroyBindGroupLayout(metadataLayout);
    device.destroyShader(fragmentShader);
    device.destroyShader(vertexShader);
    device.shutdown();
    return true;
}

bool testGlRhiTerrainWaterPipeline() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for water terrain pipeline")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_terrain_water_pipeline_test";
    if (!requireTrue(device.init(deviceDesc),
                     "OpenGL RHI device must initialize for water terrain pipeline")) {
        return false;
    }

    renderer::rhi::RhiShaderSourceOptions sourceOptions;
    sourceOptions.preprocessorDefinitions.push_back("RHI_TERRAIN_WATER_MDI");
    const auto vertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/chunk_lit.vert",
        sourceOptions);
    const auto fragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/water_composite.frag",
        sourceOptions);
    if (!requireTrue(vertexSource.has_value() && fragmentSource.has_value(),
                     "water terrain RHI shader sources must load")) {
        device.shutdown();
        return false;
    }

    RhiShaderDesc vertexShaderDesc;
    vertexShaderDesc.debugName = "TerrainWater.Vertex";
    vertexShaderDesc.stage = RhiShaderStage::Vertex;
    vertexShaderDesc.source = vertexSource->c_str();
    vertexShaderDesc.sourceSize = vertexSource->size();
    const RhiShaderHandle vertexShader = device.createShader(vertexShaderDesc);

    RhiShaderDesc fragmentShaderDesc;
    fragmentShaderDesc.debugName = "TerrainWater.Fragment";
    fragmentShaderDesc.stage = RhiShaderStage::Fragment;
    fragmentShaderDesc.source = fragmentSource->c_str();
    fragmentShaderDesc.sourceSize = fragmentSource->size();
    const RhiShaderHandle fragmentShader = device.createShader(fragmentShaderDesc);
    if (!requireTrue(vertexShader.isValid() && fragmentShader.isValid(),
                     "water terrain RHI shaders must compile")) {
        device.shutdown();
        return false;
    }

    RhiBindGroupLayoutDesc metadataLayoutDesc;
    metadataLayoutDesc.entries.push_back({
        0u,
        RhiBindingType::StorageBuffer,
        rhiFlag(RhiShaderStage::Vertex),
        1u
    });
    const RhiBindGroupLayoutHandle metadataLayout = device.createBindGroupLayout(metadataLayoutDesc);

    RhiBindGroupLayoutDesc materialLayoutDesc;
    constexpr std::array<uint32_t, 9> kTextureBindings = {
        0u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u
    };
    for (const uint32_t binding : kTextureBindings) {
        materialLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    materialLayoutDesc.entries.push_back({
        13u,
        RhiBindingType::UniformBuffer,
        rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    const RhiBindGroupLayoutHandle materialLayout = device.createBindGroupLayout(materialLayoutDesc);
    if (!requireTrue(metadataLayout.isValid() && materialLayout.isValid(),
                     "water terrain bind group layouts must be created")) {
        device.shutdown();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.bindGroupLayouts.push_back(metadataLayout);
    pipelineLayoutDesc.bindGroupLayouts.push_back(materialLayout);
    const RhiPipelineLayoutHandle pipelineLayout = device.createPipelineLayout(pipelineLayoutDesc);
    if (!requireTrue(pipelineLayout.isValid(), "water terrain pipeline layout must be created")) {
        device.shutdown();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertexInput.bindings.push_back({0u, sizeof(uint32_t) * 4u, RhiVertexInputRate::Vertex});
    for (uint32_t attribute = 0u; attribute < 4u; ++attribute) {
        pipelineDesc.vertexInput.attributes.push_back({
            11u + attribute,
            0u,
            RhiVertexFormat::Uint,
            attribute * static_cast<uint32_t>(sizeof(uint32_t))
        });
    }
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::Less;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba16Float};
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    const RhiPipelineHandle pipeline = device.createGraphicsPipeline(pipelineDesc);
    if (!requireTrue(pipeline.isValid(), "water terrain RHI pipeline must be created")) {
        device.shutdown();
        return false;
    }

    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(pipelineLayout);
    device.destroyBindGroupLayout(materialLayout);
    device.destroyBindGroupLayout(metadataLayout);
    device.destroyShader(fragmentShader);
    device.destroyShader(vertexShader);
    device.shutdown();
    return true;
}

bool testGlRhiTerrainForwardPipeline() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for forward terrain pipeline")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_terrain_forward_pipeline_test";
    if (!requireTrue(device.init(deviceDesc),
                     "OpenGL RHI device must initialize for forward terrain pipeline")) {
        return false;
    }

    renderer::rhi::RhiShaderSourceOptions sourceOptions;
    sourceOptions.preprocessorDefinitions.push_back("RHI_TERRAIN_FORWARD_MDI");
    const auto vertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/chunk_lit.vert",
        sourceOptions);
    const auto fragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/forward_basic_terrain.frag",
        sourceOptions);
    if (!requireTrue(vertexSource.has_value() && fragmentSource.has_value(),
                     "forward terrain RHI shader sources must load")) {
        device.shutdown();
        return false;
    }

    RhiShaderDesc vertexShaderDesc;
    vertexShaderDesc.stage = RhiShaderStage::Vertex;
    vertexShaderDesc.source = vertexSource->c_str();
    vertexShaderDesc.sourceSize = vertexSource->size();
    const RhiShaderHandle vertexShader = device.createShader(vertexShaderDesc);

    RhiShaderDesc fragmentShaderDesc;
    fragmentShaderDesc.stage = RhiShaderStage::Fragment;
    fragmentShaderDesc.source = fragmentSource->c_str();
    fragmentShaderDesc.sourceSize = fragmentSource->size();
    const RhiShaderHandle fragmentShader = device.createShader(fragmentShaderDesc);
    if (!requireTrue(vertexShader.isValid() && fragmentShader.isValid(),
                     "forward terrain RHI shaders must compile")) {
        device.shutdown();
        return false;
    }

    RhiBindGroupLayoutDesc metadataLayoutDesc;
    metadataLayoutDesc.entries.push_back({
        0u,
        RhiBindingType::StorageBuffer,
        rhiFlag(RhiShaderStage::Vertex),
        1u
    });
    const RhiBindGroupLayoutHandle metadataLayout = device.createBindGroupLayout(metadataLayoutDesc);

    RhiBindGroupLayoutDesc materialLayoutDesc;
    for (uint32_t binding = 0u; binding < 5u; ++binding) {
        materialLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    materialLayoutDesc.entries.push_back({
        5u,
        RhiBindingType::UniformBuffer,
        rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    const RhiBindGroupLayoutHandle materialLayout = device.createBindGroupLayout(materialLayoutDesc);

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.bindGroupLayouts.push_back(metadataLayout);
    pipelineLayoutDesc.bindGroupLayouts.push_back(materialLayout);
    const RhiPipelineLayoutHandle pipelineLayout = device.createPipelineLayout(pipelineLayoutDesc);
    if (!requireTrue(metadataLayout.isValid() && materialLayout.isValid() && pipelineLayout.isValid(),
                     "forward terrain pipeline layouts must be created")) {
        device.shutdown();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertexInput.bindings.push_back({0u, sizeof(uint32_t) * 4u, RhiVertexInputRate::Vertex});
    for (uint32_t attribute = 0u; attribute < 4u; ++attribute) {
        pipelineDesc.vertexInput.attributes.push_back({
            11u + attribute,
            0u,
            RhiVertexFormat::Uint,
            attribute * static_cast<uint32_t>(sizeof(uint32_t))
        });
    }
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::Less;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba8Unorm};
    pipelineDesc.depthFormat = RhiTextureFormat::Depth24;
    const RhiPipelineHandle pipeline = device.createGraphicsPipeline(pipelineDesc);
    if (!requireTrue(pipeline.isValid(), "forward terrain RHI pipeline must be created")) {
        device.shutdown();
        return false;
    }

    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(pipelineLayout);
    device.destroyBindGroupLayout(materialLayout);
    device.destroyBindGroupLayout(metadataLayout);
    device.destroyShader(fragmentShader);
    device.destroyShader(vertexShader);
    device.shutdown();
    return true;
}

bool testGlRhiTextureSubresourceStates() {
    GlTestContext context;
    if (!requireTrue(context.init(), "OpenGL test context must initialize for texture subresource states")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_texture_subresource_state_test";
    if (!requireTrue(device.init(deviceDesc),
                     "OpenGL RHI device must initialize for texture subresource states")) {
        return false;
    }

    RhiTextureDesc textureDesc;
    textureDesc.debugName = "subresource-state-texture";
    textureDesc.dimension = RhiTextureDimension::Texture2DArray;
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = 4u;
    textureDesc.height = 4u;
    textureDesc.depthOrLayers = 2u;
    textureDesc.mipLevels = 2u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment) |
                        rhiFlag(RhiTextureUsage::Sampled);
    const RhiTextureHandle texture = device.createTexture(textureDesc, nullptr);
    if (!requireTrue(texture.isValid(), "subresource state texture must be created")) {
        device.shutdown();
        return false;
    }

    const auto createView = [&device, texture](const uint32_t mip,
                                               const uint32_t layer) {
        RhiTextureViewDesc viewDesc;
        viewDesc.texture = texture;
        viewDesc.viewType = RhiTextureViewType::Texture2D;
        viewDesc.baseMip = mip;
        viewDesc.mipCount = 1u;
        viewDesc.baseLayer = layer;
        viewDesc.layerCount = 1u;
        return device.createTextureView(viewDesc);
    };
    const RhiTextureViewHandle mip0Layer0View = createView(0u, 0u);
    const RhiTextureViewHandle mip0Layer1View = createView(0u, 1u);
    const RhiTextureViewHandle mip1Layer0View = createView(1u, 0u);
    if (!requireTrue(mip0Layer0View.isValid() && mip0Layer1View.isValid() &&
                     mip1Layer0View.isValid(),
                     "independent subresource views must be created")) {
        device.shutdown();
        return false;
    }

    RhiCommandList& invalidCommands = beginTestCommands(device, commandPool);
    invalidCommands.textureBarrier({texture, RhiResourceState::RenderTarget,
                                    RhiResourceState::ShaderRead, 0u, 1u, 1u, 1u});
    RhiCommandList* invalidSubmission[] = {&invalidCommands};
    bool rejected = false;
    {
        ScopedErrorCapture capture;
        rejected = invalidCommands.end() &&
            !device.submit({"InvalidSubresourceState.Submit", invalidSubmission, 1u});
    }
    if (!requireTrue(rejected && commandPool->reset(),
                     "oldState mismatch must reject the complete subresource submission")) {
        device.shutdown();
        return false;
    }

    RhiCommandList& commandList = beginTestCommands(device, commandPool);
    commandList.textureBarrier({texture, RhiResourceState::Undefined,
                                RhiResourceState::RenderTarget, 0u, 1u, 0u, 1u});
    commandList.textureBarrier({texture, RhiResourceState::Undefined,
                                RhiResourceState::RenderTarget, 0u, 1u, 1u, 1u});
    commandList.textureBarrier({texture, RhiResourceState::Undefined,
                                RhiResourceState::RenderTarget, 1u, 1u, 0u, 1u});

    const auto clearSubresource = [&commandList](const RhiTextureViewHandle view,
                                                 const uint32_t extent,
                                                 const float red) {
        RhiColorAttachment attachment;
        attachment.view = view;
        attachment.loadOp = RhiLoadOp::Clear;
        attachment.storeOp = RhiStoreOp::Store;
        attachment.clearColor[0] = red;
        attachment.clearColor[3] = 1.0f;
        RhiRenderingInfo renderingInfo;
        renderingInfo.renderArea = {0, 0, extent, extent};
        renderingInfo.colorAttachments = &attachment;
        renderingInfo.colorAttachmentCount = 1u;
        commandList.beginRendering(renderingInfo);
        commandList.endRendering();
    };
    clearSubresource(mip0Layer0View, 4u, 0.25f);
    clearSubresource(mip0Layer1View, 4u, 0.5f);
    clearSubresource(mip1Layer0View, 2u, 0.75f);
    submitTestCommands(device, commandPool, commandList);

    const bool noError = requireTrue(
        glGetError() == GL_NO_ERROR,
        "oldState rejection must preserve independent mip and layer states");
    device.destroyTextureView(mip1Layer0View);
    device.destroyTextureView(mip0Layer1View);
    device.destroyTextureView(mip0Layer0View);
    device.destroyTexture(texture);
    device.shutdown();
    return noError;
}

bool testGlRhiDeferredCommandPayloadOwnership() {
    GlTestContext context;
    if (!requireTrue(context.init(),
                     "OpenGL test context must initialize for deferred command payload ownership")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_deferred_command_payload_test";
    if (!requireTrue(device.init(deviceDesc),
                     "OpenGL RHI device must initialize for deferred command payload ownership")) {
        return false;
    }

    constexpr std::array<uint32_t, 4> kInitialBufferData{
        0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
    constexpr std::array<uint32_t, 4> kRecordedBufferData{
        0xaaaaaaaau, 0xbbbbbbbbu, 0xccccccccu, 0xddddddddu};
    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "deferred-update-buffer";
    bufferDesc.size = sizeof(kInitialBufferData);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) |
                       rhiFlag(RhiBufferUsage::MapRead);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    bufferDesc.initialState = RhiResourceState::HostRead;
    const RhiBufferHandle buffer = device.createBuffer(
        bufferDesc, kInitialBufferData.data(), sizeof(kInitialBufferData));

    RhiTextureDesc textureDesc;
    textureDesc.debugName = "deferred-render-target";
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = 1u;
    textureDesc.height = 1u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment) |
                        rhiFlag(RhiTextureUsage::TransferSrc);
    const RhiTextureHandle texture = device.createTexture(textureDesc, nullptr);
    RhiTextureViewDesc viewDesc;
    viewDesc.texture = texture;
    const RhiTextureViewHandle view = device.createTextureView(viewDesc);
    if (!requireTrue(buffer.isValid() && texture.isValid() && view.isValid(),
                     "deferred command payload resources must be created")) {
        device.shutdown();
        return false;
    }

    std::array<uint32_t, 4> updatePayload = kRecordedBufferData;
    RhiColorAttachment attachment;
    attachment.view = view;
    attachment.loadOp = RhiLoadOp::Clear;
    attachment.storeOp = RhiStoreOp::Store;
    attachment.clearColor[0] = 1.0f;
    attachment.clearColor[3] = 1.0f;
    RhiRenderingInfo renderingInfo;
    renderingInfo.renderArea = {0, 0, 1u, 1u};
    renderingInfo.colorAttachments = &attachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiCommandList& commandList = beginTestCommands(device, commandPool);
    if (!requireTrue(commandList.state() == RhiCommandListState::Recording,
                     "acquired command list must begin recording")) {
        device.shutdown();
        return false;
    }
    commandList.bufferBarrier({buffer, RhiResourceState::HostRead,
                               RhiResourceState::TransferDst});
    commandList.updateBuffer(buffer, 0u, updatePayload.data(), sizeof(updatePayload));
    commandList.bufferBarrier({buffer, RhiResourceState::TransferDst,
                               RhiResourceState::HostRead});
    commandList.textureBarrier({texture, RhiResourceState::Undefined,
                                RhiResourceState::RenderTarget});
    commandList.beginRendering(renderingInfo);
    if (!requireTrue(!commandList.end(),
                     "command list end must reject an active rendering scope")) {
        device.shutdown();
        return false;
    }
    commandList.endRendering();

    updatePayload.fill(0xeeeeeeeeu);
    attachment.clearColor[0] = 0.0f;
    attachment.clearColor[1] = 1.0f;

    const auto* beforeSubmit = static_cast<const uint32_t*>(
        device.mapBuffer(buffer, 0u, sizeof(kInitialBufferData)));
    const bool unchangedBeforeSubmit = beforeSubmit != nullptr &&
        std::memcmp(beforeSubmit, kInitialBufferData.data(), sizeof(kInitialBufferData)) == 0;
    if (beforeSubmit != nullptr) {
        device.unmapBuffer(buffer);
    }
    if (!requireTrue(unchangedBeforeSubmit,
                     "recording commands must not modify the OpenGL buffer before submission")) {
        device.shutdown();
        return false;
    }

    submitTestCommands(device, commandPool, commandList);
    if (!requireTrue(commandList.state() == RhiCommandListState::Initial,
                     "submitted command lists must become resettable")) {
        device.shutdown();
        return false;
    }
    device.waitIdle();
    const auto* afterSubmit = static_cast<const uint32_t*>(
        device.mapBuffer(buffer, 0u, sizeof(kRecordedBufferData)));
    const bool recordedUpdatePreserved = afterSubmit != nullptr &&
        std::memcmp(afterSubmit, kRecordedBufferData.data(), sizeof(kRecordedBufferData)) == 0;
    if (afterSubmit != nullptr) {
        device.unmapBuffer(buffer);
    }

    std::array<uint8_t, 4> pixel{};
    RhiBufferDesc readbackDesc;
    readbackDesc.debugName = "deferred-render-readback";
    readbackDesc.size = pixel.size();
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) |
                         rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    const RhiBufferHandle readback = device.createBuffer(readbackDesc, nullptr, 0u);
    RhiCommandList& readbackCommands = beginTestCommands(device, commandPool);
    readbackCommands.textureBarrier({texture, RhiResourceState::RenderTarget,
                                     RhiResourceState::TransferSrc});
    RhiTextureBufferCopy copy;
    copy.srcTexture = texture;
    copy.dstBuffer = readback;
    copy.bytesPerRow = 4u;
    copy.rowsPerImage = 1u;
    copy.width = 1u;
    copy.height = 1u;
    readbackCommands.copyTextureToBuffer(copy);
    readbackCommands.bufferBarrier({readback, RhiResourceState::TransferDst,
                                    RhiResourceState::HostRead});
    submitTestCommands(device, commandPool, readbackCommands);
    device.waitIdle();
    const auto* mappedPixel = static_cast<const uint8_t*>(
        device.mapBuffer(readback, 0u, pixel.size()));
    if (mappedPixel != nullptr) {
        std::memcpy(pixel.data(), mappedPixel, pixel.size());
        device.unmapBuffer(readback);
    }

    const bool recordedAttachmentPreserved = mappedPixel != nullptr &&
        pixel[0] >= 250u && pixel[1] <= 5u && pixel[2] <= 5u && pixel[3] >= 250u;
    const bool passed = requireTrue(
        recordedUpdatePreserved,
        "updateBuffer replay must own a deep copy of its recorded source bytes") &&
        requireTrue(recordedAttachmentPreserved,
                    "beginRendering replay must own a deep copy of its attachment payload");

    device.destroyBuffer(readback);
    device.destroyTextureView(view);
    device.destroyTexture(texture);
    device.destroyBuffer(buffer);
    device.shutdown();
    return passed;
}

bool testGlRhiRejectsDestroyedRecordedResourcesAtomically() {
    GlTestContext context;
    if (!requireTrue(context.init(),
                     "OpenGL test context must initialize for stale command resources")) {
        return false;
    }

    GlRhiDevice device;
    std::unique_ptr<RhiCommandListPool> commandPool;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_stale_recorded_resource_test";
    if (!requireTrue(device.init(deviceDesc),
                     "OpenGL RHI device must initialize for stale command resources")) {
        return false;
    }

    constexpr std::array<uint32_t, 4> kInitialSentinel{
        0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
    constexpr std::array<uint32_t, 4> kRecordedSentinel{
        0xaaaaaaaau, 0xbbbbbbbbu, 0xccccccccu, 0xddddddddu};
    constexpr std::array<uint8_t, 4> kTexturePixel{255u, 0u, 0u, 255u};

    RhiBufferDesc sentinelDesc;
    sentinelDesc.debugName = "stale-command-sentinel";
    sentinelDesc.size = sizeof(kInitialSentinel);
    sentinelDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) |
                         rhiFlag(RhiBufferUsage::MapRead);
    sentinelDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    sentinelDesc.initialState = RhiResourceState::HostRead;
    const RhiBufferHandle sentinel = device.createBuffer(
        sentinelDesc, kInitialSentinel.data(), sizeof(kInitialSentinel));

    RhiBufferDesc stagingDesc;
    stagingDesc.debugName = "destroyed-texture-upload-staging";
    stagingDesc.size = kTexturePixel.size();
    stagingDesc.usage = rhiFlag(RhiBufferUsage::TransferSrc) |
                        rhiFlag(RhiBufferUsage::TransferDst);
    stagingDesc.initialState = RhiResourceState::TransferSrc;
    const RhiBufferHandle staging = device.createBuffer(
        stagingDesc, kTexturePixel.data(), kTexturePixel.size());

    RhiTextureDesc textureDesc;
    textureDesc.debugName = "stale-command-upload-target";
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = 1u;
    textureDesc.height = 1u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::TransferDst);
    const RhiTextureHandle texture = device.createTexture(textureDesc, nullptr);
    if (!requireTrue(sentinel.isValid() && staging.isValid() && texture.isValid(),
                     "stale command resource test objects must be created")) {
        device.shutdown();
        return false;
    }

    RhiBufferTextureCopy upload;
    upload.srcBuffer = staging;
    upload.dstTexture = texture;
    upload.width = 1u;
    upload.height = 1u;
    RhiCommandList& commandList = beginTestCommands(device, commandPool);
    commandList.bufferBarrier({sentinel, RhiResourceState::HostRead,
                               RhiResourceState::TransferDst});
    commandList.updateBuffer(sentinel, 0u,
                             kRecordedSentinel.data(), sizeof(kRecordedSentinel));
    commandList.textureBarrier({texture, RhiResourceState::Undefined,
                                RhiResourceState::TransferDst});
    commandList.copyBufferToTexture(upload);
    device.destroyBuffer(staging);
    RhiCommandList* commandLists[] = {&commandList};
    const bool rejectedSubmit = commandList.end() &&
        !device.submit({"stale-recorded-resource-submit", commandLists, 1u});

    const auto* mapped = static_cast<const uint32_t*>(
        device.mapBuffer(sentinel, 0u, sizeof(kInitialSentinel)));
    const bool submitWasAtomic = mapped != nullptr &&
        std::memcmp(mapped, kInitialSentinel.data(), sizeof(kInitialSentinel)) == 0;
    if (mapped != nullptr) {
        device.unmapBuffer(sentinel);
    }
    const bool resetSucceeded = commandPool->reset();
    const bool passed = requireTrue(
        rejectedSubmit,
        "submission must reject a command list that references a destroyed resource") &&
        requireTrue(
        submitWasAtomic,
        "submission must execute no commands when a recorded resource was destroyed") &&
        requireTrue(resetSucceeded,
                    "rejected command lists must allow their pool to reset") &&
        requireTrue(commandList.state() == RhiCommandListState::Initial,
                    "rejected command lists must become resettable");

    device.destroyTexture(texture);
    device.destroyBuffer(sentinel);
    device.waitIdle();
    device.shutdown();
    return passed;
}

bool testGlRhiSemanticDryRunRejectsMultiListSubmissionAtomically() {
    GlTestContext context;
    if (!requireTrue(context.init(),
                     "OpenGL test context must initialize for semantic dry-run validation")) {
        return false;
    }

    GlRhiDevice device;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_semantic_dry_run_test";
    if (!requireTrue(device.init(deviceDesc),
                     "OpenGL RHI device must initialize for semantic dry-run validation")) {
        return false;
    }

    constexpr std::array<uint32_t, 4> kInitialData{
        0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
    constexpr std::array<uint32_t, 4> kUpdatedData{
        0xaaaaaaaau, 0xbbbbbbbbu, 0xccccccccu, 0xddddddddu};

    RhiBufferDesc sourceDesc;
    sourceDesc.debugName = "semantic-dry-run-source";
    sourceDesc.size = sizeof(kInitialData);
    sourceDesc.usage = rhiFlag(RhiBufferUsage::TransferSrc) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    sourceDesc.initialState = RhiResourceState::TransferDst;
    const RhiBufferHandle source = device.createBuffer(
        sourceDesc, kInitialData.data(), sizeof(kInitialData));

    RhiBufferDesc destinationDesc;
    destinationDesc.debugName = "semantic-dry-run-destination";
    destinationDesc.size = sizeof(kInitialData);
    destinationDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) |
                            rhiFlag(RhiBufferUsage::MapRead);
    destinationDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    destinationDesc.initialState = RhiResourceState::HostRead;
    const RhiBufferHandle destination = device.createBuffer(
        destinationDesc, kInitialData.data(), sizeof(kInitialData));

    RhiBufferDesc contractDesc = destinationDesc;
    contractDesc.debugName = "semantic-dry-run-contract";
    const RhiBufferHandle contract = device.createBuffer(
        contractDesc, kInitialData.data(), sizeof(kInitialData));
    if (!requireTrue(source.isValid() && destination.isValid() && contract.isValid(),
                     "semantic dry-run buffers must be created")) {
        device.shutdown();
        return false;
    }

    std::unique_ptr<RhiCommandListPool> submissionPool =
        device.createCommandListPool({"SemanticDryRun.SubmissionPool", 2u, 4096u});
    RhiCommandList* first = submissionPool != nullptr
        ? submissionPool->acquire(RhiCommandListType::Graphics)
        : nullptr;
    RhiCommandList* second = submissionPool != nullptr
        ? submissionPool->acquire(RhiCommandListType::Graphics)
        : nullptr;
    if (!requireTrue(first != nullptr && second != nullptr &&
                     first->begin({"SemanticDryRun.First", RhiCommandListType::Graphics}) &&
                     second->begin({"SemanticDryRun.Second", RhiCommandListType::Graphics}),
                     "semantic dry-run command lists must begin recording")) {
        device.shutdown();
        return false;
    }

    first->updateBuffer(source, 0u, kUpdatedData.data(), sizeof(kUpdatedData));
    first->bufferBarrier({source, RhiResourceState::TransferDst,
                          RhiResourceState::TransferSrc});
    first->bufferBarrier({destination, RhiResourceState::HostRead,
                          RhiResourceState::TransferDst});
    first->copyBuffer({source, destination, 0u, 0u, sizeof(kUpdatedData)});
    first->bufferBarrier({destination, RhiResourceState::TransferDst,
                          RhiResourceState::HostRead});
    first->bufferBarrier({source, RhiResourceState::TransferSrc,
                          RhiResourceState::TransferDst});
    second->bufferBarrier({contract, RhiResourceState::TransferDst,
                           RhiResourceState::HostRead});
    if (!requireTrue(first->end() && second->end(),
                     "semantic dry-run command lists must become executable")) {
        device.shutdown();
        return false;
    }

    RhiCommandList* submitted[] = {first, second};
    bool rejected = false;
    std::string rejectionOutput;
    {
        ScopedErrorCapture capture;
        rejected = !device.submit({"SemanticDryRun.Rejected", submitted, 2u});
        rejectionOutput = capture.output();
    }
    const auto* afterRejectedSubmit = static_cast<const uint32_t*>(
        device.mapBuffer(destination, 0u, sizeof(kInitialData)));
    const bool destinationUnchanged = afterRejectedSubmit != nullptr &&
        std::memcmp(afterRejectedSubmit, kInitialData.data(), sizeof(kInitialData)) == 0;
    if (afterRejectedSubmit != nullptr) {
        device.unmapBuffer(destination);
    }
    if (!requireTrue(rejected &&
                     rejectionOutput.find("semantic validation") != std::string::npos,
                     "submit must reject a later semantic error during dry-run validation") ||
        !requireTrue(first->state() == RhiCommandListState::Executable &&
                     second->state() == RhiCommandListState::Executable,
                     "dry-run rejection must preserve every command list as executable") ||
        !requireTrue(destinationUnchanged,
                     "dry-run rejection must execute neither updateBuffer nor copyBuffer")) {
        device.shutdown();
        return false;
    }

    std::unique_ptr<RhiCommandListPool> correctionPool;
    RhiCommandList& correction = beginTestCommands(device, correctionPool);
    correction.bufferBarrier({contract, RhiResourceState::HostRead,
                              RhiResourceState::TransferDst});
    submitTestCommands(device, correctionPool, correction);

    if (!requireTrue(device.submit({"SemanticDryRun.Corrected", submitted, 2u}),
                     "the same executable command lists must submit after correcting resource state")) {
        device.shutdown();
        return false;
    }
    device.waitIdle();
    const auto* afterCorrectedSubmit = static_cast<const uint32_t*>(
        device.mapBuffer(destination, 0u, sizeof(kUpdatedData)));
    const bool updatedAndCopied = afterCorrectedSubmit != nullptr &&
        std::memcmp(afterCorrectedSubmit, kUpdatedData.data(), sizeof(kUpdatedData)) == 0;
    if (afterCorrectedSubmit != nullptr) {
        device.unmapBuffer(destination);
    }

    const bool passed = requireTrue(
        updatedAndCopied,
        "corrected resubmission must execute the original updateBuffer and copyBuffer") &&
        requireTrue(first->state() == RhiCommandListState::Initial &&
                    second->state() == RhiCommandListState::Initial,
                    "completed corrected submission must reclaim both command lists") &&
        requireTrue(submissionPool->reset(),
                    "semantic dry-run submission pool must reset after completion");

    device.destroyBuffer(contract);
    device.destroyBuffer(destination);
    device.destroyBuffer(source);
    device.waitIdle();
    device.shutdown();
    return passed;
}

bool testGlRhiCommandListTypeContracts() {
    GlTestContext context;
    if (!requireTrue(context.init(),
                     "OpenGL test context must initialize for command-list type contracts")) {
        return false;
    }

    GlRhiDevice device;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_command_list_type_contract_test";
    if (!requireTrue(device.init(deviceDesc),
                     "OpenGL RHI device must initialize for command-list type contracts")) {
        return false;
    }

    std::unique_ptr<RhiCommandListPool> pool =
        device.createCommandListPool({"CommandTypeContract.Pool", 1u, 4096u});
    if (!requireTrue(pool != nullptr,
                     "command-list type contract pool must be created") ||
        !requireTrue(pool->acquire(static_cast<RhiCommandListType>(255u)) == nullptr,
                     "command-list pool must reject unknown command-list types")) {
        device.shutdown();
        return false;
    }

    const auto beginTyped = [&pool](const RhiCommandListType type) -> RhiCommandList* {
        RhiCommandList* commandList = pool->acquire(type);
        if (commandList == nullptr ||
            !commandList->begin({"CommandTypeContract.Commands", type})) {
            return nullptr;
        }
        return commandList;
    };
    const auto resetExecutable = [&pool](RhiCommandList& commandList) {
        return commandList.end() && pool->reset();
    };
    const auto rejects = [&device, &beginTyped](
                             const RhiCommandListType type,
                             const auto& recordIllegalCommand) {
        RhiCommandList* commandList = beginTyped(type);
        if (commandList == nullptr) {
            return false;
        }
        recordIllegalCommand(*commandList);
        const bool endRejected = !commandList->end();
        RhiCommandList* submitted[] = {commandList};
        const bool submitRejected = !device.submit(
            {"CommandTypeContract.InvalidSubmit", submitted, 1u});
        return endRejected && submitRejected &&
               commandList->state() == RhiCommandListState::Initial;
    };

    RhiCommandList* graphics = beginTyped(RhiCommandListType::Graphics);
    if (!requireTrue(graphics != nullptr,
                     "graphics command list must begin")) {
        device.shutdown();
        return false;
    }
    graphics->setComputePipeline({});
    graphics->dispatch(1u, 1u, 1u);
    graphics->copyBuffer({});
    graphics->beginDebugLabel("graphics", glm::vec4(1.0f));
    graphics->endDebugLabel();
    if (!requireTrue(resetExecutable(*graphics),
                     "graphics command lists must accept compute, transfer, and debug commands")) {
        device.shutdown();
        return false;
    }

    RhiCommandList* compute = beginTyped(RhiCommandListType::Compute);
    if (!requireTrue(compute != nullptr,
                     "compute command list must begin")) {
        device.shutdown();
        return false;
    }
    compute->setComputePipeline({});
    compute->dispatch(1u, 1u, 1u);
    compute->copyTexture({});
    compute->writeTimestamp({}, 0u);
    compute->insertDebugMarker("compute", glm::vec4(1.0f));
    if (!requireTrue(resetExecutable(*compute),
                     "compute command lists must accept compute, transfer, query, and debug commands")) {
        device.shutdown();
        return false;
    }

    RhiCommandList* transfer = beginTyped(RhiCommandListType::Transfer);
    if (!requireTrue(transfer != nullptr,
                     "transfer command list must begin")) {
        device.shutdown();
        return false;
    }
    transfer->copyBufferToTexture({});
    transfer->textureBarrier({});
    transfer->resetQueryPool({}, 0u, 1u);
    transfer->insertDebugMarker("transfer", glm::vec4(1.0f));
    if (!requireTrue(resetExecutable(*transfer),
                     "transfer command lists must accept transfer, barrier, query, and debug commands")) {
        device.shutdown();
        return false;
    }

    RhiCommandList* openLabel = beginTyped(RhiCommandListType::Transfer);
    if (!requireTrue(openLabel != nullptr,
                     "debug-label scope command list must begin")) {
        device.shutdown();
        return false;
    }
    openLabel->beginDebugLabel("open-label", glm::vec4(1.0f));
    if (!requireTrue(!openLabel->end(),
                     "command list end must reject an open debug-label scope")) {
        device.shutdown();
        return false;
    }
    openLabel->endDebugLabel();
    if (!requireTrue(resetExecutable(*openLabel),
                     "a closed debug-label scope must remain executable")) {
        device.shutdown();
        return false;
    }

    RhiRenderingInfo renderingInfo;
    const bool negativeContracts =
        requireTrue(rejects(RhiCommandListType::Compute,
                            [&renderingInfo](RhiCommandList& commands) {
                                commands.beginRendering(renderingInfo);
                            }),
                    "compute command lists must reject rendering commands") &&
        requireTrue(rejects(RhiCommandListType::Compute,
                            [](RhiCommandList& commands) {
                                commands.setGraphicsPipeline({});
                            }),
                    "compute command lists must reject graphics pipeline commands") &&
        requireTrue(rejects(RhiCommandListType::Compute,
                            [](RhiCommandList& commands) {
                                commands.draw(3u, 1u, 0u, 0u);
                            }),
                    "compute command lists must reject draw commands") &&
        requireTrue(rejects(RhiCommandListType::Transfer,
                            [](RhiCommandList& commands) {
                                commands.setComputePipeline({});
                            }),
                    "transfer command lists must reject compute pipeline commands") &&
        requireTrue(rejects(RhiCommandListType::Transfer,
                            [](RhiCommandList& commands) {
                                commands.dispatch(1u, 1u, 1u);
                            }),
                    "transfer command lists must reject dispatch commands") &&
        requireTrue(rejects(RhiCommandListType::Transfer,
                            [](RhiCommandList& commands) {
                                commands.setBindGroup(0u, {});
                            }),
                    "transfer command lists must reject descriptor binding commands") &&
        requireTrue(rejects(RhiCommandListType::Graphics,
                            [](RhiCommandList& commands) {
                                commands.drawIndexed(3u, 1u, 0u, 0, 0u);
                            }),
                    "draw commands must require an active rendering scope") &&
        requireTrue(rejects(RhiCommandListType::Graphics,
                            [&renderingInfo](RhiCommandList& commands) {
                                commands.beginRendering(renderingInfo);
                                commands.copyBuffer({});
                            }),
                    "transfer commands must be rejected inside a rendering scope") &&
        requireTrue(rejects(RhiCommandListType::Graphics,
                            [&renderingInfo](RhiCommandList& commands) {
                                commands.beginRendering(renderingInfo);
                                commands.resetQueryPool({}, 0u, 1u);
                            }),
                    "query reset must be rejected inside a rendering scope") &&
        requireTrue(rejects(RhiCommandListType::Graphics,
                            [&renderingInfo](RhiCommandList& commands) {
                                commands.beginRendering(renderingInfo);
                                commands.beginRendering(renderingInfo);
                            }),
                    "rendering scopes must reject nesting") &&
        requireTrue(rejects(RhiCommandListType::Transfer,
                            [](RhiCommandList& commands) {
                                commands.endDebugLabel();
                            }),
                    "debug labels must reject unmatched ends") &&
        requireTrue(rejects(RhiCommandListType::Transfer,
                            [](RhiCommandList& commands) {
                                commands.insertDebugMarker(nullptr, glm::vec4(1.0f));
                            }),
                    "debug markers must reject empty names");

    device.shutdown();
    return negativeContracts;
}

bool testGlRhiIndependentCommandListPools() {
    GlTestContext context;
    if (!requireTrue(context.init(),
                     "OpenGL test context must initialize for independent command pools")) {
        return false;
    }

    GlRhiDevice device;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_independent_command_pool_test";
    if (!requireTrue(device.init(deviceDesc),
                     "OpenGL RHI device must initialize for independent command pools")) {
        return false;
    }

    RhiCommandListPool* workerPools[2] = {nullptr, nullptr};
    RhiCommandList* workerCommandLists[2] = {nullptr, nullptr};
    bool workerRecorded[2] = {false, false};
    std::mutex workerMutex;
    std::condition_variable workerCondition;
    uint32_t readyWorkerCount = 0u;
    bool releaseWorkers = false;
    std::thread workers[2];
    for (uint32_t index = 0u; index < 2u; ++index) {
        workers[index] = std::thread([&device, &workerPools, &workerCommandLists,
                                      &workerRecorded, &workerMutex, &workerCondition,
                                      &readyWorkerCount, &releaseWorkers, index]() {
            RhiCommandListPoolDesc poolDesc;
            poolDesc.debugName = index == 0u ? "WorkerPool0" : "WorkerPool1";
            poolDesc.initialCommandListCapacity = 2u;
            std::unique_ptr<RhiCommandListPool> ownedPool =
                device.createCommandListPool(poolDesc);
            RhiCommandList* commandList = ownedPool != nullptr
                ? ownedPool->acquire(RhiCommandListType::Graphics)
                : nullptr;
            bool recorded = false;
            if (commandList != nullptr &&
                commandList->begin(
                    {index == 0u ? "WorkerCommands0" : "WorkerCommands1",
                     RhiCommandListType::Graphics})) {
                commandList->insertDebugMarker(
                    index == 0u ? "worker-zero" : "worker-one", glm::vec4(1.0f));
                recorded = commandList->end();
            }
            std::unique_lock<std::mutex> workerLock(workerMutex);
            workerPools[index] = ownedPool.get();
            workerCommandLists[index] = commandList;
            workerRecorded[index] = recorded;
            ++readyWorkerCount;
            workerCondition.notify_all();
            workerCondition.wait(workerLock, [&releaseWorkers]() {
                return releaseWorkers;
            });
        });
    }
    {
        std::unique_lock<std::mutex> workerLock(workerMutex);
        workerCondition.wait(workerLock, [&readyWorkerCount]() {
            return readyWorkerCount == 2u;
        });
    }
    const auto finishWorkers = [&]() {
        {
            std::lock_guard<std::mutex> workerLock(workerMutex);
            releaseWorkers = true;
        }
        workerCondition.notify_all();
        for (std::thread& worker : workers) {
            worker.join();
        }
    };

    if (!requireTrue(workerRecorded[0] && workerRecorded[1] &&
                     workerCommandLists[0] != workerCommandLists[1],
                     "two worker threads must record independent command lists from two pools")) {
        finishWorkers();
        device.shutdown();
        return false;
    }
    if (!requireTrue(
            workerPools[0]->acquire(RhiCommandListType::Graphics) == nullptr &&
            !workerPools[1]->reset(),
            "pool acquire and reset must reject a thread other than the pool owner")) {
        finishWorkers();
        device.shutdown();
        return false;
    }
    if (!requireTrue(device.submit(
            {"WorkerPools.Submit", workerCommandLists, 2u}),
                     "device thread must submit independently recorded worker command lists")) {
        finishWorkers();
        device.shutdown();
        return false;
    }
    device.waitIdle();
    finishWorkers();

    std::unique_ptr<RhiCommandListPool> multiListPool =
        device.createCommandListPool({"MultiListPool", 2u, 4096u});
    if (!requireTrue(multiListPool != nullptr,
                     "multi-list pool must be created")) {
        device.shutdown();
        return false;
    }
    RhiCommandList* first = multiListPool->acquire(RhiCommandListType::Graphics);
    RhiCommandList* second = multiListPool->acquire(RhiCommandListType::Graphics);
    if (!requireTrue(first != nullptr && second != nullptr && first != second,
                     "one pool must return multiple distinct stable command-list addresses") ||
        !requireTrue(first->begin({"MultiList.First", RhiCommandListType::Graphics}) &&
                     first->end() &&
                     second->begin({"MultiList.Second", RhiCommandListType::Graphics}) &&
                     second->end(),
                     "multiple lists from one pool must record independently")) {
        device.shutdown();
        return false;
    }
    RhiCommandList* multiSubmit[] = {first, second};
    if (!requireTrue(device.submit({"MultiList.Submit", multiSubmit, 2u}),
                     "one submission must accept multiple executable command lists") ||
        !requireTrue(first->state() == RhiCommandListState::Pending &&
                     second->state() == RhiCommandListState::Pending,
                     "submitted command lists must remain pending until completion is reclaimed") ||
        !requireTrue(!multiListPool->reset(),
                     "command-list pool reset must reject pending command lists")) {
        device.shutdown();
        return false;
    }
    device.waitIdle();
    const bool resetAfterCompletion = multiListPool->reset();
    multiListPool.reset();
    device.shutdown();
    return requireTrue(resetAfterCompletion,
                       "command-list pool reset must succeed after pending work completes");
}

bool testGlRhiCommandListPoolLifetimeContracts() {
    GlTestContext context;
    if (!requireTrue(context.init(),
                     "OpenGL test context must initialize for command-pool lifetime contracts")) {
        return false;
    }

    auto device = std::make_unique<GlRhiDevice>();
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_command_pool_lifetime_test";
    if (!requireTrue(device->init(deviceDesc),
                     "OpenGL RHI device must initialize for command-pool lifetime contracts")) {
        return false;
    }

    std::unique_ptr<RhiCommandListPool> survivingPool =
        device->createCommandListPool({"SurvivingPool", 1u, 4096u});
    RhiCommandList* commandList = survivingPool != nullptr
        ? survivingPool->acquire(RhiCommandListType::Graphics)
        : nullptr;
    if (!requireTrue(commandList != nullptr &&
                     commandList->begin({"SurvivingPool.Recording",
                                         RhiCommandListType::Graphics}),
                     "lifetime test command list must begin recording") ||
        !requireTrue(!survivingPool->reset(),
                     "command-pool reset must reject a recording command list") ||
        !requireTrue(commandList->end() && survivingPool->reset() &&
                     commandList->state() == RhiCommandListState::Initial,
                     "command-pool reset must discard executable command lists")) {
        device->shutdown();
        return false;
    }

    commandList = survivingPool->acquire(RhiCommandListType::Graphics);
    if (!requireTrue(commandList != nullptr &&
                     commandList->begin({"SurvivingPool.Executable",
                                         RhiCommandListType::Graphics}) &&
                     commandList->end(),
                     "device-first destruction test command list must become executable")) {
        device->shutdown();
        return false;
    }
    device.reset();
    if (!requireTrue(survivingPool->reset() &&
                     commandList->state() == RhiCommandListState::Initial,
                     "a pool surviving its device must safely discard executable command lists") ||
        !requireTrue(survivingPool->acquire(RhiCommandListType::Graphics) == nullptr,
                     "a pool detached from its destroyed device must reject acquisition")) {
        return false;
    }
    survivingPool.reset();

    GlRhiDevice pendingDevice;
    deviceDesc.debugName = "rhi_pending_pool_destruction_test";
    if (!requireTrue(pendingDevice.init(deviceDesc),
                     "OpenGL RHI device must initialize for pending pool destruction")) {
        return false;
    }
    std::unique_ptr<RhiCommandListPool> pendingPool =
        pendingDevice.createCommandListPool({"PendingDestructionPool", 1u, 4096u});
    RhiCommandList* pendingList = pendingPool != nullptr
        ? pendingPool->acquire(RhiCommandListType::Graphics)
        : nullptr;
    RhiSubmissionToken pendingToken;
    RhiCommandList* pendingLists[] = {pendingList};
    if (!requireTrue(pendingList != nullptr &&
                     pendingList->begin({"PendingDestruction.Commands",
                                         RhiCommandListType::Graphics}) &&
                     pendingList->end() &&
                     pendingDevice.submit({"PendingDestruction.Submit", pendingLists, 1u},
                                          &pendingToken),
                     "pending pool destruction test must submit executable work")) {
        pendingDevice.shutdown();
        return false;
    }
    pendingPool.reset();
    if (!requireTrue(pendingDevice.waitForSubmission(pendingToken),
                     "pending submission must remain safe after its pool is destroyed")) {
        pendingDevice.shutdown();
        return false;
    }

    std::unique_ptr<RhiCommandListPool> wrongThreadPool =
        pendingDevice.createCommandListPool({"WrongThreadDestructionPool", 1u, 4096u});
    std::string destructionDiagnostic;
    {
        ScopedErrorCapture errorCapture;
        std::thread destroyer([pool = std::move(wrongThreadPool)]() mutable {
            pool.reset();
        });
        destroyer.join();
        destructionDiagnostic = errorCapture.output();
    }
    const bool diagnosedWrongThread = requireTrue(
        destructionDiagnostic.find(
            "command-list pool destruction requires its owner thread") != std::string::npos,
        "command-pool destruction on a foreign thread must emit a strict diagnostic");
    pendingDevice.shutdown();
    return diagnosedWrongThread;
}

bool testGlRhiSubmissionCompletionTokens() {
    GlTestContext context;
    if (!requireTrue(context.init(),
                     "OpenGL test context must initialize for submission token tests")) {
        return false;
    }

    GlRhiDevice device;
    RhiDeviceDesc deviceDesc = makeDeviceDesc();
    deviceDesc.debugName = "rhi_submission_completion_token_test";
    if (!requireTrue(device.init(deviceDesc),
                     "OpenGL RHI device must initialize for submission token tests")) {
        return false;
    }

    std::unique_ptr<RhiCommandListPool> commandPool =
        device.createCommandListPool({"SubmissionTokenPool", 1u, 4096u});
    RhiCommandList* commandList = commandPool != nullptr
        ? commandPool->acquire(RhiCommandListType::Graphics)
        : nullptr;
    if (!requireTrue(commandList != nullptr &&
                     commandList->begin({"SubmissionTokenCommands",
                                         RhiCommandListType::Graphics}) &&
                     commandList->end(),
                     "submission token test command list must become executable")) {
        device.shutdown();
        return false;
    }

    RhiCommandList* submittedLists[] = {commandList};
    RhiSubmissionToken token;
    bool complete = true;
    if (!requireTrue(device.submit({"SubmissionToken.Submit", submittedLists, 1u}, &token) &&
                     token.isValid(),
                     "successful submission must return a valid completion token") ||
        !requireTrue(device.isSubmissionComplete(token, complete),
                     "non-blocking completion query must accept its device token") ||
        !requireTrue(!device.isSubmissionComplete({}, complete) && !complete,
                     "completion query must reject an invalid token") ||
        !requireTrue(!device.isSubmissionComplete(
                         {token.deviceId + 1u, token.sequence}, complete) && !complete,
                     "completion query must reject a token from another device") ||
        !requireTrue(!device.waitForSubmission({token.deviceId + 1u, token.sequence}),
                     "submission wait must reject a token from another device") ||
        !requireTrue(device.waitForSubmission(token),
                     "submission wait must complete the specified submission") ||
        !requireTrue(device.isSubmissionComplete(token, complete) && complete,
                     "waited submission must report complete") ||
        !requireTrue(commandList->state() == RhiCommandListState::Initial,
                     "submission wait must reclaim its pending command list") ||
        !requireTrue(commandPool->reset(),
                     "command pool must reset after waiting for its submission")) {
        device.shutdown();
        return false;
    }

    commandList = commandPool->acquire(RhiCommandListType::Graphics);
    if (!requireTrue(commandList != nullptr &&
                     commandList->begin({"SubmissionDependency.Commands",
                                         RhiCommandListType::Graphics}) &&
                     commandList->end(),
                     "queue dependency test command list must become executable")) {
        device.shutdown();
        return false;
    }
    submittedLists[0] = commandList;
    RhiSubmitInfo invalidQueueSubmit{
        "SubmissionDependency.InvalidQueue", submittedLists, 1u};
    invalidQueueSubmit.queue = RhiQueueType::Compute;
    RhiSubmitInfo invalidWaitSubmit{
        "SubmissionDependency.InvalidWaitArray", submittedLists, 1u};
    invalidWaitSubmit.waitCount = 1u;
    const RhiQueueDependency dependency{token, token.sequence};
    RhiSubmitInfo dependencySubmit{
        "SubmissionDependency.Valid", submittedLists, 1u};
    dependencySubmit.waits = &dependency;
    dependencySubmit.waitCount = 1u;
    RhiSubmissionToken dependencyToken;
    if (!requireTrue(!device.submit(invalidQueueSubmit),
                     "OpenGL submission must reject unsupported queue types") ||
        !requireTrue(!device.submit(invalidWaitSubmit),
                     "submission must reject an inconsistent wait dependency array") ||
        !requireTrue(device.submit(dependencySubmit, &dependencyToken) &&
                         dependencyToken.sequence > token.sequence,
                     "submission must accept a valid ordered queue dependency") ||
        !requireTrue(device.waitForSubmission(dependencyToken),
                     "queue dependency submission must complete successfully")) {
        device.shutdown();
        return false;
    }

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
    if (!testVertexRangeAllocator()) {
        return 1;
    }
    if (!testGlRhiDeviceHandles()) {
        return 1;
    }
    if (!testGlRhiDeferredResourceRetirement()) {
        return 1;
    }
    if (!testGlRhiTextureDescriptorUsageValidation()) {
        return 1;
    }
    if (!testGlRhiShaderLayoutContracts()) {
        return 1;
    }
    if (!testGlRhiUiSharedPipelines()) {
        return 1;
    }
    if (!testGlRhiTimestampQueryPool()) {
        return 1;
    }
    if (!testRenderDebugServiceTimestampSegments()) {
        return 1;
    }
    if (!testGlRhiGrowableBuffer()) {
        return 1;
    }
    if (!testGlRhiSwapchainBackbuffer()) {
        return 1;
    }
    if (!testGlRhiBlitToSwapchainBackbuffer()) {
        return 1;
    }
    if (!testGlRhiTexture3DInitialData()) {
        return 1;
    }
    if (!testGlRhiCubemapInitialData()) {
        return 1;
    }
    if (!testGlRhiGenerateMipmaps()) {
        return 1;
    }
    if (!testGlRhiRejectsInvalidTransferTextureStates()) {
        return 1;
    }
    if (!testGlRhiBufferStateContracts()) {
        return 1;
    }
    if (!testGlRhiFullscreenTriangle()) {
        return 1;
    }
    if (!testGlRhiDepthLoadClearIgnoresPreviousWriteMask()) {
        return 1;
    }
    if (!testGlRhiBufferCopyToTexture()) {
        return 1;
    }
    if (!testGlRhiTightlyPackedR8BufferCopy()) {
        return 1;
    }
    if (!testGlRhiTextureCopyToPaddedReadbackBuffer()) {
        return 1;
    }
    if (!testGlRhiBufferCopyToTexture3DRegion()) {
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
    if (!testGlRhiTerrainGBufferPipeline()) {
        return 1;
    }
    if (!testGlRhiTerrainTransparentPipeline()) {
        return 1;
    }
    if (!testGlRhiTerrainWaterPipeline()) {
        return 1;
    }
    if (!testGlRhiTerrainForwardPipeline()) {
        return 1;
    }
    if (!testGlRhiTextureSubresourceStates()) {
        return 1;
    }
    if (!testGlRhiDeferredCommandPayloadOwnership()) {
        return 1;
    }
    if (!testGlRhiRejectsDestroyedRecordedResourcesAtomically()) {
        return 1;
    }
    if (!testGlRhiSemanticDryRunRejectsMultiListSubmissionAtomically()) {
        return 1;
    }
    if (!testGlRhiCommandListTypeContracts()) {
        return 1;
    }
    if (!testGlRhiIndependentCommandListPools()) {
        return 1;
    }
    if (!testGlRhiCommandListPoolLifetimeContracts()) {
        return 1;
    }
    if (!testGlRhiSubmissionCompletionTokens()) {
        return 1;
    }
    return 0;
}
