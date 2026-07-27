#include "Fsr1Pass.h"

#include "../debug/RenderDebugService.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiCommandListPool.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <optional>

namespace {
[[nodiscard]] bool sameTextureView(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
} // namespace

Fsr1Pass::~Fsr1Pass() {
    shutdown();
}

bool Fsr1Pass::isSupported(const RhiDevice& rhiDevice) {
    return rhiDevice.backend() == RhiBackend::OpenGL;
}

void Fsr1Pass::init(ResourceMgr&, RhiCommandListPool& commandListPool) {
    m_commandListPool = &commandListPool;
}

void Fsr1Pass::shutdown() {
    destroyTargets();
    destroyRhiResources();
    m_commandListPool = nullptr;
}

bool Fsr1Pass::prepareTextureOutput(RhiDevice& rhiDevice,
                                    const int width,
                                    const int height) {
    return isSupported(rhiDevice) && width > 0 && height > 0 &&
           ensureRhiPipeline(rhiDevice) &&
           ensureTargets(rhiDevice, width, height) &&
           ensureOutputTarget(rhiDevice, width, height);
}

RhiCommandList& Fsr1Pass::beginCommandList(const char* const debugName) const {
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

void Fsr1Pass::submitCommandList(RhiDevice& rhiDevice,
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

bool Fsr1Pass::execute(RhiDevice& rhiDevice,
                       const RhiTextureViewHandle swapchainColorView,
                       const RhiTextureHandle inputTexture,
                       const RhiTextureViewHandle inputView,
                       const int inputWidth,
                       const int inputHeight,
                       const int outputWidth,
                       const int outputHeight,
                       const float sharpness,
                       RenderDebugService& debugService) {
    const RhiTextureHandle swapchainTexture =
        rhiDevice.currentSwapchainColorTexture();
    return executeToOutput(
        rhiDevice, swapchainTexture, swapchainColorView,
        RhiResourceState::Present, RhiLoadOp::Load,
        inputTexture, inputView, inputWidth, inputHeight,
        outputWidth, outputHeight, sharpness, debugService);
}

bool Fsr1Pass::executeToTexture(
    RhiDevice& rhiDevice,
    const RhiTextureHandle inputTexture,
    const RhiTextureViewHandle inputView,
    const int inputWidth,
    const int inputHeight,
    const int outputWidth,
    const int outputHeight,
    const float sharpness,
    RenderDebugService& debugService) {
    if (!ensureRhiPipeline(rhiDevice) ||
        !ensureTargets(rhiDevice, outputWidth, outputHeight) ||
        !ensureOutputTarget(rhiDevice, outputWidth, outputHeight)) {
        return false;
    }
    return executeToOutput(
        rhiDevice, m_outputHandle, m_outputView,
        RhiResourceState::ShaderRead, RhiLoadOp::DontCare,
        inputTexture, inputView, inputWidth, inputHeight,
        outputWidth, outputHeight, sharpness, debugService);
}

bool Fsr1Pass::executeToOutput(
    RhiDevice& rhiDevice,
    const RhiTextureHandle outputTexture,
    const RhiTextureViewHandle outputView,
    const RhiResourceState outputStableState,
    const RhiLoadOp outputLoadOp,
    const RhiTextureHandle inputTexture,
    const RhiTextureViewHandle inputView,
    const int inputWidth,
    const int inputHeight,
    const int outputWidth,
    const int outputHeight,
    const float sharpness,
    RenderDebugService& debugService) {
    if (!isSupported(rhiDevice) || !inputTexture.isValid() ||
        !inputView.isValid() || !outputTexture.isValid() ||
        !outputView.isValid() || inputWidth <= 0 || inputHeight <= 0 ||
        outputWidth <= 0 || outputHeight <= 0 ||
        !ensureRhiPipeline(rhiDevice) ||
        !ensureTargets(rhiDevice, outputWidth, outputHeight) ||
        !ensureEasuBindGroup(rhiDevice, inputView) ||
        !ensureRcasBindGroup(rhiDevice)) {
        return false;
    }

    glm::vec4 con0;
    glm::vec4 con1;
    glm::vec4 con2;
    glm::vec4 con3;
    populateEasuConstants(con0, con1, con2, con3,
                          static_cast<float>(inputWidth),
                          static_cast<float>(inputHeight),
                          static_cast<float>(inputWidth),
                          static_cast<float>(inputHeight),
                          static_cast<float>(outputWidth),
                          static_cast<float>(outputHeight));

    const glm::vec4 easuPushConstants[4] = {
        con0,
        con1,
        con2,
        con3
    };
    const glm::vec4 rcasPushConstants = populateRcasConstants(sharpness);

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

    RgTextureHandle graphInput;
    RgTextureHandle graphEasu;
    RgTextureHandle graphSwapchain;
    if (!importTexture(inputTexture, inputView, RhiResourceState::ShaderRead,
                       graphInput) ||
        !importTexture(m_easuHandle, m_easuView, RhiResourceState::ShaderRead,
                       graphEasu) ||
        !importTexture(outputTexture, outputView,
                       outputStableState, graphSwapchain)) {
        return false;
    }

    RenderGraphPassBuilder easu = m_renderGraph.addPass(
        {"FSR1.EASU", RgPassType::Graphics, RhiQueueType::Graphics});
    easu.readTexture(graphInput, RhiResourceState::ShaderRead)
        .writeTexture(graphEasu, RhiResourceState::RenderTarget)
        .setExecute([this, &debugService, easuPushConstants,
                     outputWidth, outputHeight](RgPassContext& pass) {
            RhiCommandList& commandList = pass.commandList();
            const GpuTimerSegmentToken timerToken = debugService.beginGpuTimer(
                commandList, GpuTimerPass::Post);
            RhiColorAttachment colorAttachment;
            colorAttachment.view = m_easuView;
            colorAttachment.loadOp = RhiLoadOp::DontCare;
            colorAttachment.storeOp = RhiStoreOp::Store;
            RhiRenderingInfo renderingInfo;
            renderingInfo.debugName = "FSR1EASU";
            renderingInfo.renderArea = {
                0, 0,
                static_cast<uint32_t>(std::max(1, outputWidth)),
                static_cast<uint32_t>(std::max(1, outputHeight))
            };
            renderingInfo.colorAttachments = &colorAttachment;
            renderingInfo.colorAttachmentCount = 1u;
            commandList.beginRendering(renderingInfo);
            commandList.setGraphicsPipeline(m_easuPipeline);
            commandList.setBindGroup(0u, m_easuBindGroup);
            commandList.pushConstants(easuPushConstants,
                                      sizeof(easuPushConstants),
                                      rhiFlag(RhiShaderStage::Fragment));
            commandList.draw(3u, 1u, 0u, 0u);
            commandList.endRendering();
            debugService.endGpuTimer(commandList, timerToken);
            return true;
        });

    RenderGraphPassBuilder rcas = m_renderGraph.addPass(
        {"FSR1.RCAS", RgPassType::Graphics, RhiQueueType::Graphics});
    rcas.dependsOn(easu.handle())
        .readTexture(graphEasu, RhiResourceState::ShaderRead)
        .writeTexture(graphSwapchain, RhiResourceState::RenderTarget)
        .setExecute([this, &debugService, rcasPushConstants,
                     outputView, outputLoadOp, outputWidth, outputHeight](
                        RgPassContext& pass) {
            RhiCommandList& commandList = pass.commandList();
            const GpuTimerSegmentToken timerToken = debugService.beginGpuTimer(
                commandList, GpuTimerPass::Post);
            RhiColorAttachment colorAttachment;
            colorAttachment.view = outputView;
            colorAttachment.loadOp = outputLoadOp;
            colorAttachment.storeOp = RhiStoreOp::Store;
            RhiRenderingInfo renderingInfo;
            renderingInfo.debugName = "FSR1RCAS";
            renderingInfo.renderArea = {
                0, 0,
                static_cast<uint32_t>(std::max(1, outputWidth)),
                static_cast<uint32_t>(std::max(1, outputHeight))
            };
            renderingInfo.colorAttachments = &colorAttachment;
            renderingInfo.colorAttachmentCount = 1u;
            commandList.beginRendering(renderingInfo);
            commandList.setGraphicsPipeline(m_rcasPipeline);
            commandList.setBindGroup(0u, m_rcasBindGroup);
            commandList.pushConstants(&rcasPushConstants,
                                      sizeof(rcasPushConstants),
                                      rhiFlag(RhiShaderStage::Fragment));
            commandList.draw(3u, 1u, 0u, 0u);
            commandList.endRendering();
            debugService.endGpuTimer(commandList, timerToken);
            return true;
        });

    const RgCompileResult compiled = m_renderGraph.compile();
    if (!compiled.succeeded()) {
        return false;
    }
    const GpuTimerCheckpoint timerCheckpoint = debugService.gpuTimerCheckpoint();
    const RgExecuteResult executed = m_renderGraph.execute(
        rhiDevice, *m_commandListPool);
    if (!executed.succeeded()) {
        debugService.cancelGpuTimersSince(timerCheckpoint);
        return false;
    }

    return true;
}

bool Fsr1Pass::ensureRhiPipeline(RhiDevice& rhiDevice) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyTargets();
        destroyRhiResources();
    }
    if (m_easuPipeline.isValid() && m_rcasPipeline.isValid()) {
        return true;
    }
    m_rhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> easuFragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/fsr1_easu.frag");
    const std::optional<std::string> rcasFragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/fsr1_rcas.frag");
    if (!vertexSource.has_value() || !easuFragmentSource.has_value() || !rcasFragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "FSR1.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_vertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc easuFragmentDesc;
    easuFragmentDesc.debugName = "FSR1.EASU.Fragment";
    easuFragmentDesc.stage = RhiShaderStage::Fragment;
    easuFragmentDesc.source = easuFragmentSource->c_str();
    easuFragmentDesc.sourceSize = easuFragmentSource->size();
    m_easuFragmentShader = rhiDevice.createShader(easuFragmentDesc);

    RhiShaderDesc rcasFragmentDesc;
    rcasFragmentDesc.debugName = "FSR1.RCAS.Fragment";
    rcasFragmentDesc.stage = RhiShaderStage::Fragment;
    rcasFragmentDesc.source = rcasFragmentSource->c_str();
    rcasFragmentDesc.sourceSize = rcasFragmentSource->size();
    m_rcasFragmentShader = rhiDevice.createShader(rcasFragmentDesc);
    if (!m_vertexShader.isValid() || !m_easuFragmentShader.isValid() || !m_rcasFragmentShader.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiSamplerDesc samplerDesc;
    samplerDesc.minFilter = RhiFilter::Linear;
    samplerDesc.magFilter = RhiFilter::Linear;
    samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    samplerDesc.addressU = RhiAddressMode::ClampToEdge;
    samplerDesc.addressV = RhiAddressMode::ClampToEdge;
    samplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_sampler = rhiDevice.createSampler(samplerDesc);
    if (!m_sampler.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "FSR1.BindGroupLayout";
    bindGroupLayoutDesc.entries.push_back({
        0u,
        RhiBindingType::CombinedTextureSampler,
        rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    m_bindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_bindGroupLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "FSR1.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4) * 4u);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_pipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_pipelineLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc easuPipelineDesc;
    easuPipelineDesc.debugName = "FSR1.EASU.Pipeline";
    easuPipelineDesc.vertexShader = m_vertexShader;
    easuPipelineDesc.fragmentShader = m_easuFragmentShader;
    easuPipelineDesc.layout = m_pipelineLayout;
    easuPipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    easuPipelineDesc.raster.cullMode = RhiCullMode::None;
    easuPipelineDesc.depthStencil.depthTestEnabled = false;
    easuPipelineDesc.depthStencil.depthWriteEnabled = false;
    easuPipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba8Unorm);
    easuPipelineDesc.blend.attachments.push_back({});
    m_easuPipeline = rhiDevice.createGraphicsPipeline(easuPipelineDesc);
    if (!m_easuPipeline.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc rcasPipelineDesc = easuPipelineDesc;
    rcasPipelineDesc.debugName = "FSR1.RCAS.Pipeline";
    rcasPipelineDesc.fragmentShader = m_rcasFragmentShader;
    rcasPipelineDesc.colorFormats.clear();
    rcasPipelineDesc.colorFormats.push_back(rhiDevice.swapchainColorFormat());
    m_rcasPipeline = rhiDevice.createGraphicsPipeline(rcasPipelineDesc);
    if (!m_rcasPipeline.isValid()) {
        destroyRhiResources();
        return false;
    }

    return true;
}

bool Fsr1Pass::ensureEasuBindGroup(RhiDevice& rhiDevice, const RhiTextureViewHandle inputView) {
    if (!ensureRhiPipeline(rhiDevice) || !inputView.isValid()) {
        return false;
    }

    if (m_easuBindGroup.isValid() && sameTextureView(m_boundEasuInputView, inputView)) {
        return true;
    }

    if (m_easuBindGroup.isValid()) {
        rhiDevice.destroyBindGroup(m_easuBindGroup);
        m_easuBindGroup = {};
    }
    m_boundEasuInputView = {};

    RhiBindGroupEntry entry;
    entry.binding = 0u;
    entry.resource.combinedTextureSampler.textureView = inputView;
    entry.resource.combinedTextureSampler.sampler = m_sampler;

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_bindGroupLayout;
    bindGroupDesc.entries.push_back(entry);
    m_easuBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_easuBindGroup.isValid()) {
        return false;
    }

    m_boundEasuInputView = inputView;
    return true;
}

bool Fsr1Pass::ensureRcasBindGroup(RhiDevice& rhiDevice) {
    if (!ensureRhiPipeline(rhiDevice) || !m_easuView.isValid()) {
        return false;
    }

    if (m_rcasBindGroup.isValid() && sameTextureView(m_boundRcasInputView, m_easuView)) {
        return true;
    }

    if (m_rcasBindGroup.isValid()) {
        rhiDevice.destroyBindGroup(m_rcasBindGroup);
        m_rcasBindGroup = {};
    }
    m_boundRcasInputView = {};

    RhiBindGroupEntry entry;
    entry.binding = 0u;
    entry.resource.combinedTextureSampler.textureView = m_easuView;
    entry.resource.combinedTextureSampler.sampler = m_sampler;

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_bindGroupLayout;
    bindGroupDesc.entries.push_back(entry);
    m_rcasBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_rcasBindGroup.isValid()) {
        return false;
    }

    m_boundRcasInputView = m_easuView;
    return true;
}

bool Fsr1Pass::ensureTargets(RhiDevice& rhiDevice, const int width, const int height) {
    const int targetWidth = std::max(1, width);
    const int targetHeight = std::max(1, height);
    if (m_easuHandle.isValid() && m_easuView.isValid() && m_rhiDevice == &rhiDevice &&
        m_width == targetWidth && m_height == targetHeight) {
        return true;
    }

    destroyTargets();
    m_width = targetWidth;
    m_height = targetHeight;

    RhiTextureDesc textureDesc;
    textureDesc.debugName = "FSR1.EASU.Target";
    textureDesc.dimension = RhiTextureDimension::Texture2D;
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = static_cast<uint32_t>(m_width);
    textureDesc.height = static_cast<uint32_t>(m_height);
    textureDesc.depthOrLayers = 1u;
    textureDesc.mipLevels = 1u;
    textureDesc.sampleCount = 1u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                        rhiFlag(RhiTextureUsage::ColorAttachment);
    m_easuHandle = rhiDevice.createTexture(textureDesc, nullptr);
    if (!m_easuHandle.isValid()) {
        destroyTargets();
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = m_easuHandle;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = RhiTextureFormat::Rgba8Unorm;
    viewDesc.baseMip = 0u;
    viewDesc.mipCount = 1u;
    viewDesc.baseLayer = 0u;
    viewDesc.layerCount = 1u;
    m_easuView = rhiDevice.createTextureView(viewDesc);
    if (!m_easuView.isValid()) {
        destroyTargets();
        return false;
    }

    RhiCommandList& commandList = beginCommandList("FSR1.TargetInitialization.Commands");
    commandList.textureBarrier({
        m_easuHandle,
        RhiResourceState::Undefined,
        RhiResourceState::ShaderRead
    });
    submitCommandList(rhiDevice, commandList, "FSR1.TargetInitialization.Submit");

    return true;
}

bool Fsr1Pass::ensureOutputTarget(RhiDevice& rhiDevice,
                                  const int width,
                                  const int height) {
    const int targetWidth = std::max(1, width);
    const int targetHeight = std::max(1, height);
    if (m_outputHandle.isValid() && m_outputView.isValid() &&
        m_rhiDevice == &rhiDevice && m_outputWidth == targetWidth &&
        m_outputHeight == targetHeight) {
        return true;
    }
    if (m_rhiDevice != nullptr && m_outputView.isValid()) {
        m_rhiDevice->destroyTextureView(m_outputView);
    }
    if (m_rhiDevice != nullptr && m_outputHandle.isValid()) {
        m_rhiDevice->destroyTexture(m_outputHandle);
    }
    m_outputHandle = {};
    m_outputView = {};
    m_outputWidth = 0;
    m_outputHeight = 0;

    RhiTextureDesc textureDesc;
    textureDesc.debugName = "FSR1.RCAS.Target";
    textureDesc.dimension = RhiTextureDimension::Texture2D;
    textureDesc.format = rhiDevice.swapchainColorFormat();
    textureDesc.width = static_cast<uint32_t>(targetWidth);
    textureDesc.height = static_cast<uint32_t>(targetHeight);
    textureDesc.depthOrLayers = 1u;
    textureDesc.mipLevels = 1u;
    textureDesc.sampleCount = 1u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                        rhiFlag(RhiTextureUsage::ColorAttachment);
    m_outputHandle = rhiDevice.createTexture(textureDesc, nullptr);
    if (!m_outputHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = m_outputHandle;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = textureDesc.format;
    viewDesc.mipCount = 1u;
    viewDesc.layerCount = 1u;
    m_outputView = rhiDevice.createTextureView(viewDesc);
    if (!m_outputView.isValid()) {
        rhiDevice.destroyTexture(m_outputHandle);
        m_outputHandle = {};
        return false;
    }

    RhiCommandList& commandList = beginCommandList(
        "FSR1.OutputInitialization.Commands");
    commandList.textureBarrier({
        m_outputHandle,
        RhiResourceState::Undefined,
        RhiResourceState::ShaderRead});
    submitCommandList(
        rhiDevice, commandList, "FSR1.OutputInitialization.Submit");
    m_outputWidth = targetWidth;
    m_outputHeight = targetHeight;
    return true;
}

void Fsr1Pass::destroyTargets() {
    destroyRhiBindGroups();
    if (m_rhiDevice != nullptr && m_outputView.isValid()) {
        m_rhiDevice->destroyTextureView(m_outputView);
    }
    if (m_rhiDevice != nullptr && m_outputHandle.isValid()) {
        m_rhiDevice->destroyTexture(m_outputHandle);
    }
    if (m_rhiDevice != nullptr && m_easuView.isValid()) {
        m_rhiDevice->destroyTextureView(m_easuView);
    }
    if (m_rhiDevice != nullptr && m_easuHandle.isValid()) {
        m_rhiDevice->destroyTexture(m_easuHandle);
    }
    m_easuView = {};
    m_easuHandle = {};
    m_outputView = {};
    m_outputHandle = {};
    m_width = 0;
    m_height = 0;
    m_outputWidth = 0;
    m_outputHeight = 0;
}

void Fsr1Pass::destroyRhiBindGroups() {
    if (m_rhiDevice != nullptr) {
        if (m_easuBindGroup.isValid()) {
            m_rhiDevice->destroyBindGroup(m_easuBindGroup);
        }
        if (m_rcasBindGroup.isValid()) {
            m_rhiDevice->destroyBindGroup(m_rcasBindGroup);
        }
    }
    m_easuBindGroup = {};
    m_rcasBindGroup = {};
    m_boundEasuInputView = {};
    m_boundRcasInputView = {};
}

void Fsr1Pass::destroyRhiResources() {
    if (m_rhiDevice != nullptr) {
        m_renderGraph.releaseTransientResources(*m_rhiDevice);
    }
    destroyRhiBindGroups();
    if (m_rhiDevice != nullptr) {
        if (m_easuPipeline.isValid()) {
            m_rhiDevice->destroyPipeline(m_easuPipeline);
        }
        if (m_rcasPipeline.isValid()) {
            m_rhiDevice->destroyPipeline(m_rcasPipeline);
        }
        if (m_vertexShader.isValid()) {
            m_rhiDevice->destroyShader(m_vertexShader);
        }
        if (m_easuFragmentShader.isValid()) {
            m_rhiDevice->destroyShader(m_easuFragmentShader);
        }
        if (m_rcasFragmentShader.isValid()) {
            m_rhiDevice->destroyShader(m_rcasFragmentShader);
        }
        if (m_pipelineLayout.isValid()) {
            m_rhiDevice->destroyPipelineLayout(m_pipelineLayout);
        }
        if (m_bindGroupLayout.isValid()) {
            m_rhiDevice->destroyBindGroupLayout(m_bindGroupLayout);
        }
        if (m_sampler.isValid()) {
            m_rhiDevice->destroySampler(m_sampler);
        }
    }

    m_easuPipeline = {};
    m_rcasPipeline = {};
    m_vertexShader = {};
    m_easuFragmentShader = {};
    m_rcasFragmentShader = {};
    m_pipelineLayout = {};
    m_bindGroupLayout = {};
    m_sampler = {};
    m_rhiDevice = nullptr;
}

void Fsr1Pass::populateEasuConstants(glm::vec4& con0,
                                     glm::vec4& con1,
                                     glm::vec4& con2,
                                     glm::vec4& con3,
                                     const float inputViewportWidth,
                                     const float inputViewportHeight,
                                     const float inputTextureWidth,
                                     const float inputTextureHeight,
                                     const float outputWidth,
                                     const float outputHeight) {
    const float invInputW = 1.0f / std::max(inputTextureWidth, 1.0f);
    const float invInputH = 1.0f / std::max(inputTextureHeight, 1.0f);
    const float invOutputW = 1.0f / std::max(outputWidth, 1.0f);
    const float invOutputH = 1.0f / std::max(outputHeight, 1.0f);

    con0 = glm::vec4(inputViewportWidth * invOutputW,
                     inputViewportHeight * invOutputH,
                     0.5f * inputViewportWidth * invOutputW - 0.5f,
                     0.5f * inputViewportHeight * invOutputH - 0.5f);
    con1 = glm::vec4(invInputW,
                     invInputH,
                     invInputW,
                     -invInputH);
    con2 = glm::vec4(-invInputW,
                     2.0f * invInputH,
                     invInputW,
                     2.0f * invInputH);
    con3 = glm::vec4(0.0f,
                     4.0f * invInputH,
                     0.0f,
                     0.0f);
}

glm::vec4 Fsr1Pass::populateRcasConstants(const float sharpness) {
    const float stops = std::clamp(sharpness, 0.0f, 2.0f);
    return glm::vec4(std::exp2(-stops), 0.0f, 0.0f, 0.0f);
}
