#include "MotionBlurPass.h"

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

void MotionBlurPass::init(ResourceMgr&) {}

void MotionBlurPass::shutdown() {
    destroyRhiResources();
}

RgPassHandle MotionBlurPass::addGraphPasses(
    RenderGraph& graph,
    const FrameContext& ctx,
    const RenderSettings& settings,
    DeferredRenderTargets& targets,
    const GraphResources& resources,
    const RgPassHandle dependency) {
    if (!dependency.isValid() || !resources.sceneResolved.isValid() ||
        !resources.temporalCurrent.isValid() || !resources.velocity.isValid() ||
        !resources.depth.isValid()) {
        return {};
    }

    const FrameContext* frame = &ctx;
    DeferredRenderTargets* frameTargets = &targets;
    // Scene color ping-pong: blur samples the current chain buffer and
    // renders into the other one, replacing the former scratch snapshot blit.
    const int inputIndex = targets.sceneColorIndex();
    const RgTextureHandle inputTexture = inputIndex == 0
        ? resources.sceneResolved : resources.temporalCurrent;
    const RgTextureHandle outputTexture = inputIndex == 0
        ? resources.temporalCurrent : resources.sceneResolved;

    RenderGraphPassBuilder blur = graph.addPass(
        {"MotionBlur.Resolve", RgPassType::Graphics,
         RhiQueueType::Graphics, /*threadSafeRecord=*/true});
    blur.dependsOn(dependency)
        .readTexture(inputTexture, RhiResourceState::ShaderRead)
        .readTexture(resources.velocity, RhiResourceState::ShaderRead)
        .readTexture(resources.depth, RhiResourceState::DepthRead)
        .writeTexture(outputTexture, RhiResourceState::RenderTarget)
        .setExecute([this, frame, frameTargets, settings,
                     inputIndex](RgPassContext& pass) {
            return recordBlur(
                pass.commandList(), *frame, settings, *frameTargets,
                inputIndex);
        });
    targets.flipSceneColor();
    return blur.handle();
}

bool MotionBlurPass::recordBlur(RhiCommandList& commandList,
                                const FrameContext& ctx,
                                const RenderSettings& settings,
                                DeferredRenderTargets& targets,
                                const int inputIndex) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        inputIndex < 0 || inputIndex > 1) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!targets.ensureSceneResolvedTextureView(rhiDevice) ||
        !targets.ensureTemporalCurrentTextureView(rhiDevice) ||
        !targets.ensureVelocityTextureView(rhiDevice) ||
        !targets.ensureGBufferTextureViews(rhiDevice) ||
        !ensureRhiPipeline(rhiDevice) ||
        !ensureRhiBindGroup(rhiDevice,
                            inputIndex,
                            targets.sceneColorTextureViewHandle(inputIndex),
                            targets.velocityTextureViewHandle(),
                            targets.depthTextureViewHandle())) {
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.sceneColorTextureViewHandle(1 - inputIndex);
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "MotionBlur";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    commandList.beginRendering(renderingInfo);
    commandList.setGraphicsPipeline(m_pipeline);
    commandList.setBindGroup(0u, m_bindGroup[inputIndex]);

    const glm::vec4 pushConstants(
        settings.postProcess.motionBlurStrength,
        static_cast<float>(std::max(1, settings.postProcess.motionBlurSamples)),
        static_cast<float>(std::max(1, targets.width())),
        static_cast<float>(std::max(1, targets.height())));
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    return true;
}

bool MotionBlurPass::ensureRhiPipeline(RhiDevice& rhiDevice) {
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
        renderer::rhi::loadShaderSource("assets/shaders/motion_blur.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "MotionBlur.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_vertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "MotionBlur.Fragment";
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
    bindGroupLayoutDesc.debugName = "MotionBlur.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 3u; ++binding) {
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
    pipelineLayoutDesc.debugName = "MotionBlur.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_pipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_pipelineLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "MotionBlur.Pipeline";
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

bool MotionBlurPass::ensureRhiBindGroup(RhiDevice& rhiDevice,
                                        const int historyIndex,
                                        const RhiTextureViewHandle sceneView,
                                        const RhiTextureViewHandle velocityView,
                                        const RhiTextureViewHandle depthView) {
    if (!ensureRhiPipeline(rhiDevice) ||
        historyIndex < 0 ||
        historyIndex >= 2 ||
        !sceneView.isValid() ||
        !velocityView.isValid() ||
        !depthView.isValid()) {
        return false;
    }

    if (m_bindGroup[historyIndex].isValid() &&
        sameTextureView(m_boundSceneView[historyIndex], sceneView) &&
        sameTextureView(m_boundVelocityView[historyIndex], velocityView) &&
        sameTextureView(m_boundDepthView[historyIndex], depthView)) {
        return true;
    }

    if (m_bindGroup[historyIndex].isValid()) {
        rhiDevice.destroyBindGroup(m_bindGroup[historyIndex]);
        m_bindGroup[historyIndex] = {};
    }

    const RhiTextureViewHandle views[3] = {
        sceneView,
        velocityView,
        depthView
    };

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_bindGroupLayout;
    for (uint32_t binding = 0u; binding < 3u; ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler = m_sampler;
        bindGroupDesc.entries.push_back(entry);
    }

    m_bindGroup[historyIndex] = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_bindGroup[historyIndex].isValid()) {
        m_boundSceneView[historyIndex] = {};
        m_boundVelocityView[historyIndex] = {};
        m_boundDepthView[historyIndex] = {};
        return false;
    }

    m_boundSceneView[historyIndex] = sceneView;
    m_boundVelocityView[historyIndex] = velocityView;
    m_boundDepthView[historyIndex] = depthView;
    return true;
}

void MotionBlurPass::destroyRhiBindGroup() {
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
        m_boundSceneView[i] = {};
        m_boundVelocityView[i] = {};
        m_boundDepthView[i] = {};
    }
}

void MotionBlurPass::destroyRhiResources() {
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
