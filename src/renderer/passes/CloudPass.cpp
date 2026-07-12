#include "CloudPass.h"
#include "../core/RenderScene.h"
#include "../debug/RenderDebugService.h"
#include "../targets/DeferredRenderTargets.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../../resource/ResourceMgr.h"

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

void CloudPass::init(ResourceMgr& resourceMgr) {
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
}

void CloudPass::shutdown() {
    destroyRhiResources();
    destroyNoiseTextureView();
    m_noiseTexture = {};
    m_hasRenderedClouds = false;
}

void CloudPass::invalidateHistory() {
    m_hasRenderedClouds = false;
}

bool CloudPass::shouldRenderClouds(const FrameContext& ctx, const RenderSettings& settings) {
    const int updateInterval = std::clamp(settings.cloud.updateInterval, 1, 8);
    if (!m_hasRenderedClouds || !ctx.hasPreviousFrame || updateInterval <= 1) {
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

void CloudPass::execute(const FrameContext& ctx, const RenderSettings& settings,
                         DeferredRenderTargets& targets) {
    if (!shouldRenderClouds(ctx, settings)) {
        if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
            return;
        }
        RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
        RhiCommandList* commandListStorage = ctx.shared->commandListPool->acquire(RhiCommandListType::Graphics);
    if (commandListStorage == nullptr ||
        !commandListStorage->begin({"RenderPass.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& commandList = *commandListStorage;
        const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
            ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Cloud)
            : GpuTimerSegmentToken{};
        targets.transitionTexture(commandList, targets.historyCloudTexturePrevHandle(),
                                  RhiResourceState::TransferSrc);
        targets.transitionTexture(commandList, targets.cloudTextureHandle(),
                                  RhiResourceState::TransferDst);
        targets.copyHistoryCloudToCloud(commandList);
        targets.transitionTexture(commandList, targets.historyCloudTexturePrevHandle(),
                                  RhiResourceState::ShaderRead);
        targets.transitionTexture(commandList, targets.cloudTextureHandle(),
                                  RhiResourceState::ShaderRead);
        if (ctx.debugService != nullptr) {
            ctx.debugService->endGpuTimer(commandList, timerToken);
        }
        if (!commandList.end()) {
        std::abort();
    }
    {
        RhiCommandList* submittedCommandLists[] = {&commandList};
        if (!rhiDevice.submit({"RenderPass.Submit", submittedCommandLists, 1u})) {
            std::abort();
        }
    }
        return;
    }

    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !targets.ensureCloudTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureGBufferTextureViews(*ctx.shared->rhiDevice) ||
        !targets.ensureSkyCaptureTextureView(*ctx.shared->rhiDevice) ||
        !targets.ensureHistoryCloudTextureViews(*ctx.shared->rhiDevice)) {
        return;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!ensureNoiseTextureView(rhiDevice)) {
        return;
    }
    const std::array<RhiTextureViewHandle, 4> views = {
        targets.depthTextureViewHandle(),
        targets.skyCaptureTextureViewHandle(),
        m_noiseTextureView,
        targets.historyCloudTexturePrevViewHandle()
    };
    if (!ensureRhiPipeline(rhiDevice) || !ensureBindGroup(rhiDevice, views)) {
        return;
    }

    const bool historyAvailable = m_hasRenderedClouds && ctx.hasPreviousFrame;
    CloudParams params{};
    params.invViewProj = ctx.camera.invViewProj;
    params.previousViewProj = ctx.previousViewProj;
    params.cameraPosSkyIntensity = glm::vec4(ctx.camera.position, ctx.skyIntensity);
    params.sunDirectionCloudWetness = glm::vec4(ctx.skyColors.sunDirection,
                                                ctx.weather.cloudWetness);
    params.moonDirectionMoonVisibility = glm::vec4(ctx.skyColors.moonDirection,
                                                    ctx.skyColors.moonVisibility);
    params.cloudDynamicWeatherTime = glm::vec4(ctx.skyIlluminance.cloudDynamicWeather,
                                               ctx.shaderTime);
    params.cloudShape = glm::vec4(ctx.cloud.coverage,
                                  ctx.cloud.density,
                                  ctx.cloud.height,
                                  ctx.cloud.thickness);
    params.planarCloud = glm::vec4(ctx.cloud.planarCoverage,
                                   ctx.cloud.planarDensity,
                                   ctx.cloud.planarAltitude,
                                   0.0f);
    params.timing = glm::vec4(ctx.cloud.timeScale,
                              ctx.weather.lightningFlash,
                              0.0f,
                              0.0f);
    params.controls = glm::ivec4(static_cast<int>(ctx.frameIndex & 0x7fffffffULL),
                                 historyAvailable ? 1 : 0,
                                 0,
                                 0);

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
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.halfWidth())),
        static_cast<uint32_t>(std::max(1, targets.halfHeight()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    RhiCommandList* commandListStorage = ctx.shared->commandListPool->acquire(RhiCommandListType::Graphics);
    if (commandListStorage == nullptr ||
        !commandListStorage->begin({"RenderPass.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& commandList = *commandListStorage;
    targets.transitionTexture(commandList, targets.depthTextureHandle(),
                              RhiResourceState::DepthRead);
    targets.transitionTexture(commandList, targets.skyCaptureTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.historyCloudTexturePrevHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.cloudTextureHandle(),
                              RhiResourceState::RenderTarget);
    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Cloud)
        : GpuTimerSegmentToken{};
    commandList.bufferBarrier({m_uniformBuffer, RhiResourceState::UniformBuffer,
                               RhiResourceState::TransferDst});
    commandList.updateBuffer(m_uniformBuffer, 0u, &params, sizeof(params));
    commandList.bufferBarrier({m_uniformBuffer, RhiResourceState::TransferDst,
                               RhiResourceState::UniformBuffer});
    commandList.beginRendering(renderingInfo);
    commandList.setGraphicsPipeline(m_pipeline);
    commandList.setBindGroup(0u, m_bindGroup);
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    targets.transitionTexture(commandList, targets.cloudTextureHandle(),
                              RhiResourceState::ShaderRead);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    if (!commandList.end()) {
        std::abort();
    }
    {
        RhiCommandList* submittedCommandLists[] = {&commandList};
        if (!rhiDevice.submit({"RenderPass.Submit", submittedCommandLists, 1u})) {
            std::abort();
        }
    }

    m_lastCameraPos = ctx.camera.position;
    m_lastWeatherSignal = ctx.weather.wetness + ctx.weather.storm +
                          ctx.weather.lightningFlash * 4.0f;
    m_hasRenderedClouds = true;
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
    uniformBufferDesc.usage = rhiFlag(RhiBufferUsage::Uniform) |
                              rhiFlag(RhiBufferUsage::TransferDst);
    uniformBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    uniformBufferDesc.initialState = RhiResourceState::UniformBuffer;
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
    if (!m_nearestSampler.isValid() || !m_linearSampler.isValid() ||
        !m_noiseSampler.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "Cloud.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 4u; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    bindGroupLayoutDesc.entries.push_back({
        4u,
        RhiBindingType::UniformBuffer,
        rhiFlag(RhiShaderStage::Fragment),
        1u
    });
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

bool CloudPass::ensureBindGroup(
    RhiDevice& rhiDevice,
    const std::array<RhiTextureViewHandle, 4>& views) {
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
    const RhiSamplerHandle samplers[4] = {
        m_nearestSampler,
        m_linearSampler,
        m_noiseSampler,
        m_linearSampler
    };
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
        const RhiSamplerHandle samplers[] = {
            m_nearestSampler,
            m_linearSampler,
            m_noiseSampler
        };
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
