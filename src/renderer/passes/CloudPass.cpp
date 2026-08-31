#include "CloudPass.h"
#include "../core/RenderScene.h"
#include "../debug/RenderDebugService.h"
#include "../targets/DeferredRenderTargets.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../../resource/GameResources.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cstddef>
#include <cmath>
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

struct alignas(16) CloudParams {
    glm::mat4 invViewProj;
    glm::mat4 previousViewProj;
    glm::vec4 cameraPosSkyIntensity;
    glm::vec4 sunDirectionCloudWetness;
    glm::vec4 moonDirectionMoonVisibility;
    glm::vec4 cloudDynamicWeatherTime;
    glm::vec4 cloudShape;
    glm::vec4 planarCloud;
    glm::vec4 timing;
    glm::ivec4 controls;
};
static_assert(sizeof(CloudParams) == 256u);
} // namespace

void CloudPass::init(GameResources& resources) {
    m_noiseTexture = resources.texture2D.getHandle("shader_noise2d");
}

void CloudPass::shutdown() {
    destroyRhiResources();
    destroyNoiseTextureView();
    m_noiseTexture = {};
    m_hasRenderedClouds = false;
    m_graphFramePrepared = false;
    m_pendingCloudRender = false;
}

void CloudPass::invalidateHistory() {
    m_hasRenderedClouds = false;
}

bool CloudPass::shouldRenderClouds(const FrameContext& ctx, const RenderSettings& settings) const {
    const int updateInterval = std::clamp(settings.cloud.updateInterval, 1, 8);
    if (!m_hasRenderedClouds || ownerRequiresTemporalReset(TemporalHistoryOwner::Clouds, ctx.temporalResetReasons) || updateInterval <= 1) {
        return true;
    }

    const float weatherSignal = ctx.weather.wetness + ctx.weather.storm + ctx.weather.lightningFlash * 4.0f;
    const bool weatherChanged = std::abs(weatherSignal - m_lastWeatherSignal) > 0.025f;
    const glm::vec3 cameraDelta = ctx.camera.position - m_lastCameraPos;
    const bool movedFar = glm::dot(cameraDelta, cameraDelta) > 36.0f;
    if (weatherChanged || movedFar) {
        return true;
    }

    return (ctx.frameIndex % static_cast<uint64_t>(updateInterval)) == 0;
}

RgPassHandle CloudPass::addGraphPass(RenderGraph& graph, const FrameContext& ctx, const RenderSettings& settings,
                                     DeferredRenderTargets& targets, const GraphResources& resources,
                                     const RgPassHandle dependency, const bool useAsyncCompute) {
    if (m_graphFramePrepared || !dependency.isValid() || !resources.depth.isValid() ||
        !resources.skyCapture.isValid() || !resources.noise.isValid() || !resources.historyPrevious.isValid() ||
        !resources.cloud.isValid()) {
        return {};
    }

    const bool renderClouds = shouldRenderClouds(ctx, settings);
    // Blits require the graphics queue, so only real render frames may move
    // to async compute; history-reuse frames always stay on graphics.
    const bool computeQueue = renderClouds && useAsyncCompute;
    const FrameContext* frame = &ctx;
    DeferredRenderTargets* frameTargets = &targets;
    RenderGraphPassBuilder cloudPass =
        graph.addPass({renderClouds ? "Cloud.Render" : "Cloud.HistoryCopy",
                       renderClouds ? (computeQueue ? RgPassType::Compute : RgPassType::Graphics) : RgPassType::Copy,
                       computeQueue ? RhiQueueType::Compute : RhiQueueType::Graphics});
    cloudPass.dependsOn(dependency);
    if (computeQueue) {
        cloudPass.readTexture(resources.depth, RhiResourceState::DepthRead)
            .readTexture(resources.skyCapture, RhiResourceState::ShaderRead)
            .readTexture(resources.noise, RhiResourceState::ShaderRead)
            .readTexture(resources.historyPrevious, RhiResourceState::ShaderRead)
            .writeTexture(resources.cloud, RhiResourceState::ShaderWrite)
            .setExecute([this, frame, frameTargets](RgPassContext& pass) {
                return recordCloudCompute(pass.commandList(), *frame, *frameTargets);
            });
    } else if (renderClouds) {
        cloudPass.readTexture(resources.depth, RhiResourceState::DepthRead)
            .readTexture(resources.skyCapture, RhiResourceState::ShaderRead)
            .readTexture(resources.noise, RhiResourceState::ShaderRead)
            .readTexture(resources.historyPrevious, RhiResourceState::ShaderRead)
            .writeTexture(resources.cloud, RhiResourceState::RenderTarget)
            .setExecute([this, frame, frameTargets](RgPassContext& pass) {
                return recordCloud(pass.commandList(), *frame, *frameTargets);
            });
    } else {
        cloudPass.readTexture(resources.historyPrevious, RhiResourceState::TransferSrc)
            .writeTexture(resources.cloud, RhiResourceState::TransferDst)
            .setExecute([this, frame, frameTargets](RgPassContext& pass) {
                return recordHistoryCopy(pass.commandList(), *frame, *frameTargets);
            });
    }

    m_graphFramePrepared = true;
    m_pendingCloudRender = renderClouds;
    m_pendingCameraPos = ctx.camera.position;
    m_pendingWeatherSignal = ctx.weather.wetness + ctx.weather.storm + ctx.weather.lightningFlash * 4.0f;
    return cloudPass.handle();
}

void CloudPass::finishGraphExecution(const bool succeeded) {
    if (!m_graphFramePrepared) {
        std::abort();
    }
    if (succeeded && m_pendingCloudRender) {
        m_lastCameraPos = m_pendingCameraPos;
        m_lastWeatherSignal = m_pendingWeatherSignal;
        m_hasRenderedClouds = true;
    }
    m_graphFramePrepared = false;
    m_pendingCloudRender = false;
}

bool CloudPass::recordHistoryCopy(RhiCommandList& commandList, const FrameContext& ctx,
                                  DeferredRenderTargets& targets) {
    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
                                                ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Cloud)
                                                : GpuTimerSegmentToken{};
    RhiTextureBlit blit;
    blit.src = targets.historyCloudTexturePrevHandle();
    blit.dst = targets.cloudTextureHandle();
    commandList.blitTexture(blit);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

namespace {
[[nodiscard]] CloudParams buildCloudParams(const FrameContext& ctx, const bool historyAvailable) {
    CloudParams params{};
    params.invViewProj = ctx.camera.invViewProj;
    params.previousViewProj = ctx.previousViewProj;
    params.cameraPosSkyIntensity = glm::vec4(ctx.camera.position, ctx.skyIntensity);
    params.sunDirectionCloudWetness = glm::vec4(ctx.skyColors.sunDirection, ctx.weather.cloudWetness);
    params.moonDirectionMoonVisibility = glm::vec4(ctx.skyColors.moonDirection, ctx.skyColors.moonVisibility);
    params.cloudDynamicWeatherTime = glm::vec4(ctx.skyIlluminance.cloudDynamicWeather, ctx.shaderTime);
    params.cloudShape = glm::vec4(ctx.cloud.coverage, ctx.cloud.density, ctx.cloud.height, ctx.cloud.thickness);
    params.planarCloud = glm::vec4(ctx.cloud.planarCoverage, ctx.cloud.planarDensity, ctx.cloud.planarAltitude, 0.0f);
    params.timing = glm::vec4(ctx.cloud.timeScale, ctx.weather.lightningFlash, ctx.preExposure, 0.0f);
    params.controls = glm::ivec4(static_cast<int>(ctx.frameIndex & 0x7fffffffULL), historyAvailable ? 1 : 0, 0, 0);
    return params;
}
} // namespace

bool CloudPass::recordCloud(RhiCommandList& commandList, const FrameContext& ctx, DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureCloudTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice) ||
        !targets.ensureSkyCaptureTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureHistoryCloudTextureViews(*ctx.shared->rhiDevice)) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!ensureNoiseTextureView(rhiDevice)) {
        return false;
    }
    const std::array<RhiTextureViewHandle, 4> views = {targets.depthTextureViewHandle(),
                                                       targets.skyCaptureTextureViewHandle(), m_noiseTextureView,
                                                       targets.historyCloudTexturePrevViewHandle()};
    if (!ensureRhiPipeline(rhiDevice) || !ensureBindGroup(rhiDevice, views)) {
        return false;
    }

    const bool historyAvailable = m_hasRenderedClouds && !ownerRequiresTemporalReset(TemporalHistoryOwner::Clouds, ctx.temporalResetReasons);
    const CloudParams params = buildCloudParams(ctx, historyAvailable);

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.cloudTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "Cloud";
    renderingInfo.renderArea = {0, 0, static_cast<uint32_t>(std::max(1, targets.halfWidth())),
                                static_cast<uint32_t>(std::max(1, targets.halfHeight()))};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
                                                ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Cloud)
                                                : GpuTimerSegmentToken{};
    commandList.bufferBarrier({m_uniformBuffer, RhiResourceState::UniformBuffer, RhiResourceState::TransferDst});
    commandList.updateBuffer(m_uniformBuffer, 0u, &params, sizeof(params));
    commandList.bufferBarrier({m_uniformBuffer, RhiResourceState::TransferDst, RhiResourceState::UniformBuffer});
    commandList.beginRendering(renderingInfo);
    commandList.setGraphicsPipeline(m_pipeline);
    commandList.setBindGroup(0u, m_bindGroup);
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool CloudPass::recordCloudCompute(RhiCommandList& commandList, const FrameContext& ctx,
                                   DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureCloudTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice) ||
        !targets.ensureSkyCaptureTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureHistoryCloudTextureViews(*ctx.shared->rhiDevice)) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!ensureNoiseTextureView(rhiDevice)) {
        return false;
    }
    const std::array<RhiTextureViewHandle, 4> views = {targets.depthTextureViewHandle(),
                                                       targets.skyCaptureTextureViewHandle(), m_noiseTextureView,
                                                       targets.historyCloudTexturePrevViewHandle()};
    if (!ensureComputeRhiPipeline(rhiDevice) ||
        !ensureComputeBindGroup(rhiDevice, views, targets.cloudTextureViewHandle())) {
        return false;
    }

    const bool historyAvailable = m_hasRenderedClouds && !ownerRequiresTemporalReset(TemporalHistoryOwner::Clouds, ctx.temporalResetReasons);
    const CloudParams params = buildCloudParams(ctx, historyAvailable);

    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
                                                ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Cloud)
                                                : GpuTimerSegmentToken{};
    commandList.bufferBarrier({m_computeUniformBuffer, RhiResourceState::UniformBuffer, RhiResourceState::TransferDst});
    commandList.updateBuffer(m_computeUniformBuffer, 0u, &params, sizeof(params));
    commandList.bufferBarrier({m_computeUniformBuffer, RhiResourceState::TransferDst, RhiResourceState::UniformBuffer});
    commandList.setComputePipeline(m_computePipeline);
    commandList.setBindGroup(0u, m_computeBindGroup);
    const uint32_t width = static_cast<uint32_t>(std::max(1, targets.halfWidth()));
    const uint32_t height = static_cast<uint32_t>(std::max(1, targets.halfHeight()));
    commandList.dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool CloudPass::ensureComputeRhiPipeline(RhiDevice& rhiDevice) {
    // The graphics path owns the shared samplers, so guarantee it first.
    if (!ensureRhiPipeline(rhiDevice)) {
        return false;
    }
    if (m_computePipeline.isValid()) {
        return true;
    }

    const std::optional<std::string> computeSource =
        renderer::rhi::loadShaderSource("assets/shaders/cloud_target.comp");
    if (!computeSource.has_value()) {
        return false;
    }
    RhiShaderDesc computeDesc;
    computeDesc.debugName = "Cloud.Compute";
    computeDesc.stage = RhiShaderStage::Compute;
    computeDesc.source = computeSource->c_str();
    computeDesc.sourceSize = computeSource->size();
    m_computeShader = rhiDevice.createShader(computeDesc);
    if (!m_computeShader.isValid()) {
        return false;
    }

    // Buffers use exclusive sharing, so the compute path keeps its own
    // uniform buffer that only ever lives on the compute queue family.
    RhiBufferDesc uniformBufferDesc;
    uniformBufferDesc.debugName = "Cloud.ComputeParams";
    uniformBufferDesc.size = sizeof(CloudParams);
    uniformBufferDesc.usage = rhiFlag(RhiBufferUsage::Uniform) | rhiFlag(RhiBufferUsage::TransferDst);
    uniformBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    uniformBufferDesc.initialState = RhiResourceState::UniformBuffer;
    uniformBufferDesc.memoryCategory = RhiMemoryCategory::Uniform;
    m_computeUniformBuffer = rhiDevice.createBuffer(uniformBufferDesc, nullptr, 0u);
    if (!m_computeUniformBuffer.isValid()) {
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "Cloud.ComputeBindGroupLayout";
    for (uint32_t binding = 0u; binding < 4u; ++binding) {
        bindGroupLayoutDesc.entries.push_back(
            {binding, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Compute), 1u});
    }
    bindGroupLayoutDesc.entries.push_back({4u, RhiBindingType::UniformBuffer, rhiFlag(RhiShaderStage::Compute), 1u});
    bindGroupLayoutDesc.entries.push_back({5u, RhiBindingType::StorageTexture, rhiFlag(RhiShaderStage::Compute), 1u});
    m_computeBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_computeBindGroupLayout.isValid()) {
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "Cloud.ComputePipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_computeBindGroupLayout);
    m_computePipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_computePipelineLayout.isValid()) {
        return false;
    }

    RhiComputePipelineDesc pipelineDesc;
    pipelineDesc.debugName = "Cloud.ComputePipeline";
    pipelineDesc.computeShader = m_computeShader;
    pipelineDesc.layout = m_computePipelineLayout;
    m_computePipeline = rhiDevice.createComputePipeline(pipelineDesc);
    return m_computePipeline.isValid();
}

bool CloudPass::ensureComputeBindGroup(RhiDevice& rhiDevice, const std::array<RhiTextureViewHandle, 4>& views,
                                       const RhiTextureViewHandle cloudStorageView) {
    if (!ensureComputeRhiPipeline(rhiDevice) || !cloudStorageView.isValid()) {
        return false;
    }
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    const std::array<RhiTextureViewHandle, 5> boundViews = {views[0], views[1], views[2], views[3], cloudStorageView};
    if (m_computeBindGroup.isValid() && sameTextureViews(m_computeBoundViews, boundViews)) {
        return true;
    }

    destroyComputeBindGroup();
    const RhiSamplerHandle samplers[4] = {m_nearestSampler, m_linearSampler, m_noiseSampler, m_linearSampler};
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_computeBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler = samplers[binding];
        bindGroupDesc.entries.push_back(entry);
    }

    RhiBindGroupEntry uniformEntry;
    uniformEntry.binding = 4u;
    uniformEntry.resource.buffer.buffer = m_computeUniformBuffer;
    uniformEntry.resource.buffer.offset = 0u;
    uniformEntry.resource.buffer.range = sizeof(CloudParams);
    bindGroupDesc.entries.push_back(uniformEntry);

    RhiBindGroupEntry storageEntry;
    storageEntry.binding = 5u;
    storageEntry.resource.textureView = cloudStorageView;
    bindGroupDesc.entries.push_back(storageEntry);

    m_computeBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_computeBindGroup.isValid()) {
        m_computeBoundViews = {};
        return false;
    }
    m_computeBoundViews = boundViews;
    return true;
}

void CloudPass::destroyComputeBindGroup() {
    if (m_rhiDevice != nullptr && m_computeBindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_computeBindGroup);
    }
    m_computeBindGroup = {};
    m_computeBoundViews = {};
}

bool CloudPass::ensureNoiseTextureView(RhiDevice& rhiDevice) {
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

void CloudPass::destroyNoiseTextureView() {
    if (m_noiseViewDevice != nullptr && m_noiseTextureView.isValid()) {
        m_noiseViewDevice->destroyTextureView(m_noiseTextureView);
    }
    m_noiseTextureView = {};
    m_noiseViewDevice = nullptr;
}

bool CloudPass::ensureRhiPipeline(RhiDevice& rhiDevice) {
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
        renderer::rhi::loadShaderSource("assets/shaders/cloud_target.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "Cloud.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_vertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "Cloud.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_vertexShader.isValid() || !m_fragmentShader.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiBufferDesc uniformBufferDesc;
    uniformBufferDesc.debugName = "Cloud.Params";
    uniformBufferDesc.size = sizeof(CloudParams);
    uniformBufferDesc.usage = rhiFlag(RhiBufferUsage::Uniform) | rhiFlag(RhiBufferUsage::TransferDst);
    uniformBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    uniformBufferDesc.initialState = RhiResourceState::UniformBuffer;
    uniformBufferDesc.memoryCategory = RhiMemoryCategory::Uniform;
    m_uniformBuffer = rhiDevice.createBuffer(uniformBufferDesc, nullptr, 0u);
    if (!m_uniformBuffer.isValid()) {
        destroyRhiResources();
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
    m_nearestSampler = createSampler(RhiFilter::Nearest, RhiAddressMode::ClampToEdge);
    m_linearSampler = createSampler(RhiFilter::Linear, RhiAddressMode::ClampToEdge);
    m_noiseSampler = createSampler(RhiFilter::Linear, RhiAddressMode::Repeat);
    if (!m_nearestSampler.isValid() || !m_linearSampler.isValid() || !m_noiseSampler.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "Cloud.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 4u; ++binding) {
        bindGroupLayoutDesc.entries.push_back(
            {binding, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    }
    bindGroupLayoutDesc.entries.push_back({4u, RhiBindingType::UniformBuffer, rhiFlag(RhiShaderStage::Fragment), 1u});
    m_bindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_bindGroupLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "Cloud.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    m_pipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_pipelineLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "Cloud.Pipeline";
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

bool CloudPass::ensureBindGroup(RhiDevice& rhiDevice, const std::array<RhiTextureViewHandle, 4>& views) {
    if (!ensureRhiPipeline(rhiDevice)) {
        return false;
    }
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_bindGroup.isValid() && sameTextureViews(m_boundViews, views)) {
        return true;
    }

    destroyBindGroup();
    const RhiSamplerHandle samplers[4] = {m_nearestSampler, m_linearSampler, m_noiseSampler, m_linearSampler};
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_bindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler = samplers[binding];
        bindGroupDesc.entries.push_back(entry);
    }

    RhiBindGroupEntry uniformEntry;
    uniformEntry.binding = 4u;
    uniformEntry.resource.buffer.buffer = m_uniformBuffer;
    uniformEntry.resource.buffer.offset = 0u;
    uniformEntry.resource.buffer.range = sizeof(CloudParams);
    bindGroupDesc.entries.push_back(uniformEntry);

    m_bindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_bindGroup.isValid()) {
        m_boundViews = {};
        return false;
    }

    m_boundViews = views;
    return true;
}

void CloudPass::destroyBindGroup() {
    if (m_rhiDevice != nullptr && m_bindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_bindGroup);
    }
    m_bindGroup = {};
    m_boundViews = {};
}

void CloudPass::destroyRhiResources() {
    destroyBindGroup();
    destroyComputeBindGroup();
    if (m_rhiDevice != nullptr) {
        if (m_computePipeline.isValid()) {
            m_rhiDevice->destroyPipeline(m_computePipeline);
        }
        if (m_computeShader.isValid()) {
            m_rhiDevice->destroyShader(m_computeShader);
        }
        if (m_computePipelineLayout.isValid()) {
            m_rhiDevice->destroyPipelineLayout(m_computePipelineLayout);
        }
        if (m_computeBindGroupLayout.isValid()) {
            m_rhiDevice->destroyBindGroupLayout(m_computeBindGroupLayout);
        }
        if (m_computeUniformBuffer.isValid()) {
            m_rhiDevice->destroyBuffer(m_computeUniformBuffer);
        }
    }
    m_computePipeline = {};
    m_computeShader = {};
    m_computePipelineLayout = {};
    m_computeBindGroupLayout = {};
    m_computeUniformBuffer = {};
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
        if (m_uniformBuffer.isValid()) {
            m_rhiDevice->destroyBuffer(m_uniformBuffer);
        }
        const RhiSamplerHandle samplers[] = {m_nearestSampler, m_linearSampler, m_noiseSampler};
        for (const RhiSamplerHandle sampler : samplers) {
            if (sampler.isValid()) {
                m_rhiDevice->destroySampler(sampler);
            }
        }
    }

    m_uniformBuffer = {};
    m_nearestSampler = {};
    m_linearSampler = {};
    m_noiseSampler = {};
    m_bindGroupLayout = {};
    m_pipelineLayout = {};
    m_vertexShader = {};
    m_fragmentShader = {};
    m_pipeline = {};
    m_rhiDevice = nullptr;
}
