#include "SsgiPass.h"
#include "../core/RenderScene.h"
#include "../debug/RenderDebugService.h"
#include "../targets/DeferredRenderTargets.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../../resource/ResourceMgr.h"

#include <algorithm>
#include <cstddef>
#include <glm/glm.hpp>
#include <optional>

namespace {
[[nodiscard]] bool sameTextureView(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

template <size_t Count>
[[nodiscard]] bool sameTextureViews(const std::array<RhiTextureViewHandle, Count>& lhs,
                                    const std::array<RhiTextureViewHandle, Count>& rhs) {
    for (size_t index = 0u; index < lhs.size(); ++index) {
        if (!sameTextureView(lhs[index], rhs[index])) {
            return false;
        }
    }
    return true;
}

struct alignas(16) SsgiBaseParams {
    glm::mat4 viewProj;
    glm::mat4 invViewProj;
    glm::vec4 cameraPosRadius;
    glm::vec4 halfResolutionStrengthMaxDistance;
    glm::vec4 quality;
    glm::ivec4 controls;
};
static_assert(sizeof(SsgiBaseParams) == 192u);
} // namespace

void SsgiPass::init(ResourceMgr& resourceMgr) {
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
}

void SsgiPass::shutdown() {
    destroyBaseRhiResources();
    destroyUpsampleRhiResources();
    destroyTemporalRhiResources();
    destroyDenoiseRhiResources();
    destroyNoiseTextureView();
    m_noiseTexture = {};
}

RgPassHandle SsgiPass::addGraphPasses(RenderGraph& graph,
                                      const FrameContext& ctx,
                                      const RenderSettings& settings,
                                      DeferredRenderTargets& targets,
                                      const GraphResources& resources,
                                      const RgPassHandle dependency) {
    const bool temporalActive =
        settings.ssgi.temporalEnabled && !ctx.temporalReset;
    const int denoiseIterations = settings.ssgi.denoiseEnabled
        ? std::clamp(settings.ssgi.denoiseIterations, 0, 4)
        : 0;
    if (!dependency.isValid() || !resources.sceneLighting.isValid() ||
        !resources.albedo.isValid() || !resources.normalAo.isValid() ||
        !resources.materialAux.isValid() || !resources.depth.isValid() ||
        !resources.noise.isValid() || !resources.halfRes.isValid() ||
        !resources.output.isValid() ||
        (denoiseIterations > 0 && !resources.denoise[0].isValid()) ||
        (denoiseIterations > 1 && !resources.denoise[1].isValid()) ||
        (temporalActive &&
         (!resources.velocity.isValid() ||
          !resources.historyDepthPrevious.isValid() ||
          !resources.historyPrevious.isValid() ||
          !resources.momentsHistoryPrevious.isValid() ||
          !resources.temporal.isValid() ||
          !resources.temporalMoments.isValid() ||
          !resources.historyCurrent.isValid() ||
          !resources.momentsHistoryCurrent.isValid()))) {
        return {};
    }

    const FrameContext* frame = &ctx;
    DeferredRenderTargets* frameTargets = &targets;
    RenderGraphPassBuilder base = graph.addPass(
        {"SSGI.Base", RgPassType::Graphics, RhiQueueType::Graphics,
         /*threadSafeRecord=*/true});
    base.dependsOn(dependency)
        .readTexture(resources.sceneLighting, RhiResourceState::ShaderRead)
        .readTexture(resources.albedo, RhiResourceState::ShaderRead)
        .readTexture(resources.normalAo, RhiResourceState::ShaderRead)
        .readTexture(resources.materialAux, RhiResourceState::ShaderRead)
        .readTexture(resources.depth, RhiResourceState::DepthRead)
        .readTexture(resources.noise, RhiResourceState::ShaderRead)
        .writeTexture(resources.halfRes, RhiResourceState::RenderTarget)
        .setExecute([this, frame, frameTargets, settings](RgPassContext& pass) {
            return recordSsgiBase(
                pass.commandList(), *frame, settings, *frameTargets);
        });
    RgPassHandle previous = base.handle();

    RenderGraphPassBuilder upsample = graph.addPass(
        {"SSGI.Upsample", RgPassType::Graphics, RhiQueueType::Graphics,
         /*threadSafeRecord=*/true});
    upsample.dependsOn(previous)
        .readTexture(resources.halfRes, RhiResourceState::ShaderRead)
        .readTexture(resources.depth, RhiResourceState::DepthRead)
        .writeTexture(resources.output, RhiResourceState::RenderTarget)
        .setExecute([this, frame, frameTargets](RgPassContext& pass) {
            return recordSsgiUpsample(
                pass.commandList(), *frame, *frameTargets);
        });
    previous = upsample.handle();

    if (temporalActive) {
        RenderGraphPassBuilder temporal = graph.addPass(
            {"SSGI.Temporal", RgPassType::Graphics, RhiQueueType::Graphics,
             /*threadSafeRecord=*/true});
        temporal.dependsOn(previous)
            .readTexture(resources.output, RhiResourceState::ShaderRead)
            .readTexture(resources.historyPrevious,
                         RhiResourceState::ShaderRead)
            .readTexture(resources.velocity, RhiResourceState::ShaderRead)
            .readTexture(resources.depth, RhiResourceState::DepthRead)
            .readTexture(resources.normalAo, RhiResourceState::ShaderRead)
            .readTexture(resources.historyDepthPrevious,
                         RhiResourceState::ShaderRead)
            .readTexture(resources.momentsHistoryPrevious,
                         RhiResourceState::ShaderRead)
            .writeTexture(resources.temporal, RhiResourceState::RenderTarget)
            .writeTexture(resources.temporalMoments,
                          RhiResourceState::RenderTarget)
            .setExecute([this, frame, frameTargets, settings](RgPassContext& pass) {
                return recordSsgiTemporal(
                    pass.commandList(), *frame, settings.ssgi, *frameTargets);
            });
        previous = temporal.handle();

        RenderGraphPassBuilder historyCopy = graph.addPass(
            {"SSGI.HistoryCopy", RgPassType::Copy, RhiQueueType::Graphics,
             /*threadSafeRecord=*/true});
        historyCopy.dependsOn(previous)
            .readTexture(resources.temporal, RhiResourceState::TransferSrc)
            .readTexture(resources.temporalMoments,
                         RhiResourceState::TransferSrc)
            .writeTexture(resources.historyCurrent,
                          RhiResourceState::TransferDst)
            .writeTexture(resources.momentsHistoryCurrent,
                          RhiResourceState::TransferDst)
            .setExecute([this, frame, frameTargets](RgPassContext& pass) {
                return recordSsgiHistoryCopy(
                    pass.commandList(), *frame, *frameTargets);
            });
        previous = historyCopy.handle();
    }

    constexpr const char* kDenoisePassNames[4] = {
        "SSGI.Denoise[0]", "SSGI.Denoise[1]",
        "SSGI.Denoise[2]", "SSGI.Denoise[3]"
    };
    for (int iteration = 0; iteration < denoiseIterations; ++iteration) {
        const int outputSlot = iteration & 1;
        const RgTextureHandle input = iteration == 0
            ? (temporalActive ? resources.temporal : resources.output)
            : resources.denoise[1 - outputSlot];
        RenderGraphPassBuilder denoise = graph.addPass(
            {kDenoisePassNames[iteration], RgPassType::Graphics,
             RhiQueueType::Graphics, /*threadSafeRecord=*/true});
        denoise.dependsOn(previous)
            .readTexture(input, RhiResourceState::ShaderRead)
            .readTexture(resources.depth, RhiResourceState::DepthRead)
            .readTexture(resources.normalAo, RhiResourceState::ShaderRead)
            .writeTexture(resources.denoise[outputSlot],
                          RhiResourceState::RenderTarget);
        if (temporalActive) {
            denoise.readTexture(resources.temporalMoments,
                                RhiResourceState::ShaderRead);
        }
        denoise.setExecute(
            [this, frame, frameTargets, settings, temporalActive, iteration](
                RgPassContext& pass) {
                return recordSsgiDenoiseIteration(
                    pass.commandList(), *frame, settings.ssgi, *frameTargets,
                    temporalActive, iteration);
            });
        previous = denoise.handle();
    }

    if (denoiseIterations > 0 || temporalActive) {
        const RgTextureHandle copySource = denoiseIterations > 0
            ? resources.denoise[(denoiseIterations - 1) & 1]
            : resources.temporal;
        const RhiTextureHandle copySourceTexture = denoiseIterations > 0
            ? targets.ssgiDenoiseTextureHandle((denoiseIterations - 1) & 1)
            : targets.ssgiTemporalTextureHandle();
        RenderGraphPassBuilder outputCopy = graph.addPass(
            {"SSGI.OutputCopy", RgPassType::Copy, RhiQueueType::Graphics,
             /*threadSafeRecord=*/true});
        outputCopy.dependsOn(previous)
            .readTexture(copySource, RhiResourceState::TransferSrc)
            .writeTexture(resources.output, RhiResourceState::TransferDst)
            .setExecute(
                [this, frame, frameTargets, copySourceTexture](
                    RgPassContext& pass) {
                    return recordSsgiOutputCopy(
                        pass.commandList(), *frame, *frameTargets,
                        copySourceTexture);
                });
        previous = outputCopy.handle();
    }

    return previous;
}

bool SsgiPass::recordSsgiBase(RhiCommandList& commandList,
                              const FrameContext& ctx,
                              const RenderSettings& settings,
                              DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsgiHalfResTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureSceneLightingTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice)) {
        return false;
    }
    const SsgiSettings& ssgi = settings.ssgi;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!ensureNoiseTextureView(rhiDevice)) {
        return false;
    }
    const std::array<RhiTextureViewHandle, 6> views = {
        targets.sceneLightingTextureViewHandle(),
        targets.albedoTextureViewHandle(),
        targets.normalAoTextureViewHandle(),
        targets.materialAuxTextureViewHandle(),
        targets.depthTextureViewHandle(),
        m_noiseTextureView
    };
    if (!ensureBaseRhiPipeline(rhiDevice) || !ensureBaseBindGroup(rhiDevice, views)) {
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.ssgiHalfResTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 0.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SsgiHalfRes";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.halfWidth())),
        static_cast<uint32_t>(std::max(1, targets.halfHeight()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    const int halfW = std::max(1, targets.width() / 2);
    const int halfH = std::max(1, targets.height() / 2);
    SsgiBaseParams params{};
    const bool projectionJitter = usesTemporalProjectionJitter(
        settings.upscale.type, settings.taa.enabled);
    params.viewProj = projectionJitter
        ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    params.invViewProj = projectionJitter
        ? ctx.camera.jitteredInvViewProj
        : ctx.camera.invViewProj;
    params.cameraPosRadius = glm::vec4(ctx.camera.position, ssgi.radius);
    params.halfResolutionStrengthMaxDistance = glm::vec4(
        static_cast<float>(halfW),
        static_cast<float>(halfH),
        ssgi.strength,
        ssgi.maxDistance);
    params.quality = glm::vec4(ssgi.thickness,
                               ssgi.radianceFilterStrength,
                               ssgi.colorBleedStrength,
                               0.0f);
    params.controls = glm::ivec4(std::clamp(ssgi.samples, 1, 32),
                                 static_cast<int>(ctx.frameIndex & 0x7fffffffULL),
                                 0,
                                 0);

    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Ssgi)
        : GpuTimerSegmentToken{};
    commandList.bufferBarrier({m_baseUniformBuffer, RhiResourceState::UniformBuffer,
                               RhiResourceState::TransferDst});
    commandList.updateBuffer(m_baseUniformBuffer, 0u, &params, sizeof(params));
    commandList.bufferBarrier({m_baseUniformBuffer, RhiResourceState::TransferDst,
                               RhiResourceState::UniformBuffer});
    commandList.beginRendering(renderingInfo);
    commandList.setGraphicsPipeline(m_basePipeline);
    commandList.setBindGroup(0u, m_baseBindGroup);
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool SsgiPass::ensureNoiseTextureView(RhiDevice& rhiDevice) {
    if (m_noiseViewDevice != nullptr && m_noiseViewDevice != &rhiDevice) {
        destroyNoiseTextureView();
    }
    if (m_noiseTextureView.isValid()) {
        return true;
    }
    if (!m_noiseTexture.isValid()) {
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = m_noiseTexture;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = RhiTextureFormat::Rgba8Unorm;
    viewDesc.baseMip = 0u;
    viewDesc.mipCount = 1u;
    viewDesc.baseLayer = 0u;
    viewDesc.layerCount = 1u;
    m_noiseTextureView = rhiDevice.createTextureView(viewDesc);
    if (!m_noiseTextureView.isValid()) {
        return false;
    }

    m_noiseViewDevice = &rhiDevice;
    return true;
}

void SsgiPass::destroyNoiseTextureView() {
    if (m_noiseViewDevice != nullptr && m_noiseTextureView.isValid()) {
        m_noiseViewDevice->destroyTextureView(m_noiseTextureView);
    }
    m_noiseTextureView = {};
    m_noiseViewDevice = nullptr;
}

bool SsgiPass::ensureBaseRhiPipeline(RhiDevice& rhiDevice) {
    if (m_baseRhiDevice != nullptr && m_baseRhiDevice != &rhiDevice) {
        destroyBaseRhiResources();
    }
    if (m_basePipeline.isValid()) {
        return true;
    }
    m_baseRhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/ssgi.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "SsgiBase.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_baseVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "SsgiBase.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_baseFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_baseVertexShader.isValid() || !m_baseFragmentShader.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    RhiBufferDesc uniformBufferDesc;
    uniformBufferDesc.debugName = "SsgiBase.Params";
    uniformBufferDesc.size = sizeof(SsgiBaseParams);
    uniformBufferDesc.usage = rhiFlag(RhiBufferUsage::Uniform) |
                              rhiFlag(RhiBufferUsage::TransferDst);
    uniformBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    uniformBufferDesc.initialState = RhiResourceState::UniformBuffer;
    m_baseUniformBuffer = rhiDevice.createBuffer(uniformBufferDesc, nullptr, 0u);
    if (!m_baseUniformBuffer.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    auto createSampler = [&](const RhiFilter filter, const RhiAddressMode addressMode) {
        RhiSamplerDesc samplerDesc;
        samplerDesc.minFilter = filter;
        samplerDesc.magFilter = filter;
        samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
        samplerDesc.addressU = addressMode;
        samplerDesc.addressV = addressMode;
        samplerDesc.addressW = addressMode;
        return rhiDevice.createSampler(samplerDesc);
    };
    m_baseNearestSampler = createSampler(RhiFilter::Nearest, RhiAddressMode::ClampToEdge);
    m_baseLinearSampler = createSampler(RhiFilter::Linear, RhiAddressMode::ClampToEdge);
    m_baseNoiseSampler = createSampler(RhiFilter::Linear, RhiAddressMode::Repeat);
    if (!m_baseNearestSampler.isValid() || !m_baseLinearSampler.isValid() ||
        !m_baseNoiseSampler.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "SsgiBase.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 6u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    bindGroupLayoutDesc.entries.push_back({
        6u,
        RhiBindingType::UniformBuffer,
        rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    m_baseBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_baseBindGroupLayout.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "SsgiBase.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_baseBindGroupLayout);
    m_basePipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_basePipelineLayout.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "SsgiBase.Pipeline";
    pipelineDesc.vertexShader = m_baseVertexShader;
    pipelineDesc.fragmentShader = m_baseFragmentShader;
    pipelineDesc.layout = m_basePipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.blend.attachments.push_back({});
    m_basePipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_basePipeline.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    return true;
}

bool SsgiPass::ensureBaseBindGroup(
    RhiDevice& rhiDevice,
    const std::array<RhiTextureViewHandle, 6>& views) {
    if (!ensureBaseRhiPipeline(rhiDevice)) {
        return false;
    }
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_baseBindGroup.isValid() && sameTextureViews(m_baseBoundViews, views)) {
        return true;
    }

    destroyBaseBindGroup();
    const RhiSamplerHandle samplers[6] = {
        m_baseLinearSampler,
        m_baseNearestSampler,
        m_baseNearestSampler,
        m_baseNearestSampler,
        m_baseNearestSampler,
        m_baseNoiseSampler
    };

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_baseBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler = samplers[binding];
        bindGroupDesc.entries.push_back(entry);
    }

    RhiBindGroupEntry uniformEntry;
    uniformEntry.binding = 6u;
    uniformEntry.resource.buffer.buffer = m_baseUniformBuffer;
    uniformEntry.resource.buffer.offset = 0u;
    uniformEntry.resource.buffer.range = sizeof(SsgiBaseParams);
    bindGroupDesc.entries.push_back(uniformEntry);

    m_baseBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_baseBindGroup.isValid()) {
        m_baseBoundViews = {};
        return false;
    }

    m_baseBoundViews = views;
    return true;
}

void SsgiPass::destroyBaseBindGroup() {
    if (m_baseRhiDevice != nullptr && m_baseBindGroup.isValid()) {
        m_baseRhiDevice->destroyBindGroup(m_baseBindGroup);
    }
    m_baseBindGroup = {};
    m_baseBoundViews = {};
}

void SsgiPass::destroyBaseRhiResources() {
    destroyBaseBindGroup();
    if (m_baseRhiDevice != nullptr) {
        if (m_basePipeline.isValid()) {
            m_baseRhiDevice->destroyPipeline(m_basePipeline);
        }
        if (m_baseVertexShader.isValid()) {
            m_baseRhiDevice->destroyShader(m_baseVertexShader);
        }
        if (m_baseFragmentShader.isValid()) {
            m_baseRhiDevice->destroyShader(m_baseFragmentShader);
        }
        if (m_basePipelineLayout.isValid()) {
            m_baseRhiDevice->destroyPipelineLayout(m_basePipelineLayout);
        }
        if (m_baseBindGroupLayout.isValid()) {
            m_baseRhiDevice->destroyBindGroupLayout(m_baseBindGroupLayout);
        }
        if (m_baseUniformBuffer.isValid()) {
            m_baseRhiDevice->destroyBuffer(m_baseUniformBuffer);
        }
        const RhiSamplerHandle samplers[] = {
            m_baseNearestSampler,
            m_baseLinearSampler,
            m_baseNoiseSampler
        };
        for (const RhiSamplerHandle sampler : samplers) {
            if (sampler.isValid()) {
                m_baseRhiDevice->destroySampler(sampler);
            }
        }
    }

    m_baseUniformBuffer = {};
    m_baseNearestSampler = {};
    m_baseLinearSampler = {};
    m_baseNoiseSampler = {};
    m_baseBindGroupLayout = {};
    m_basePipelineLayout = {};
    m_baseVertexShader = {};
    m_baseFragmentShader = {};
    m_basePipeline = {};
    m_baseRhiDevice = nullptr;
}

bool SsgiPass::recordSsgiUpsample(RhiCommandList& commandList,
                                  const FrameContext& ctx,
                                  DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsgiTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureSsgiHalfResTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice)) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const std::array<RhiTextureViewHandle, 2> views = {
        targets.ssgiHalfResTextureViewHandle(),
        targets.depthTextureViewHandle()
    };
    if (!ensureUpsampleRhiPipeline(rhiDevice) || !ensureUpsampleBindGroup(rhiDevice, views)) {
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.ssgiTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 0.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "Ssgi";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Ssgi)
        : GpuTimerSegmentToken{};
    commandList.beginRendering(renderingInfo);

    const int halfW = std::max(1, targets.width() / 2);
    const int halfH = std::max(1, targets.height() / 2);
    const glm::vec4 pushConstants(
        static_cast<float>(halfW),
        static_cast<float>(halfH),
        ctx.camera.nearPlane,
        0.0f);
    commandList.setGraphicsPipeline(m_upsamplePipeline);
    commandList.setBindGroup(0u, m_upsampleBindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool SsgiPass::ensureUpsampleRhiPipeline(RhiDevice& rhiDevice) {
    if (m_upsampleRhiDevice != nullptr && m_upsampleRhiDevice != &rhiDevice) {
        destroyUpsampleRhiResources();
    }
    if (m_upsamplePipeline.isValid()) {
        return true;
    }
    m_upsampleRhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/ssgi_upsample.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "SsgiUpsample.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_upsampleVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "SsgiUpsample.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_upsampleFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_upsampleVertexShader.isValid() || !m_upsampleFragmentShader.isValid()) {
        destroyUpsampleRhiResources();
        return false;
    }

    RhiSamplerDesc nearestSamplerDesc;
    nearestSamplerDesc.minFilter = RhiFilter::Nearest;
    nearestSamplerDesc.magFilter = RhiFilter::Nearest;
    nearestSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    nearestSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_upsampleNearestSampler = rhiDevice.createSampler(nearestSamplerDesc);

    RhiSamplerDesc linearSamplerDesc;
    linearSamplerDesc.minFilter = RhiFilter::Linear;
    linearSamplerDesc.magFilter = RhiFilter::Linear;
    linearSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    linearSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_upsampleLinearSampler = rhiDevice.createSampler(linearSamplerDesc);
    if (!m_upsampleNearestSampler.isValid() || !m_upsampleLinearSampler.isValid()) {
        destroyUpsampleRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "SsgiUpsample.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 2u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    m_upsampleBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_upsampleBindGroupLayout.isValid()) {
        destroyUpsampleRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "SsgiUpsample.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_upsampleBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_upsamplePipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_upsamplePipelineLayout.isValid()) {
        destroyUpsampleRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "SsgiUpsample.Pipeline";
    pipelineDesc.vertexShader = m_upsampleVertexShader;
    pipelineDesc.fragmentShader = m_upsampleFragmentShader;
    pipelineDesc.layout = m_upsamplePipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.blend.attachments.push_back({});
    m_upsamplePipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_upsamplePipeline.isValid()) {
        destroyUpsampleRhiResources();
        return false;
    }

    return true;
}

bool SsgiPass::ensureUpsampleBindGroup(
    RhiDevice& rhiDevice,
    const std::array<RhiTextureViewHandle, 2>& views) {
    if (!ensureUpsampleRhiPipeline(rhiDevice)) {
        return false;
    }
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_upsampleBindGroup.isValid() && sameTextureViews(m_upsampleBoundViews, views)) {
        return true;
    }

    destroyUpsampleBindGroup();
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_upsampleBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler =
            binding == 0u ? m_upsampleNearestSampler : m_upsampleLinearSampler;
        bindGroupDesc.entries.push_back(entry);
    }

    m_upsampleBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_upsampleBindGroup.isValid()) {
        m_upsampleBoundViews = {};
        return false;
    }

    m_upsampleBoundViews = views;
    return true;
}

void SsgiPass::destroyUpsampleBindGroup() {
    if (m_upsampleRhiDevice != nullptr && m_upsampleBindGroup.isValid()) {
        m_upsampleRhiDevice->destroyBindGroup(m_upsampleBindGroup);
    }
    m_upsampleBindGroup = {};
    m_upsampleBoundViews = {};
}

void SsgiPass::destroyUpsampleRhiResources() {
    destroyUpsampleBindGroup();
    if (m_upsampleRhiDevice != nullptr) {
        if (m_upsamplePipeline.isValid()) {
            m_upsampleRhiDevice->destroyPipeline(m_upsamplePipeline);
        }
        if (m_upsampleVertexShader.isValid()) {
            m_upsampleRhiDevice->destroyShader(m_upsampleVertexShader);
        }
        if (m_upsampleFragmentShader.isValid()) {
            m_upsampleRhiDevice->destroyShader(m_upsampleFragmentShader);
        }
        if (m_upsamplePipelineLayout.isValid()) {
            m_upsampleRhiDevice->destroyPipelineLayout(m_upsamplePipelineLayout);
        }
        if (m_upsampleBindGroupLayout.isValid()) {
            m_upsampleRhiDevice->destroyBindGroupLayout(m_upsampleBindGroupLayout);
        }
        if (m_upsampleNearestSampler.isValid()) {
            m_upsampleRhiDevice->destroySampler(m_upsampleNearestSampler);
        }
        if (m_upsampleLinearSampler.isValid()) {
            m_upsampleRhiDevice->destroySampler(m_upsampleLinearSampler);
        }
    }

    m_upsamplePipeline = {};
    m_upsampleVertexShader = {};
    m_upsampleFragmentShader = {};
    m_upsamplePipelineLayout = {};
    m_upsampleBindGroupLayout = {};
    m_upsampleNearestSampler = {};
    m_upsampleLinearSampler = {};
    m_upsampleRhiDevice = nullptr;
}

bool SsgiPass::recordSsgiDenoiseIteration(
    RhiCommandList& commandList,
    const FrameContext& ctx,
    const SsgiSettings& ssgi,
    DeferredRenderTargets& targets,
    const bool momentsEnabled,
    const int iteration) {
    if (iteration < 0 || iteration >= 4) {
        return false;
    }
    const int outputSlot = iteration & 1;
    const int inputSlot = iteration == 0 ? -1 : 1 - outputSlot;
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsgiDenoiseTextureView(*ctx.shared->rhiDevice,
                                              outputSlot) ||
        (inputSlot >= 0 &&
         !targets.ensureSsgiDenoiseTextureView(*ctx.shared->rhiDevice,
                                               inputSlot)) ||
        !targets.ensureSsgiTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice) ||
        (momentsEnabled && !targets.ensureSsgiTemporalTextureViews(*ctx.shared->rhiDevice))) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!ensureDenoiseRhiPipelines(rhiDevice)) {
        return false;
    }

    const glm::vec2 screenSize(
        static_cast<float>(std::max(1, targets.width())),
        static_cast<float>(std::max(1, targets.height())));
    const uint32_t bindGroupCacheIndex =
        inputSlot < 0 ? 0u : static_cast<uint32_t>(inputSlot + 1);
    const RhiTextureViewHandle inputView = inputSlot < 0
        ? (momentsEnabled
               ? targets.ssgiTemporalTextureViewHandle()
               : targets.ssgiTextureViewHandle())
        : targets.ssgiDenoiseTextureViewHandle(inputSlot);
    const std::array<RhiTextureViewHandle, 4> views = {
        inputView,
        targets.depthTextureViewHandle(),
        targets.normalAoTextureViewHandle(),
        momentsEnabled
            ? targets.ssgiTemporalMomentsTextureViewHandle()
            : RhiTextureViewHandle{}
    };
    if (!ensureDenoiseBindGroup(
            rhiDevice, momentsEnabled, bindGroupCacheIndex, views)) {
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.ssgiDenoiseTextureViewHandle(outputSlot);
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 0.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SsgiDenoise";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Ssgi)
        : GpuTimerSegmentToken{};
    commandList.beginRendering(renderingInfo);
    const glm::vec4 pushConstants[2] = {
        glm::vec4(screenSize,
                  ctx.camera.nearPlane,
                  static_cast<float>(1 << iteration)),
        glm::vec4(ssgi.denoiseStrength, 0.0f, 0.0f, 0.0f)
    };
    commandList.setGraphicsPipeline(
        momentsEnabled ? m_denoiseMomentsPipeline : m_denoiseSpatialPipeline);
    commandList.setBindGroup(
        0u,
        momentsEnabled
            ? m_denoiseMomentsBindGroups[bindGroupCacheIndex]
            : m_denoiseSpatialBindGroups[bindGroupCacheIndex]);
    commandList.pushConstants(pushConstants,
                              sizeof(pushConstants),
                              rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool SsgiPass::ensureDenoiseRhiPipelines(RhiDevice& rhiDevice) {
    if (m_denoiseRhiDevice != nullptr && m_denoiseRhiDevice != &rhiDevice) {
        destroyDenoiseRhiResources();
    }
    if (m_denoiseSpatialPipeline.isValid() && m_denoiseMomentsPipeline.isValid()) {
        return true;
    }
    m_denoiseRhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> spatialFragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/ssgi_denoise_spatial.frag");
    const std::optional<std::string> momentsFragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/ssgi_denoise.frag");
    if (!vertexSource.has_value() || !spatialFragmentSource.has_value() ||
        !momentsFragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "SsgiDenoise.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_denoiseVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "SsgiDenoiseSpatial.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = spatialFragmentSource->c_str();
    fragmentDesc.sourceSize = spatialFragmentSource->size();
    m_denoiseSpatialFragmentShader = rhiDevice.createShader(fragmentDesc);
    fragmentDesc.debugName = "SsgiDenoiseMoments.Fragment";
    fragmentDesc.source = momentsFragmentSource->c_str();
    fragmentDesc.sourceSize = momentsFragmentSource->size();
    m_denoiseMomentsFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_denoiseVertexShader.isValid() || !m_denoiseSpatialFragmentShader.isValid() ||
        !m_denoiseMomentsFragmentShader.isValid()) {
        destroyDenoiseRhiResources();
        return false;
    }

    auto createSampler = [&](const RhiFilter filter) {
        RhiSamplerDesc samplerDesc;
        samplerDesc.minFilter = filter;
        samplerDesc.magFilter = filter;
        samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
        samplerDesc.addressU = RhiAddressMode::ClampToEdge;
        samplerDesc.addressV = RhiAddressMode::ClampToEdge;
        samplerDesc.addressW = RhiAddressMode::ClampToEdge;
        return rhiDevice.createSampler(samplerDesc);
    };
    m_denoiseNearestSampler = createSampler(RhiFilter::Nearest);
    m_denoiseLinearSampler = createSampler(RhiFilter::Linear);
    if (!m_denoiseNearestSampler.isValid() || !m_denoiseLinearSampler.isValid()) {
        destroyDenoiseRhiResources();
        return false;
    }

    auto createBindGroupLayout = [&](const char* debugName, const uint32_t bindingCount) {
        RhiBindGroupLayoutDesc desc;
        desc.debugName = debugName;
        for (uint32_t binding = 0u; binding < bindingCount; ++binding) {
            desc.entries.push_back({
                binding,
                RhiBindingType::CombinedTextureSampler,
                rhiFlag(RhiShaderStage::Fragment),
                1u
            });
        }
        return rhiDevice.createBindGroupLayout(desc);
    };
    m_denoiseSpatialBindGroupLayout =
        createBindGroupLayout("SsgiDenoiseSpatial.BindGroupLayout", 3u);
    m_denoiseMomentsBindGroupLayout =
        createBindGroupLayout("SsgiDenoiseMoments.BindGroupLayout", 4u);
    if (!m_denoiseSpatialBindGroupLayout.isValid() ||
        !m_denoiseMomentsBindGroupLayout.isValid()) {
        destroyDenoiseRhiResources();
        return false;
    }

    auto createPipelineLayout = [&](const char* debugName,
                                    const RhiBindGroupLayoutHandle bindGroupLayout) {
        RhiPipelineLayoutDesc desc;
        desc.debugName = debugName;
        desc.bindGroupLayouts.push_back(bindGroupLayout);
        desc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4) * 2u);
        desc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
        return rhiDevice.createPipelineLayout(desc);
    };
    m_denoiseSpatialPipelineLayout = createPipelineLayout(
        "SsgiDenoiseSpatial.PipelineLayout",
        m_denoiseSpatialBindGroupLayout);
    m_denoiseMomentsPipelineLayout = createPipelineLayout(
        "SsgiDenoiseMoments.PipelineLayout",
        m_denoiseMomentsBindGroupLayout);
    if (!m_denoiseSpatialPipelineLayout.isValid() ||
        !m_denoiseMomentsPipelineLayout.isValid()) {
        destroyDenoiseRhiResources();
        return false;
    }

    auto createPipeline = [&](const char* debugName,
                              const RhiShaderHandle fragmentShader,
                              const RhiPipelineLayoutHandle pipelineLayout) {
        RhiGraphicsPipelineDesc desc;
        desc.debugName = debugName;
        desc.vertexShader = m_denoiseVertexShader;
        desc.fragmentShader = fragmentShader;
        desc.layout = pipelineLayout;
        desc.topology = RhiPrimitiveTopology::TriangleList;
        desc.raster.cullMode = RhiCullMode::None;
        desc.depthStencil.depthTestEnabled = false;
        desc.depthStencil.depthWriteEnabled = false;
        desc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
        desc.blend.attachments.push_back({});
        return rhiDevice.createGraphicsPipeline(desc);
    };
    m_denoiseSpatialPipeline = createPipeline(
        "SsgiDenoiseSpatial.Pipeline",
        m_denoiseSpatialFragmentShader,
        m_denoiseSpatialPipelineLayout);
    m_denoiseMomentsPipeline = createPipeline(
        "SsgiDenoiseMoments.Pipeline",
        m_denoiseMomentsFragmentShader,
        m_denoiseMomentsPipelineLayout);
    if (!m_denoiseSpatialPipeline.isValid() || !m_denoiseMomentsPipeline.isValid()) {
        destroyDenoiseRhiResources();
        return false;
    }

    return true;
}

bool SsgiPass::ensureDenoiseBindGroup(
    RhiDevice& rhiDevice,
    const bool momentsEnabled,
    const uint32_t cacheIndex,
    const std::array<RhiTextureViewHandle, 4>& views) {
    if (!ensureDenoiseRhiPipelines(rhiDevice) || cacheIndex >= 3u) {
        return false;
    }
    const uint32_t bindingCount = momentsEnabled ? 4u : 3u;
    for (uint32_t binding = 0u; binding < bindingCount; ++binding) {
        if (!views[binding].isValid()) {
            return false;
        }
    }

    std::array<RhiBindGroupHandle, 3>& bindGroups = momentsEnabled
        ? m_denoiseMomentsBindGroups
        : m_denoiseSpatialBindGroups;
    std::array<std::array<RhiTextureViewHandle, 4>, 3>& boundViews = momentsEnabled
        ? m_denoiseMomentsBoundViews
        : m_denoiseSpatialBoundViews;
    if (bindGroups[cacheIndex].isValid() &&
        sameTextureViews(boundViews[cacheIndex], views)) {
        return true;
    }
    if (bindGroups[cacheIndex].isValid()) {
        m_denoiseRhiDevice->destroyBindGroup(bindGroups[cacheIndex]);
        bindGroups[cacheIndex] = {};
    }

    const RhiSamplerHandle samplers[4] = {
        m_denoiseLinearSampler,
        m_denoiseNearestSampler,
        m_denoiseNearestSampler,
        m_denoiseLinearSampler
    };
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = momentsEnabled
        ? m_denoiseMomentsBindGroupLayout
        : m_denoiseSpatialBindGroupLayout;
    for (uint32_t binding = 0u; binding < bindingCount; ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler = samplers[binding];
        bindGroupDesc.entries.push_back(entry);
    }

    bindGroups[cacheIndex] = rhiDevice.createBindGroup(bindGroupDesc);
    if (!bindGroups[cacheIndex].isValid()) {
        boundViews[cacheIndex] = {};
        return false;
    }
    boundViews[cacheIndex] = views;
    return true;
}

void SsgiPass::destroyDenoiseBindGroups() {
    if (m_denoiseRhiDevice != nullptr) {
        for (RhiBindGroupHandle& bindGroup : m_denoiseSpatialBindGroups) {
            if (bindGroup.isValid()) {
                m_denoiseRhiDevice->destroyBindGroup(bindGroup);
            }
            bindGroup = {};
        }
        for (RhiBindGroupHandle& bindGroup : m_denoiseMomentsBindGroups) {
            if (bindGroup.isValid()) {
                m_denoiseRhiDevice->destroyBindGroup(bindGroup);
            }
            bindGroup = {};
        }
    }
    m_denoiseSpatialBoundViews = {};
    m_denoiseMomentsBoundViews = {};
}

void SsgiPass::destroyDenoiseRhiResources() {
    destroyDenoiseBindGroups();
    if (m_denoiseRhiDevice != nullptr) {
        const RhiPipelineHandle pipelines[] = {
            m_denoiseSpatialPipeline,
            m_denoiseMomentsPipeline
        };
        for (const RhiPipelineHandle pipeline : pipelines) {
            if (pipeline.isValid()) {
                m_denoiseRhiDevice->destroyPipeline(pipeline);
            }
        }
        const RhiShaderHandle shaders[] = {
            m_denoiseVertexShader,
            m_denoiseSpatialFragmentShader,
            m_denoiseMomentsFragmentShader
        };
        for (const RhiShaderHandle shader : shaders) {
            if (shader.isValid()) {
                m_denoiseRhiDevice->destroyShader(shader);
            }
        }
        const RhiPipelineLayoutHandle pipelineLayouts[] = {
            m_denoiseSpatialPipelineLayout,
            m_denoiseMomentsPipelineLayout
        };
        for (const RhiPipelineLayoutHandle layout : pipelineLayouts) {
            if (layout.isValid()) {
                m_denoiseRhiDevice->destroyPipelineLayout(layout);
            }
        }
        const RhiBindGroupLayoutHandle bindGroupLayouts[] = {
            m_denoiseSpatialBindGroupLayout,
            m_denoiseMomentsBindGroupLayout
        };
        for (const RhiBindGroupLayoutHandle layout : bindGroupLayouts) {
            if (layout.isValid()) {
                m_denoiseRhiDevice->destroyBindGroupLayout(layout);
            }
        }
        if (m_denoiseNearestSampler.isValid()) {
            m_denoiseRhiDevice->destroySampler(m_denoiseNearestSampler);
        }
        if (m_denoiseLinearSampler.isValid()) {
            m_denoiseRhiDevice->destroySampler(m_denoiseLinearSampler);
        }
    }

    m_denoiseSpatialPipeline = {};
    m_denoiseMomentsPipeline = {};
    m_denoiseVertexShader = {};
    m_denoiseSpatialFragmentShader = {};
    m_denoiseMomentsFragmentShader = {};
    m_denoiseSpatialPipelineLayout = {};
    m_denoiseMomentsPipelineLayout = {};
    m_denoiseSpatialBindGroupLayout = {};
    m_denoiseMomentsBindGroupLayout = {};
    m_denoiseNearestSampler = {};
    m_denoiseLinearSampler = {};
    m_denoiseRhiDevice = nullptr;
}

bool SsgiPass::recordSsgiTemporal(RhiCommandList& commandList,
                                  const FrameContext& ctx,
                                  const SsgiSettings& ssgi,
                                  DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsgiTemporalTextureViews(*ctx.shared->rhiDevice) ||
        !targets.ensureSsgiHistoryTextureViews(*ctx.shared->rhiDevice) ||
        !targets.ensureSsgiTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureVelocityTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice) ||
        !targets.ensureHistoryDepthTextureViews(*ctx.shared->rhiDevice)) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const std::array<RhiTextureViewHandle, 7> views = {
        targets.ssgiTextureViewHandle(),
        targets.ssgiHistoryTexturePrevViewHandle(),
        targets.velocityTextureViewHandle(),
        targets.depthTextureViewHandle(),
        targets.normalAoTextureViewHandle(),
        targets.historyDepthTexturePrevViewHandle(),
        targets.ssgiMomentsHistoryTexturePrevViewHandle()
    };
    if (!ensureTemporalRhiPipeline(rhiDevice) || !ensureTemporalBindGroup(rhiDevice, views)) {
        return false;
    }

    RhiColorAttachment colorAttachments[2];
    colorAttachments[0].view = targets.ssgiTemporalTextureViewHandle();
    colorAttachments[0].loadOp = RhiLoadOp::Clear;
    colorAttachments[0].storeOp = RhiStoreOp::Store;
    colorAttachments[0].clearColor[0] = 0.0f;
    colorAttachments[0].clearColor[1] = 0.0f;
    colorAttachments[0].clearColor[2] = 0.0f;
    colorAttachments[0].clearColor[3] = 0.0f;
    colorAttachments[1].view = targets.ssgiTemporalMomentsTextureViewHandle();
    colorAttachments[1].loadOp = RhiLoadOp::Clear;
    colorAttachments[1].storeOp = RhiStoreOp::Store;
    colorAttachments[1].clearColor[0] = 0.0f;
    colorAttachments[1].clearColor[1] = 0.0f;
    colorAttachments[1].clearColor[2] = 0.0f;
    colorAttachments[1].clearColor[3] = 0.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SsgiTemporal";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = colorAttachments;
    renderingInfo.colorAttachmentCount = 2u;

    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Ssgi)
        : GpuTimerSegmentToken{};
    commandList.beginRendering(renderingInfo);
    const glm::vec4 pushConstants(
        static_cast<float>(std::max(1, targets.width())),
        static_cast<float>(std::max(1, targets.height())),
        ssgi.historyWeight,
        ctx.camera.nearPlane);
    commandList.setGraphicsPipeline(m_temporalPipeline);
    commandList.setBindGroup(0u, m_temporalBindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool SsgiPass::recordSsgiHistoryCopy(RhiCommandList& commandList,
                                     const FrameContext& ctx,
                                     DeferredRenderTargets& targets) {
    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Ssgi)
        : GpuTimerSegmentToken{};
    RhiTextureBlit radianceBlit;
    radianceBlit.src = targets.ssgiTemporalTextureHandle();
    radianceBlit.dst = targets.ssgiHistoryTextureHandle();
    commandList.blitTexture(radianceBlit);
    RhiTextureBlit momentsBlit;
    momentsBlit.src = targets.ssgiTemporalMomentsTextureHandle();
    momentsBlit.dst = targets.ssgiMomentsHistoryTextureHandle();
    commandList.blitTexture(momentsBlit);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool SsgiPass::recordSsgiOutputCopy(RhiCommandList& commandList,
                                    const FrameContext& ctx,
                                    DeferredRenderTargets& targets,
                                    const RhiTextureHandle source) {
    if (!source.isValid()) {
        return false;
    }
    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Ssgi)
        : GpuTimerSegmentToken{};
    RhiTextureBlit blit;
    blit.src = source;
    blit.dst = targets.ssgiTextureHandle();
    commandList.blitTexture(blit);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool SsgiPass::ensureTemporalRhiPipeline(RhiDevice& rhiDevice) {
    if (m_temporalRhiDevice != nullptr && m_temporalRhiDevice != &rhiDevice) {
        destroyTemporalRhiResources();
    }
    if (m_temporalPipeline.isValid()) {
        return true;
    }
    m_temporalRhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/ssgi_temporal.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "SsgiTemporal.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_temporalVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "SsgiTemporal.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_temporalFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_temporalVertexShader.isValid() || !m_temporalFragmentShader.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    auto createSampler = [&](const RhiFilter filter) {
        RhiSamplerDesc samplerDesc;
        samplerDesc.minFilter = filter;
        samplerDesc.magFilter = filter;
        samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
        samplerDesc.addressU = RhiAddressMode::ClampToEdge;
        samplerDesc.addressV = RhiAddressMode::ClampToEdge;
        samplerDesc.addressW = RhiAddressMode::ClampToEdge;
        return rhiDevice.createSampler(samplerDesc);
    };
    m_temporalNearestSampler = createSampler(RhiFilter::Nearest);
    m_temporalLinearSampler = createSampler(RhiFilter::Linear);
    if (!m_temporalNearestSampler.isValid() || !m_temporalLinearSampler.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "SsgiTemporal.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 7u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    m_temporalBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_temporalBindGroupLayout.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "SsgiTemporal.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_temporalBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_temporalPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_temporalPipelineLayout.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "SsgiTemporal.Pipeline";
    pipelineDesc.vertexShader = m_temporalVertexShader;
    pipelineDesc.fragmentShader = m_temporalFragmentShader;
    pipelineDesc.layout = m_temporalPipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.blend.attachments.push_back({});
    pipelineDesc.blend.attachments.push_back({});
    m_temporalPipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_temporalPipeline.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    return true;
}

bool SsgiPass::ensureTemporalBindGroup(
    RhiDevice& rhiDevice,
    const std::array<RhiTextureViewHandle, 7>& views) {
    if (!ensureTemporalRhiPipeline(rhiDevice)) {
        return false;
    }
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_temporalBindGroup.isValid() && sameTextureViews(m_temporalBoundViews, views)) {
        return true;
    }

    destroyTemporalBindGroup();
    const RhiSamplerHandle samplers[7] = {
        m_temporalNearestSampler,
        m_temporalLinearSampler,
        m_temporalNearestSampler,
        m_temporalNearestSampler,
        m_temporalNearestSampler,
        m_temporalNearestSampler,
        m_temporalLinearSampler
    };

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_temporalBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler = samplers[binding];
        bindGroupDesc.entries.push_back(entry);
    }

    m_temporalBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_temporalBindGroup.isValid()) {
        m_temporalBoundViews = {};
        return false;
    }

    m_temporalBoundViews = views;
    return true;
}

void SsgiPass::destroyTemporalBindGroup() {
    if (m_temporalRhiDevice != nullptr && m_temporalBindGroup.isValid()) {
        m_temporalRhiDevice->destroyBindGroup(m_temporalBindGroup);
    }
    m_temporalBindGroup = {};
    m_temporalBoundViews = {};
}

void SsgiPass::destroyTemporalRhiResources() {
    destroyTemporalBindGroup();
    if (m_temporalRhiDevice != nullptr) {
        if (m_temporalPipeline.isValid()) {
            m_temporalRhiDevice->destroyPipeline(m_temporalPipeline);
        }
        if (m_temporalVertexShader.isValid()) {
            m_temporalRhiDevice->destroyShader(m_temporalVertexShader);
        }
        if (m_temporalFragmentShader.isValid()) {
            m_temporalRhiDevice->destroyShader(m_temporalFragmentShader);
        }
        if (m_temporalPipelineLayout.isValid()) {
            m_temporalRhiDevice->destroyPipelineLayout(m_temporalPipelineLayout);
        }
        if (m_temporalBindGroupLayout.isValid()) {
            m_temporalRhiDevice->destroyBindGroupLayout(m_temporalBindGroupLayout);
        }
        if (m_temporalNearestSampler.isValid()) {
            m_temporalRhiDevice->destroySampler(m_temporalNearestSampler);
        }
        if (m_temporalLinearSampler.isValid()) {
            m_temporalRhiDevice->destroySampler(m_temporalLinearSampler);
        }
    }

    m_temporalPipeline = {};
    m_temporalVertexShader = {};
    m_temporalFragmentShader = {};
    m_temporalPipelineLayout = {};
    m_temporalBindGroupLayout = {};
    m_temporalNearestSampler = {};
    m_temporalLinearSampler = {};
    m_temporalRhiDevice = nullptr;
}
