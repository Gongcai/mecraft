#include "SsaoPass.h"
#include "../core/RenderScene.h"
#include "../targets/DeferredRenderTargets.h"
#include "../debug/RenderDebugService.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../../resource/ResourceMgr.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstddef>
#include <optional>

namespace {
[[nodiscard]] bool sameTextureView(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

template <size_t Count>
[[nodiscard]] bool sameTextureViews(const std::array<RhiTextureViewHandle, Count>& lhs,
                                    const std::array<RhiTextureViewHandle, Count>& rhs) {
    for (size_t i = 0u; i < lhs.size(); ++i) {
        if (!sameTextureView(lhs[i], rhs[i])) {
            return false;
        }
    }
    return true;
}
} // namespace

void SsaoPass::init(ResourceMgr& resourceMgr) {
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
}

void SsaoPass::shutdown() {
    destroyBaseRhiResources();
    destroyComputeStage(m_computeBase);
    destroyComputeStage(m_computeFilter);
    destroyComputeStage(m_computeUpsample);
    destroyComputeStage(m_computeTemporal);
    destroyUpsampleRhiResources();
    destroyFilterRhiResources();
    destroyTemporalRhiResources();
    destroyNoiseTextureView();
    m_noiseTexture = {};
}

RgPassHandle SsaoPass::addGraphPasses(RenderGraph& graph,
                                      const FrameContext& ctx,
                                      const SsaoSettings& ssao,
                                      DeferredRenderTargets& targets,
                                      const GraphResources& resources,
                                      const RgPassHandle dependency,
                                      const bool useAsyncCompute) {
    const bool temporalEnabled = ssao.temporalEnabled && !ctx.temporalReset;
    if (!dependency.isValid() || !resources.depth.isValid() ||
        !resources.normalAo.isValid() || !resources.velocity.isValid() ||
        !resources.noise.isValid() || !resources.halfRes.isValid() ||
        !resources.filtered.isValid() ||
        (ssao.filterEnabled && !resources.halfResFiltered.isValid()) ||
        (temporalEnabled &&
         (!resources.temporal.isValid() || !resources.historyCurrent.isValid() ||
          !resources.historyPrevious.isValid()))) {
        return {};
    }

    const FrameContext* frame = &ctx;
    DeferredRenderTargets* frameTargets = &targets;
    if (useAsyncCompute) {
        // Compute-queue mirror of the fragment chain. Depth remains in its
        // sampled read-only layout, and the history copy uses vkCmdCopyImage,
        // which is legal on compute queues. The whole chain therefore stays
        // on one queue without blocking graphics before shadow rendering.
        RenderGraphPassBuilder base = graph.addPass(
            {"SSAO.Base", RgPassType::Compute, RhiQueueType::Compute,
             /*threadSafeRecord=*/true});
        base.dependsOn(dependency)
            .readTexture(resources.depth, RhiResourceState::DepthRead)
            .readTexture(resources.normalAo, RhiResourceState::ShaderRead)
            .readTexture(resources.noise, RhiResourceState::ShaderRead)
            .writeTexture(resources.halfRes, RhiResourceState::ShaderWrite)
            .setExecute([this, frame, frameTargets, ssao](RgPassContext& pass) {
                return recordSsaoBaseCompute(
                    pass.commandList(), *frame, ssao, *frameTargets);
            });
        RgPassHandle previous = base.handle();

        if (ssao.filterEnabled) {
            RenderGraphPassBuilder filter = graph.addPass(
                {"SSAO.Filter", RgPassType::Compute, RhiQueueType::Compute,
                 /*threadSafeRecord=*/true});
            filter.dependsOn(previous)
                .readTexture(resources.halfRes, RhiResourceState::ShaderRead)
                .readTexture(resources.depth, RhiResourceState::DepthRead)
                .readTexture(resources.normalAo, RhiResourceState::ShaderRead)
                .writeTexture(resources.halfResFiltered,
                              RhiResourceState::ShaderWrite)
                .setExecute([this, frame, frameTargets](RgPassContext& pass) {
                    return recordSsaoFilterCompute(
                        pass.commandList(), *frame, *frameTargets);
                });
            previous = filter.handle();
        }

        const RgTextureHandle halfInput = ssao.filterEnabled
            ? resources.halfResFiltered
            : resources.halfRes;
        RenderGraphPassBuilder upsample = graph.addPass(
            {"SSAO.Upsample", RgPassType::Compute, RhiQueueType::Compute,
             /*threadSafeRecord=*/true});
        upsample.dependsOn(previous)
            .readTexture(halfInput, RhiResourceState::ShaderRead)
            .readTexture(resources.depth, RhiResourceState::DepthRead)
            .writeTexture(resources.filtered, RhiResourceState::ShaderWrite)
            .setExecute([this, frame, frameTargets, ssao](RgPassContext& pass) {
                return recordSsaoUpsampleCompute(
                    pass.commandList(), *frame, ssao, *frameTargets);
            });
        previous = upsample.handle();

        if (temporalEnabled) {
            RenderGraphPassBuilder temporal = graph.addPass(
                {"SSAO.Temporal", RgPassType::Compute, RhiQueueType::Compute,
                 /*threadSafeRecord=*/true});
            temporal.dependsOn(previous)
                .readTexture(resources.filtered, RhiResourceState::ShaderRead)
                .readTexture(resources.historyPrevious,
                             RhiResourceState::ShaderRead)
                .readTexture(resources.velocity, RhiResourceState::ShaderRead)
                .readTexture(resources.depth, RhiResourceState::DepthRead)
                .writeTexture(resources.temporal, RhiResourceState::ShaderWrite)
                .setExecute(
                    [this, frame, frameTargets, ssao](RgPassContext& pass) {
                        return recordSsaoTemporalCompute(
                            pass.commandList(), *frame, ssao, *frameTargets);
                    });
            previous = temporal.handle();

            RenderGraphPassBuilder historyCopy = graph.addPass(
                {"SSAO.HistoryCopy", RgPassType::Copy, RhiQueueType::Compute,
                 /*threadSafeRecord=*/true});
            historyCopy.dependsOn(previous)
                .readTexture(resources.temporal, RhiResourceState::TransferSrc)
                .writeTexture(resources.historyCurrent,
                              RhiResourceState::TransferDst)
                .setExecute([this, frame, frameTargets](RgPassContext& pass) {
                    return recordSsaoHistoryCopyCompute(
                        pass.commandList(), *frame, *frameTargets);
                });
            previous = historyCopy.handle();
        }
        return previous;
    }

    RenderGraphPassBuilder base = graph.addPass(
        {"SSAO.Base", RgPassType::Graphics, RhiQueueType::Graphics,
         /*threadSafeRecord=*/true});
    base.dependsOn(dependency)
        .readTexture(resources.depth, RhiResourceState::DepthRead)
        .readTexture(resources.normalAo, RhiResourceState::ShaderRead)
        .readTexture(resources.noise, RhiResourceState::ShaderRead)
        .writeTexture(resources.halfRes, RhiResourceState::RenderTarget)
        .setExecute([this, frame, frameTargets, ssao](RgPassContext& pass) {
            return recordSsaoBase(
                pass.commandList(), *frame, ssao, *frameTargets);
        });
    RgPassHandle previous = base.handle();

    if (ssao.filterEnabled) {
        RenderGraphPassBuilder filter = graph.addPass(
            {"SSAO.Filter", RgPassType::Graphics, RhiQueueType::Graphics,
             /*threadSafeRecord=*/true});
        filter.dependsOn(previous)
            .readTexture(resources.halfRes, RhiResourceState::ShaderRead)
            .readTexture(resources.depth, RhiResourceState::DepthRead)
            .readTexture(resources.normalAo, RhiResourceState::ShaderRead)
            .writeTexture(resources.halfResFiltered,
                          RhiResourceState::RenderTarget)
            .setExecute([this, frame, frameTargets](RgPassContext& pass) {
                return recordSsaoFilter(
                    pass.commandList(), *frame, *frameTargets);
            });
        previous = filter.handle();
    }

    const RgTextureHandle halfResInput = ssao.filterEnabled
        ? resources.halfResFiltered
        : resources.halfRes;
    RenderGraphPassBuilder upsample = graph.addPass(
        {"SSAO.Upsample", RgPassType::Graphics, RhiQueueType::Graphics,
         /*threadSafeRecord=*/true});
    upsample.dependsOn(previous)
        .readTexture(halfResInput, RhiResourceState::ShaderRead)
        .readTexture(resources.depth, RhiResourceState::DepthRead)
        .writeTexture(resources.filtered, RhiResourceState::RenderTarget)
        .setExecute([this, frame, frameTargets, ssao](RgPassContext& pass) {
            return recordSsaoUpsample(
                pass.commandList(), *frame, ssao, *frameTargets);
        });
    previous = upsample.handle();

    if (temporalEnabled) {
        RenderGraphPassBuilder temporal = graph.addPass(
            {"SSAO.Temporal", RgPassType::Graphics, RhiQueueType::Graphics,
             /*threadSafeRecord=*/true});
        temporal.dependsOn(previous)
            .readTexture(resources.filtered, RhiResourceState::ShaderRead)
            .readTexture(resources.historyPrevious, RhiResourceState::ShaderRead)
            .readTexture(resources.velocity, RhiResourceState::ShaderRead)
            .readTexture(resources.depth, RhiResourceState::DepthRead)
            .writeTexture(resources.temporal, RhiResourceState::RenderTarget)
            .setExecute([this, frame, frameTargets, ssao](RgPassContext& pass) {
                return recordSsaoTemporal(
                    pass.commandList(), *frame, ssao, *frameTargets);
            });
        previous = temporal.handle();

        RenderGraphPassBuilder historyCopy = graph.addPass(
            {"SSAO.HistoryCopy", RgPassType::Copy, RhiQueueType::Graphics,
             /*threadSafeRecord=*/true});
        historyCopy.dependsOn(previous)
            .readTexture(resources.temporal, RhiResourceState::TransferSrc)
            .writeTexture(resources.historyCurrent, RhiResourceState::TransferDst)
            .setExecute([this, frame, frameTargets](RgPassContext& pass) {
                return recordSsaoHistoryCopy(
                    pass.commandList(), *frame, *frameTargets);
            });
        previous = historyCopy.handle();
    }
    return previous;
}

bool SsaoPass::ensureComputeStage(RhiDevice& rhiDevice,
                                  ComputeStage& stage,
                                  const char* shaderPath,
                                  const char* debugName,
                                  const uint32_t sampledCount,
                                  const uint32_t pushConstantBytes) {
    if (stage.device != nullptr && stage.device != &rhiDevice) {
        destroyComputeStage(stage);
    }
    if (stage.pipeline.isValid()) {
        return true;
    }
    stage.device = &rhiDevice;

    const std::optional<std::string> source =
        renderer::rhi::loadShaderSource(shaderPath);
    if (!source.has_value()) {
        return false;
    }
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = debugName;
    shaderDesc.stage = RhiShaderStage::Compute;
    shaderDesc.source = source->c_str();
    shaderDesc.sourceSize = source->size();
    stage.shader = rhiDevice.createShader(shaderDesc);
    if (!stage.shader.isValid()) {
        return false;
    }

    RhiBindGroupLayoutDesc layoutDesc;
    layoutDesc.debugName = debugName;
    for (uint32_t binding = 0u; binding < sampledCount; ++binding) {
        layoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Compute),
            1u
        });
    }
    layoutDesc.entries.push_back({
        sampledCount,
        RhiBindingType::StorageTexture,
        rhiFlag(RhiShaderStage::Compute),
        1u
    });
    stage.bindGroupLayout = rhiDevice.createBindGroupLayout(layoutDesc);
    if (!stage.bindGroupLayout.isValid()) {
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = debugName;
    pipelineLayoutDesc.bindGroupLayouts.push_back(stage.bindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = pushConstantBytes;
    if (pushConstantBytes > 0u) {
        pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Compute);
    }
    stage.pipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!stage.pipelineLayout.isValid()) {
        return false;
    }

    RhiComputePipelineDesc pipelineDesc;
    pipelineDesc.debugName = debugName;
    pipelineDesc.computeShader = stage.shader;
    pipelineDesc.layout = stage.pipelineLayout;
    stage.pipeline = rhiDevice.createComputePipeline(pipelineDesc);
    return stage.pipeline.isValid();
}

bool SsaoPass::ensureComputeStageBindGroup(
    RhiDevice& rhiDevice,
    ComputeStage& stage,
    const RhiTextureViewHandle* views,
    const RhiSamplerHandle* samplers,
    const uint32_t sampledCount,
    const RhiTextureViewHandle storageView) {
    if (!stage.pipeline.isValid() || !storageView.isValid() ||
        sampledCount + 1u > stage.boundViews.size()) {
        return false;
    }
    std::array<RhiTextureViewHandle, 5> boundViews = {};
    for (uint32_t i = 0u; i < sampledCount; ++i) {
        if (!views[i].isValid()) {
            return false;
        }
        boundViews[i] = views[i];
    }
    boundViews[sampledCount] = storageView;
    if (stage.bindGroup.isValid() &&
        sameTextureViews(stage.boundViews, boundViews)) {
        return true;
    }

    if (stage.bindGroup.isValid()) {
        rhiDevice.destroyBindGroup(stage.bindGroup);
        stage.bindGroup = {};
    }
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = stage.bindGroupLayout;
    for (uint32_t binding = 0u; binding < sampledCount; ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler = samplers[binding];
        bindGroupDesc.entries.push_back(entry);
    }
    RhiBindGroupEntry storageEntry;
    storageEntry.binding = sampledCount;
    storageEntry.resource.textureView = storageView;
    bindGroupDesc.entries.push_back(storageEntry);

    stage.bindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!stage.bindGroup.isValid()) {
        stage.boundViews = {};
        return false;
    }
    stage.boundViews = boundViews;
    return true;
}

void SsaoPass::destroyComputeStage(ComputeStage& stage) {
    if (stage.device != nullptr) {
        if (stage.bindGroup.isValid()) {
            stage.device->destroyBindGroup(stage.bindGroup);
        }
        if (stage.pipeline.isValid()) {
            stage.device->destroyPipeline(stage.pipeline);
        }
        if (stage.pipelineLayout.isValid()) {
            stage.device->destroyPipelineLayout(stage.pipelineLayout);
        }
        if (stage.bindGroupLayout.isValid()) {
            stage.device->destroyBindGroupLayout(stage.bindGroupLayout);
        }
        if (stage.shader.isValid()) {
            stage.device->destroyShader(stage.shader);
        }
    }
    stage = {};
}

bool SsaoPass::recordSsaoBaseCompute(RhiCommandList& commandList,
                                     const FrameContext& ctx,
                                     const SsaoSettings& ssao,
                                     DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsaoHalfResTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice)) {
        return false;
    }
    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    // The fragment path owns the shared samplers; guarantee them first.
    if (!ensureBaseRhiPipeline(rhiDevice) || !ensureNoiseTextureView(rhiDevice)) {
        return false;
    }
    struct BasePushConstants {
        glm::mat4 projection;
        glm::mat4 invProjection;
        glm::vec4 params0;
        glm::ivec4 params1;
    };
    if (!ensureComputeStage(rhiDevice, m_computeBase,
                            "assets/shaders/ssao.comp", "SSAO.BaseCompute", 3u,
                            static_cast<uint32_t>(sizeof(BasePushConstants)))) {
        return false;
    }
    const RhiTextureViewHandle views[3] = {
        targets.depthTextureViewHandle(),
        targets.normalAoTextureViewHandle(),
        m_noiseTextureView
    };
    const RhiSamplerHandle samplers[3] = {
        m_baseNearestSampler,
        m_baseNearestSampler,
        m_baseNoiseSampler
    };
    if (!ensureComputeStageBindGroup(rhiDevice, m_computeBase, views, samplers,
                                     3u,
                                     targets.ssaoHalfResTextureViewHandle())) {
        return false;
    }

    const int halfW = std::max(1, targets.width() / 2);
    const int halfH = std::max(1, targets.height() / 2);
    const glm::mat4& projection = ctx.camera.projection;
    const BasePushConstants pushConstants{
        projection,
        glm::inverse(projection),
        glm::vec4(1.0f / static_cast<float>(halfW),
                  1.0f / static_cast<float>(halfH),
                  ssao.radius,
                  ssao.strength),
        glm::ivec4(static_cast<int>(ctx.frameIndex % 64),
                   std::clamp(ssao.samples, 1, 64),
                   0,
                   0)
    };
    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Ssao)
        : GpuTimerSegmentToken{};
    commandList.setComputePipeline(m_computeBase.pipeline);
    commandList.setBindGroup(0u, m_computeBase.bindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                              rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch((static_cast<uint32_t>(halfW) + 7u) / 8u,
                         (static_cast<uint32_t>(halfH) + 7u) / 8u, 1u);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool SsaoPass::recordSsaoFilterCompute(RhiCommandList& commandList,
                                       const FrameContext& ctx,
                                       DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsaoHalfResTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureSsaoHalfResFilteredTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice)) {
        return false;
    }
    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!ensureFilterRhiPipeline(rhiDevice) ||
        !ensureComputeStage(rhiDevice, m_computeFilter,
                            "assets/shaders/ssao_filter.comp",
                            "SSAO.FilterCompute", 3u,
                            static_cast<uint32_t>(sizeof(glm::vec4)))) {
        return false;
    }
    const RhiTextureViewHandle views[3] = {
        targets.ssaoHalfResTextureViewHandle(),
        targets.depthTextureViewHandle(),
        targets.normalAoTextureViewHandle()
    };
    const RhiSamplerHandle samplers[3] = {
        m_filterSampler, m_filterSampler, m_filterSampler
    };
    if (!ensureComputeStageBindGroup(
            rhiDevice, m_computeFilter, views, samplers, 3u,
            targets.ssaoHalfResFilteredTextureViewHandle())) {
        return false;
    }

    const int halfW = std::max(1, targets.width() / 2);
    const int halfH = std::max(1, targets.height() / 2);
    const glm::vec4 pushConstants(
        static_cast<float>(halfW),
        static_cast<float>(halfH),
        ctx.camera.nearPlane,
        0.0f);
    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Ssao)
        : GpuTimerSegmentToken{};
    commandList.setComputePipeline(m_computeFilter.pipeline);
    commandList.setBindGroup(0u, m_computeFilter.bindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                              rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch((static_cast<uint32_t>(halfW) + 7u) / 8u,
                         (static_cast<uint32_t>(halfH) + 7u) / 8u, 1u);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool SsaoPass::recordSsaoUpsampleCompute(RhiCommandList& commandList,
                                         const FrameContext& ctx,
                                         const SsaoSettings& ssao,
                                         DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsaoFilteredTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureSsaoHalfResTextureView(*ctx.shared->rhiDevice) ||
        (ssao.filterEnabled &&
         !targets.ensureSsaoHalfResFilteredTextureView(*ctx.shared->rhiDevice)) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice)) {
        return false;
    }
    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!ensureUpsampleRhiPipeline(rhiDevice) ||
        !ensureComputeStage(rhiDevice, m_computeUpsample,
                            "assets/shaders/ssao_upsample.comp",
                            "SSAO.UpsampleCompute", 2u,
                            static_cast<uint32_t>(sizeof(glm::vec4)))) {
        return false;
    }
    const RhiTextureViewHandle views[2] = {
        ssao.filterEnabled
            ? targets.ssaoHalfResFilteredTextureViewHandle()
            : targets.ssaoHalfResTextureViewHandle(),
        targets.depthTextureViewHandle()
    };
    const RhiSamplerHandle samplers[2] = {
        m_upsampleNearestSampler, m_upsampleLinearSampler
    };
    if (!ensureComputeStageBindGroup(rhiDevice, m_computeUpsample, views,
                                     samplers, 2u,
                                     targets.ssaoFilteredTextureViewHandle())) {
        return false;
    }

    const int halfW = std::max(1, targets.width() / 2);
    const int halfH = std::max(1, targets.height() / 2);
    const glm::vec4 pushConstants(
        static_cast<float>(halfW),
        static_cast<float>(halfH),
        ctx.camera.nearPlane,
        0.0f);
    const uint32_t width = static_cast<uint32_t>(std::max(1, targets.width()));
    const uint32_t height = static_cast<uint32_t>(std::max(1, targets.height()));
    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Ssao)
        : GpuTimerSegmentToken{};
    commandList.setComputePipeline(m_computeUpsample.pipeline);
    commandList.setBindGroup(0u, m_computeUpsample.bindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                              rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool SsaoPass::recordSsaoTemporalCompute(RhiCommandList& commandList,
                                         const FrameContext& ctx,
                                         const SsaoSettings& ssao,
                                         DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsaoFilteredTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureSsaoTemporalTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureSsaoHistoryTextureViews(*ctx.shared->rhiDevice) ||
        !targets.ensureVelocityTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice)) {
        return false;
    }
    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!ensureTemporalRhiPipeline(rhiDevice) ||
        !ensureComputeStage(rhiDevice, m_computeTemporal,
                            "assets/shaders/ssao_temporal.comp",
                            "SSAO.TemporalCompute", 4u,
                            static_cast<uint32_t>(sizeof(glm::vec4)))) {
        return false;
    }
    const RhiTextureViewHandle views[4] = {
        targets.ssaoFilteredTextureViewHandle(),
        targets.ssaoHistoryTexturePrevViewHandle(),
        targets.velocityTextureViewHandle(),
        targets.depthTextureViewHandle()
    };
    const RhiSamplerHandle samplers[4] = {
        m_temporalNearestSampler,
        m_temporalLinearSampler,
        m_temporalNearestSampler,
        m_temporalNearestSampler
    };
    if (!ensureComputeStageBindGroup(rhiDevice, m_computeTemporal, views,
                                     samplers, 4u,
                                     targets.ssaoTemporalTextureViewHandle())) {
        return false;
    }

    const uint32_t width = static_cast<uint32_t>(std::max(1, targets.width()));
    const uint32_t height = static_cast<uint32_t>(std::max(1, targets.height()));
    const glm::vec4 pushConstants(
        static_cast<float>(width),
        static_cast<float>(height),
        ssao.historyWeight,
        ctx.camera.nearPlane);
    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Ssao)
        : GpuTimerSegmentToken{};
    commandList.setComputePipeline(m_computeTemporal.pipeline);
    commandList.setBindGroup(0u, m_computeTemporal.bindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                              rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool SsaoPass::recordSsaoHistoryCopyCompute(RhiCommandList& commandList,
                                            const FrameContext& ctx,
                                            DeferredRenderTargets& targets) {
    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Ssao)
        : GpuTimerSegmentToken{};
    // vkCmdCopyImage is legal on compute queues (blits are not), which keeps
    // the SSAO chain on one queue instead of forcing a graphics wait.
    RhiTextureCopy copy;
    copy.src = targets.ssaoTemporalTextureHandle();
    copy.dst = targets.ssaoHistoryTextureHandle();
    copy.extent = {static_cast<uint32_t>(std::max(1, targets.width())),
                   static_cast<uint32_t>(std::max(1, targets.height())), 1u};
    commandList.copyTexture(copy);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool SsaoPass::recordSsaoBase(RhiCommandList& commandList,
                              const FrameContext& ctx,
                              const SsaoSettings& ssao,
                              DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsaoHalfResTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice)) {
        return false;
    }

    // Render SSAO at half resolution for performance
    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.ssaoHalfResTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SsaoHalfRes";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.halfWidth())),
        static_cast<uint32_t>(std::max(1, targets.halfHeight()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!ensureNoiseTextureView(rhiDevice)) {
        return false;
    }
    const std::array<RhiTextureViewHandle, 3> views = {
        targets.depthTextureViewHandle(),
        targets.normalAoTextureViewHandle(),
        m_noiseTextureView
    };
    if (!ensureBaseRhiPipeline(rhiDevice) ||
        !ensureBaseBindGroup(rhiDevice, views)) {
        return false;
    }

    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Ssao)
        : GpuTimerSegmentToken{};
    commandList.beginRendering(renderingInfo);

    // Half-res: invResolution refers to the half-res viewport for UV computation
    const int halfW = std::max(1, targets.width() / 2);
    const int halfH = std::max(1, targets.height() / 2);
    struct BasePushConstants {
        glm::mat4 projection;
        glm::mat4 invProjection;
        glm::vec4 params0;
        glm::ivec4 params1;
    };
    const glm::mat4& projection = ctx.camera.projection;
    const BasePushConstants pushConstants{
        projection,
        glm::inverse(projection),
        glm::vec4(1.0f / static_cast<float>(halfW),
                  1.0f / static_cast<float>(halfH),
                  ssao.radius,
                  ssao.strength),
        glm::ivec4(static_cast<int>(ctx.frameIndex % 64),
                   std::clamp(ssao.samples, 1, 64),
                   0,
                   0)
    };
    commandList.setGraphicsPipeline(m_basePipeline);
    commandList.setBindGroup(0u, m_baseBindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool SsaoPass::recordSsaoFilter(RhiCommandList& commandList,
                                const FrameContext& ctx,
                                DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsaoHalfResFilteredTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureSsaoHalfResTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice)) {
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.ssaoHalfResFilteredTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SsaoHalfResFilter";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.halfWidth())),
        static_cast<uint32_t>(std::max(1, targets.halfHeight()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const std::array<RhiTextureViewHandle, 3> views = {
        targets.ssaoHalfResTextureViewHandle(),
        targets.depthTextureViewHandle(),
        targets.normalAoTextureViewHandle()
    };
    if (!ensureFilterRhiPipeline(rhiDevice) ||
        !ensureFilterBindGroup(rhiDevice, views)) {
        return false;
    }

    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Ssao)
        : GpuTimerSegmentToken{};
    commandList.beginRendering(renderingInfo);

    const int halfW = std::max(1, targets.width() / 2);
    const int halfH = std::max(1, targets.height() / 2);
    const glm::vec4 pushConstants(
        static_cast<float>(halfW),
        static_cast<float>(halfH),
        ctx.camera.nearPlane,
        0.0f);
    commandList.setGraphicsPipeline(m_filterPipeline);
    commandList.setBindGroup(0u, m_filterBindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool SsaoPass::recordSsaoUpsample(RhiCommandList& commandList,
                                  const FrameContext& ctx,
                                  const SsaoSettings& ssao,
                                  DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsaoFilteredTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice) ||
        !(ssao.filterEnabled
              ? targets.ensureSsaoHalfResFilteredTextureView(*ctx.shared->rhiDevice)
              : targets.ensureSsaoHalfResTextureView(*ctx.shared->rhiDevice))) {
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.ssaoFilteredTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SsaoUpsample";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const std::array<RhiTextureViewHandle, 2> views = {
        ssao.filterEnabled
            ? targets.ssaoHalfResFilteredTextureViewHandle()
            : targets.ssaoHalfResTextureViewHandle(),
        targets.depthTextureViewHandle()
    };
    if (!ensureUpsampleRhiPipeline(rhiDevice) ||
        !ensureUpsampleBindGroup(rhiDevice, views)) {
        return false;
    }

    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Ssao)
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

bool SsaoPass::recordSsaoTemporal(RhiCommandList& commandList,
                                  const FrameContext& ctx,
                                  const SsaoSettings& ssao,
                                  DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureSsaoTemporalTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureSsaoHistoryTextureViews(*ctx.shared->rhiDevice) ||
        !targets.ensureSsaoFilteredTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureVelocityTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice)) {
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.ssaoTemporalTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SsaoTemporal";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const std::array<RhiTextureViewHandle, 4> views = {
        targets.ssaoFilteredTextureViewHandle(),
        targets.ssaoHistoryTexturePrevViewHandle(),
        targets.velocityTextureViewHandle(),
        targets.depthTextureViewHandle()
    };
    if (!ensureTemporalRhiPipeline(rhiDevice) ||
        !ensureTemporalBindGroup(rhiDevice, views)) {
        return false;
    }

    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Ssao)
        : GpuTimerSegmentToken{};
    commandList.beginRendering(renderingInfo);
    commandList.setGraphicsPipeline(m_temporalPipeline);
    commandList.setBindGroup(0u, m_temporalBindGroup);
    const glm::vec4 pushConstants(
        static_cast<float>(std::max(1, targets.width())),
        static_cast<float>(std::max(1, targets.height())),
        ssao.historyWeight,
        ctx.camera.nearPlane);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool SsaoPass::recordSsaoHistoryCopy(RhiCommandList& commandList,
                                     const FrameContext& ctx,
                                     DeferredRenderTargets& targets) {
    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Ssao)
        : GpuTimerSegmentToken{};
    RhiTextureBlit blit;
    blit.src = targets.ssaoTemporalTextureHandle();
    blit.dst = targets.ssaoHistoryTextureHandle();
    commandList.blitTexture(blit);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool SsaoPass::ensureNoiseTextureView(RhiDevice& rhiDevice) {
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

void SsaoPass::destroyNoiseTextureView() {
    if (m_noiseViewDevice != nullptr && m_noiseTextureView.isValid()) {
        m_noiseViewDevice->destroyTextureView(m_noiseTextureView);
    }
    m_noiseTextureView = {};
    m_noiseViewDevice = nullptr;
}

bool SsaoPass::ensureBaseRhiPipeline(RhiDevice& rhiDevice) {
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
        renderer::rhi::loadShaderSource("assets/shaders/ssao.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "SsaoBase.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_baseVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "SsaoBase.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_baseFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_baseVertexShader.isValid() || !m_baseFragmentShader.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    RhiSamplerDesc nearestSamplerDesc;
    nearestSamplerDesc.minFilter = RhiFilter::Nearest;
    nearestSamplerDesc.magFilter = RhiFilter::Nearest;
    nearestSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    nearestSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_baseNearestSampler = rhiDevice.createSampler(nearestSamplerDesc);

    RhiSamplerDesc noiseSamplerDesc;
    noiseSamplerDesc.minFilter = RhiFilter::Linear;
    noiseSamplerDesc.magFilter = RhiFilter::Linear;
    noiseSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    noiseSamplerDesc.addressU = RhiAddressMode::Repeat;
    noiseSamplerDesc.addressV = RhiAddressMode::Repeat;
    noiseSamplerDesc.addressW = RhiAddressMode::Repeat;
    m_baseNoiseSampler = rhiDevice.createSampler(noiseSamplerDesc);
    if (!m_baseNearestSampler.isValid() || !m_baseNoiseSampler.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "SsaoBase.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 3u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    m_baseBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_baseBindGroupLayout.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "SsaoBase.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_baseBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::mat4) * 2u +
                                                                 sizeof(glm::vec4) +
                                                                 sizeof(glm::ivec4));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_basePipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_basePipelineLayout.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "SsaoBase.Pipeline";
    pipelineDesc.vertexShader = m_baseVertexShader;
    pipelineDesc.fragmentShader = m_baseFragmentShader;
    pipelineDesc.layout = m_basePipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::R8Unorm);
    pipelineDesc.blend.attachments.push_back({});
    m_basePipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_basePipeline.isValid()) {
        destroyBaseRhiResources();
        return false;
    }

    return true;
}

bool SsaoPass::ensureBaseBindGroup(RhiDevice& rhiDevice,
                                   const std::array<RhiTextureViewHandle, 3>& views) {
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

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_baseBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler =
            binding == 2u ? m_baseNoiseSampler : m_baseNearestSampler;
        bindGroupDesc.entries.push_back(entry);
    }

    m_baseBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_baseBindGroup.isValid()) {
        m_baseBoundViews = {};
        return false;
    }

    m_baseBoundViews = views;
    return true;
}

void SsaoPass::destroyBaseBindGroup() {
    if (m_baseRhiDevice != nullptr && m_baseBindGroup.isValid()) {
        m_baseRhiDevice->destroyBindGroup(m_baseBindGroup);
    }
    m_baseBindGroup = {};
    m_baseBoundViews = {};
}

void SsaoPass::destroyBaseRhiResources() {
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
        if (m_baseNearestSampler.isValid()) {
            m_baseRhiDevice->destroySampler(m_baseNearestSampler);
        }
        if (m_baseNoiseSampler.isValid()) {
            m_baseRhiDevice->destroySampler(m_baseNoiseSampler);
        }
    }

    m_basePipeline = {};
    m_baseVertexShader = {};
    m_baseFragmentShader = {};
    m_basePipelineLayout = {};
    m_baseBindGroupLayout = {};
    m_baseNearestSampler = {};
    m_baseNoiseSampler = {};
    m_baseRhiDevice = nullptr;
}

bool SsaoPass::ensureFilterRhiPipeline(RhiDevice& rhiDevice) {
    if (m_filterRhiDevice != nullptr && m_filterRhiDevice != &rhiDevice) {
        destroyFilterRhiResources();
    }
    if (m_filterPipeline.isValid()) {
        return true;
    }
    m_filterRhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/ssao_filter.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "SsaoFilter.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_filterVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "SsaoFilter.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_filterFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_filterVertexShader.isValid() || !m_filterFragmentShader.isValid()) {
        destroyFilterRhiResources();
        return false;
    }

    RhiSamplerDesc samplerDesc;
    samplerDesc.minFilter = RhiFilter::Nearest;
    samplerDesc.magFilter = RhiFilter::Nearest;
    samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    samplerDesc.addressU = RhiAddressMode::ClampToEdge;
    samplerDesc.addressV = RhiAddressMode::ClampToEdge;
    samplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_filterSampler = rhiDevice.createSampler(samplerDesc);
    if (!m_filterSampler.isValid()) {
        destroyFilterRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "SsaoFilter.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 3u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    m_filterBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_filterBindGroupLayout.isValid()) {
        destroyFilterRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "SsaoFilter.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_filterBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_filterPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_filterPipelineLayout.isValid()) {
        destroyFilterRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "SsaoFilter.Pipeline";
    pipelineDesc.vertexShader = m_filterVertexShader;
    pipelineDesc.fragmentShader = m_filterFragmentShader;
    pipelineDesc.layout = m_filterPipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::R8Unorm);
    pipelineDesc.blend.attachments.push_back({});
    m_filterPipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_filterPipeline.isValid()) {
        destroyFilterRhiResources();
        return false;
    }

    return true;
}

bool SsaoPass::ensureFilterBindGroup(RhiDevice& rhiDevice,
                                     const std::array<RhiTextureViewHandle, 3>& views) {
    if (!ensureFilterRhiPipeline(rhiDevice)) {
        return false;
    }
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_filterBindGroup.isValid() && sameTextureViews(m_filterBoundViews, views)) {
        return true;
    }

    destroyFilterBindGroup();

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_filterBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler = m_filterSampler;
        bindGroupDesc.entries.push_back(entry);
    }

    m_filterBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_filterBindGroup.isValid()) {
        m_filterBoundViews = {};
        return false;
    }

    m_filterBoundViews = views;
    return true;
}

void SsaoPass::destroyFilterBindGroup() {
    if (m_filterRhiDevice != nullptr && m_filterBindGroup.isValid()) {
        m_filterRhiDevice->destroyBindGroup(m_filterBindGroup);
    }
    m_filterBindGroup = {};
    m_filterBoundViews = {};
}

void SsaoPass::destroyFilterRhiResources() {
    destroyFilterBindGroup();
    if (m_filterRhiDevice != nullptr) {
        if (m_filterPipeline.isValid()) {
            m_filterRhiDevice->destroyPipeline(m_filterPipeline);
        }
        if (m_filterVertexShader.isValid()) {
            m_filterRhiDevice->destroyShader(m_filterVertexShader);
        }
        if (m_filterFragmentShader.isValid()) {
            m_filterRhiDevice->destroyShader(m_filterFragmentShader);
        }
        if (m_filterPipelineLayout.isValid()) {
            m_filterRhiDevice->destroyPipelineLayout(m_filterPipelineLayout);
        }
        if (m_filterBindGroupLayout.isValid()) {
            m_filterRhiDevice->destroyBindGroupLayout(m_filterBindGroupLayout);
        }
        if (m_filterSampler.isValid()) {
            m_filterRhiDevice->destroySampler(m_filterSampler);
        }
    }

    m_filterPipeline = {};
    m_filterVertexShader = {};
    m_filterFragmentShader = {};
    m_filterPipelineLayout = {};
    m_filterBindGroupLayout = {};
    m_filterSampler = {};
    m_filterRhiDevice = nullptr;
}

bool SsaoPass::ensureUpsampleRhiPipeline(RhiDevice& rhiDevice) {
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
        renderer::rhi::loadShaderSource("assets/shaders/ssao_upsample.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "SsaoUpsample.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_upsampleVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "SsaoUpsample.Fragment";
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
    bindGroupLayoutDesc.debugName = "SsaoUpsample.BindGroupLayout";
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
    pipelineLayoutDesc.debugName = "SsaoUpsample.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_upsampleBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_upsamplePipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_upsamplePipelineLayout.isValid()) {
        destroyUpsampleRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "SsaoUpsample.Pipeline";
    pipelineDesc.vertexShader = m_upsampleVertexShader;
    pipelineDesc.fragmentShader = m_upsampleFragmentShader;
    pipelineDesc.layout = m_upsamplePipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::R8Unorm);
    pipelineDesc.blend.attachments.push_back({});
    m_upsamplePipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_upsamplePipeline.isValid()) {
        destroyUpsampleRhiResources();
        return false;
    }

    return true;
}

bool SsaoPass::ensureUpsampleBindGroup(RhiDevice& rhiDevice,
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

void SsaoPass::destroyUpsampleBindGroup() {
    if (m_upsampleRhiDevice != nullptr && m_upsampleBindGroup.isValid()) {
        m_upsampleRhiDevice->destroyBindGroup(m_upsampleBindGroup);
    }
    m_upsampleBindGroup = {};
    m_upsampleBoundViews = {};
}

void SsaoPass::destroyUpsampleRhiResources() {
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

bool SsaoPass::ensureTemporalRhiPipeline(RhiDevice& rhiDevice) {
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
        renderer::rhi::loadShaderSource("assets/shaders/ssao_temporal.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "SsaoTemporal.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_temporalVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "SsaoTemporal.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_temporalFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_temporalVertexShader.isValid() || !m_temporalFragmentShader.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiSamplerDesc nearestSamplerDesc;
    nearestSamplerDesc.minFilter = RhiFilter::Nearest;
    nearestSamplerDesc.magFilter = RhiFilter::Nearest;
    nearestSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    nearestSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_temporalNearestSampler = rhiDevice.createSampler(nearestSamplerDesc);

    RhiSamplerDesc linearSamplerDesc;
    linearSamplerDesc.minFilter = RhiFilter::Linear;
    linearSamplerDesc.magFilter = RhiFilter::Linear;
    linearSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    linearSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_temporalLinearSampler = rhiDevice.createSampler(linearSamplerDesc);
    if (!m_temporalNearestSampler.isValid() || !m_temporalLinearSampler.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "SsaoTemporal.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 4u; ++binding) {
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
    pipelineLayoutDesc.debugName = "SsaoTemporal.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_temporalBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_temporalPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_temporalPipelineLayout.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "SsaoTemporal.Pipeline";
    pipelineDesc.vertexShader = m_temporalVertexShader;
    pipelineDesc.fragmentShader = m_temporalFragmentShader;
    pipelineDesc.layout = m_temporalPipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::R8Unorm);
    pipelineDesc.blend.attachments.push_back({});
    m_temporalPipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_temporalPipeline.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    return true;
}

bool SsaoPass::ensureTemporalBindGroup(RhiDevice& rhiDevice,
                                       const std::array<RhiTextureViewHandle, 4>& views) {
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

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_temporalBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler =
            binding == 1u ? m_temporalLinearSampler : m_temporalNearestSampler;
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

void SsaoPass::destroyTemporalBindGroup() {
    if (m_temporalRhiDevice != nullptr && m_temporalBindGroup.isValid()) {
        m_temporalRhiDevice->destroyBindGroup(m_temporalBindGroup);
    }
    m_temporalBindGroup = {};
    m_temporalBoundViews = {};
}

void SsaoPass::destroyTemporalRhiResources() {
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
