#include "HiZPass.h"

#include "../core/RenderScene.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../targets/DeferredRenderTargets.h"

#include <algorithm>
#include <cstdio>
#include <optional>

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
            {passName, RgPassType::Compute, RhiQueueType::Graphics});
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
    m_mipBindings.clear();
    m_pipeline = {};
    m_pipelineLayout = {};
    m_bindGroupLayout = {};
    m_sampler = {};
    m_shader = {};
    m_rhiDevice = nullptr;
}
