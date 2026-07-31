#include "TemporalResolvePass.h"

#include "../core/RenderScene.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../targets/DeferredRenderTargets.h"

#include <algorithm>
#include <optional>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace {
struct TemporalResolvePushConstants {
    glm::mat4 currentInvViewProj;
    glm::mat4 previousJitteredViewProj;
    glm::vec4 screenSizeJitter;
    glm::vec4 temporalParams;
};

[[nodiscard]] bool sameTextureView(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
} // namespace

void TemporalResolvePass::init(ResourceMgr&) {}

void TemporalResolvePass::shutdown() {
    destroyRhiResources();
}

RgPassHandle TemporalResolvePass::addGraphPasses(RenderGraph& graph, const FrameContext& ctx,
                                                 const RenderSettings& settings, DeferredRenderTargets& targets,
                                                 const GraphResources& resources, const RgPassHandle dependency) {
    if (!dependency.isValid() || !resources.sceneResolved.isValid() || !resources.temporalCurrent.isValid() ||
        !resources.historyPrevious.isValid() || !resources.velocity.isValid() ||
        !resources.historyDepthPrevious.isValid() || !resources.depth.isValid() ||
        !resources.transparentDepth.isValid() || !resources.reactiveMask.isValid() ||
        !resources.transparencyMask.isValid() || !resources.materialAux.isValid()) {
        return {};
    }

    const FrameContext* frame = &ctx;
    DeferredRenderTargets* frameTargets = &targets;
    // Scene color ping-pong: sample the current chain buffer directly and
    // resolve into the other one instead of snapshotting through a copy.
    const int inputIndex = targets.sceneColorIndex();
    const RgTextureHandle inputTexture = inputIndex == 0 ? resources.sceneResolved : resources.temporalCurrent;
    const RgTextureHandle outputTexture = inputIndex == 0 ? resources.temporalCurrent : resources.sceneResolved;

    RenderGraphPassBuilder resolve =
        graph.addPass({"TemporalResolve.Resolve", RgPassType::Graphics, RhiQueueType::Graphics});
    resolve.dependsOn(dependency)
        .readTexture(inputTexture, RhiResourceState::ShaderRead)
        .readTexture(resources.historyPrevious, RhiResourceState::ShaderRead)
        .readTexture(resources.historyDepthPrevious, RhiResourceState::DepthRead)
        .readTexture(resources.velocity, RhiResourceState::ShaderRead)
        .readTexture(resources.depth, RhiResourceState::DepthRead)
        .readTexture(resources.transparentDepth, RhiResourceState::DepthRead)
        .readTexture(resources.reactiveMask, RhiResourceState::ShaderRead)
        .readTexture(resources.transparencyMask, RhiResourceState::ShaderRead)
        .readTexture(resources.materialAux, RhiResourceState::ShaderRead)
        .writeTexture(outputTexture, RhiResourceState::RenderTarget)
        .setExecute([this, frame, frameTargets, settings, inputIndex](RgPassContext& pass) {
            return recordResolve(pass.commandList(), *frame, settings, *frameTargets, inputIndex);
        });
    targets.flipSceneColor();
    return resolve.handle();
}

bool TemporalResolvePass::recordResolve(RhiCommandList& commandList, const FrameContext& ctx,
                                        const RenderSettings& settings, DeferredRenderTargets& targets,
                                        const int inputIndex) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr || inputIndex < 0 || inputIndex > 1) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!targets.ensureSceneResolvedTextureView(rhiDevice) || !targets.ensureTemporalCurrentTextureView(rhiDevice) ||
        !targets.ensureHistorySceneTextureViews(rhiDevice) || !targets.ensureTaaHistoryDepthTextureViews(rhiDevice) ||
        !targets.ensureVelocityTextureView(rhiDevice) || !targets.ensureGBufferTextureViews(rhiDevice) ||
        !targets.ensureTransparentCompositeTextureViews(rhiDevice) ||
        !targets.ensureReactiveMaskTextureView(rhiDevice) || !targets.ensureTransparencyMaskTextureView(rhiDevice) ||
        !ensureRhiPipeline(rhiDevice)) {
        return false;
    }

    const int historyPrevIndex = 1 - targets.currentHistoryIndex();
    if (!ensureRhiBindGroup(rhiDevice, historyPrevIndex, targets.sceneColorTextureViewHandle(inputIndex),
                            targets.historySceneTexturePrevViewHandle(), targets.taaHistoryDepthTexturePrevViewHandle(),
                            targets.velocityTextureViewHandle(), targets.depthTextureViewHandle(),
                            targets.transparentCompositeDepthTextureViewHandle(),
                            targets.reactiveMaskTextureViewHandle(), targets.transparencyMaskTextureViewHandle(),
                            targets.materialAuxTextureViewHandle())) {
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.sceneColorTextureViewHandle(1 - inputIndex);
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "TemporalResolve";
    renderingInfo.renderArea = {0, 0, static_cast<uint32_t>(std::max(1, targets.width())),
                                static_cast<uint32_t>(std::max(1, targets.height()))};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    commandList.beginRendering(renderingInfo);
    commandList.setGraphicsPipeline(m_pipeline);
    commandList.setBindGroup(0u, m_bindGroup[historyPrevIndex]);

    // Convert bottom-left clip UV jitter to the native texture UV delta stored by temporal passes.
    glm::vec2 textureJitter = ctx.jitter.projectionOffset;
    if (rhiDevice.backend() == RhiBackend::Vulkan) {
        textureJitter.y = -textureJitter.y;
    }
    const bool projectionJitter = usesTemporalProjectionJitter(settings.upscale.type, settings.taa.enabled);
    const TemporalResolvePushConstants pushConstants{
        projectionJitter ? ctx.camera.jitteredInvViewProj : ctx.camera.invViewProj, ctx.previousJitteredViewProj,
        glm::vec4(static_cast<float>(std::max(1, targets.width())), static_cast<float>(std::max(1, targets.height())),
                  textureJitter.x, textureJitter.y),
        glm::vec4(ctx.weather.surfaceWetness, settings.weather.rainLinesEnabled ? 1.0f : 0.0f, ctx.prevCamera.nearPlane,
                  ctx.prevCamera.farPlane)};
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    return true;
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
    for (uint32_t binding = 0u; binding < 9u; ++binding) {
        bindGroupLayoutDesc.entries.push_back(
            {binding, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    }
    m_bindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_bindGroupLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "TemporalResolve.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(TemporalResolvePushConstants));
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

bool TemporalResolvePass::ensureRhiBindGroup(
    RhiDevice& rhiDevice, const int historyPrevIndex, const RhiTextureViewHandle currentView,
    const RhiTextureViewHandle historyView, const RhiTextureViewHandle historyDepthView,
    const RhiTextureViewHandle velocityView, const RhiTextureViewHandle depthView,
    const RhiTextureViewHandle transparentDepthView, const RhiTextureViewHandle reactiveMaskView,
    const RhiTextureViewHandle transparencyMaskView, const RhiTextureViewHandle materialAuxView) {
    if (!ensureRhiPipeline(rhiDevice) || historyPrevIndex < 0 || historyPrevIndex >= 2 || !currentView.isValid() ||
        !historyView.isValid() || !historyDepthView.isValid() || !velocityView.isValid() || !depthView.isValid() ||
        !transparentDepthView.isValid() || !reactiveMaskView.isValid() || !transparencyMaskView.isValid() ||
        !materialAuxView.isValid()) {
        return false;
    }

    if (m_bindGroup[historyPrevIndex].isValid() && sameTextureView(m_boundCurrentView[historyPrevIndex], currentView) &&
        sameTextureView(m_boundHistoryView[historyPrevIndex], historyView) &&
        sameTextureView(m_boundHistoryDepthView[historyPrevIndex], historyDepthView) &&
        sameTextureView(m_boundVelocityView[historyPrevIndex], velocityView) &&
        sameTextureView(m_boundDepthView[historyPrevIndex], depthView) &&
        sameTextureView(m_boundTransparentDepthView[historyPrevIndex], transparentDepthView) &&
        sameTextureView(m_boundReactiveMaskView[historyPrevIndex], reactiveMaskView) &&
        sameTextureView(m_boundTransparencyMaskView[historyPrevIndex], transparencyMaskView) &&
        sameTextureView(m_boundMaterialAuxView[historyPrevIndex], materialAuxView)) {
        return true;
    }

    if (m_bindGroup[historyPrevIndex].isValid()) {
        rhiDevice.destroyBindGroup(m_bindGroup[historyPrevIndex]);
        m_bindGroup[historyPrevIndex] = {};
    }

    const RhiTextureViewHandle views[9] = {currentView,    historyView,          historyDepthView, velocityView,
                                           depthView,      transparentDepthView, reactiveMaskView, transparencyMaskView,
                                           materialAuxView};

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_bindGroupLayout;
    for (uint32_t binding = 0u; binding < 9u; ++binding) {
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
        m_boundHistoryDepthView[historyPrevIndex] = {};
        m_boundVelocityView[historyPrevIndex] = {};
        m_boundDepthView[historyPrevIndex] = {};
        m_boundTransparentDepthView[historyPrevIndex] = {};
        m_boundReactiveMaskView[historyPrevIndex] = {};
        m_boundTransparencyMaskView[historyPrevIndex] = {};
        m_boundMaterialAuxView[historyPrevIndex] = {};
        return false;
    }

    m_boundCurrentView[historyPrevIndex] = currentView;
    m_boundHistoryView[historyPrevIndex] = historyView;
    m_boundHistoryDepthView[historyPrevIndex] = historyDepthView;
    m_boundVelocityView[historyPrevIndex] = velocityView;
    m_boundDepthView[historyPrevIndex] = depthView;
    m_boundTransparentDepthView[historyPrevIndex] = transparentDepthView;
    m_boundReactiveMaskView[historyPrevIndex] = reactiveMaskView;
    m_boundTransparencyMaskView[historyPrevIndex] = transparencyMaskView;
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
        m_boundHistoryDepthView[i] = {};
        m_boundVelocityView[i] = {};
        m_boundDepthView[i] = {};
        m_boundTransparentDepthView[i] = {};
        m_boundReactiveMaskView[i] = {};
        m_boundTransparencyMaskView[i] = {};
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
