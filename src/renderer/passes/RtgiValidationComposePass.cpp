#include "RtgiValidationComposePass.h"

#include "renderer/core/RenderScene.h"
#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiShaderSourceLoader.h"

#include <cmath>
#include <cstddef>
#include <optional>

namespace {
struct alignas(16) RtgiValidationComposePushConstants final {
    glm::vec4 renderExtentAndInversePreExposure{1.0f};
};
static_assert(sizeof(RtgiValidationComposePushConstants) == 16u);

[[nodiscard]] bool sameHandle(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] bool sameHandle(const RgTextureHandle lhs, const RgTextureHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] bool textureViewMatches(RhiDevice& rhiDevice, const RhiTextureViewHandle view,
                                      const RhiTextureFormat expectedFormat,
                                      const RhiTextureUsage requiredUsage, const uint32_t width,
                                      const uint32_t height) {
    RhiTextureViewDesc viewDesc;
    RhiTextureDesc textureDesc;
    return view.isValid() && rhiDevice.getTextureViewDesc(view, viewDesc) &&
           rhiDevice.getTextureDesc(viewDesc.texture, textureDesc) &&
           viewDesc.viewType == RhiTextureViewType::Texture2D && viewDesc.format == expectedFormat &&
           viewDesc.baseMip == 0u && viewDesc.mipCount == 1u && viewDesc.baseLayer == 0u &&
           viewDesc.layerCount == 1u && textureDesc.dimension == RhiTextureDimension::Texture2D &&
           textureDesc.format == expectedFormat && textureDesc.width == width &&
           textureDesc.height == height && textureDesc.depthOrLayers == 1u && textureDesc.mipLevels == 1u &&
           textureDesc.sampleCount == 1u && (textureDesc.usage & rhiFlag(requiredUsage)) != 0u;
}
} // namespace

void RtgiValidationComposePass::shutdown() {
    destroyRhiResources();
    m_stats = {};
}

RgPassHandle RtgiValidationComposePass::addGraphPass(RenderGraph& graph, const FrameContext& ctx,
                                                     const Settings& settings, const GraphResources& resources,
                                                     const RgPassHandle dependency) {
    if (!dependency.isValid() || ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        ctx.shared->rhiDevice->backend() != RhiBackend::Vulkan || !ctx.temporalExtents.renderExtent.isValid() ||
        !std::isfinite(ctx.preExposure) || ctx.preExposure <= 0.0f ||
        (settings.encoding != Encoding::RelaxLinearRgb && settings.encoding != Encoding::ReblurYCoCg) ||
        !resources.denoisedIndirectRadianceHitDistance.isValid() || !resources.emissiveDirectRadiance.isValid() ||
        !resources.combinedValidationRadiance.isValid() ||
        sameHandle(resources.denoisedIndirectRadianceHitDistance, resources.emissiveDirectRadiance) ||
        sameHandle(resources.denoisedIndirectRadianceHitDistance, resources.combinedValidationRadiance) ||
        sameHandle(resources.emissiveDirectRadiance, resources.combinedValidationRadiance)) {
        return {};
    }

    const FrameContext* frame = &ctx;
    const GraphResources frameResources = resources;
    RenderGraphPassBuilder compose =
        graph.addPass({"RTGI.ValidationCompose", RgPassType::Compute, RhiQueueType::Graphics});
    compose.dependsOn(dependency)
        .readTexture(resources.denoisedIndirectRadianceHitDistance, RhiResourceState::ShaderRead)
        .readTexture(resources.emissiveDirectRadiance, RhiResourceState::ShaderRead)
        .writeTexture(resources.combinedValidationRadiance, RhiResourceState::ShaderWrite)
        .setExecute([this, frame, settings, frameResources](RgPassContext& pass) {
            const ComposeViews views{pass.textureView(frameResources.denoisedIndirectRadianceHitDistance),
                                     pass.textureView(frameResources.emissiveDirectRadiance),
                                     pass.textureView(frameResources.combinedValidationRadiance)};
            return recordCompose(pass.commandList(), *frame, settings, views);
        });
    return compose.handle();
}

bool RtgiValidationComposePass::recordCompose(RhiCommandList& commandList, const FrameContext& ctx,
                                              const Settings& settings, const ComposeViews& views) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr || !std::isfinite(ctx.preExposure) ||
        ctx.preExposure <= 0.0f || !ensurePipeline(*ctx.shared->rhiDevice, settings.encoding)) {
        return false;
    }
    const TemporalExtent extent = ctx.temporalExtents.renderExtent;
    if (!ensureBindGroup(*ctx.shared->rhiDevice, views, extent.width, extent.height)) {
        return false;
    }

    RtgiValidationComposePushConstants pushConstants;
    pushConstants.renderExtentAndInversePreExposure =
        glm::vec4(static_cast<float>(extent.width), static_cast<float>(extent.height), 1.0f / ctx.preExposure, 0.0f);
    commandList.setComputePipeline(m_pipeline);
    commandList.setBindGroup(0u, m_bindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch((extent.width + 7u) / 8u, (extent.height + 7u) / 8u, 1u);

    m_stats.dispatched = true;
    m_stats.width = extent.width;
    m_stats.height = extent.height;
    m_stats.inversePreExposure = 1.0f / ctx.preExposure;
    m_stats.encoding = settings.encoding;
    return true;
}

bool RtgiValidationComposePass::ensurePipeline(RhiDevice& rhiDevice, const Encoding encoding) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyRhiResources();
    }
    if (m_pipeline.isValid() && m_pipelineEncodingValid && m_pipelineEncoding == encoding) {
        return true;
    }
    if (m_pipeline.isValid()) {
        destroyRhiResources();
    }
    m_rhiDevice = &rhiDevice;

    renderer::rhi::RhiShaderSourceOptions sourceOptions;
    sourceOptions.preprocessorDefinitions.emplace_back(
        encoding == Encoding::RelaxLinearRgb ? "MECRAFT_RTGI_VALIDATION_COMPOSE_RELAX"
                                             : "MECRAFT_RTGI_VALIDATION_COMPOSE_REBLUR");
    const std::optional<std::string> source =
        renderer::rhi::loadShaderSource("assets/shaders/rtgi_validation_compose.comp", sourceOptions);
    if (!source.has_value()) {
        destroyRhiResources();
        return false;
    }

    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = encoding == Encoding::RelaxLinearRgb ? "RTGI.ValidationCompose.Relax.Compute"
                                                                : "RTGI.ValidationCompose.Reblur.Compute";
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
    layoutDesc.debugName = "RTGI.ValidationCompose.BindGroupLayout";
    layoutDesc.entries.push_back({0u, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Compute), 1u});
    layoutDesc.entries.push_back({1u, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Compute), 1u});
    layoutDesc.entries.push_back({2u, RhiBindingType::StorageTexture, rhiFlag(RhiShaderStage::Compute), 1u});
    m_bindGroupLayout = rhiDevice.createBindGroupLayout(layoutDesc);
    if (!m_bindGroupLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "RTGI.ValidationCompose.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(RtgiValidationComposePushConstants);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Compute);
    m_pipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_pipelineLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiComputePipelineDesc pipelineDesc;
    pipelineDesc.debugName = encoding == Encoding::RelaxLinearRgb ? "RTGI.ValidationCompose.Relax.Pipeline"
                                                                 : "RTGI.ValidationCompose.Reblur.Pipeline";
    pipelineDesc.computeShader = m_shader;
    pipelineDesc.layout = m_pipelineLayout;
    m_pipeline = rhiDevice.createComputePipeline(pipelineDesc);
    if (!m_pipeline.isValid()) {
        destroyRhiResources();
        return false;
    }
    m_pipelineEncoding = encoding;
    m_pipelineEncodingValid = true;
    return true;
}

bool RtgiValidationComposePass::ensureBindGroup(RhiDevice& rhiDevice, const ComposeViews& views,
                                                const uint32_t width, const uint32_t height) {
    const std::array<RhiTextureViewHandle, 3u> boundViews{views.denoisedIndirectRadianceHitDistance,
                                                         views.emissiveDirectRadiance,
                                                         views.combinedValidationRadiance};
    if (sameHandle(boundViews[0], boundViews[1]) || sameHandle(boundViews[0], boundViews[2]) ||
        sameHandle(boundViews[1], boundViews[2]) ||
        !textureViewMatches(rhiDevice, boundViews[0], RhiTextureFormat::Rgba16Float,
                            RhiTextureUsage::Sampled, width, height) ||
        !textureViewMatches(rhiDevice, boundViews[1], RhiTextureFormat::Rgba32Float,
                            RhiTextureUsage::Sampled, width, height) ||
        !textureViewMatches(rhiDevice, boundViews[2], RhiTextureFormat::Rgba16Float,
                            RhiTextureUsage::Storage, width, height)) {
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
    RhiBindGroupEntry outputEntry;
    outputEntry.binding = 2u;
    outputEntry.resource.textureView = boundViews[2];
    bindGroupDesc.entries.push_back(outputEntry);
    m_bindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_bindGroup.isValid()) {
        m_boundViews = {};
        return false;
    }
    m_boundViews = boundViews;
    return true;
}

void RtgiValidationComposePass::destroyRhiResources() {
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
    m_pipelineEncoding = Encoding::RelaxLinearRgb;
    m_pipelineEncodingValid = false;
}
