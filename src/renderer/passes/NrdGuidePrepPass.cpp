#include "NrdGuidePrepPass.h"

#include "renderer/core/RenderScene.h"
#include "renderer/debug/RenderDebugService.h"
#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiShaderSourceLoader.h"

#include <cmath>
#include <optional>

namespace {
struct alignas(16) NrdGuidePrepPushConstants final {
    glm::mat4 inverseProjection{1.0f};
    glm::mat4 currentClipToPreviousView{1.0f};
    glm::vec4 renderExtentInvalidViewZAndHistory{1.0f};
};
static_assert(sizeof(NrdGuidePrepPushConstants) == 144u);

[[nodiscard]] bool sameHandle(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
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

void NrdGuidePrepPass::shutdown() {
    destroyRhiResources();
    m_stats = {};
}

RgPassHandle NrdGuidePrepPass::addGraphPass(RenderGraph& graph, const FrameContext& ctx, const Settings& settings,
                                            const GraphResources& resources, const RgPassHandle dependency) {
    if (!dependency.isValid() || ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        ctx.shared->rhiDevice->backend() != RhiBackend::Vulkan || !ctx.temporalExtents.renderExtent.isValid() ||
        !ctx.shared->rhiDevice->capabilities().storageImageExtendedFormats || !std::isfinite(settings.denoisingRange) ||
        settings.denoisingRange <= 0.0f || !resources.depth.isValid() || !resources.normalAo.isValid() ||
        !resources.material.isValid() || !resources.velocity.isValid() || !resources.validation.isValid() ||
        !resources.previousValidation.isValid() || !resources.motion.isValid() ||
        !resources.normalRoughness.isValid() || !resources.viewZ.isValid() || !resources.confidence.isValid()) {
        return {};
    }
    if (!resources.currentValidationHistory.isValid()) {
        return {};
    }

    const FrameContext* frame = &ctx;
    const GraphResources frameResources = resources;
    RenderGraphPassBuilder guide = graph.addPass({"NRD.GuidePrep", RgPassType::Compute, RhiQueueType::Graphics});
    guide.dependsOn(dependency)
        .readTexture(resources.depth, RhiResourceState::DepthRead)
        .readTexture(resources.normalAo, RhiResourceState::ShaderRead)
        .readTexture(resources.material, RhiResourceState::ShaderRead)
        .readTexture(resources.velocity, RhiResourceState::ShaderRead)
        .readTexture(resources.validation, RhiResourceState::ShaderRead)
        .readTexture(resources.previousValidation, RhiResourceState::ShaderRead)
        .writeTexture(resources.motion, RhiResourceState::ShaderWrite)
        .writeTexture(resources.normalRoughness, RhiResourceState::ShaderWrite)
        .writeTexture(resources.viewZ, RhiResourceState::ShaderWrite)
        .writeTexture(resources.confidence, RhiResourceState::ShaderWrite)
        .writeTexture(resources.currentValidationHistory, RhiResourceState::ShaderWrite)
        .setExecute([this, frame, settings, frameResources](RgPassContext& pass) {
            const GuideViews views{pass.textureView(frameResources.depth),
                                   pass.textureView(frameResources.normalAo),
                                   pass.textureView(frameResources.material),
                                   pass.textureView(frameResources.velocity),
                                   pass.textureView(frameResources.validation),
                                   pass.textureView(frameResources.previousValidation),
                                   pass.textureView(frameResources.motion),
                                   pass.textureView(frameResources.normalRoughness),
                                   pass.textureView(frameResources.viewZ),
                                   pass.textureView(frameResources.confidence),
                                   pass.textureView(frameResources.currentValidationHistory)};
            return recordGuide(pass.commandList(), *frame, settings, views);
        });
    return guide.handle();
}

bool NrdGuidePrepPass::recordGuide(RhiCommandList& commandList, const FrameContext& ctx, const Settings& settings,
                                   const GuideViews& views) {
    glm::mat4 projection = ctx.camera.projection;
    if (settings.useJitteredProjection) {
        for (uint32_t column = 0u; column < 4u; ++column) {
            projection[column][0] += ctx.jitter.projectionOffset.x * ctx.camera.projection[column][3];
            projection[column][1] += ctx.jitter.projectionOffset.y * ctx.camera.projection[column][3];
        }
    }
    const glm::mat4 inverseProjection = glm::inverse(projection);
    glm::mat4 currentViewRotation = ctx.camera.view;
    glm::mat4 previousViewRotation = ctx.prevCamera.view;
    currentViewRotation[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    previousViewRotation[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    const glm::mat4 currentClipToPreviousView =
        previousViewRotation * glm::translate(glm::mat4(1.0f), ctx.camera.position - ctx.prevCamera.position) *
        glm::inverse(currentViewRotation) * inverseProjection;
    const TemporalExtent extent = ctx.temporalExtents.renderExtent;
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr || !finiteMatrix(inverseProjection) ||
        !finiteMatrix(currentClipToPreviousView) || !ensurePipeline(*ctx.shared->rhiDevice) ||
        !ensureBindGroup(*ctx.shared->rhiDevice, views, extent.width, extent.height)) {
        return false;
    }

    NrdGuidePrepPushConstants pushConstants;
    pushConstants.inverseProjection = inverseProjection;
    pushConstants.currentClipToPreviousView = currentClipToPreviousView;
    const uint32_t historyFlags = (settings.historyValid ? 1u : 0u) | (settings.validateHitIdentity ? 2u : 0u);
    pushConstants.renderExtentInvalidViewZAndHistory =
        glm::vec4(static_cast<float>(extent.width), static_cast<float>(extent.height), settings.denoisingRange * 2.0f,
                  static_cast<float>(historyFlags));

    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
                                              ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::NrdGuidePrep)
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
    m_stats.denoisingRange = settings.denoisingRange;
    return true;
}

bool NrdGuidePrepPass::ensurePipeline(RhiDevice& rhiDevice) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyRhiResources();
    }
    if (m_pipeline.isValid()) {
        return true;
    }
    m_rhiDevice = &rhiDevice;

    const std::optional<std::string> source = renderer::rhi::loadShaderSource("assets/shaders/nrd_guide_prep.comp");
    if (!source.has_value()) {
        destroyRhiResources();
        return false;
    }
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "NRD.GuidePrep.Compute";
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
    layoutDesc.debugName = "NRD.GuidePrep.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 6u; ++binding) {
        layoutDesc.entries.push_back(
            {binding, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Compute), 1u});
    }
    for (uint32_t binding = 6u; binding < 11u; ++binding) {
        layoutDesc.entries.push_back({binding, RhiBindingType::StorageTexture, rhiFlag(RhiShaderStage::Compute), 1u});
    }
    m_bindGroupLayout = rhiDevice.createBindGroupLayout(layoutDesc);
    if (!m_bindGroupLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "NRD.GuidePrep.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(NrdGuidePrepPushConstants);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Compute);
    m_pipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_pipelineLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiComputePipelineDesc pipelineDesc;
    pipelineDesc.debugName = "NRD.GuidePrep.Pipeline";
    pipelineDesc.computeShader = m_shader;
    pipelineDesc.layout = m_pipelineLayout;
    m_pipeline = rhiDevice.createComputePipeline(pipelineDesc);
    if (!m_pipeline.isValid()) {
        destroyRhiResources();
        return false;
    }
    return true;
}

bool NrdGuidePrepPass::ensureBindGroup(RhiDevice& rhiDevice, const GuideViews& views, const uint32_t width,
                                       const uint32_t height) {
    const std::array<RhiTextureViewHandle, 11u> boundViews{
        views.depth, views.normalAo, views.material, views.velocity, views.validation, views.previousValidation,
        views.motion, views.normalRoughness, views.viewZ, views.confidence, views.currentValidationHistory};
    const std::array<RhiTextureFormat, 11u> formats{
        RhiTextureFormat::Depth32Float, RhiTextureFormat::Rgb10A2Unorm, RhiTextureFormat::Rgba8Unorm,
        RhiTextureFormat::Rg16Float, RhiTextureFormat::Rg32Uint, RhiTextureFormat::Rg32Uint,
        RhiTextureFormat::Rgba16Float, RhiTextureFormat::Rgb10A2Unorm, RhiTextureFormat::R32Float,
        RhiTextureFormat::R8Unorm, RhiTextureFormat::Rg32Uint};
    for (uint32_t index = 0u; index < boundViews.size(); ++index) {
        const RhiTextureUsage usage = index < 6u ? RhiTextureUsage::Sampled : RhiTextureUsage::Storage;
        if (!textureViewMatches(rhiDevice, boundViews[index], formats[index], usage, width, height)) {
            return false;
        }
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
    for (uint32_t binding = 0u; binding < 6u; ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler = {boundViews[binding], m_sampler};
        bindGroupDesc.entries.push_back(entry);
    }
    for (uint32_t binding = 6u; binding < boundViews.size(); ++binding) {
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

void NrdGuidePrepPass::destroyRhiResources() {
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
