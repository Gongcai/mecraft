#include "RtgiSignalPackPass.h"

#include "renderer/core/RenderScene.h"
#include "renderer/debug/RenderDebugService.h"
#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiShaderSourceLoader.h"

#include <cstddef>
#include <cmath>
#include <optional>

namespace {
[[nodiscard]] bool sameHandle(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] bool sameHandle(const RgTextureHandle lhs, const RgTextureHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] bool finiteMatrix(const glm::mat4& matrix) {
    for (uint32_t column = 0u; column < 4u; ++column) {
        for (uint32_t row = 0u; row < 4u; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool textureViewMatches(RhiDevice& rhiDevice, const RhiTextureViewHandle view,
                                      const RhiTextureFormat format, const RhiTextureUsage requiredUsage,
                                      const uint32_t width, const uint32_t height) {
    RhiTextureViewDesc viewDesc;
    RhiTextureDesc textureDesc;
    return view.isValid() && rhiDevice.getTextureViewDesc(view, viewDesc) &&
           rhiDevice.getTextureDesc(viewDesc.texture, textureDesc) &&
           viewDesc.viewType == RhiTextureViewType::Texture2D && viewDesc.format == format && viewDesc.baseMip == 0u &&
           viewDesc.mipCount == 1u && viewDesc.baseLayer == 0u && viewDesc.layerCount == 1u &&
           textureDesc.dimension == RhiTextureDimension::Texture2D && textureDesc.format == format &&
           textureDesc.width == width && textureDesc.height == height && textureDesc.depthOrLayers == 1u &&
           textureDesc.mipLevels == 1u && textureDesc.sampleCount == 1u &&
           (textureDesc.usage & rhiFlag(requiredUsage)) != 0u;
}
} // namespace

void RtgiSignalPackPass::shutdown() {
    destroyRhiResources();
    m_stats = {};
}

RgPassHandle RtgiSignalPackPass::addGraphPass(RenderGraph& graph, const FrameContext& ctx, const Settings& settings,
                                              const GraphResources& resources, const RgPassHandle dependency) {
    if (!dependency.isValid() || ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        ctx.shared->rhiDevice->backend() != RhiBackend::Vulkan || !ctx.temporalExtents.renderExtent.isValid() ||
        !renderer::contracts::rtgiReblurHitDistanceParametersValid(settings.reblurHitDistance) ||
        !std::isfinite(settings.diffuseRoughness) || settings.diffuseRoughness < 0.0f ||
        settings.diffuseRoughness > 1.0f || !std::isfinite(ctx.preExposure) || ctx.preExposure <= 0.0f ||
        !std::isfinite(ctx.previousPreExposure) || ctx.previousPreExposure <= 0.0f ||
        !resources.rawDiffuseRadianceHitDistance.isValid() || !resources.depth.isValid() ||
        !resources.relaxDiffuseRadianceHitDistance.isValid() || !resources.reblurDiffuseRadianceHitDistance.isValid() ||
        !resources.validation.isValid() ||
        sameHandle(resources.rawDiffuseRadianceHitDistance, resources.relaxDiffuseRadianceHitDistance) ||
        sameHandle(resources.rawDiffuseRadianceHitDistance, resources.reblurDiffuseRadianceHitDistance) ||
        sameHandle(resources.relaxDiffuseRadianceHitDistance, resources.reblurDiffuseRadianceHitDistance)) {
        return {};
    }

    const FrameContext* frame = &ctx;
    const GraphResources frameResources = resources;
    RenderGraphPassBuilder pack = graph.addPass({"RTGI.SignalPack", RgPassType::Compute, RhiQueueType::Graphics});
    pack.dependsOn(dependency)
        .readTexture(resources.rawDiffuseRadianceHitDistance, RhiResourceState::ShaderRead)
        .readTexture(resources.depth, RhiResourceState::DepthRead)
        .writeTexture(resources.relaxDiffuseRadianceHitDistance, RhiResourceState::ShaderWrite)
        .writeTexture(resources.reblurDiffuseRadianceHitDistance, RhiResourceState::ShaderWrite)
        .readWriteTexture(resources.validation, RhiResourceState::ShaderWrite)
        .setExecute([this, frame, settings, frameResources](RgPassContext& pass) {
            const PackViews views{pass.textureView(frameResources.rawDiffuseRadianceHitDistance),
                                  pass.textureView(frameResources.depth),
                                  pass.textureView(frameResources.relaxDiffuseRadianceHitDistance),
                                  pass.textureView(frameResources.reblurDiffuseRadianceHitDistance),
                                  pass.textureView(frameResources.validation)};
            return recordPack(pass.commandList(), *frame, settings, views);
        });
    return pack.handle();
}

bool RtgiSignalPackPass::recordPack(RhiCommandList& commandList, const FrameContext& ctx, const Settings& settings,
                                    const PackViews& views) {
    const glm::mat4& inverseViewProjection =
        settings.useJitteredProjection ? ctx.camera.jitteredInvViewProj : ctx.camera.invViewProj;
    const glm::mat4 inverseProjection = ctx.camera.view * inverseViewProjection;
    const TemporalExtent extent = ctx.temporalExtents.renderExtent;
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr || !finiteMatrix(ctx.camera.view) ||
        !finiteMatrix(inverseViewProjection) || !finiteMatrix(inverseProjection) || !std::isfinite(ctx.preExposure) ||
        ctx.preExposure <= 0.0f || !std::isfinite(ctx.previousPreExposure) || ctx.previousPreExposure <= 0.0f ||
        !ensurePipeline(*ctx.shared->rhiDevice) ||
        !ensureBindGroup(*ctx.shared->rhiDevice, views, extent.width, extent.height)) {
        return false;
    }

    renderer::contracts::RtgiSignalPackPushConstants pushConstants;
    pushConstants.inverseProjection = inverseProjection;
    pushConstants.renderExtentAndInverse =
        glm::vec4(static_cast<float>(extent.width), static_cast<float>(extent.height),
                  1.0f / static_cast<float>(extent.width), 1.0f / static_cast<float>(extent.height));
    pushConstants.reblurParametersAndDiffuseRoughness =
        glm::vec4(settings.reblurHitDistance.constantScale, settings.reblurHitDistance.viewZScale,
                  settings.reblurHitDistance.roughnessScale, settings.diffuseRoughness);
    pushConstants.preExposureAndInverse = glm::vec4(ctx.preExposure, 1.0f / ctx.preExposure, ctx.previousPreExposure,
                                                    ctx.previousPreExposure / ctx.preExposure);

    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
                                              ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Rtgi)
                                              : GpuTimerSegmentToken{};
    commandList.setComputePipeline(m_pipeline);
    commandList.setBindGroup(0u, m_bindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch((extent.width + 7u) / 8u, (extent.height + 7u) / 8u, 1u);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, gpuTimer);
    }

    m_stats.dispatched = true;
    m_stats.width = extent.width;
    m_stats.height = extent.height;
    m_stats.reblurHitDistance = settings.reblurHitDistance;
    m_stats.diffuseRoughness = settings.diffuseRoughness;
    m_stats.preExposure = ctx.preExposure;
    m_stats.previousPreExposure = ctx.previousPreExposure;
    return true;
}

bool RtgiSignalPackPass::ensurePipeline(RhiDevice& rhiDevice) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyRhiResources();
    }
    if (m_pipeline.isValid()) {
        return true;
    }
    m_rhiDevice = &rhiDevice;

    const std::optional<std::string> source =
        renderer::rhi::loadShaderSource("assets/shaders/rtgi_nrd_signal_pack.comp");
    if (!source.has_value()) {
        destroyRhiResources();
        return false;
    }
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "RTGI.SignalPack.Compute";
    shaderDesc.stage = RhiShaderStage::Compute;
    shaderDesc.source = source->c_str();
    shaderDesc.sourceSize = source->size();
    m_shader = rhiDevice.createShader(shaderDesc);
    if (!m_shader.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiSamplerDesc samplerDesc;
    samplerDesc.minFilter = RhiFilter::Nearest;
    samplerDesc.magFilter = RhiFilter::Nearest;
    samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    samplerDesc.addressU = RhiAddressMode::ClampToEdge;
    samplerDesc.addressV = RhiAddressMode::ClampToEdge;
    samplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_sampler = rhiDevice.createSampler(samplerDesc);
    if (!m_sampler.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc layoutDesc;
    layoutDesc.debugName = "RTGI.SignalPack.BindGroupLayout";
    layoutDesc.entries.push_back({0u, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Compute), 1u});
    layoutDesc.entries.push_back({1u, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Compute), 1u});
    layoutDesc.entries.push_back({2u, RhiBindingType::StorageTexture, rhiFlag(RhiShaderStage::Compute), 1u});
    layoutDesc.entries.push_back({3u, RhiBindingType::StorageTexture, rhiFlag(RhiShaderStage::Compute), 1u});
    layoutDesc.entries.push_back({4u, RhiBindingType::StorageTexture, rhiFlag(RhiShaderStage::Compute), 1u});
    m_bindGroupLayout = rhiDevice.createBindGroupLayout(layoutDesc);
    if (!m_bindGroupLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "RTGI.SignalPack.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes =
        static_cast<uint32_t>(sizeof(renderer::contracts::RtgiSignalPackPushConstants));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Compute);
    m_pipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_pipelineLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiComputePipelineDesc pipelineDesc;
    pipelineDesc.debugName = "RTGI.SignalPack.Pipeline";
    pipelineDesc.computeShader = m_shader;
    pipelineDesc.layout = m_pipelineLayout;
    m_pipeline = rhiDevice.createComputePipeline(pipelineDesc);
    if (!m_pipeline.isValid()) {
        destroyRhiResources();
        return false;
    }
    return true;
}

bool RtgiSignalPackPass::ensureBindGroup(RhiDevice& rhiDevice, const PackViews& views, const uint32_t width,
                                         const uint32_t height) {
    const std::array<RhiTextureViewHandle, 5u> boundViews{views.rawDiffuseRadianceHitDistance, views.depth,
                                                          views.relaxDiffuseRadianceHitDistance,
                                                          views.reblurDiffuseRadianceHitDistance, views.validation};
    if (sameHandle(boundViews[0], boundViews[2]) || sameHandle(boundViews[0], boundViews[3]) ||
        sameHandle(boundViews[2], boundViews[3]) ||
        !textureViewMatches(rhiDevice, boundViews[0], RhiTextureFormat::Rgba16Float, RhiTextureUsage::Sampled, width,
                            height) ||
        !textureViewMatches(rhiDevice, boundViews[1], RhiTextureFormat::Depth32Float, RhiTextureUsage::Sampled, width,
                            height) ||
        !textureViewMatches(rhiDevice, boundViews[2], RhiTextureFormat::Rgba16Float, RhiTextureUsage::Storage, width,
                            height) ||
        !textureViewMatches(rhiDevice, boundViews[3], RhiTextureFormat::Rgba16Float, RhiTextureUsage::Storage, width,
                            height) ||
        !textureViewMatches(rhiDevice, boundViews[4], RhiTextureFormat::Rg32Uint, RhiTextureUsage::Storage, width,
                            height)) {
        return false;
    }

    bool unchanged = m_bindGroup.isValid();
    for (size_t index = 0u; index < boundViews.size(); ++index) {
        unchanged = unchanged && sameHandle(m_boundViews[index], boundViews[index]);
    }
    if (unchanged) {
        return true;
    }
    if (m_bindGroup.isValid()) {
        rhiDevice.destroyBindGroup(m_bindGroup);
        m_bindGroup = {};
    }

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_bindGroupLayout;
    for (uint32_t binding = 0u; binding < 2u; ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler = {boundViews[binding], m_sampler};
        bindGroupDesc.entries.push_back(entry);
    }
    for (uint32_t binding = 2u; binding < static_cast<uint32_t>(boundViews.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.textureView = boundViews[binding];
        bindGroupDesc.entries.push_back(entry);
    }
    m_bindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_bindGroup.isValid()) {
        m_boundViews = {};
        return false;
    }
    m_boundViews = boundViews;
    return true;
}

void RtgiSignalPackPass::destroyRhiResources() {
    if (m_rhiDevice != nullptr) {
        if (m_bindGroup.isValid()) {
            m_rhiDevice->destroyBindGroup(m_bindGroup);
        }
        if (m_pipeline.isValid()) {
            m_rhiDevice->destroyPipeline(m_pipeline);
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
        if (m_shader.isValid()) {
            m_rhiDevice->destroyShader(m_shader);
        }
    }
    m_rhiDevice = nullptr;
    m_shader = {};
    m_sampler = {};
    m_bindGroupLayout = {};
    m_pipelineLayout = {};
    m_pipeline = {};
    m_bindGroup = {};
    m_boundViews = {};
}
