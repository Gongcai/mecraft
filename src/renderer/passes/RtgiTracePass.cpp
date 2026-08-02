#include "RtgiTracePass.h"

#include "renderer/contracts/RtgiSamplingContract.h"
#include "renderer/core/GlobalBindlessSet.h"
#include "renderer/core/RenderScene.h"
#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiShaderSourceLoader.h"
#include "renderer/rhi/SceneTlasCache.h"

#include <cmath>
#include <optional>

namespace {
[[nodiscard]] bool sameHandle(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] bool sameHandle(const RhiBindGroupLayoutHandle lhs, const RhiBindGroupLayoutHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] bool sameHandle(const RhiAccelerationStructureHandle lhs, const RhiAccelerationStructureHandle rhs) {
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
} // namespace

void RtgiTracePass::shutdown() {
    destroyRhiResources();
    m_stats = {};
}

RgPassHandle RtgiTracePass::addGraphPass(RenderGraph& graph, const FrameContext& ctx, const Settings& settings,
                                         const GraphResources& resources, const RgPassHandle dependency) {
    if (!dependency.isValid() || ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        ctx.shared->globalBindlessSet == nullptr || ctx.shared->sceneTlasCache == nullptr ||
        ctx.shared->rhiDevice->backend() != RhiBackend::Vulkan || !ctx.shared->globalBindlessSet->initialized() ||
        !ctx.temporalExtents.renderExtent.isValid() || !std::isfinite(settings.maxRayDistance) ||
        settings.maxRayDistance <= 0.0f || !std::isfinite(settings.minimumRayOriginBias) ||
        settings.minimumRayOriginBias <= 0.0f || settings.minimumRayOriginBias >= settings.maxRayDistance ||
        settings.instanceMask == 0u || !resources.depth.isValid() || !resources.normalAo.isValid() ||
        !resources.materialAux.isValid() || !resources.blueNoise.isValid() ||
        !resources.diffuseRadianceHitDistance.isValid() || !resources.validation.isValid()) {
        return {};
    }

    const std::optional<renderer::rt::SceneTlasView> activeTlas = ctx.shared->sceneTlasCache->activeView();
    if (!activeTlas.has_value() ||
        !sameHandle(ctx.shared->globalBindlessSet->accelerationStructure(), activeTlas->accelerationStructure)) {
        return {};
    }

    const FrameContext* frame = &ctx;
    const GraphResources frameResources = resources;
    const uint64_t sceneTlasRevision = activeTlas->revision;
    RenderGraphPassBuilder trace = graph.addPass({"RTGI.TraceOpaque", RgPassType::Compute, RhiQueueType::Graphics});
    trace.dependsOn(dependency)
        .readTexture(resources.depth, RhiResourceState::DepthRead)
        .readTexture(resources.normalAo, RhiResourceState::ShaderRead)
        .readTexture(resources.materialAux, RhiResourceState::ShaderRead)
        .readTexture(resources.blueNoise, RhiResourceState::ShaderRead)
        .writeTexture(resources.diffuseRadianceHitDistance, RhiResourceState::ShaderWrite)
        .writeTexture(resources.validation, RhiResourceState::ShaderWrite)
        .setExecute([this, frame, settings, frameResources, sceneTlasRevision](RgPassContext& pass) {
            const TraceViews views{pass.textureView(frameResources.depth),
                                   pass.textureView(frameResources.normalAo),
                                   pass.textureView(frameResources.materialAux),
                                   pass.textureView(frameResources.blueNoise),
                                   pass.textureView(frameResources.diffuseRadianceHitDistance),
                                   pass.textureView(frameResources.validation)};
            return recordTrace(pass.commandList(), *frame, settings, views, sceneTlasRevision);
        });
    return trace.handle();
}

bool RtgiTracePass::recordTrace(RhiCommandList& commandList, const FrameContext& ctx, const Settings& settings,
                                const TraceViews& views, const uint64_t sceneTlasRevision) {
    const glm::mat4& inverseViewProjection =
        settings.useJitteredProjection ? ctx.camera.jitteredInvViewProj : ctx.camera.invViewProj;
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr || ctx.shared->globalBindlessSet == nullptr ||
        !finiteMatrix(inverseViewProjection) ||
        !ensurePipeline(*ctx.shared->rhiDevice, ctx.shared->globalBindlessSet->layout()) ||
        !ensureBindGroup(*ctx.shared->rhiDevice, views)) {
        return false;
    }

    const TemporalExtent extent = ctx.temporalExtents.renderExtent;
    renderer::contracts::RtgiTracePushConstants pushConstants;
    pushConstants.inverseViewProjection = inverseViewProjection;
    pushConstants.cameraPositionAndMaxDistance = glm::vec4(ctx.camera.position, settings.maxRayDistance);
    pushConstants.renderExtentAndBias = glm::vec4(static_cast<float>(extent.width), static_cast<float>(extent.height),
                                                  settings.minimumRayOriginBias, 0.0f);
    pushConstants.frameMaskAndFlags =
        glm::uvec4(static_cast<uint32_t>(ctx.frameIndex), static_cast<uint32_t>(settings.instanceMask), 0u, 0u);

    commandList.setComputePipeline(m_pipeline);
    commandList.setBindGroup(0u, ctx.shared->globalBindlessSet->bindGroup());
    commandList.setBindGroup(1u, m_traceBindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch((extent.width + 7u) / 8u, (extent.height + 7u) / 8u, 1u);

    m_stats.dispatched = true;
    m_stats.frameIndex = ctx.frameIndex;
    m_stats.sceneTlasRevision = sceneTlasRevision;
    m_stats.width = extent.width;
    m_stats.height = extent.height;
    m_stats.instanceMask = settings.instanceMask;
    return true;
}

bool RtgiTracePass::ensurePipeline(RhiDevice& rhiDevice, const RhiBindGroupLayoutHandle globalBindlessLayout) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyRhiResources();
    }
    if (m_pipeline.isValid() && sameHandle(m_globalBindlessLayout, globalBindlessLayout)) {
        return true;
    }
    if (m_pipeline.isValid()) {
        destroyRhiResources();
    }
    if (!globalBindlessLayout.isValid()) {
        return false;
    }
    m_rhiDevice = &rhiDevice;
    m_globalBindlessLayout = globalBindlessLayout;

    const std::optional<std::string> source = renderer::rhi::loadShaderSource("assets/shaders/rtgi_trace.comp");
    if (!source.has_value()) {
        destroyRhiResources();
        return false;
    }
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "RTGI.TraceOpaque.Compute";
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

    RhiBindGroupLayoutDesc traceLayoutDesc;
    traceLayoutDesc.debugName = "RTGI.TraceOpaque.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 4u; ++binding) {
        traceLayoutDesc.entries.push_back(
            {binding, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Compute), 1u});
    }
    traceLayoutDesc.entries.push_back({4u, RhiBindingType::StorageTexture, rhiFlag(RhiShaderStage::Compute), 1u});
    traceLayoutDesc.entries.push_back({5u, RhiBindingType::StorageTexture, rhiFlag(RhiShaderStage::Compute), 1u});
    m_traceBindGroupLayout = rhiDevice.createBindGroupLayout(traceLayoutDesc);
    if (!m_traceBindGroupLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "RTGI.TraceOpaque.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(globalBindlessLayout);
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_traceBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(renderer::contracts::RtgiTracePushConstants);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Compute);
    m_pipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_pipelineLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiComputePipelineDesc pipelineDesc;
    pipelineDesc.debugName = "RTGI.TraceOpaque.Pipeline";
    pipelineDesc.computeShader = m_shader;
    pipelineDesc.layout = m_pipelineLayout;
    m_pipeline = rhiDevice.createComputePipeline(pipelineDesc);
    if (!m_pipeline.isValid()) {
        destroyRhiResources();
        return false;
    }
    return true;
}

bool RtgiTracePass::ensureBindGroup(RhiDevice& rhiDevice, const TraceViews& views) {
    const std::array<RhiTextureViewHandle, 6u> boundViews{
        views.depth,     views.normalAo, views.materialAux, views.blueNoise, views.diffuseRadianceHitDistance,
        views.validation};
    for (const RhiTextureViewHandle view : boundViews) {
        if (!view.isValid()) {
            return false;
        }
    }
    bool unchanged = m_traceBindGroup.isValid();
    for (size_t index = 0u; index < boundViews.size(); ++index) {
        unchanged = unchanged && sameHandle(m_boundViews[index], boundViews[index]);
    }
    if (unchanged) {
        return true;
    }
    if (m_traceBindGroup.isValid()) {
        rhiDevice.destroyBindGroup(m_traceBindGroup);
        m_traceBindGroup = {};
    }

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_traceBindGroupLayout;
    for (uint32_t binding = 0u; binding < 4u; ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = boundViews[binding];
        entry.resource.combinedTextureSampler.sampler = m_sampler;
        bindGroupDesc.entries.push_back(entry);
    }
    for (uint32_t binding = 4u; binding < 6u; ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.textureView = boundViews[binding];
        bindGroupDesc.entries.push_back(entry);
    }
    m_traceBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_traceBindGroup.isValid()) {
        m_boundViews = {};
        return false;
    }
    m_boundViews = boundViews;
    return true;
}

void RtgiTracePass::destroyRhiResources() {
    if (m_rhiDevice != nullptr) {
        if (m_traceBindGroup.isValid()) {
            m_rhiDevice->destroyBindGroup(m_traceBindGroup);
        }
        if (m_pipeline.isValid()) {
            m_rhiDevice->destroyPipeline(m_pipeline);
        }
        if (m_pipelineLayout.isValid()) {
            m_rhiDevice->destroyPipelineLayout(m_pipelineLayout);
        }
        if (m_traceBindGroupLayout.isValid()) {
            m_rhiDevice->destroyBindGroupLayout(m_traceBindGroupLayout);
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
    m_globalBindlessLayout = {};
    m_traceBindGroupLayout = {};
    m_pipelineLayout = {};
    m_pipeline = {};
    m_traceBindGroup = {};
    m_boundViews = {};
}
