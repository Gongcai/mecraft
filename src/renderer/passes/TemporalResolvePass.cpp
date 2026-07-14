#include "TemporalResolvePass.h"

#include "../core/RenderScene.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../targets/DeferredRenderTargets.h"

#include <algorithm>
#include <optional>

#include <glm/vec4.hpp>

namespace {
[[nodiscard]] bool sameTextureView(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
} // namespace

void TemporalResolvePass::init(ResourceMgr&) {}

void TemporalResolvePass::shutdown() {
    destroyRhiResources();
}

void TemporalResolvePass::execute(const FrameContext& ctx, const RenderSettings& settings,
                                  DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!targets.ensureSceneResolvedTextureView(rhiDevice) ||
        !targets.ensureTemporalCurrentTextureView(rhiDevice) ||
        !targets.ensureHistorySceneTextureViews(rhiDevice) ||
        !targets.ensureVelocityTextureView(rhiDevice) ||
        !targets.ensureGBufferTextureViews(rhiDevice) ||
        !ensureRhiPipeline(rhiDevice)) {
        return;
    }

    const int historyPrevIndex = 1 - targets.currentHistoryIndex();
    if (!ensureRhiBindGroup(rhiDevice,
                            historyPrevIndex,
                            targets.temporalCurrentTextureViewHandle(),
                            targets.historySceneTexturePrevViewHandle(),
                            targets.velocityTextureViewHandle(),
                            targets.depthTextureViewHandle(),
                            targets.materialAuxTextureViewHandle())) {
        return;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.sceneResolvedTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "TemporalResolve";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiCommandList* commandListStorage = ctx.shared->commandListPool->acquire(RhiCommandListType::Graphics);
    if (commandListStorage == nullptr ||
        !commandListStorage->begin({"RenderPass.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& commandList = *commandListStorage;
    targets.transitionTexture(commandList,
                              targets.sceneResolvedTextureHandle(),
                              RhiResourceState::TransferSrc);
    targets.transitionTexture(commandList,
                              targets.temporalCurrentTextureHandle(),
                              RhiResourceState::TransferDst);
    RhiTextureBlit temporalCopy;
    temporalCopy.src = targets.sceneResolvedTextureHandle();
    temporalCopy.dst = targets.temporalCurrentTextureHandle();
    commandList.blitTexture(temporalCopy);
    targets.transitionTexture(commandList,
                              targets.temporalCurrentTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList,
                              targets.sceneResolvedTextureHandle(),
                              RhiResourceState::RenderTarget);
    commandList.beginRendering(renderingInfo);
    commandList.setGraphicsPipeline(m_pipeline);
    commandList.setBindGroup(0u, m_bindGroup[historyPrevIndex]);

    // Convert bottom-left clip UV jitter to the native texture UV delta stored by temporal passes.
    glm::vec2 textureJitter = ctx.jitter.projectionOffset;
    if (rhiDevice.backend() == RhiBackend::Vulkan) {
        textureJitter.y = -textureJitter.y;
    }
    const glm::vec4 pushConstants[2] = {
        glm::vec4(static_cast<float>(std::max(1, targets.width())),
                  static_cast<float>(std::max(1, targets.height())),
                  textureJitter.x,
                  textureJitter.y),
        glm::vec4(ctx.weather.surfaceWetness,
                  settings.weather.rainLinesEnabled ? 1.0f : 0.0f,
                  0.0f,
                  0.0f)
    };
    commandList.pushConstants(pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    targets.transitionTexture(commandList,
                              targets.sceneResolvedTextureHandle(),
                              RhiResourceState::ShaderRead);
    if (!commandList.end()) {
        std::abort();
    }
    {
        RhiCommandList* submittedCommandLists[] = {&commandList};
        if (!rhiDevice.submit({"RenderPass.Submit", submittedCommandLists, 1u})) {
            std::abort();
        }
    }
}

bool TemporalResolvePass::ensureRhiPipeline(RhiDevice& rhiDevice) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyRhiResources();
    }
    if (m_pipeline.isValid()) {
        return true;
    }
    m_rhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/temporal_resolve.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "TemporalResolve.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_vertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "TemporalResolve.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_vertexShader.isValid() || !m_fragmentShader.isValid()) {
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
    bindGroupLayoutDesc.debugName = "TemporalResolve.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 5u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    m_bindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_bindGroupLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "TemporalResolve.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4) * 2u);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_pipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_pipelineLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "TemporalResolve.Pipeline";
    pipelineDesc.vertexShader = m_vertexShader;
    pipelineDesc.fragmentShader = m_fragmentShader;
    pipelineDesc.layout = m_pipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.blend.attachments.push_back({});
    m_pipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_pipeline.isValid()) {
        destroyRhiResources();
        return false;
    }

    return true;
}

bool TemporalResolvePass::ensureRhiBindGroup(RhiDevice& rhiDevice,
                                             const int historyPrevIndex,
                                             const RhiTextureViewHandle currentView,
                                             const RhiTextureViewHandle historyView,
                                             const RhiTextureViewHandle velocityView,
                                             const RhiTextureViewHandle depthView,
                                             const RhiTextureViewHandle materialAuxView) {
    if (!ensureRhiPipeline(rhiDevice) ||
        historyPrevIndex < 0 ||
        historyPrevIndex >= 2 ||
        !currentView.isValid() ||
        !historyView.isValid() ||
        !velocityView.isValid() ||
        !depthView.isValid() ||
        !materialAuxView.isValid()) {
        return false;
    }

    if (m_bindGroup[historyPrevIndex].isValid() &&
        sameTextureView(m_boundCurrentView[historyPrevIndex], currentView) &&
        sameTextureView(m_boundHistoryView[historyPrevIndex], historyView) &&
        sameTextureView(m_boundVelocityView[historyPrevIndex], velocityView) &&
        sameTextureView(m_boundDepthView[historyPrevIndex], depthView) &&
        sameTextureView(m_boundMaterialAuxView[historyPrevIndex], materialAuxView)) {
        return true;
    }

    if (m_bindGroup[historyPrevIndex].isValid()) {
        rhiDevice.destroyBindGroup(m_bindGroup[historyPrevIndex]);
        m_bindGroup[historyPrevIndex] = {};
    }

    const RhiTextureViewHandle views[5] = {
        currentView,
        historyView,
        velocityView,
        depthView,
        materialAuxView
    };

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_bindGroupLayout;
    for (uint32_t binding = 0u; binding < 5u; ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler = m_sampler;
        bindGroupDesc.entries.push_back(entry);
    }

    m_bindGroup[historyPrevIndex] = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_bindGroup[historyPrevIndex].isValid()) {
        m_boundCurrentView[historyPrevIndex] = {};
        m_boundHistoryView[historyPrevIndex] = {};
        m_boundVelocityView[historyPrevIndex] = {};
        m_boundDepthView[historyPrevIndex] = {};
        m_boundMaterialAuxView[historyPrevIndex] = {};
        return false;
    }

    m_boundCurrentView[historyPrevIndex] = currentView;
    m_boundHistoryView[historyPrevIndex] = historyView;
    m_boundVelocityView[historyPrevIndex] = velocityView;
    m_boundDepthView[historyPrevIndex] = depthView;
    m_boundMaterialAuxView[historyPrevIndex] = materialAuxView;
    return true;
}

void TemporalResolvePass::destroyRhiBindGroup() {
    if (m_rhiDevice != nullptr) {
        for (RhiBindGroupHandle& bindGroup : m_bindGroup) {
            if (bindGroup.isValid()) {
                m_rhiDevice->destroyBindGroup(bindGroup);
            }
            bindGroup = {};
        }
    } else {
        m_bindGroup[0] = {};
        m_bindGroup[1] = {};
    }

    for (int i = 0; i < 2; ++i) {
        m_boundCurrentView[i] = {};
        m_boundHistoryView[i] = {};
        m_boundVelocityView[i] = {};
        m_boundDepthView[i] = {};
        m_boundMaterialAuxView[i] = {};
    }
}

void TemporalResolvePass::destroyRhiResources() {
    destroyRhiBindGroup();
    if (m_rhiDevice != nullptr) {
        if (m_pipeline.isValid()) {
            m_rhiDevice->destroyPipeline(m_pipeline);
        }
        if (m_vertexShader.isValid()) {
            m_rhiDevice->destroyShader(m_vertexShader);
        }
        if (m_fragmentShader.isValid()) {
            m_rhiDevice->destroyShader(m_fragmentShader);
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

    m_pipeline = {};
    m_vertexShader = {};
    m_fragmentShader = {};
    m_pipelineLayout = {};
    m_bindGroupLayout = {};
    m_sampler = {};
    m_rhiDevice = nullptr;
}
