#include "HiZPass.h"

#include "../core/RenderScene.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../core/RenderSettings.h"
#include "../mesh/WorldRenderBuffer.h"
#include "../targets/DeferredRenderTargets.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace {
[[nodiscard]] bool sameTextureView(const RhiTextureViewHandle lhs,
                                   const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
} // namespace

void HiZPass::shutdown() {
    destroyRhiResources();
}

RgPassHandle HiZPass::addGraphPasses(RenderGraph& graph,
                                     const FrameContext& ctx,
                                     DeferredRenderTargets& targets,
                                     const GraphResources& resources,
                                     const RgPassHandle dependency) {
    const uint32_t mipCount = targets.hiZMipCount();
    if (!dependency.isValid() || !resources.historyDepthPrevious.isValid() ||
        !resources.hiZ.isValid() || mipCount == 0u) {
        return {};
    }

    const FrameContext* frame = &ctx;
    DeferredRenderTargets* frameTargets = &targets;
    RgPassHandle previous = dependency;
    for (uint32_t mip = 0u; mip < mipCount; ++mip) {
        char passName[32];
        std::snprintf(passName, sizeof(passName), "HiZ.Mip%u", mip);
        RenderGraphPassBuilder pass = graph.addPass(
            {passName, RgPassType::Compute, RhiQueueType::Graphics,
             /*threadSafeRecord=*/true});
        RgTextureSubresourceRange destRange;
        destRange.baseMip = mip;
        destRange.mipCount = 1u;
        destRange.baseLayer = 0u;
        destRange.layerCount = 1u;
        pass.dependsOn(previous)
            .writeTexture(resources.hiZ, RhiResourceState::ShaderWrite,
                          destRange);
        if (mip == 0u) {
            // Depth-format textures are sampled through the DepthRead state
            // on the graphics queue per the RHI's tracking convention.
            pass.readTexture(resources.historyDepthPrevious,
                             RhiResourceState::DepthRead);
        } else {
            RgTextureSubresourceRange sourceRange = destRange;
            sourceRange.baseMip = mip - 1u;
            pass.readTexture(resources.hiZ, RhiResourceState::ShaderRead,
                             sourceRange);
        }
        pass.setExecute([this, frame, frameTargets, mip](RgPassContext& p) {
            return recordMip(p.commandList(), *frame, *frameTargets, mip);
        });
        previous = pass.handle();
    }
    return previous;
}

bool HiZPass::recordMip(RhiCommandList& commandList,
                        const FrameContext& ctx,
                        DeferredRenderTargets& targets,
                        const uint32_t mip) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return false;
    }
    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!targets.ensureHiZTextureViews(rhiDevice) ||
        !targets.ensureHistoryDepthTextureViews(rhiDevice) ||
        !ensurePipeline(rhiDevice)) {
        return false;
    }
    const RhiTextureViewHandle sourceView = mip == 0u
        ? targets.historyDepthTexturePrevViewHandle()
        : targets.hiZMipTextureViewHandle(mip - 1u);
    const RhiTextureViewHandle destView = targets.hiZMipTextureViewHandle(mip);
    if (!ensureMipBindGroup(rhiDevice, mip, sourceView, destView)) {
        return false;
    }

    const uint32_t destWidth = std::max(
        1u, static_cast<uint32_t>(std::max(1, targets.width())) >> mip);
    const uint32_t destHeight = std::max(
        1u, static_cast<uint32_t>(std::max(1, targets.height())) >> mip);
    const glm::ivec4 pushConstants(static_cast<int>(destWidth),
                                   static_cast<int>(destHeight),
                                   mip == 0u ? 1 : 0,
                                   0);
    commandList.setComputePipeline(m_pipeline);
    commandList.setBindGroup(0u, m_mipBindings[mip].bindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                              rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch((destWidth + 7u) / 8u, (destHeight + 7u) / 8u, 1u);
    return true;
}

bool HiZPass::ensurePipeline(RhiDevice& rhiDevice) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyRhiResources();
    }
    if (m_pipeline.isValid()) {
        return true;
    }
    m_rhiDevice = &rhiDevice;

    const std::optional<std::string> source =
        renderer::rhi::loadShaderSource("assets/shaders/hiz_build.comp");
    if (!source.has_value()) {
        return false;
    }
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "HiZ.Build";
    shaderDesc.stage = RhiShaderStage::Compute;
    shaderDesc.source = source->c_str();
    shaderDesc.sourceSize = source->size();
    m_shader = rhiDevice.createShader(shaderDesc);
    if (!m_shader.isValid()) {
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
    layoutDesc.debugName = "HiZ.BindGroupLayout";
    layoutDesc.entries.push_back({
        0u,
        RhiBindingType::CombinedTextureSampler,
        rhiFlag(RhiShaderStage::Compute),
        1u
    });
    layoutDesc.entries.push_back({
        1u,
        RhiBindingType::StorageTexture,
        rhiFlag(RhiShaderStage::Compute),
        1u
    });
    m_bindGroupLayout = rhiDevice.createBindGroupLayout(layoutDesc);
    if (!m_bindGroupLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "HiZ.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes =
        static_cast<uint32_t>(sizeof(glm::ivec4));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Compute);
    m_pipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_pipelineLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiComputePipelineDesc pipelineDesc;
    pipelineDesc.debugName = "HiZ.Pipeline";
    pipelineDesc.computeShader = m_shader;
    pipelineDesc.layout = m_pipelineLayout;
    m_pipeline = rhiDevice.createComputePipeline(pipelineDesc);
    if (!m_pipeline.isValid()) {
        destroyRhiResources();
        return false;
    }
    return true;
}

bool HiZPass::ensureMipBindGroup(RhiDevice& rhiDevice,
                                 const uint32_t mip,
                                 const RhiTextureViewHandle sourceView,
                                 const RhiTextureViewHandle destView) {
    if (!sourceView.isValid() || !destView.isValid()) {
        return false;
    }
    if (m_mipBindings.size() <= mip) {
        m_mipBindings.resize(mip + 1u);
    }
    MipBinding& binding = m_mipBindings[mip];
    if (binding.bindGroup.isValid() &&
        sameTextureView(binding.boundSource, sourceView) &&
        sameTextureView(binding.boundDest, destView)) {
        return true;
    }
    if (binding.bindGroup.isValid()) {
        rhiDevice.destroyBindGroup(binding.bindGroup);
        binding = {};
    }

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_bindGroupLayout;
    RhiBindGroupEntry sourceEntry;
    sourceEntry.binding = 0u;
    sourceEntry.resource.combinedTextureSampler.textureView = sourceView;
    sourceEntry.resource.combinedTextureSampler.sampler = m_sampler;
    bindGroupDesc.entries.push_back(sourceEntry);
    RhiBindGroupEntry destEntry;
    destEntry.binding = 1u;
    destEntry.resource.textureView = destView;
    bindGroupDesc.entries.push_back(destEntry);

    binding.bindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!binding.bindGroup.isValid()) {
        binding = {};
        return false;
    }
    binding.boundSource = sourceView;
    binding.boundDest = destView;
    return true;
}


namespace {
struct HiZCullPushConstants {
    glm::mat4 viewProj;
    glm::vec4 params0;
    glm::vec4 params1;
};
} // namespace

RgPassHandle HiZPass::addCullPass(RenderGraph& graph,
                                  const FrameContext& ctx,
                                  const RenderSettings& settings,
                                  DeferredRenderTargets& targets,
                                  WorldRenderBuffer& worldBuffer,
                                  const RgTextureHandle hiZ,
                                  const RgPassHandle dependency) {
    if (!dependency.isValid() || !hiZ.isValid()) {
        return {};
    }
    const FrameContext* frame = &ctx;
    DeferredRenderTargets* frameTargets = &targets;
    WorldRenderBuffer* frameWorldBuffer = &worldBuffer;
    RenderGraphPassBuilder cull = graph.addPass(
        {"HiZ.Cull", RgPassType::Compute, RhiQueueType::Graphics});
    cull.dependsOn(dependency)
        .readTexture(hiZ, RhiResourceState::ShaderRead)
        .setExecute([this, frame, frameTargets, frameWorldBuffer,
                     settings](RgPassContext& pass) {
            return recordCull(pass.commandList(), *frame, settings,
                              *frameTargets, *frameWorldBuffer);
        });
    return cull.handle();
}

bool HiZPass::recordCull(RhiCommandList& commandList,
                         const FrameContext& ctx,
                         const RenderSettings& settings,
                         DeferredRenderTargets& targets,
                         WorldRenderBuffer& worldBuffer) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return false;
    }
    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!targets.ensureHiZTextureViews(rhiDevice) ||
        !ensurePipeline(rhiDevice) || !ensureCullPipeline(rhiDevice)) {
        return false;
    }

    const RhiBufferHandle commandBuffers[2] = {
        worldBuffer.opaqueIndirectBufferHandle(),
        worldBuffer.cutoutIndirectBufferHandle()
    };
    const uint64_t commandCapacities[2] = {
        worldBuffer.opaqueIndirectBufferCapacity(),
        worldBuffer.cutoutIndirectBufferCapacity()
    };
    const uint32_t commandCounts[2] = {
        static_cast<uint32_t>(worldBuffer.opaqueCommandCount()),
        static_cast<uint32_t>(worldBuffer.cutoutCommandCount())
    };

    // The pyramid holds the PREVIOUS frame's depth, so the test projects
    // with the previous frame's raster matrix (jittered when the raster
    // itself is jittered): matrix and depth from the same frame make the
    // test exact for static geometry under any camera motion.
    const bool projectionJitter = usesTemporalProjectionJitter(
        settings.upscale.type, settings.taa.enabled);
    HiZCullPushConstants pushConstants{};
    pushConstants.viewProj = projectionJitter ? ctx.previousJitteredViewProj
                                              : ctx.previousViewProj;
    pushConstants.params0 =
        glm::vec4(static_cast<float>(std::max(1, targets.width())),
                  static_cast<float>(std::max(1, targets.height())),
                  static_cast<float>(targets.hiZMipCount() - 1u),
                  0.0f);
    // Depth slack covers the pyramid's one-frame latency plus small vertex
    // animation; expressed in post-projection 0-1 depth.
    pushConstants.params1 = glm::vec4(5.0e-4f, 0.0f, 0.0f, 0.0f);

    // Consume the oldest readback slot before overwriting it; the ring is
    // deep enough that its copy completed frames ago.
    const uint32_t ringIndex = m_cullRingWriteIndex;
    if (m_cullRingWritten[ringIndex]) {
        const void* mapped = rhiDevice.mapBuffer(
            m_cullReadbackBuffers[ringIndex], 0u, sizeof(uint32_t) * 2u);
        if (mapped != nullptr) {
            uint32_t counts[2];
            std::memcpy(counts, mapped, sizeof(counts));
            rhiDevice.unmapBuffer(m_cullReadbackBuffers[ringIndex]);
            m_cullStats.valid = true;
            m_cullStats.opaqueCulled = counts[0];
            m_cullStats.cutoutCulled = counts[1];
            m_cullStats.opaqueTotal = m_cullTotalsRing[ringIndex][0];
            m_cullStats.cutoutTotal = m_cullTotalsRing[ringIndex][1];
        }
    }

    const uint32_t zeroCounts[2] = {0u, 0u};
    commandList.bufferBarrier({m_cullCounterBuffer,
                               RhiResourceState::StorageBuffer,
                               RhiResourceState::TransferDst});
    commandList.updateBuffer(m_cullCounterBuffer, 0u, zeroCounts,
                             sizeof(zeroCounts));
    commandList.bufferBarrier({m_cullCounterBuffer,
                               RhiResourceState::TransferDst,
                               RhiResourceState::StorageBuffer});

    for (int slot = 0; slot < 2; ++slot) {
        if (commandCounts[slot] == 0u || !commandBuffers[slot].isValid()) {
            continue;
        }
        if (!ensureCullBindGroup(rhiDevice, slot, commandBuffers[slot],
                                 commandCapacities[slot],
                                 worldBuffer.metadataBufferHandle(),
                                 worldBuffer.metadataBufferCapacity(),
                                 targets.hiZFullTextureViewHandle())) {
            return false;
        }
        pushConstants.params0.w = static_cast<float>(commandCounts[slot]);
        pushConstants.params1.y = static_cast<float>(slot);
        commandList.bufferBarrier({commandBuffers[slot],
                                   RhiResourceState::IndirectArgument,
                                   RhiResourceState::StorageBuffer});
        commandList.setComputePipeline(m_cullPipeline);
        commandList.setBindGroup(0u, m_cullBindings[slot].bindGroup);
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Compute));
        commandList.dispatch((commandCounts[slot] + 63u) / 64u, 1u, 1u);
        commandList.bufferBarrier({commandBuffers[slot],
                                   RhiResourceState::StorageBuffer,
                                   RhiResourceState::IndirectArgument});
    }

    // Ship this frame's counters into the readback ring.
    commandList.bufferBarrier({m_cullCounterBuffer,
                               RhiResourceState::StorageBuffer,
                               RhiResourceState::TransferSrc});
    if (m_cullRingWritten[ringIndex]) {
        commandList.bufferBarrier({m_cullReadbackBuffers[ringIndex],
                                   RhiResourceState::HostRead,
                                   RhiResourceState::TransferDst});
    }
    RhiBufferCopy statsCopy;
    statsCopy.src = m_cullCounterBuffer;
    statsCopy.dst = m_cullReadbackBuffers[ringIndex];
    statsCopy.size = sizeof(uint32_t) * 2u;
    commandList.copyBuffer(statsCopy);
    commandList.bufferBarrier({m_cullReadbackBuffers[ringIndex],
                               RhiResourceState::TransferDst,
                               RhiResourceState::HostRead});
    commandList.bufferBarrier({m_cullCounterBuffer,
                               RhiResourceState::TransferSrc,
                               RhiResourceState::StorageBuffer});
    m_pendingCullRingIndex = ringIndex;
    m_pendingCullTotals[0] = commandCounts[0];
    m_pendingCullTotals[1] = commandCounts[1];
    m_cullSubmissionPending = true;
    return true;
}

void HiZPass::finishGraphExecution(const bool succeeded) {
    if (!m_cullSubmissionPending) {
        return;
    }
    if (succeeded) {
        m_cullTotalsRing[m_pendingCullRingIndex][0] = m_pendingCullTotals[0];
        m_cullTotalsRing[m_pendingCullRingIndex][1] = m_pendingCullTotals[1];
        m_cullRingWritten[m_pendingCullRingIndex] = true;
        m_cullRingWriteIndex =
            (m_pendingCullRingIndex + 1u) % kCullStatsRingSize;
    }
    m_cullSubmissionPending = false;
}

bool HiZPass::ensureCullPipeline(RhiDevice& rhiDevice) {
    if (m_cullPipeline.isValid() && m_cullCounterBuffer.isValid() &&
        m_cullReadbackBuffers[kCullStatsRingSize - 1u].isValid()) {
        return true;
    }
    if (m_cullPipeline.isValid()) {
        return ensureCullStatsBuffers(rhiDevice);
    }
    const std::optional<std::string> source =
        renderer::rhi::loadShaderSource("assets/shaders/hiz_cull.comp");
    if (!source.has_value()) {
        return false;
    }
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "HiZ.Cull";
    shaderDesc.stage = RhiShaderStage::Compute;
    shaderDesc.source = source->c_str();
    shaderDesc.sourceSize = source->size();
    m_cullShader = rhiDevice.createShader(shaderDesc);
    if (!m_cullShader.isValid()) {
        return false;
    }

    RhiBindGroupLayoutDesc layoutDesc;
    layoutDesc.debugName = "HiZ.CullBindGroupLayout";
    layoutDesc.entries.push_back({
        0u, RhiBindingType::StorageBuffer, rhiFlag(RhiShaderStage::Compute), 1u});
    layoutDesc.entries.push_back({
        1u, RhiBindingType::StorageBuffer, rhiFlag(RhiShaderStage::Compute), 1u});
    layoutDesc.entries.push_back({
        2u, RhiBindingType::CombinedTextureSampler,
        rhiFlag(RhiShaderStage::Compute), 1u});
    layoutDesc.entries.push_back({
        3u, RhiBindingType::StorageBuffer, rhiFlag(RhiShaderStage::Compute), 1u});
    m_cullBindGroupLayout = rhiDevice.createBindGroupLayout(layoutDesc);
    if (!m_cullBindGroupLayout.isValid()) {
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "HiZ.CullPipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_cullBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes =
        static_cast<uint32_t>(sizeof(HiZCullPushConstants));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Compute);
    m_cullPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_cullPipelineLayout.isValid()) {
        return false;
    }

    RhiComputePipelineDesc pipelineDesc;
    pipelineDesc.debugName = "HiZ.CullPipeline";
    pipelineDesc.computeShader = m_cullShader;
    pipelineDesc.layout = m_cullPipelineLayout;
    m_cullPipeline = rhiDevice.createComputePipeline(pipelineDesc);
    if (!m_cullPipeline.isValid()) {
        return false;
    }

    return ensureCullStatsBuffers(rhiDevice);
}

bool HiZPass::ensureCullStatsBuffers(RhiDevice& rhiDevice) {
    if (!m_cullCounterBuffer.isValid()) {
        RhiBufferDesc counterDesc;
        counterDesc.debugName = "HiZ.CullCounters";
        counterDesc.size = sizeof(uint32_t) * 2u;
        counterDesc.usage = rhiFlag(RhiBufferUsage::Storage) |
                            rhiFlag(RhiBufferUsage::TransferSrc) |
                            rhiFlag(RhiBufferUsage::TransferDst);
        counterDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
        counterDesc.initialState = RhiResourceState::StorageBuffer;
        counterDesc.memoryCategory = RhiMemoryCategory::SceneData;
        m_cullCounterBuffer = rhiDevice.createBuffer(counterDesc, nullptr, 0u);
        if (!m_cullCounterBuffer.isValid()) {
            return false;
        }
    }
    for (uint32_t slot = 0u; slot < kCullStatsRingSize; ++slot) {
        if (m_cullReadbackBuffers[slot].isValid()) {
            continue;
        }
        // Mappable readback contract: MapRead usage with a TransferDst
        // resting state (HostRead is entered only after each copy).
        RhiBufferDesc readbackDesc;
        readbackDesc.debugName = "HiZ.CullReadback";
        readbackDesc.size = sizeof(uint32_t) * 2u;
        readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) |
                             rhiFlag(RhiBufferUsage::MapRead);
        readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
        readbackDesc.initialState = RhiResourceState::TransferDst;
        readbackDesc.memoryCategory = RhiMemoryCategory::Readback;
        m_cullReadbackBuffers[slot] = rhiDevice.createBuffer(readbackDesc, nullptr, 0u);
        if (!m_cullReadbackBuffers[slot].isValid()) {
            return false;
        }
    }
    return true;
}

bool HiZPass::ensureCullBindGroup(RhiDevice& rhiDevice,
                                  const int slot,
                                  const RhiBufferHandle commandBuffer,
                                  const uint64_t commandCapacity,
                                  const RhiBufferHandle metadataBuffer,
                                  const uint64_t metadataCapacity,
                                  const RhiTextureViewHandle hiZView) {
    if (!commandBuffer.isValid() || !metadataBuffer.isValid() ||
        !hiZView.isValid() || slot < 0 || slot >= 2) {
        return false;
    }
    CullBinding& binding = m_cullBindings[slot];
    if (binding.bindGroup.isValid() &&
        binding.boundCommands.index == commandBuffer.index &&
        binding.boundCommands.generation == commandBuffer.generation &&
        binding.boundMetadata.index == metadataBuffer.index &&
        binding.boundMetadata.generation == metadataBuffer.generation &&
        sameTextureView(binding.boundHiZ, hiZView)) {
        return true;
    }
    if (binding.bindGroup.isValid()) {
        rhiDevice.destroyBindGroup(binding.bindGroup);
        binding = {};
    }

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_cullBindGroupLayout;
    RhiBindGroupEntry commandsEntry;
    commandsEntry.binding = 0u;
    commandsEntry.resource.buffer.buffer = commandBuffer;
    commandsEntry.resource.buffer.offset = 0u;
    commandsEntry.resource.buffer.range = commandCapacity;
    bindGroupDesc.entries.push_back(commandsEntry);
    RhiBindGroupEntry metadataEntry;
    metadataEntry.binding = 1u;
    metadataEntry.resource.buffer.buffer = metadataBuffer;
    metadataEntry.resource.buffer.offset = 0u;
    metadataEntry.resource.buffer.range = metadataCapacity;
    bindGroupDesc.entries.push_back(metadataEntry);
    RhiBindGroupEntry hiZEntry;
    hiZEntry.binding = 2u;
    hiZEntry.resource.combinedTextureSampler.textureView = hiZView;
    hiZEntry.resource.combinedTextureSampler.sampler = m_sampler;
    bindGroupDesc.entries.push_back(hiZEntry);
    RhiBindGroupEntry statsEntry;
    statsEntry.binding = 3u;
    statsEntry.resource.buffer.buffer = m_cullCounterBuffer;
    statsEntry.resource.buffer.offset = 0u;
    statsEntry.resource.buffer.range = sizeof(uint32_t) * 2u;
    bindGroupDesc.entries.push_back(statsEntry);

    binding.bindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!binding.bindGroup.isValid()) {
        binding = {};
        return false;
    }
    binding.boundCommands = commandBuffer;
    binding.boundMetadata = metadataBuffer;
    binding.boundHiZ = hiZView;
    return true;
}

void HiZPass::destroyRhiResources() {
    if (m_rhiDevice != nullptr) {
        for (MipBinding& binding : m_mipBindings) {
            if (binding.bindGroup.isValid()) {
                m_rhiDevice->destroyBindGroup(binding.bindGroup);
            }
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
    if (m_rhiDevice != nullptr) {
        for (CullBinding& binding : m_cullBindings) {
            if (binding.bindGroup.isValid()) {
                m_rhiDevice->destroyBindGroup(binding.bindGroup);
            }
            binding = {};
        }
        if (m_cullPipeline.isValid()) {
            m_rhiDevice->destroyPipeline(m_cullPipeline);
        }
        if (m_cullPipelineLayout.isValid()) {
            m_rhiDevice->destroyPipelineLayout(m_cullPipelineLayout);
        }
        if (m_cullBindGroupLayout.isValid()) {
            m_rhiDevice->destroyBindGroupLayout(m_cullBindGroupLayout);
        }
        if (m_cullShader.isValid()) {
            m_rhiDevice->destroyShader(m_cullShader);
        }
        if (m_cullCounterBuffer.isValid()) {
            m_rhiDevice->destroyBuffer(m_cullCounterBuffer);
        }
        for (RhiBufferHandle& buffer : m_cullReadbackBuffers) {
            if (buffer.isValid()) {
                m_rhiDevice->destroyBuffer(buffer);
            }
            buffer = {};
        }
    }
    m_cullCounterBuffer = {};
    m_cullStats = {};
    for (bool& written : m_cullRingWritten) {
        written = false;
    }
    m_cullRingWriteIndex = 0u;
    m_pendingCullRingIndex = 0u;
    m_pendingCullTotals[0] = 0u;
    m_pendingCullTotals[1] = 0u;
    m_cullSubmissionPending = false;
    m_cullPipeline = {};
    m_cullPipelineLayout = {};
    m_cullBindGroupLayout = {};
    m_cullShader = {};
    m_mipBindings.clear();
    m_pipeline = {};
    m_pipelineLayout = {};
    m_bindGroupLayout = {};
    m_sampler = {};
    m_shader = {};
    m_rhiDevice = nullptr;
}
