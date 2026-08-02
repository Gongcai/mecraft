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
constexpr uint8_t kRtgiKnownInstanceMask =
    renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::GiOpaque) |
    renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::GiCutout);

[[nodiscard]] bool sameHandle(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] bool sameHandle(const RhiBufferHandle lhs, const RhiBufferHandle rhs) {
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
        settings.instanceMask == 0u || (settings.instanceMask & static_cast<uint8_t>(~kRtgiKnownInstanceMask)) != 0u ||
        !resources.depth.isValid() || !resources.normalAo.isValid() || !resources.materialAux.isValid() ||
        !resources.blueNoise.isValid() || !resources.terrainAlbedo.isValid() ||
        !resources.diffuseRadianceHitDistance.isValid() || !resources.validation.isValid()) {
        return {};
    }

    const std::optional<renderer::rt::SceneTlasView> activeTlas = ctx.shared->sceneTlasCache->activeView();
    RhiBufferDesc terrainHitDataDesc;
    if (!activeTlas.has_value() ||
        !sameHandle(ctx.shared->globalBindlessSet->accelerationStructure(), activeTlas->accelerationStructure) ||
        !activeTlas->terrainHitDataBuffer.isValid() || activeTlas->terrainHitDataBytes == 0u ||
        activeTlas->terrainHitDataBytes != static_cast<uint64_t>(activeTlas->instanceCount) *
                                               sizeof(renderer::contracts::TerrainRayTracingGpuInstance) ||
        !ctx.shared->rhiDevice->getBufferDesc(activeTlas->terrainHitDataBuffer, terrainHitDataDesc) ||
        terrainHitDataDesc.size != activeTlas->terrainHitDataBytes ||
        (terrainHitDataDesc.usage & rhiFlag(RhiBufferUsage::Storage)) == 0u) {
        return {};
    }

    const RgBufferHandle terrainHitData =
        graph.importBuffer({terrainHitDataDesc.debugName, activeTlas->terrainHitDataBuffer, terrainHitDataDesc,
                            RhiResourceState::StorageBuffer, RhiResourceState::StorageBuffer, RhiQueueType::Graphics,
                            RhiQueueType::Graphics});
    if (!terrainHitData.isValid()) {
        return {};
    }

    const FrameContext* frame = &ctx;
    const GraphResources frameResources = resources;
    const uint64_t sceneTlasRevision = activeTlas->revision;
    const uint32_t sceneInstanceCount = activeTlas->instanceCount;
    const uint64_t terrainHitDataBytes = activeTlas->terrainHitDataBytes;
    RenderGraphPassBuilder trace = graph.addPass({"RTGI.Trace", RgPassType::Compute, RhiQueueType::Graphics});
    trace.dependsOn(dependency)
        .readTexture(resources.depth, RhiResourceState::DepthRead)
        .readTexture(resources.normalAo, RhiResourceState::ShaderRead)
        .readTexture(resources.materialAux, RhiResourceState::ShaderRead)
        .readTexture(resources.blueNoise, RhiResourceState::ShaderRead)
        .readTexture(resources.terrainAlbedo, RhiResourceState::ShaderRead)
        .readBuffer(terrainHitData, RhiResourceState::StorageBuffer)
        .writeTexture(resources.diffuseRadianceHitDistance, RhiResourceState::ShaderWrite)
        .writeTexture(resources.validation, RhiResourceState::ShaderWrite)
        .setExecute([this, frame, settings, frameResources, terrainHitData, sceneInstanceCount, terrainHitDataBytes,
                     sceneTlasRevision](RgPassContext& pass) {
            const TraceViews views{pass.textureView(frameResources.depth),
                                   pass.textureView(frameResources.normalAo),
                                   pass.textureView(frameResources.materialAux),
                                   pass.textureView(frameResources.blueNoise),
                                   pass.textureView(frameResources.terrainAlbedo),
                                   pass.textureView(frameResources.diffuseRadianceHitDistance),
                                   pass.textureView(frameResources.validation)};
            return recordTrace(pass.commandList(), *frame, settings, views, pass.buffer(terrainHitData),
                               sceneInstanceCount, terrainHitDataBytes, sceneTlasRevision);
        });
    return trace.handle();
}

bool RtgiTracePass::recordTrace(RhiCommandList& commandList, const FrameContext& ctx, const Settings& settings,
                                const TraceViews& views, const RhiBufferHandle terrainHitDataBuffer,
                                const uint32_t sceneInstanceCount, const uint64_t terrainHitDataBytes,
                                const uint64_t sceneTlasRevision) {
    const glm::mat4& inverseViewProjection =
        settings.useJitteredProjection ? ctx.camera.jitteredInvViewProj : ctx.camera.invViewProj;
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr || ctx.shared->globalBindlessSet == nullptr ||
        !finiteMatrix(inverseViewProjection) || !std::isfinite(ctx.animationTime) || sceneInstanceCount == 0u ||
        !ensurePipeline(*ctx.shared->rhiDevice, ctx.shared->globalBindlessSet->layout()) ||
        !ensureBindGroup(*ctx.shared->rhiDevice, views, terrainHitDataBuffer, terrainHitDataBytes)) {
        return false;
    }

    const TemporalExtent extent = ctx.temporalExtents.renderExtent;
    renderer::contracts::RtgiTracePushConstants pushConstants;
    pushConstants.inverseViewProjection = inverseViewProjection;
    pushConstants.cameraPositionAndMaxDistance = glm::vec4(ctx.camera.position, settings.maxRayDistance);
    pushConstants.renderExtentAndBias = glm::vec4(static_cast<float>(extent.width), static_cast<float>(extent.height),
                                                  settings.minimumRayOriginBias, ctx.animationTime);
    pushConstants.frameMaskAndFlags = glm::uvec4(static_cast<uint32_t>(ctx.frameIndex),
                                                 static_cast<uint32_t>(settings.instanceMask), sceneInstanceCount, 0u);

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
    m_stats.terrainHitDataBytes = terrainHitDataBytes;
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
    shaderDesc.debugName = "RTGI.Trace.Compute";
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
    RhiSamplerDesc terrainSamplerDesc;
    terrainSamplerDesc.minFilter = RhiFilter::Nearest;
    terrainSamplerDesc.magFilter = RhiFilter::Nearest;
    terrainSamplerDesc.mipmapMode = RhiMipmapMode::Linear;
    terrainSamplerDesc.addressU = RhiAddressMode::Repeat;
    terrainSamplerDesc.addressV = RhiAddressMode::Repeat;
    terrainSamplerDesc.addressW = RhiAddressMode::Repeat;
    m_terrainSampler = rhiDevice.createSampler(terrainSamplerDesc);
    if (!m_sampler.isValid() || !m_terrainSampler.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc traceLayoutDesc;
    traceLayoutDesc.debugName = "RTGI.Trace.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 4u; ++binding) {
        traceLayoutDesc.entries.push_back(
            {binding, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Compute), 1u});
    }
    traceLayoutDesc.entries.push_back({4u, RhiBindingType::StorageTexture, rhiFlag(RhiShaderStage::Compute), 1u});
    traceLayoutDesc.entries.push_back({5u, RhiBindingType::StorageTexture, rhiFlag(RhiShaderStage::Compute), 1u});
    traceLayoutDesc.entries.push_back({6u, RhiBindingType::StorageBuffer, rhiFlag(RhiShaderStage::Compute), 1u});
    traceLayoutDesc.entries.push_back(
        {7u, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Compute), 1u});
    m_traceBindGroupLayout = rhiDevice.createBindGroupLayout(traceLayoutDesc);
    if (!m_traceBindGroupLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "RTGI.Trace.PipelineLayout";
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
    pipelineDesc.debugName = "RTGI.Trace.Pipeline";
    pipelineDesc.computeShader = m_shader;
    pipelineDesc.layout = m_pipelineLayout;
    m_pipeline = rhiDevice.createComputePipeline(pipelineDesc);
    if (!m_pipeline.isValid()) {
        destroyRhiResources();
        return false;
    }
    return true;
}

bool RtgiTracePass::ensureBindGroup(RhiDevice& rhiDevice, const TraceViews& views,
                                    const RhiBufferHandle terrainHitDataBuffer, const uint64_t terrainHitDataBytes) {
    const std::array<RhiTextureViewHandle, 7u> boundViews{views.depth,         views.normalAo,
                                                          views.materialAux,   views.blueNoise,
                                                          views.terrainAlbedo, views.diffuseRadianceHitDistance,
                                                          views.validation};
    for (const RhiTextureViewHandle view : boundViews) {
        if (!view.isValid()) {
            return false;
        }
    }
    RhiTextureViewDesc terrainAlbedoDesc;
    if (!terrainHitDataBuffer.isValid() || terrainHitDataBytes == 0u ||
        !rhiDevice.getTextureViewDesc(views.terrainAlbedo, terrainAlbedoDesc) ||
        terrainAlbedoDesc.viewType != RhiTextureViewType::Texture2DArray) {
        return false;
    }
    bool unchanged = m_traceBindGroup.isValid() && sameHandle(m_boundTerrainHitDataBuffer, terrainHitDataBuffer) &&
                     m_boundTerrainHitDataBytes == terrainHitDataBytes;
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
        entry.resource.textureView = boundViews[binding + 1u];
        bindGroupDesc.entries.push_back(entry);
    }
    RhiBindGroupEntry hitDataEntry;
    hitDataEntry.binding = 6u;
    hitDataEntry.resource.buffer = {terrainHitDataBuffer, 0u, terrainHitDataBytes};
    bindGroupDesc.entries.push_back(hitDataEntry);
    RhiBindGroupEntry terrainAlbedoEntry;
    terrainAlbedoEntry.binding = 7u;
    terrainAlbedoEntry.resource.combinedTextureSampler = {views.terrainAlbedo, m_terrainSampler};
    bindGroupDesc.entries.push_back(terrainAlbedoEntry);
    m_traceBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_traceBindGroup.isValid()) {
        m_boundViews = {};
        m_boundTerrainHitDataBuffer = {};
        m_boundTerrainHitDataBytes = 0u;
        return false;
    }
    m_boundViews = boundViews;
    m_boundTerrainHitDataBuffer = terrainHitDataBuffer;
    m_boundTerrainHitDataBytes = terrainHitDataBytes;
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
        if (m_terrainSampler.isValid()) {
            m_rhiDevice->destroySampler(m_terrainSampler);
        }
        if (m_shader.isValid()) {
            m_rhiDevice->destroyShader(m_shader);
        }
    }
    m_rhiDevice = nullptr;
    m_shader = {};
    m_sampler = {};
    m_terrainSampler = {};
    m_globalBindlessLayout = {};
    m_traceBindGroupLayout = {};
    m_pipelineLayout = {};
    m_pipeline = {};
    m_traceBindGroup = {};
    m_boundTerrainHitDataBuffer = {};
    m_boundTerrainHitDataBytes = 0u;
    m_boundViews = {};
}
