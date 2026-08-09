#include "RtgiTracePass.h"

#include "Diagnostics.h"
#include "renderer/contracts/GpuMaterialContract.h"
#include "renderer/contracts/GpuSceneContract.h"
#include "renderer/contracts/RtgiSamplingContract.h"
#include "renderer/core/GlobalBindlessSet.h"
#include "renderer/core/RenderScene.h"
#include "renderer/debug/RenderDebugService.h"
#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiShaderSourceLoader.h"
#include "renderer/rhi/SceneTlasCache.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>

namespace {
constexpr uint8_t kRtgiKnownInstanceMask =
    renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::GiOpaque) |
    renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::GiCutout);
constexpr uint8_t kRtgiKnownShadowInstanceMask =
    renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::ShadowCaster);

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

[[nodiscard]] bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool validDirection(const glm::vec3& value) {
    return finite(value) && glm::dot(value, value) > 1.0e-12f;
}

[[nodiscard]] bool validRadiance(const glm::vec3& value) {
    return finite(value) && value.x >= 0.0f && value.y >= 0.0f && value.z >= 0.0f;
}

[[nodiscard]] bool unitInterval(const float value) {
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

[[nodiscard]] bool storageBufferMatches(RhiDevice& rhiDevice, const RhiBufferHandle buffer,
                                        const uint64_t expectedBytes, RhiBufferDesc& desc) {
    return buffer.isValid() && expectedBytes != 0u && rhiDevice.getBufferDesc(buffer, desc) &&
           desc.size == expectedBytes && (desc.usage & rhiFlag(RhiBufferUsage::Storage)) != 0u;
}
} // namespace

void RtgiTracePass::shutdown() {
    destroyRhiResources();
    m_stats = {};
    m_counterStats = {};
    m_counterReadbackSequence = 0u;
}

RgPassHandle RtgiTracePass::addGraphPass(RenderGraph& graph, const FrameContext& ctx, const Settings& settings,
                                         const GraphResources& resources, const LightingResources& lighting,
                                         const RgPassHandle dependency) {
    if (!dependency.isValid() || ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        ctx.shared->globalBindlessSet == nullptr || ctx.shared->sceneTlasCache == nullptr ||
        ctx.shared->rhiDevice->backend() != RhiBackend::Vulkan || !ctx.shared->globalBindlessSet->initialized() ||
        !ctx.temporalExtents.renderExtent.isValid() || !std::isfinite(settings.maxRayDistance) ||
        settings.maxRayDistance <= 0.0f || !std::isfinite(settings.maxShadowRayDistance) ||
        settings.maxShadowRayDistance <= 0.0f || !std::isfinite(settings.minimumRayOriginBias) ||
        settings.minimumRayOriginBias < renderer::contracts::kRtgiMinimumRayOriginBias ||
        settings.minimumRayOriginBias >= settings.maxRayDistance ||
        settings.minimumRayOriginBias >= settings.maxShadowRayDistance || settings.instanceMask == 0u ||
        !std::isfinite(settings.celestialRadianceScale) || settings.celestialRadianceScale < 0.0f ||
        (settings.instanceMask & static_cast<uint8_t>(~kRtgiKnownInstanceMask)) != 0u ||
        settings.shadowInstanceMask == 0u ||
        (settings.shadowInstanceMask & static_cast<uint8_t>(~kRtgiKnownShadowInstanceMask)) != 0u ||
        !resources.depth.isValid() || !resources.normalAo.isValid() || !resources.materialAux.isValid() ||
        !resources.voxelLight.isValid() || !resources.blueNoise.isValid() || !resources.terrainAlbedo.isValid() ||
        !resources.terrainNormal.isValid() || !resources.terrainSpecular.isValid() ||
        !resources.grassColormap.isValid() || !resources.foliageColormap.isValid() || !resources.skyCapture.isValid() ||
        !resources.diffuseRadianceHitDistance.isValid() || !resources.validation.isValid() ||
        !std::isfinite(ctx.preExposure) || ctx.preExposure <= 0.0f || !lighting.bindGroupLayout.isValid() ||
        !lighting.bindGroup.isValid() || !lighting.lights.isValid() || !lighting.worldCells.isValid() ||
        !lighting.worldIndices.isValid() || !lighting.worldHeader.isValid() ||
        !lighting.localShadowMetadata.isValid() || !lighting.localShadowSpotAtlas.isValid() ||
        !lighting.localShadowPointCubeArray.isValid() || !validDirection(ctx.skyColors.sunDirection) ||
        !validDirection(ctx.skyColors.moonDirection) || !validRadiance(ctx.skyIlluminance.sunIlluminance) ||
        !validRadiance(ctx.skyIlluminance.moonIlluminance) || !validRadiance(ctx.skyIlluminance.skyIlluminance) ||
        !unitInterval(ctx.skyColors.sunVisibility) || !unitInterval(ctx.skyColors.moonVisibility)) {
        return {};
    }

    const std::optional<renderer::rt::SceneTlasView> activeTlas = ctx.shared->sceneTlasCache->activeView();
    RhiBufferDesc terrainHitDataDesc;
    RhiBufferDesc gpuSceneMaterialDesc;
    RhiBufferDesc gpuSceneGeometryDesc;
    RhiBufferDesc gpuSceneInstanceDesc;
    const uint64_t expectedMaterialBytes =
        static_cast<uint64_t>(std::max(activeTlas.has_value() ? activeTlas->gpuSceneMaterialCount : 0u, 1u)) *
        sizeof(renderer::contracts::GpuMaterial);
    const uint64_t expectedGeometryBytes =
        static_cast<uint64_t>(std::max(activeTlas.has_value() ? activeTlas->gpuSceneGeometryCount : 0u, 1u)) *
        sizeof(renderer::contracts::GpuSceneGeometry);
    const uint64_t expectedInstanceBytes =
        static_cast<uint64_t>(activeTlas.has_value() ? activeTlas->instanceCount : 0u) *
        sizeof(renderer::contracts::GpuSceneInstance);
    if (!activeTlas.has_value() ||
        !sameHandle(ctx.shared->globalBindlessSet->accelerationStructure(), activeTlas->accelerationStructure) ||
        activeTlas->terrainHitDataBytes != static_cast<uint64_t>(activeTlas->instanceCount) *
                                               sizeof(renderer::contracts::TerrainRayTracingGpuInstance) ||
        (activeTlas->gpuSceneMaterialCount == 0u) != (activeTlas->gpuSceneGeometryCount == 0u) ||
        activeTlas->gpuSceneMaterialBytes != expectedMaterialBytes ||
        activeTlas->gpuSceneGeometryBytes != expectedGeometryBytes ||
        activeTlas->gpuSceneInstanceBytes != expectedInstanceBytes ||
        (activeTlas->gpuSceneMaterialCount == 0u && activeTlas->bindlessIdentity != 0u) ||
        (activeTlas->gpuSceneMaterialCount != 0u &&
         activeTlas->bindlessIdentity != ctx.shared->globalBindlessSet->identity()) ||
        !storageBufferMatches(*ctx.shared->rhiDevice, activeTlas->terrainHitDataBuffer, activeTlas->terrainHitDataBytes,
                              terrainHitDataDesc) ||
        !storageBufferMatches(*ctx.shared->rhiDevice, activeTlas->gpuSceneMaterialBuffer,
                              activeTlas->gpuSceneMaterialBytes, gpuSceneMaterialDesc) ||
        !storageBufferMatches(*ctx.shared->rhiDevice, activeTlas->gpuSceneGeometryBuffer,
                              activeTlas->gpuSceneGeometryBytes, gpuSceneGeometryDesc) ||
        !storageBufferMatches(*ctx.shared->rhiDevice, activeTlas->gpuSceneInstanceBuffer,
                              activeTlas->gpuSceneInstanceBytes, gpuSceneInstanceDesc)) {
        return {};
    }

    if (!m_counterHealthy || m_counterReadbackPending ||
        !ensurePipeline(*ctx.shared->rhiDevice, ctx.shared->globalBindlessSet->layout(), lighting.bindGroupLayout) ||
        !pollCounterReadback(*ctx.shared->rhiDevice)) {
        return {};
    }
    RhiBufferDesc counterBufferDesc;
    if (!ctx.shared->rhiDevice->getBufferDesc(m_counterBuffer, counterBufferDesc) ||
        counterBufferDesc.size != sizeof(uint32_t) * renderer::contracts::kRtgiTraceCounterWordCount ||
        (counterBufferDesc.usage & rhiFlag(RhiBufferUsage::Storage)) == 0u ||
        (counterBufferDesc.usage & rhiFlag(RhiBufferUsage::TransferSrc)) == 0u ||
        (counterBufferDesc.usage & rhiFlag(RhiBufferUsage::TransferDst)) == 0u) {
        return {};
    }

    const RgBufferHandle terrainHitData =
        graph.importBuffer({terrainHitDataDesc.debugName, activeTlas->terrainHitDataBuffer, terrainHitDataDesc,
                            RhiResourceState::StorageBuffer, RhiResourceState::StorageBuffer, RhiQueueType::Graphics,
                            RhiQueueType::Graphics});
    const RgBufferHandle gpuSceneMaterials =
        graph.importBuffer({gpuSceneMaterialDesc.debugName, activeTlas->gpuSceneMaterialBuffer, gpuSceneMaterialDesc,
                            RhiResourceState::StorageBuffer, RhiResourceState::StorageBuffer, RhiQueueType::Graphics,
                            RhiQueueType::Graphics});
    const RgBufferHandle gpuSceneGeometries =
        graph.importBuffer({gpuSceneGeometryDesc.debugName, activeTlas->gpuSceneGeometryBuffer, gpuSceneGeometryDesc,
                            RhiResourceState::StorageBuffer, RhiResourceState::StorageBuffer, RhiQueueType::Graphics,
                            RhiQueueType::Graphics});
    const RgBufferHandle gpuSceneInstances =
        graph.importBuffer({gpuSceneInstanceDesc.debugName, activeTlas->gpuSceneInstanceBuffer, gpuSceneInstanceDesc,
                            RhiResourceState::StorageBuffer, RhiResourceState::StorageBuffer, RhiQueueType::Graphics,
                            RhiQueueType::Graphics});
    const RgBufferHandle counterBuffer = graph.importBuffer(
        {counterBufferDesc.debugName, m_counterBuffer, counterBufferDesc, RhiResourceState::StorageBuffer,
         RhiResourceState::StorageBuffer, RhiQueueType::Graphics, RhiQueueType::Graphics});
    if (!terrainHitData.isValid() || !gpuSceneMaterials.isValid() || !gpuSceneGeometries.isValid() ||
        !gpuSceneInstances.isValid() || !counterBuffer.isValid()) {
        return {};
    }

    const FrameContext* frame = &ctx;
    const GraphResources frameResources = resources;
    const LightingResources frameLighting = lighting;
    const uint64_t sceneTlasRevision = activeTlas->revision;
    TraceSceneBuffers sceneBuffers;
    sceneBuffers.terrainHitDataBytes = activeTlas->terrainHitDataBytes;
    sceneBuffers.gpuSceneMaterialBytes = activeTlas->gpuSceneMaterialBytes;
    sceneBuffers.gpuSceneGeometryBytes = activeTlas->gpuSceneGeometryBytes;
    sceneBuffers.gpuSceneInstanceBytes = activeTlas->gpuSceneInstanceBytes;
    sceneBuffers.sceneInstanceCount = activeTlas->instanceCount;
    sceneBuffers.gpuSceneMaterialCount = activeTlas->gpuSceneMaterialCount;
    sceneBuffers.gpuSceneGeometryCount = activeTlas->gpuSceneGeometryCount;
    sceneBuffers.sceneOrigin = activeTlas->sceneOrigin;
    RenderGraphPassBuilder trace = graph.addPass({"RTGI.Trace", RgPassType::Compute, RhiQueueType::Graphics});
    trace.dependsOn(dependency)
        .readTexture(resources.depth, RhiResourceState::DepthRead)
        .readTexture(resources.normalAo, RhiResourceState::ShaderRead)
        .readTexture(resources.materialAux, RhiResourceState::ShaderRead)
        .readTexture(resources.voxelLight, RhiResourceState::ShaderRead)
        .readTexture(resources.blueNoise, RhiResourceState::ShaderRead)
        .readTexture(resources.terrainAlbedo, RhiResourceState::ShaderRead)
        .readTexture(resources.terrainNormal, RhiResourceState::ShaderRead)
        .readTexture(resources.terrainSpecular, RhiResourceState::ShaderRead)
        .readTexture(resources.grassColormap, RhiResourceState::ShaderRead)
        .readTexture(resources.foliageColormap, RhiResourceState::ShaderRead)
        .readTexture(resources.skyCapture, RhiResourceState::ShaderRead)
        .readBuffer(terrainHitData, RhiResourceState::StorageBuffer)
        .readBuffer(gpuSceneMaterials, RhiResourceState::StorageBuffer)
        .readBuffer(gpuSceneGeometries, RhiResourceState::StorageBuffer)
        .readBuffer(gpuSceneInstances, RhiResourceState::StorageBuffer)
        .readBuffer(lighting.lights, RhiResourceState::StorageBuffer)
        .readBuffer(lighting.worldCells, RhiResourceState::StorageBuffer)
        .readBuffer(lighting.worldIndices, RhiResourceState::StorageBuffer)
        .readBuffer(lighting.worldHeader, RhiResourceState::StorageBuffer)
        .readBuffer(lighting.localShadowMetadata, RhiResourceState::StorageBuffer)
        .readTexture(lighting.localShadowSpotAtlas, RhiResourceState::DepthRead)
        .readTexture(lighting.localShadowPointCubeArray, RhiResourceState::DepthRead)
        .writeTexture(resources.diffuseRadianceHitDistance, RhiResourceState::ShaderWrite)
        .writeTexture(resources.validation, RhiResourceState::ShaderWrite)
        .setExecute([this, frame, settings, frameResources, frameLighting, terrainHitData, gpuSceneMaterials,
                     gpuSceneGeometries, gpuSceneInstances, sceneBuffers,
                     sceneTlasRevision](RgPassContext& pass) mutable {
            const TraceViews views{pass.textureView(frameResources.depth),
                                   pass.textureView(frameResources.normalAo),
                                   pass.textureView(frameResources.materialAux),
                                   pass.textureView(frameResources.voxelLight),
                                   pass.textureView(frameResources.blueNoise),
                                   pass.textureView(frameResources.terrainAlbedo),
                                   pass.textureView(frameResources.terrainNormal),
                                   pass.textureView(frameResources.terrainSpecular),
                                   pass.textureView(frameResources.grassColormap),
                                   pass.textureView(frameResources.foliageColormap),
                                   pass.textureView(frameResources.skyCapture),
                                   pass.textureView(frameResources.diffuseRadianceHitDistance),
                                   pass.textureView(frameResources.validation)};
            sceneBuffers.terrainHitData = pass.buffer(terrainHitData);
            sceneBuffers.gpuSceneMaterials = pass.buffer(gpuSceneMaterials);
            sceneBuffers.gpuSceneGeometries = pass.buffer(gpuSceneGeometries);
            sceneBuffers.gpuSceneInstances = pass.buffer(gpuSceneInstances);
            return recordTrace(pass.commandList(), *frame, settings, views, sceneBuffers, frameLighting.bindGroupLayout,
                               frameLighting.bindGroup, sceneTlasRevision);
        });
    if (!m_counterReadbackSlotAvailable) {
        return trace.handle();
    }

    const TemporalExtent counterExtent = ctx.temporalExtents.renderExtent;
    const uint64_t counterFrameIndex = ctx.frameIndex;
    RenderGraphPassBuilder counters =
        graph.addPass({"RTGI.TraceCounters", RgPassType::Compute, RhiQueueType::Graphics});
    counters.dependsOn(trace.handle())
        .readWriteTexture(resources.validation, RhiResourceState::ShaderWrite)
        .readWriteBuffer(counterBuffer, RhiResourceState::StorageBuffer)
        .setExecute([this, frameResources, counterFrameIndex, counterExtent](RgPassContext& pass) {
            return recordCounterReduction(pass.commandList(), pass.textureView(frameResources.validation),
                                          counterFrameIndex, counterExtent.width, counterExtent.height);
        });
    return counters.handle();
}

void RtgiTracePass::finishGraphExecution(const bool succeeded, const RhiSubmissionToken completionToken) {
    if (!m_counterReadbackPending) {
        return;
    }
    if (succeeded && completionToken.isValid()) {
        m_counterReadbackWritten[m_pendingCounterReadbackIndex] = true;
        m_counterReadbackTokens[m_pendingCounterReadbackIndex] = completionToken;
        m_counterReadbackWriteIndex = (m_pendingCounterReadbackIndex + 1u) % kCounterReadbackRingSize;
    } else if (succeeded) {
        m_counterHealthy = false;
        MECRAFT_LOG_STREAM(std::cerr << "[RtgiTracePass] Counter readback submission token is invalid\n");
    }
    m_counterReadbackPending = false;
}

bool RtgiTracePass::pollCounterReadback(RhiDevice& rhiDevice) {
    return m_rhiDevice == &rhiDevice && m_counterHealthy && consumeCounterReadback(rhiDevice);
}

bool RtgiTracePass::consumeCounterReadback(RhiDevice& rhiDevice) {
    for (uint32_t offset = 0u; offset < kCounterReadbackRingSize; ++offset) {
        const uint32_t ringIndex = (m_counterReadbackWriteIndex + offset) % kCounterReadbackRingSize;
        const RhiSubmissionToken token = m_counterReadbackTokens[ringIndex];
        if (!token.isValid()) {
            continue;
        }

        bool complete = false;
        if (!rhiDevice.isSubmissionComplete(token, complete)) {
            m_counterHealthy = false;
            MECRAFT_LOG_STREAM(std::cerr << "[RtgiTracePass] Counter readback submission query failed\n");
            return false;
        }
        if (!complete) {
            break;
        }

        constexpr size_t kReadbackBytes = sizeof(uint32_t) * renderer::contracts::kRtgiTraceCounterWordCount;
        const void* mapped = rhiDevice.mapBuffer(m_counterReadbackBuffers[ringIndex], 0u, kReadbackBytes);
        if (mapped == nullptr) {
            m_counterHealthy = false;
            MECRAFT_LOG_STREAM(std::cerr << "[RtgiTracePass] Counter readback mapping failed\n");
            return false;
        }
        std::array<uint32_t, renderer::contracts::kRtgiTraceCounterWordCount> words{};
        std::memcpy(words.data(), mapped, kReadbackBytes);
        rhiDevice.unmapBuffer(m_counterReadbackBuffers[ringIndex]);

        if (m_counterReadbackSequence == std::numeric_limits<uint64_t>::max()) {
            std::abort();
        }
        const CounterReadbackMetadata& metadata = m_counterReadbackMetadata[ringIndex];
        const std::optional<renderer::contracts::RtgiTraceCounterFrameStats> decoded =
            renderer::contracts::decodeRtgiTraceCounterReadback(words, m_counterReadbackSequence + 1u,
                                                                metadata.frameIndex, metadata.width, metadata.height);
        if (!decoded.has_value()) {
            m_counterHealthy = false;
            MECRAFT_LOG_STREAM(std::cerr << "[RtgiTracePass] Counter readback invariant failed\n");
            return false;
        }

        ++m_counterReadbackSequence;
        m_counterStats = *decoded;
        m_counterReadbackTokens[ringIndex] = {};
        break;
    }
    m_counterReadbackSlotAvailable = !m_counterReadbackTokens[m_counterReadbackWriteIndex].isValid();
    return true;
}

bool RtgiTracePass::ensureCounterBindGroup(RhiDevice& rhiDevice, const RhiTextureViewHandle validationView) {
    if (!validationView.isValid() || !m_counterBindGroupLayout.isValid() || !m_counterBuffer.isValid()) {
        return false;
    }
    if (m_counterBindGroup.isValid() && sameHandle(m_boundCounterValidationView, validationView)) {
        return true;
    }
    if (m_counterBindGroup.isValid()) {
        rhiDevice.waitIdle();
        rhiDevice.destroyBindGroup(m_counterBindGroup);
        m_counterBindGroup = {};
    }

    RhiBindGroupDesc desc;
    desc.layout = m_counterBindGroupLayout;
    RhiBindGroupEntry validationEntry;
    validationEntry.binding = 0u;
    validationEntry.resource.textureView = validationView;
    desc.entries.push_back(validationEntry);
    RhiBindGroupEntry counterEntry;
    counterEntry.binding = 1u;
    counterEntry.resource.buffer = {m_counterBuffer, 0u,
                                    sizeof(uint32_t) * renderer::contracts::kRtgiTraceCounterWordCount};
    desc.entries.push_back(counterEntry);
    m_counterBindGroup = rhiDevice.createBindGroup(desc);
    if (!m_counterBindGroup.isValid()) {
        return false;
    }
    m_boundCounterValidationView = validationView;
    return true;
}

bool RtgiTracePass::recordCounterReduction(RhiCommandList& commandList, const RhiTextureViewHandle validationView,
                                           const uint64_t frameIndex, const uint32_t width, const uint32_t height) {
    if (m_rhiDevice == nullptr || !m_counterHealthy || m_counterReadbackPending || !m_counterReadbackSlotAvailable ||
        width == 0u || height == 0u || !m_counterPipeline.isValid() || !m_counterBuffer.isValid() ||
        !m_counterReadbackBuffers[m_counterReadbackWriteIndex].isValid() ||
        !ensureCounterBindGroup(*m_rhiDevice, validationView)) {
        return false;
    }

    constexpr size_t kCounterBytes = sizeof(uint32_t) * renderer::contracts::kRtgiTraceCounterWordCount;
    constexpr std::array<uint32_t, renderer::contracts::kRtgiTraceCounterWordCount> kZeroCounters{};
    commandList.bufferBarrier({m_counterBuffer, RhiResourceState::StorageBuffer, RhiResourceState::TransferDst});
    commandList.updateBuffer(m_counterBuffer, 0u, kZeroCounters.data(), kCounterBytes);
    commandList.bufferBarrier({m_counterBuffer, RhiResourceState::TransferDst, RhiResourceState::StorageBuffer});

    renderer::contracts::RtgiTraceCounterPushConstants pushConstants;
    pushConstants.renderExtentAndContract =
        glm::uvec4(width, height, renderer::contracts::kRtgiTraceCounterContractVersion, 0u);
    commandList.setComputePipeline(m_counterPipeline);
    commandList.setBindGroup(0u, m_counterBindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);

    const uint32_t ringIndex = m_counterReadbackWriteIndex;
    commandList.bufferBarrier({m_counterBuffer, RhiResourceState::StorageBuffer, RhiResourceState::TransferSrc});
    if (m_counterReadbackWritten[ringIndex]) {
        commandList.bufferBarrier(
            {m_counterReadbackBuffers[ringIndex], RhiResourceState::HostRead, RhiResourceState::TransferDst});
    }
    RhiBufferCopy copy;
    copy.src = m_counterBuffer;
    copy.dst = m_counterReadbackBuffers[ringIndex];
    copy.size = kCounterBytes;
    commandList.copyBuffer(copy);
    commandList.bufferBarrier(
        {m_counterReadbackBuffers[ringIndex], RhiResourceState::TransferDst, RhiResourceState::HostRead});
    commandList.bufferBarrier({m_counterBuffer, RhiResourceState::TransferSrc, RhiResourceState::StorageBuffer});

    m_counterReadbackMetadata[ringIndex] = {frameIndex, width, height};
    m_pendingCounterReadbackIndex = ringIndex;
    m_counterReadbackPending = true;
    return true;
}

bool RtgiTracePass::recordTrace(RhiCommandList& commandList, const FrameContext& ctx, const Settings& settings,
                                const TraceViews& views, const TraceSceneBuffers& sceneBuffers,
                                const RhiBindGroupLayoutHandle lightingLayout,
                                const RhiBindGroupHandle lightingBindGroup, const uint64_t sceneTlasRevision) {
    const auto reject = [](const char* reason) {
        MECRAFT_LOG_STREAM(std::cerr << "[RtgiTracePass] Trace recording rejected: " << reason << '\n');
        return false;
    };
    glm::mat4 projection = ctx.camera.projection;
    if (settings.useJitteredProjection) {
        for (uint32_t column = 0u; column < 4u; ++column) {
            projection[column][0] += ctx.jitter.projectionOffset.x * ctx.camera.projection[column][3];
            projection[column][1] += ctx.jitter.projectionOffset.y * ctx.camera.projection[column][3];
        }
    }
    glm::mat4 inverseViewProjection;
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr || ctx.shared->globalBindlessSet == nullptr) {
        return reject("shared Vulkan renderer state is unavailable");
    }
    if (!renderer::contracts::makeRtgiCameraRelativeInverseViewProjection(
            projection, ctx.camera.view, ctx.camera.position, sceneBuffers.sceneOrigin, inverseViewProjection)) {
        return reject("camera-relative inverse view-projection matrix is invalid");
    }
    if (!std::isfinite(ctx.animationTime)) {
        return reject("animation time is non-finite");
    }
    if (!std::isfinite(ctx.preExposure) || ctx.preExposure <= 0.0f) {
        return reject("pre-exposure is not positive and finite");
    }
    if (!std::isfinite(settings.celestialRadianceScale) || settings.celestialRadianceScale < 0.0f) {
        return reject("celestial radiance scale is not finite and non-negative");
    }
    if (sceneBuffers.sceneInstanceCount == 0u) {
        return reject("active Scene TLAS has no instances");
    }
    if (!lightingBindGroup.isValid()) {
        return reject("clustered lighting bind group is invalid");
    }
    if (!ensurePipeline(*ctx.shared->rhiDevice, ctx.shared->globalBindlessSet->layout(), lightingLayout)) {
        return reject("RTGI pipeline resources could not be created");
    }
    if (!ensureBindGroup(*ctx.shared->rhiDevice, views, sceneBuffers)) {
        return reject("RTGI texture or scene-buffer bind group could not be created");
    }

    const TemporalExtent extent = ctx.temporalExtents.renderExtent;
    renderer::contracts::RtgiTracePushConstants pushConstants;
    pushConstants.inverseViewProjection = inverseViewProjection;
    pushConstants.cameraPositionAndMaxDistance =
        glm::vec4(ctx.camera.position - sceneBuffers.sceneOrigin, settings.maxRayDistance);
    pushConstants.renderExtentAndBias = glm::vec4(static_cast<float>(extent.width), static_cast<float>(extent.height),
                                                  settings.minimumRayOriginBias, ctx.animationTime);
    pushConstants.frameMaskAndFlags =
        glm::uvec4(settings.temporalSamplingEnabled ? settings.temporalSampleIndex : 0u,
                   static_cast<uint32_t>(settings.instanceMask), sceneBuffers.sceneInstanceCount,
                   static_cast<uint32_t>(settings.shadowInstanceMask));
    pushConstants.materialGeometryCounts =
        glm::uvec4(sceneBuffers.gpuSceneMaterialCount, sceneBuffers.gpuSceneGeometryCount,
                   settings.referenceSamplingEnabled ? 1u : 0u, 0u);

    renderer::contracts::RtgiSecondaryLightingParams lightingParams;
    lightingParams.sunDirectionAndVisibility = glm::vec4(ctx.skyColors.sunDirection, ctx.skyColors.sunVisibility);
    lightingParams.moonDirectionAndVisibility = glm::vec4(ctx.skyColors.moonDirection, ctx.skyColors.moonVisibility);
    lightingParams.sunRadiance = glm::vec4(ctx.skyIlluminance.sunIlluminance * settings.celestialRadianceScale, 0.0f);
    lightingParams.moonRadiance = glm::vec4(ctx.skyIlluminance.moonIlluminance * settings.celestialRadianceScale, 0.0f);
    lightingParams.skyAmbientRadiance = glm::vec4(ctx.skyIlluminance.skyIlluminance, 0.0f);
    lightingParams.traceAndEmissionScales = glm::vec4(settings.maxShadowRayDistance, 1.5f, 1.0f, ctx.preExposure);
    lightingParams.terrainLightScales = glm::vec4(0.0f);
    lightingParams.flags.x =
        (settings.terrainNormalMapsEnabled ? renderer::contracts::kRtgiSecondaryLightingTerrainNormalMapBit : 0u) |
        (settings.terrainSpecularMapsEnabled ? renderer::contracts::kRtgiSecondaryLightingTerrainSpecularMapBit : 0u);

    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
                                              ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::RtgiTrace)
                                              : GpuTimerSegmentToken{};
    commandList.bufferBarrier(
        {m_secondaryLightingBuffer, RhiResourceState::UniformBuffer, RhiResourceState::TransferDst});
    commandList.updateBuffer(m_secondaryLightingBuffer, 0u, &lightingParams, sizeof(lightingParams));
    commandList.bufferBarrier(
        {m_secondaryLightingBuffer, RhiResourceState::TransferDst, RhiResourceState::UniformBuffer});

    commandList.setComputePipeline(m_pipeline);
    commandList.setBindGroup(0u, ctx.shared->globalBindlessSet->bindGroup());
    commandList.setBindGroup(1u, m_traceBindGroup);
    commandList.setBindGroup(2u, lightingBindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch((extent.width + 7u) / 8u, (extent.height + 7u) / 8u, 1u);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, gpuTimer);
    }

    m_stats.dispatched = true;
    m_stats.frameIndex = ctx.frameIndex;
    m_stats.sceneTlasRevision = sceneTlasRevision;
    m_stats.width = extent.width;
    m_stats.height = extent.height;
    m_stats.instanceMask = settings.instanceMask;
    m_stats.terrainHitDataBytes = sceneBuffers.terrainHitDataBytes;
    m_stats.gpuSceneMaterialBytes = sceneBuffers.gpuSceneMaterialBytes;
    m_stats.gpuSceneGeometryBytes = sceneBuffers.gpuSceneGeometryBytes;
    m_stats.gpuSceneInstanceBytes = sceneBuffers.gpuSceneInstanceBytes;
    m_stats.gpuSceneMaterialCount = sceneBuffers.gpuSceneMaterialCount;
    m_stats.gpuSceneGeometryCount = sceneBuffers.gpuSceneGeometryCount;
    return true;
}

bool RtgiTracePass::ensurePipeline(RhiDevice& rhiDevice, const RhiBindGroupLayoutHandle globalBindlessLayout,
                                   const RhiBindGroupLayoutHandle lightingLayout) {
    const auto reject = [](const char* reason) {
        MECRAFT_LOG_STREAM(std::cerr << "[RtgiTracePass] Pipeline setup rejected: " << reason << '\n');
        return false;
    };
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyRhiResources();
    }
    const bool counterReadbacksValid = std::all_of(m_counterReadbackBuffers.begin(), m_counterReadbackBuffers.end(),
                                                   [](const RhiBufferHandle buffer) { return buffer.isValid(); });
    if (m_pipeline.isValid() && m_counterPipeline.isValid() && m_counterBuffer.isValid() && counterReadbacksValid &&
        sameHandle(m_globalBindlessLayout, globalBindlessLayout) && sameHandle(m_lightingLayout, lightingLayout)) {
        return true;
    }
    if (m_rhiDevice != nullptr) {
        destroyRhiResources();
    }
    if (!globalBindlessLayout.isValid() || !lightingLayout.isValid()) {
        return reject("global bindless or clustered lighting layout is invalid");
    }
    m_rhiDevice = &rhiDevice;
    m_globalBindlessLayout = globalBindlessLayout;
    m_lightingLayout = lightingLayout;

    const std::optional<std::string> source = renderer::rhi::loadShaderSource("assets/shaders/rtgi_trace.comp");
    const std::optional<std::string> counterSource =
        renderer::rhi::loadShaderSource("assets/shaders/rtgi_trace_counter_reduce.comp");
    if (!source.has_value() || !counterSource.has_value()) {
        destroyRhiResources();
        return reject("RTGI trace or counter-reduction shader source is unavailable");
    }
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "RTGI.Trace.Compute";
    shaderDesc.stage = RhiShaderStage::Compute;
    shaderDesc.source = source->c_str();
    shaderDesc.sourceSize = source->size();
    m_shader = rhiDevice.createShader(shaderDesc);
    if (!m_shader.isValid()) {
        destroyRhiResources();
        return reject("RTGI trace shader compilation failed");
    }
    RhiShaderDesc counterShaderDesc;
    counterShaderDesc.debugName = "RTGI.TraceCounters.Compute";
    counterShaderDesc.stage = RhiShaderStage::Compute;
    counterShaderDesc.source = counterSource->c_str();
    counterShaderDesc.sourceSize = counterSource->size();
    m_counterShader = rhiDevice.createShader(counterShaderDesc);
    if (!m_counterShader.isValid()) {
        destroyRhiResources();
        return reject("RTGI counter-reduction shader compilation failed");
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
    terrainSamplerDesc.minFilter = RhiFilter::Linear;
    terrainSamplerDesc.magFilter = RhiFilter::Linear;
    terrainSamplerDesc.mipmapMode = RhiMipmapMode::Linear;
    terrainSamplerDesc.addressU = RhiAddressMode::Repeat;
    terrainSamplerDesc.addressV = RhiAddressMode::Repeat;
    terrainSamplerDesc.addressW = RhiAddressMode::Repeat;
    m_terrainSampler = rhiDevice.createSampler(terrainSamplerDesc);
    RhiSamplerDesc linearClampSamplerDesc = terrainSamplerDesc;
    linearClampSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    linearClampSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    linearClampSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_linearClampSampler = rhiDevice.createSampler(linearClampSamplerDesc);
    RhiBufferDesc lightingBufferDesc;
    lightingBufferDesc.debugName = "RTGI.SecondaryLightingParams";
    lightingBufferDesc.size = sizeof(renderer::contracts::RtgiSecondaryLightingParams);
    lightingBufferDesc.usage = rhiFlag(RhiBufferUsage::Uniform) | rhiFlag(RhiBufferUsage::TransferDst);
    lightingBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    lightingBufferDesc.initialState = RhiResourceState::UniformBuffer;
    lightingBufferDesc.memoryCategory = RhiMemoryCategory::SceneData;
    m_secondaryLightingBuffer = rhiDevice.createBuffer(lightingBufferDesc, nullptr, 0u);
    RhiBufferDesc counterBufferDesc;
    counterBufferDesc.debugName = "RTGI.TraceCounters";
    counterBufferDesc.size = sizeof(uint32_t) * renderer::contracts::kRtgiTraceCounterWordCount;
    counterBufferDesc.usage =
        rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::TransferSrc) | rhiFlag(RhiBufferUsage::TransferDst);
    counterBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    counterBufferDesc.initialState = RhiResourceState::StorageBuffer;
    counterBufferDesc.memoryCategory = RhiMemoryCategory::SceneData;
    m_counterBuffer = rhiDevice.createBuffer(counterBufferDesc, nullptr, 0u);
    for (RhiBufferHandle& readback : m_counterReadbackBuffers) {
        RhiBufferDesc readbackDesc;
        readbackDesc.debugName = "RTGI.TraceCountersReadback";
        readbackDesc.size = counterBufferDesc.size;
        readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
        readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
        readbackDesc.initialState = RhiResourceState::TransferDst;
        readbackDesc.memoryCategory = RhiMemoryCategory::Readback;
        readback = rhiDevice.createBuffer(readbackDesc, nullptr, 0u);
        if (!readback.isValid()) {
            break;
        }
    }
    if (!m_sampler.isValid() || !m_terrainSampler.isValid() || !m_linearClampSampler.isValid() ||
        !m_secondaryLightingBuffer.isValid() || !m_counterBuffer.isValid() ||
        !std::all_of(m_counterReadbackBuffers.begin(), m_counterReadbackBuffers.end(),
                     [](const RhiBufferHandle buffer) { return buffer.isValid(); })) {
        destroyRhiResources();
        return reject("RTGI trace sampler, uniform buffer, or counter buffers could not be created");
    }

    RhiBindGroupLayoutDesc traceLayoutDesc;
    traceLayoutDesc.debugName = "RTGI.Trace.BindGroupLayout";
    const RhiCapabilities& capabilities = rhiDevice.capabilities();
    const RhiBindingFlags partiallyBoundAndUnused =
        rhiFlag(RhiBindingFlag::PartiallyBound) | rhiFlag(RhiBindingFlag::UpdateUnusedWhilePending);
    const auto traceBindingFlags = [&](const RhiBindingType type) {
        bool updateAfterBind = false;
        switch (type) {
        case RhiBindingType::UniformBuffer:
            updateAfterBind = capabilities.descriptorBindingUniformBufferUpdateAfterBind;
            break;
        case RhiBindingType::StorageBuffer:
            updateAfterBind = capabilities.descriptorBindingStorageBufferUpdateAfterBind;
            break;
        case RhiBindingType::StorageTexture:
            updateAfterBind = capabilities.descriptorBindingStorageImageUpdateAfterBind;
            break;
        case RhiBindingType::CombinedTextureSampler:
        case RhiBindingType::SampledTexture:
            updateAfterBind = capabilities.descriptorBindingSampledImageUpdateAfterBind;
            break;
        default: break;
        }
        return partiallyBoundAndUnused | (updateAfterBind ? rhiFlag(RhiBindingFlag::UpdateAfterBind) : 0u);
    };
    for (uint32_t binding = 0u; binding < 4u; ++binding) {
        traceLayoutDesc.entries.push_back({binding, RhiBindingType::CombinedTextureSampler,
                                           rhiFlag(RhiShaderStage::Compute), 1u,
                                           traceBindingFlags(RhiBindingType::CombinedTextureSampler)});
    }
    traceLayoutDesc.entries.push_back({4u, RhiBindingType::StorageTexture, rhiFlag(RhiShaderStage::Compute), 1u,
                                       traceBindingFlags(RhiBindingType::StorageTexture)});
    traceLayoutDesc.entries.push_back({5u, RhiBindingType::StorageTexture, rhiFlag(RhiShaderStage::Compute), 1u,
                                       traceBindingFlags(RhiBindingType::StorageTexture)});
    traceLayoutDesc.entries.push_back({6u, RhiBindingType::StorageBuffer, rhiFlag(RhiShaderStage::Compute), 1u,
                                       traceBindingFlags(RhiBindingType::StorageBuffer)});
    traceLayoutDesc.entries.push_back({7u, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Compute), 1u,
                                       traceBindingFlags(RhiBindingType::CombinedTextureSampler)});
    for (uint32_t binding = 8u; binding <= 10u; ++binding) {
        traceLayoutDesc.entries.push_back({binding, RhiBindingType::StorageBuffer, rhiFlag(RhiShaderStage::Compute), 1u,
                                           traceBindingFlags(RhiBindingType::StorageBuffer)});
    }
    for (uint32_t binding = 11u; binding <= 15u; ++binding) {
        traceLayoutDesc.entries.push_back({binding, RhiBindingType::CombinedTextureSampler,
                                           rhiFlag(RhiShaderStage::Compute), 1u,
                                           traceBindingFlags(RhiBindingType::CombinedTextureSampler)});
    }
    traceLayoutDesc.entries.push_back({16u, RhiBindingType::UniformBuffer, rhiFlag(RhiShaderStage::Compute), 1u,
                                       traceBindingFlags(RhiBindingType::UniformBuffer)});
    traceLayoutDesc.entries.push_back({17u, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Compute),
                                       1u, traceBindingFlags(RhiBindingType::CombinedTextureSampler)});
    m_traceBindGroupLayout = rhiDevice.createBindGroupLayout(traceLayoutDesc);
    if (!m_traceBindGroupLayout.isValid()) {
        destroyRhiResources();
        return reject("RTGI trace bind group layout creation failed");
    }

    RhiBindGroupLayoutDesc counterLayoutDesc;
    counterLayoutDesc.debugName = "RTGI.TraceCounters.BindGroupLayout";
    counterLayoutDesc.entries.push_back({0u, RhiBindingType::StorageTexture, rhiFlag(RhiShaderStage::Compute), 1u});
    counterLayoutDesc.entries.push_back({1u, RhiBindingType::StorageBuffer, rhiFlag(RhiShaderStage::Compute), 1u});
    m_counterBindGroupLayout = rhiDevice.createBindGroupLayout(counterLayoutDesc);
    if (!m_counterBindGroupLayout.isValid()) {
        destroyRhiResources();
        return reject("RTGI counter bind group layout creation failed");
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "RTGI.Trace.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(globalBindlessLayout);
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_traceBindGroupLayout);
    pipelineLayoutDesc.bindGroupLayouts.push_back(lightingLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(renderer::contracts::RtgiTracePushConstants);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Compute);
    m_pipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_pipelineLayout.isValid()) {
        destroyRhiResources();
        return reject("RTGI trace pipeline layout creation failed");
    }

    RhiPipelineLayoutDesc counterPipelineLayoutDesc;
    counterPipelineLayoutDesc.debugName = "RTGI.TraceCounters.PipelineLayout";
    counterPipelineLayoutDesc.bindGroupLayouts.push_back(m_counterBindGroupLayout);
    counterPipelineLayoutDesc.pushConstantBytes = sizeof(renderer::contracts::RtgiTraceCounterPushConstants);
    counterPipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Compute);
    m_counterPipelineLayout = rhiDevice.createPipelineLayout(counterPipelineLayoutDesc);
    if (!m_counterPipelineLayout.isValid()) {
        destroyRhiResources();
        return reject("RTGI counter pipeline layout creation failed");
    }

    RhiComputePipelineDesc pipelineDesc;
    pipelineDesc.debugName = "RTGI.Trace.Pipeline";
    pipelineDesc.computeShader = m_shader;
    pipelineDesc.layout = m_pipelineLayout;
    m_pipeline = rhiDevice.createComputePipeline(pipelineDesc);
    if (!m_pipeline.isValid()) {
        destroyRhiResources();
        return reject("RTGI trace compute pipeline creation failed");
    }

    RhiComputePipelineDesc counterPipelineDesc;
    counterPipelineDesc.debugName = "RTGI.TraceCounters.Pipeline";
    counterPipelineDesc.computeShader = m_counterShader;
    counterPipelineDesc.layout = m_counterPipelineLayout;
    m_counterPipeline = rhiDevice.createComputePipeline(counterPipelineDesc);
    if (!m_counterPipeline.isValid()) {
        destroyRhiResources();
        return reject("RTGI counter compute pipeline creation failed");
    }
    m_counterStats.supported = true;
    return true;
}

bool RtgiTracePass::ensureBindGroup(RhiDevice& rhiDevice, const TraceViews& views,
                                    const TraceSceneBuffers& sceneBuffers) {
    const auto reject = [](const char* reason) {
        MECRAFT_LOG_STREAM(std::cerr << "[RtgiTracePass] Bind group setup rejected: " << reason << '\n');
        return false;
    };
    const std::array<RhiTextureViewHandle, 13u> boundViews{views.depth,
                                                           views.normalAo,
                                                           views.materialAux,
                                                           views.blueNoise,
                                                           views.terrainAlbedo,
                                                           views.terrainNormal,
                                                           views.terrainSpecular,
                                                           views.grassColormap,
                                                           views.foliageColormap,
                                                           views.skyCapture,
                                                           views.diffuseRadianceHitDistance,
                                                           views.validation,
                                                           views.voxelLight};
    for (const RhiTextureViewHandle view : boundViews) {
        if (!view.isValid()) {
            return reject("one or more RTGI texture views are invalid");
        }
    }
    RhiTextureViewDesc terrainAlbedoDesc;
    RhiTextureViewDesc terrainNormalDesc;
    RhiTextureViewDesc terrainSpecularDesc;
    RhiTextureViewDesc grassColormapDesc;
    RhiTextureViewDesc foliageColormapDesc;
    RhiTextureViewDesc skyCaptureDesc;
    RhiTextureViewDesc voxelLightDesc;
    const std::array<RhiBufferHandle, 4u> boundSceneBuffers{sceneBuffers.terrainHitData, sceneBuffers.gpuSceneMaterials,
                                                            sceneBuffers.gpuSceneGeometries,
                                                            sceneBuffers.gpuSceneInstances};
    const std::array<uint64_t, 4u> boundSceneBufferBytes{
        sceneBuffers.terrainHitDataBytes, sceneBuffers.gpuSceneMaterialBytes, sceneBuffers.gpuSceneGeometryBytes,
        sceneBuffers.gpuSceneInstanceBytes};
    if (std::any_of(boundSceneBuffers.begin(), boundSceneBuffers.end(),
                    [](const RhiBufferHandle buffer) { return !buffer.isValid(); })) {
        return reject("one or more Scene TLAS buffers are invalid");
    }
    if (std::any_of(boundSceneBufferBytes.begin(), boundSceneBufferBytes.end(),
                    [](const uint64_t bytes) { return bytes == 0u; })) {
        return reject("one or more Scene TLAS buffer ranges are empty");
    }
    if (!rhiDevice.getTextureViewDesc(views.terrainAlbedo, terrainAlbedoDesc) ||
        !rhiDevice.getTextureViewDesc(views.terrainNormal, terrainNormalDesc) ||
        !rhiDevice.getTextureViewDesc(views.terrainSpecular, terrainSpecularDesc) ||
        !rhiDevice.getTextureViewDesc(views.grassColormap, grassColormapDesc) ||
        !rhiDevice.getTextureViewDesc(views.foliageColormap, foliageColormapDesc) ||
        !rhiDevice.getTextureViewDesc(views.skyCapture, skyCaptureDesc) ||
        !rhiDevice.getTextureViewDesc(views.voxelLight, voxelLightDesc)) {
        return reject("one or more RTGI texture view descriptors are unavailable");
    }
    if (terrainAlbedoDesc.viewType != RhiTextureViewType::Texture2DArray ||
        terrainNormalDesc.viewType != RhiTextureViewType::Texture2DArray ||
        terrainSpecularDesc.viewType != RhiTextureViewType::Texture2DArray) {
        return reject("terrain material views are not 2D arrays");
    }
    if (grassColormapDesc.viewType != RhiTextureViewType::Texture2D ||
        foliageColormapDesc.viewType != RhiTextureViewType::Texture2D ||
        skyCaptureDesc.viewType != RhiTextureViewType::Texture2D ||
        voxelLightDesc.viewType != RhiTextureViewType::Texture2D) {
        return reject("colormap, sky capture, or voxel-light views are not 2D textures");
    }
    bool unchanged = m_traceBindGroup.isValid();
    for (size_t index = 0u; index < boundViews.size(); ++index) {
        unchanged = unchanged && sameHandle(m_boundViews[index], boundViews[index]);
    }
    for (size_t index = 0u; index < boundSceneBuffers.size(); ++index) {
        unchanged = unchanged && sameHandle(m_boundSceneBuffers[index], boundSceneBuffers[index]) &&
                    m_boundSceneBufferBytes[index] == boundSceneBufferBytes[index];
    }
    if (unchanged) {
        return true;
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
        entry.resource.textureView = boundViews[binding + 6u];
        bindGroupDesc.entries.push_back(entry);
    }
    RhiBindGroupEntry hitDataEntry;
    hitDataEntry.binding = 6u;
    hitDataEntry.resource.buffer = {sceneBuffers.terrainHitData, 0u, sceneBuffers.terrainHitDataBytes};
    bindGroupDesc.entries.push_back(hitDataEntry);
    RhiBindGroupEntry terrainAlbedoEntry;
    terrainAlbedoEntry.binding = 7u;
    terrainAlbedoEntry.resource.combinedTextureSampler = {views.terrainAlbedo, m_terrainSampler};
    bindGroupDesc.entries.push_back(terrainAlbedoEntry);
    for (uint32_t index = 1u; index < boundSceneBuffers.size(); ++index) {
        RhiBindGroupEntry sceneBufferEntry;
        sceneBufferEntry.binding = 7u + index;
        sceneBufferEntry.resource.buffer = {boundSceneBuffers[index], 0u, boundSceneBufferBytes[index]};
        bindGroupDesc.entries.push_back(sceneBufferEntry);
    }
    const std::array<RhiTextureViewHandle, 5u> materialViews{
        views.terrainNormal, views.terrainSpecular, views.grassColormap, views.foliageColormap, views.skyCapture};
    for (uint32_t index = 0u; index < materialViews.size(); ++index) {
        RhiBindGroupEntry entry;
        entry.binding = 11u + index;
        entry.resource.combinedTextureSampler.textureView = materialViews[index];
        entry.resource.combinedTextureSampler.sampler = index < 2u ? m_terrainSampler : m_linearClampSampler;
        bindGroupDesc.entries.push_back(entry);
    }
    RhiBindGroupEntry lightingParamsEntry;
    lightingParamsEntry.binding = 16u;
    lightingParamsEntry.resource.buffer = {m_secondaryLightingBuffer, 0u,
                                           sizeof(renderer::contracts::RtgiSecondaryLightingParams)};
    bindGroupDesc.entries.push_back(lightingParamsEntry);
    RhiBindGroupEntry voxelLightEntry;
    voxelLightEntry.binding = 17u;
    voxelLightEntry.resource.combinedTextureSampler = {views.voxelLight, m_sampler};
    bindGroupDesc.entries.push_back(voxelLightEntry);
    const RhiBindGroupHandle previousBindGroup = m_traceBindGroup;
    if (previousBindGroup.isValid()) {
        // Keep one descriptor set across TLAS and transient-view generations.
        // This avoids descriptor-pool churn and is legal after the previous
        // frame has been made idle; the layout flags also cover executable
        // command lists retained by the command-list pool.
        rhiDevice.waitIdle();
        std::vector<RhiBindingResource> resources;
        resources.reserve(bindGroupDesc.entries.size());
        for (const RhiBindGroupEntry& entry : bindGroupDesc.entries) {
            resources.push_back(entry.resource);
        }
        std::vector<RhiBindGroupUpdate> updates;
        updates.reserve(bindGroupDesc.entries.size());
        for (size_t index = 0u; index < bindGroupDesc.entries.size(); ++index) {
            updates.push_back({previousBindGroup, bindGroupDesc.entries[index].binding,
                               bindGroupDesc.entries[index].arrayElement, &resources[index], 1u});
        }
        if (rhiDevice.updateBindGroups(updates.data(), static_cast<uint32_t>(updates.size()))) {
            m_boundViews = boundViews;
            m_boundSceneBuffers = boundSceneBuffers;
            m_boundSceneBufferBytes = boundSceneBufferBytes;
            return true;
        }

        // The update can be rejected for a lifecycle or resource-contract
        // reason. Once the device is idle, the old descriptor set is no
        // longer needed and must be released before allocating its replacement;
        // otherwise a finite descriptor pool can remain exhausted forever.
        rhiDevice.destroyBindGroup(previousBindGroup);
        m_traceBindGroup = {};
    }
    RhiBindGroupHandle newBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!newBindGroup.isValid()) {
        // Scene/TLAS revisions can arrive while descriptor allocations from the
        // previous frame are still being reclaimed. Retry after the idle point.
        rhiDevice.waitIdle();
        newBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    }
    if (!newBindGroup.isValid()) {
        return reject("RHI rejected RTGI bind group creation");
    }
    if (previousBindGroup.isValid()) {
        rhiDevice.destroyBindGroup(previousBindGroup);
    }
    m_traceBindGroup = newBindGroup;
    m_boundViews = boundViews;
    m_boundSceneBuffers = boundSceneBuffers;
    m_boundSceneBufferBytes = boundSceneBufferBytes;
    return true;
}

void RtgiTracePass::destroyRhiResources() {
    if (m_rhiDevice != nullptr) {
        if (m_counterBindGroup.isValid()) {
            m_rhiDevice->destroyBindGroup(m_counterBindGroup);
        }
        if (m_traceBindGroup.isValid()) {
            m_rhiDevice->destroyBindGroup(m_traceBindGroup);
        }
        if (m_counterPipeline.isValid()) {
            m_rhiDevice->destroyPipeline(m_counterPipeline);
        }
        if (m_pipeline.isValid()) {
            m_rhiDevice->destroyPipeline(m_pipeline);
        }
        if (m_counterPipelineLayout.isValid()) {
            m_rhiDevice->destroyPipelineLayout(m_counterPipelineLayout);
        }
        if (m_pipelineLayout.isValid()) {
            m_rhiDevice->destroyPipelineLayout(m_pipelineLayout);
        }
        if (m_counterBindGroupLayout.isValid()) {
            m_rhiDevice->destroyBindGroupLayout(m_counterBindGroupLayout);
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
        if (m_linearClampSampler.isValid()) {
            m_rhiDevice->destroySampler(m_linearClampSampler);
        }
        if (m_secondaryLightingBuffer.isValid()) {
            m_rhiDevice->destroyBuffer(m_secondaryLightingBuffer);
        }
        if (m_counterBuffer.isValid()) {
            m_rhiDevice->destroyBuffer(m_counterBuffer);
        }
        for (const RhiBufferHandle readback : m_counterReadbackBuffers) {
            if (readback.isValid()) {
                m_rhiDevice->destroyBuffer(readback);
            }
        }
        if (m_counterShader.isValid()) {
            m_rhiDevice->destroyShader(m_counterShader);
        }
        if (m_shader.isValid()) {
            m_rhiDevice->destroyShader(m_shader);
        }
    }
    m_rhiDevice = nullptr;
    m_shader = {};
    m_counterShader = {};
    m_sampler = {};
    m_terrainSampler = {};
    m_linearClampSampler = {};
    m_secondaryLightingBuffer = {};
    m_globalBindlessLayout = {};
    m_lightingLayout = {};
    m_traceBindGroupLayout = {};
    m_counterBindGroupLayout = {};
    m_pipelineLayout = {};
    m_counterPipelineLayout = {};
    m_pipeline = {};
    m_counterPipeline = {};
    m_traceBindGroup = {};
    m_counterBindGroup = {};
    m_counterBuffer = {};
    m_counterReadbackBuffers.fill({});
    m_counterReadbackWritten.fill(false);
    m_counterReadbackTokens.fill({});
    m_counterReadbackMetadata.fill({});
    m_counterReadbackWriteIndex = 0u;
    m_pendingCounterReadbackIndex = 0u;
    m_counterReadbackSlotAvailable = true;
    m_counterReadbackPending = false;
    m_counterHealthy = true;
    m_boundCounterValidationView = {};
    m_boundSceneBuffers = {};
    m_boundSceneBufferBytes = {};
    m_boundViews = {};
    m_counterStats = {};
}
