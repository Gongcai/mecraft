#include "SkyboxRenderer.h"

#include "../../Diagnostics.h"
#include <algorithm>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../../resource/ResourceMgr.h"

namespace {
constexpr float kCubeVertices[] = {
    // Back face (-Z)
    -1.0f,  1.0f, -1.0f,   -1.0f, -1.0f, -1.0f,    1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,    1.0f,  1.0f, -1.0f,   -1.0f,  1.0f, -1.0f,
    // Front face (+Z)
    -1.0f, -1.0f,  1.0f,   -1.0f,  1.0f,  1.0f,    1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,    1.0f, -1.0f,  1.0f,   -1.0f, -1.0f,  1.0f,
    // Left face (-X)
    -1.0f,  1.0f,  1.0f,   -1.0f,  1.0f, -1.0f,   -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f,  1.0f,   -1.0f,  1.0f,  1.0f,
    // Right face (+X)
     1.0f,  1.0f, -1.0f,    1.0f,  1.0f,  1.0f,    1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,    1.0f, -1.0f, -1.0f,    1.0f,  1.0f, -1.0f,
    // Top face (+Y)
    -1.0f,  1.0f,  1.0f,    1.0f,  1.0f,  1.0f,    1.0f,  1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,   -1.0f,  1.0f, -1.0f,   -1.0f,  1.0f,  1.0f,
    // Bottom face (-Y)
    -1.0f, -1.0f, -1.0f,    1.0f, -1.0f, -1.0f,    1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,   -1.0f, -1.0f,  1.0f,   -1.0f, -1.0f, -1.0f,
};

bool blitBlurTargetToSwapchain(RhiDevice& rhiDevice,
                               const RhiTextureHandle source,
                               const int width,
                               const int height) {
    if (!source.isValid() ||
        !rhiDevice.resizeSwapchain(static_cast<uint32_t>(std::max(1, width)),
                                   static_cast<uint32_t>(std::max(1, height)))) {
        return false;
    }

    const RhiTextureViewHandle swapchainColorView = rhiDevice.currentSwapchainColorView();
    if (!swapchainColorView.isValid()) {
        return false;
    }

    RhiTextureBlit blit;
    blit.src = source;
    blit.dstView = swapchainColorView;

    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.blitTexture(blit);
    rhiDevice.submitFrame(commandList);
    return true;
}

RhiTextureViewHandle createBlurTargetView(RhiDevice& rhiDevice, const RhiTextureHandle texture) {
    if (!texture.isValid()) {
        return {};
    }

    RhiTextureViewDesc desc;
    desc.texture = texture;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba8Unorm;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;
    return rhiDevice.createTextureView(desc);
}

void beginSkyboxBlurOutput(RhiCommandList& commandList,
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
}

void SkyboxRenderer::init(ResourceMgr& resourceMgr, RhiDevice& rhiDevice) {
    m_rhiDevice = &rhiDevice;
    m_cubemapTexture = resourceMgr.getCubemap("menu_skybox");
    if (!m_cubemapTexture.isValid()) std::abort();
    RhiTextureViewDesc cubemapViewDesc;
    cubemapViewDesc.texture = m_cubemapTexture;
    cubemapViewDesc.viewType = RhiTextureViewType::Cube;
    m_cubemapView = rhiDevice.createTextureView(cubemapViewDesc);
    RhiSamplerDesc cubemapSamplerDesc;
    cubemapSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    cubemapSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    cubemapSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_cubemapSampler = rhiDevice.createSampler(cubemapSamplerDesc);
    if (!m_cubemapView.isValid() || !m_cubemapSampler.isValid()) std::abort();
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/skybox_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/skybox_rhi.frag");
    if (!vertexSource || !fragmentSource) std::abort();
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "Skybox.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_skyboxVertexShader = rhiDevice.createShader(shaderDesc);
    shaderDesc.debugName = "Skybox.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_skyboxFragmentShader = rhiDevice.createShader(shaderDesc);
    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "Skybox.BindGroupLayout";
    bindGroupLayoutDesc.entries.push_back({0u, RhiBindingType::CombinedTextureSampler,
                                           rhiFlag(RhiShaderStage::Fragment), 1u});
    m_skyboxBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "Skybox.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_skyboxBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(glm::mat4) * 2u;
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex);
    m_skyboxPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "Skybox.Pipeline";
    pipelineDesc.vertexShader = m_skyboxVertexShader;
    pipelineDesc.fragmentShader = m_skyboxFragmentShader;
    pipelineDesc.layout = m_skyboxPipelineLayout;
    pipelineDesc.vertexInput.bindings = {{0u, sizeof(float) * 3u, RhiVertexInputRate::Vertex}};
    pipelineDesc.vertexInput.attributes = {{0u, 0u, RhiVertexFormat::Float3, 0u}};
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba8Unorm};
    pipelineDesc.blend.attachments.resize(1u);
    m_skyboxPipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_skyboxBindGroupLayout;
    RhiBindGroupEntry textureEntry;
    textureEntry.binding = 0u;
    textureEntry.resource.combinedTextureSampler = {m_cubemapView, m_cubemapSampler};
    bindGroupDesc.entries.push_back(textureEntry);
    m_skyboxBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_skyboxVertexShader.isValid() || !m_skyboxFragmentShader.isValid() ||
        !m_skyboxBindGroupLayout.isValid() || !m_skyboxPipelineLayout.isValid() ||
        !m_skyboxPipeline.isValid() || !m_skyboxBindGroup.isValid()) std::abort();
    const auto blurVertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/skybox_blur_rhi.vert");
    const auto blurFragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/skybox_blur_rhi.frag");
    if (!blurVertexSource || !blurFragmentSource) std::abort();
    RhiShaderDesc blurShaderDesc;
    blurShaderDesc.debugName = "Skybox.Blur.Vertex";
    blurShaderDesc.stage = RhiShaderStage::Vertex;
    blurShaderDesc.source = blurVertexSource->c_str();
    blurShaderDesc.sourceSize = blurVertexSource->size();
    m_blurVertexShader = rhiDevice.createShader(blurShaderDesc);
    blurShaderDesc.debugName = "Skybox.Blur.Fragment";
    blurShaderDesc.stage = RhiShaderStage::Fragment;
    blurShaderDesc.source = blurFragmentSource->c_str();
    blurShaderDesc.sourceSize = blurFragmentSource->size();
    m_blurFragmentShader = rhiDevice.createShader(blurShaderDesc);
    RhiSamplerDesc blurSamplerDesc;
    blurSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    blurSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    m_blurSampler = rhiDevice.createSampler(blurSamplerDesc);
    RhiBindGroupLayoutDesc blurBindGroupLayoutDesc;
    blurBindGroupLayoutDesc.debugName = "Skybox.Blur.BindGroupLayout";
    blurBindGroupLayoutDesc.entries.push_back({0u, RhiBindingType::CombinedTextureSampler,
                                               rhiFlag(RhiShaderStage::Fragment), 1u});
    m_blurBindGroupLayout = rhiDevice.createBindGroupLayout(blurBindGroupLayoutDesc);
    RhiPipelineLayoutDesc blurPipelineLayoutDesc;
    blurPipelineLayoutDesc.debugName = "Skybox.Blur.PipelineLayout";
    blurPipelineLayoutDesc.bindGroupLayouts.push_back(m_blurBindGroupLayout);
    blurPipelineLayoutDesc.pushConstantBytes = sizeof(glm::vec4);
    blurPipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_blurPipelineLayout = rhiDevice.createPipelineLayout(blurPipelineLayoutDesc);
    RhiGraphicsPipelineDesc blurPipelineDesc;
    blurPipelineDesc.debugName = "Skybox.Blur.Pipeline";
    blurPipelineDesc.vertexShader = m_blurVertexShader;
    blurPipelineDesc.fragmentShader = m_blurFragmentShader;
    blurPipelineDesc.layout = m_blurPipelineLayout;
    blurPipelineDesc.raster.cullMode = RhiCullMode::None;
    blurPipelineDesc.depthStencil.depthTestEnabled = false;
    blurPipelineDesc.depthStencil.depthWriteEnabled = false;
    blurPipelineDesc.colorFormats = {RhiTextureFormat::Rgba8Unorm};
    blurPipelineDesc.blend.attachments.resize(1u);
    m_blurPipeline = rhiDevice.createGraphicsPipeline(blurPipelineDesc);
    if (!m_blurVertexShader.isValid() || !m_blurFragmentShader.isValid() ||
        !m_blurSampler.isValid() || !m_blurBindGroupLayout.isValid() ||
        !m_blurPipelineLayout.isValid() || !m_blurPipeline.isValid()) std::abort();
    initCubeMesh();

}

void SkyboxRenderer::shutdown() {
    destroyBlurTargets();
    destroyCubeMesh();
    if (m_skyboxBindGroup.isValid()) m_rhiDevice->destroyBindGroup(m_skyboxBindGroup);
    if (m_skyboxPipeline.isValid()) m_rhiDevice->destroyPipeline(m_skyboxPipeline);
    if (m_skyboxPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_skyboxPipelineLayout);
    if (m_skyboxBindGroupLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_skyboxBindGroupLayout);
    if (m_skyboxFragmentShader.isValid()) m_rhiDevice->destroyShader(m_skyboxFragmentShader);
    if (m_skyboxVertexShader.isValid()) m_rhiDevice->destroyShader(m_skyboxVertexShader);
    if (m_blurPipeline.isValid()) m_rhiDevice->destroyPipeline(m_blurPipeline);
    if (m_blurPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_blurPipelineLayout);
    if (m_blurBindGroupLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_blurBindGroupLayout);
    if (m_blurSampler.isValid()) m_rhiDevice->destroySampler(m_blurSampler);
    if (m_blurFragmentShader.isValid()) m_rhiDevice->destroyShader(m_blurFragmentShader);
    if (m_blurVertexShader.isValid()) m_rhiDevice->destroyShader(m_blurVertexShader);
    if (m_cubemapSampler.isValid()) m_rhiDevice->destroySampler(m_cubemapSampler);
    if (m_cubemapView.isValid()) m_rhiDevice->destroyTextureView(m_cubemapView);

    m_cubemapTexture = {};
    m_skyboxBindGroup = {};
    m_skyboxPipeline = {};
    m_skyboxPipelineLayout = {};
    m_skyboxBindGroupLayout = {};
    m_skyboxFragmentShader = {};
    m_skyboxVertexShader = {};
    m_blurPipeline = {};
    m_blurPipelineLayout = {};
    m_blurBindGroupLayout = {};
    m_blurSampler = {};
    m_blurFragmentShader = {};
    m_blurVertexShader = {};
    m_cubemapView = {};
    m_cubemapSampler = {};
    m_rhiDevice = nullptr;
}

void SkyboxRenderer::render(const int width, const int height, const float aspect,
                            const float yawDegrees, const float pitchDegrees,
                            RhiDevice& rhiDevice) {
    if (!m_cubemapTexture.isValid() || !m_cubeVertexBuffer.isValid()) {
        return;
    }
    if (width <= 0 || height <= 0) {
        return;
    }

    // Half resolution for blur (matches Minecraft's approach)
    const int blurW = std::max(1, width / 2);
    const int blurH = std::max(1, height / 2);

    if (!ensureBlurTargets(rhiDevice, blurW, blurH)) {
        return;
    }

    RhiCommandList& commandList = rhiDevice.beginFrame();

    // --- Pass 1: Render skybox to scene FBO ---
    beginSkyboxBlurOutput(commandList, "SkyboxScene", m_sceneColorView, blurW, blurH, true);

    glm::mat4 view(1.0f);
    view = glm::rotate(view, glm::radians(pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
    view = glm::rotate(view, glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 projection = glm::perspective(glm::radians(70.0f), aspect, 0.1f, 100.0f);

    struct SkyboxPushConstants { glm::mat4 projection; glm::mat4 view; };
    const SkyboxPushConstants skyboxConstants{projection, view};
    commandList.setViewport({0.0f, 0.0f, static_cast<float>(blurW),
                             static_cast<float>(blurH), 0.0f, 1.0f});
    commandList.setScissor({0, 0, static_cast<uint32_t>(blurW),
                            static_cast<uint32_t>(blurH)});
    commandList.setGraphicsPipeline(m_skyboxPipeline);
    commandList.setBindGroup(0u, m_skyboxBindGroup);
    commandList.setVertexBuffer(0u, m_cubeVertexBuffer, 0u);
    commandList.pushConstants(&skyboxConstants, sizeof(skyboxConstants),
                              rhiFlag(RhiShaderStage::Vertex));
    commandList.draw(36u, 1u, 0u, 0u);
    commandList.endRendering();

    // --- Pass 2: Horizontal blur (scene -> ping) ---
    beginSkyboxBlurOutput(commandList, "SkyboxBlurHorizontal", m_pingColorView, blurW, blurH, false);

    const glm::vec4 horizontalDirection(1.0f / static_cast<float>(blurW), 0.0f, 0.0f, 0.0f);
    commandList.setViewport({0.0f, 0.0f, static_cast<float>(blurW),
                             static_cast<float>(blurH), 0.0f, 1.0f});
    commandList.setScissor({0, 0, static_cast<uint32_t>(blurW),
                            static_cast<uint32_t>(blurH)});
    commandList.setGraphicsPipeline(m_blurPipeline);
    commandList.setBindGroup(0u, m_sceneBlurBindGroup);
    commandList.pushConstants(&horizontalDirection, sizeof(horizontalDirection),
                              rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();

    // --- Pass 3: Vertical blur (ping -> pong) ---
    beginSkyboxBlurOutput(commandList, "SkyboxBlurVertical", m_pongColorView, blurW, blurH, false);

    const glm::vec4 verticalDirection(0.0f, 1.0f / static_cast<float>(blurH), 0.0f, 0.0f);
    commandList.setGraphicsPipeline(m_blurPipeline);
    commandList.setBindGroup(0u, m_pingBlurBindGroup);
    commandList.pushConstants(&verticalDirection, sizeof(verticalDirection),
                              rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);

    const bool blitted = blitBlurTargetToSwapchain(rhiDevice, m_pongColorHandle, width, height);
    if (!blitted) {
        MECRAFT_LOG_STREAM(std::cerr << "[SkyboxRenderer] Failed to blit menu skybox through RHI\n");
    }
}

bool SkyboxRenderer::ensureBlurTargets(RhiDevice& rhiDevice, int width, int height) {
    if (width == m_blurWidth && height == m_blurHeight &&
        m_sceneColorHandle.isValid() && m_pingColorHandle.isValid() &&
        m_pongColorHandle.isValid() &&
        m_sceneColorView.isValid() && m_pingColorView.isValid() && m_pongColorView.isValid() &&
        m_rhiDevice == &rhiDevice) {
        return true;
    }

    destroyBlurTargets();

    const auto createTexture = [&](const char* debugName) {
        RhiTextureDesc desc;
        desc.debugName = debugName;
        desc.format = RhiTextureFormat::Rgba8Unorm;
        desc.width = static_cast<uint32_t>(width);
        desc.height = static_cast<uint32_t>(height);
        desc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                     rhiFlag(RhiTextureUsage::ColorAttachment) |
                     rhiFlag(RhiTextureUsage::TransferSrc);
        return rhiDevice.createTexture(desc, nullptr);
    };
    m_sceneColorHandle = createTexture("Skybox.SceneColor");
    m_pingColorHandle = createTexture("Skybox.BlurPing");
    m_pongColorHandle = createTexture("Skybox.BlurPong");
    m_sceneColorView = createBlurTargetView(rhiDevice, m_sceneColorHandle);
    m_pingColorView = createBlurTargetView(rhiDevice, m_pingColorHandle);
    m_pongColorView = createBlurTargetView(rhiDevice, m_pongColorHandle);

    const auto createBlurBindGroup = [&](const RhiTextureViewHandle view) {
        RhiBindGroupDesc desc;
        desc.layout = m_blurBindGroupLayout;
        RhiBindGroupEntry entry;
        entry.binding = 0u;
        entry.resource.combinedTextureSampler = {view, m_blurSampler};
        desc.entries.push_back(entry);
        return rhiDevice.createBindGroup(desc);
    };
    m_sceneBlurBindGroup = createBlurBindGroup(m_sceneColorView);
    m_pingBlurBindGroup = createBlurBindGroup(m_pingColorView);

    if (!m_sceneColorHandle.isValid() || !m_pingColorHandle.isValid() ||
        !m_pongColorHandle.isValid() ||
        !m_sceneColorView.isValid() || !m_pingColorView.isValid() ||
        !m_pongColorView.isValid() || !m_sceneBlurBindGroup.isValid() ||
        !m_pingBlurBindGroup.isValid()) {
        destroyBlurTargets();
        return false;
    }

    m_blurWidth = width;
    m_blurHeight = height;
    return true;
}

void SkyboxRenderer::destroyBlurTargets() {
    if (m_rhiDevice != nullptr && m_sceneBlurBindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_sceneBlurBindGroup);
    }
    if (m_rhiDevice != nullptr && m_pingBlurBindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_pingBlurBindGroup);
    }
    m_sceneBlurBindGroup = {};
    m_pingBlurBindGroup = {};
    if (m_rhiDevice != nullptr && m_sceneColorView.isValid()) {
        m_rhiDevice->destroyTextureView(m_sceneColorView);
    }
    if (m_rhiDevice != nullptr && m_pingColorView.isValid()) {
        m_rhiDevice->destroyTextureView(m_pingColorView);
    }
    if (m_rhiDevice != nullptr && m_pongColorView.isValid()) {
        m_rhiDevice->destroyTextureView(m_pongColorView);
    }
    m_sceneColorView = {};
    m_pingColorView = {};
    m_pongColorView = {};
    if (m_rhiDevice != nullptr && m_sceneColorHandle.isValid()) m_rhiDevice->destroyTexture(m_sceneColorHandle);
    if (m_rhiDevice != nullptr && m_pingColorHandle.isValid()) m_rhiDevice->destroyTexture(m_pingColorHandle);
    if (m_rhiDevice != nullptr && m_pongColorHandle.isValid()) m_rhiDevice->destroyTexture(m_pongColorHandle);
    m_sceneColorHandle = {};
    m_pingColorHandle = {};
    m_pongColorHandle = {};
    m_blurWidth = 0;
    m_blurHeight = 0;
}

void SkyboxRenderer::initCubeMesh() {
    if (m_cubeVertexBuffer.isValid()) return;

    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "Skybox.Cube.VertexBuffer";
    bufferDesc.size = sizeof(kCubeVertices);
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    m_cubeVertexBuffer = m_rhiDevice->createBuffer(
        bufferDesc, kCubeVertices, sizeof(kCubeVertices));
    if (!m_cubeVertexBuffer.isValid()) std::abort();

}

void SkyboxRenderer::destroyCubeMesh() {
    if (m_rhiDevice != nullptr && m_cubeVertexBuffer.isValid()) {
        m_rhiDevice->destroyBuffer(m_cubeVertexBuffer);
        m_cubeVertexBuffer = {};
    }
}
