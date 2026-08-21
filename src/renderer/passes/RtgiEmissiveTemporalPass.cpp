#include "RtgiEmissiveTemporalPass.h"

#include "renderer/core/RenderScene.h"
#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiShaderSourceLoader.h"

#include <cmath>
#include <cstddef>
#include <optional>

namespace {
struct alignas(16) RtgiEmissiveTemporalPushConstants final {
    glm::vec4 renderExtentHistoryAndExposure{1.0f};
    glm::vec4 rejectionParameters{1.0f};
};
static_assert(sizeof(RtgiEmissiveTemporalPushConstants) == 32u);

constexpr std::array<const char*, 2u> kEmissiveHistoryNames{
    "RTGI.EmissiveTemporal.EmissiveHistory0",
    "RTGI.EmissiveTemporal.EmissiveHistory1",
};
constexpr std::array<const char*, 2u> kNormalViewZHistoryNames{
    "RTGI.EmissiveTemporal.NormalViewZHistory0",
    "RTGI.EmissiveTemporal.NormalViewZHistory1",
};

[[nodiscard]] bool sameHandle(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
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

void RtgiEmissiveTemporalPass::shutdown() {
    destroyRhiResources();
    m_stats = {};
}

void RtgiEmissiveTemporalPass::invalidateHistory() {
    m_historyValid = false;
}

RtgiEmissiveTemporalPass::GraphOutput
RtgiEmissiveTemporalPass::addGraphPass(RenderGraph& graph, const FrameContext& ctx, const Settings& settings,
                                       const GraphResources& resources, const RgPassHandle dependency) {
    if (m_framePending || !dependency.isValid() || ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        ctx.shared->rhiDevice->backend() != RhiBackend::Vulkan ||
        !ctx.shared->rhiDevice->capabilities().storageImageExtendedFormats ||
        !ctx.temporalExtents.renderExtent.isValid() || !std::isfinite(ctx.preExposure) || ctx.preExposure <= 0.0f ||
        !std::isfinite(ctx.previousPreExposure) || ctx.previousPreExposure <= 0.0f ||
        !std::isfinite(settings.relativeDepthThreshold) || settings.relativeDepthThreshold <= 0.0f ||
        !std::isfinite(settings.maximumViewZ) || settings.maximumViewZ <= 0.0f ||
        !resources.currentEmissiveDirectRadiance.isValid() || !resources.motion.isValid() ||
        !resources.reprojectionCoverage.isValid() || !resources.normalRoughness.isValid() ||
        !resources.viewZ.isValid()) {
        return {};
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const TemporalExtent extent = ctx.temporalExtents.renderExtent;
    if (!ensurePipeline(rhiDevice) || !ensureHistoryResources(rhiDevice, extent.width, extent.height)) {
        return {};
    }

    if (!settings.historyValid) {
        m_historyValid = false;
    }
    const uint32_t readGeneration = m_readGeneration;
    const uint32_t writeGeneration = 1u - readGeneration;
    const bool initializeReadGeneration = !m_generationInitialized[readGeneration];
    const bool historyInputEnabled =
        settings.historyValid && m_historyValid && m_generationInitialized[readGeneration];

    const auto importHistoryTexture = [&](const char* name, const RhiTextureHandle texture,
                                          const RhiTextureViewHandle view, const bool initialized) {
        RhiTextureDesc desc;
        if (!rhiDevice.getTextureDesc(texture, desc)) {
            return RgTextureHandle{};
        }
        return graph.importTexture({name, texture, desc,
                                    initialized ? RhiResourceState::ShaderRead : RhiResourceState::Undefined,
                                    RhiResourceState::ShaderRead, view, RhiQueueType::Graphics,
                                    RhiQueueType::Graphics});
    };

    const HistoryGeneration& readHistory = m_historyGenerations[readGeneration];
    const HistoryGeneration& writeHistory = m_historyGenerations[writeGeneration];
    const RgTextureHandle previousEmissive =
        importHistoryTexture(kEmissiveHistoryNames[readGeneration], readHistory.emissive, readHistory.emissiveView,
                             m_generationInitialized[readGeneration]);
    const RgTextureHandle previousNormalViewZ =
        importHistoryTexture(kNormalViewZHistoryNames[readGeneration], readHistory.normalViewZ,
                             readHistory.normalViewZView, m_generationInitialized[readGeneration]);
    const RgTextureHandle outputEmissive =
        importHistoryTexture(kEmissiveHistoryNames[writeGeneration], writeHistory.emissive, writeHistory.emissiveView,
                             m_generationInitialized[writeGeneration]);
    const RgTextureHandle outputNormalViewZ =
        importHistoryTexture(kNormalViewZHistoryNames[writeGeneration], writeHistory.normalViewZ,
                             writeHistory.normalViewZView, m_generationInitialized[writeGeneration]);
    if (!previousEmissive.isValid() || !previousNormalViewZ.isValid() || !outputEmissive.isValid() ||
        !outputNormalViewZ.isValid()) {
        return {};
    }

    RgPassHandle temporalDependency = dependency;
    if (initializeReadGeneration) {
        RenderGraphPassBuilder initialize =
            graph.addPass({"RTGI.EmissiveTemporal.HistoryInitialize", RgPassType::Graphics,
                           RhiQueueType::Graphics, /*threadSafeRecord=*/true});
        initialize.dependsOn(dependency)
            .writeTexture(previousEmissive, RhiResourceState::RenderTarget)
            .writeTexture(previousNormalViewZ, RhiResourceState::RenderTarget)
            .setExecute([this, previousEmissive, previousNormalViewZ, extent](RgPassContext& pass) {
                return recordHistoryInitialize(pass.commandList(), pass.textureView(previousEmissive),
                                               pass.textureView(previousNormalViewZ), extent.width, extent.height);
            });
        temporalDependency = initialize.handle();
        if (!temporalDependency.isValid()) {
            return {};
        }
    }

    const FrameContext* frame = &ctx;
    const GraphResources frameResources = resources;
    RenderGraphPassBuilder temporal =
        graph.addPass({"RTGI.EmissiveTemporal", RgPassType::Compute, RhiQueueType::Graphics});
    temporal.dependsOn(temporalDependency)
        .readTexture(resources.currentEmissiveDirectRadiance, RhiResourceState::ShaderRead)
        .readTexture(resources.motion, RhiResourceState::ShaderRead)
        .readTexture(resources.reprojectionCoverage, RhiResourceState::ShaderRead)
        .readTexture(resources.normalRoughness, RhiResourceState::ShaderRead)
        .readTexture(resources.viewZ, RhiResourceState::ShaderRead)
        .readTexture(previousEmissive, RhiResourceState::ShaderRead)
        .readTexture(previousNormalViewZ, RhiResourceState::ShaderRead)
        .writeTexture(outputEmissive, RhiResourceState::ShaderWrite)
        .writeTexture(outputNormalViewZ, RhiResourceState::ShaderWrite)
        .setExecute([this, frame, settings, historyInputEnabled, readGeneration, writeGeneration, frameResources,
                     previousEmissive, previousNormalViewZ, outputEmissive,
                     outputNormalViewZ](RgPassContext& pass) {
            const TemporalViews views{
                pass.textureView(frameResources.currentEmissiveDirectRadiance),
                pass.textureView(frameResources.motion),
                pass.textureView(frameResources.reprojectionCoverage),
                pass.textureView(frameResources.normalRoughness),
                pass.textureView(frameResources.viewZ),
                pass.textureView(previousEmissive),
                pass.textureView(previousNormalViewZ),
                pass.textureView(outputEmissive),
                pass.textureView(outputNormalViewZ),
            };
            return recordTemporal(pass.commandList(), *frame, settings, historyInputEnabled, readGeneration,
                                  writeGeneration, views);
        });
    if (!temporal.handle().isValid()) {
        return {};
    }

    m_framePending = true;
    m_pendingReadGeneration = readGeneration;
    m_pendingWriteGeneration = writeGeneration;
    m_pendingReadInitialization = initializeReadGeneration;
    return {temporal.handle(), outputEmissive};
}

void RtgiEmissiveTemporalPass::finishGraphExecution(const bool succeeded) {
    if (!m_framePending) {
        return;
    }
    if (succeeded) {
        if (m_pendingReadInitialization) {
            m_generationInitialized[m_pendingReadGeneration] = true;
        }
        m_generationInitialized[m_pendingWriteGeneration] = true;
        m_readGeneration = m_pendingWriteGeneration;
        m_historyValid = true;
    } else {
        // The write generation may have been touched by a partially recorded graph.
        // Import it as undefined on the next attempt and preserve the last committed read generation.
        m_generationInitialized[m_pendingWriteGeneration] = false;
    }
    m_framePending = false;
    m_pendingReadGeneration = 0u;
    m_pendingWriteGeneration = 0u;
    m_pendingReadInitialization = false;
}

bool RtgiEmissiveTemporalPass::recordHistoryInitialize(RhiCommandList& commandList,
                                                       const RhiTextureViewHandle emissiveHistory,
                                                       const RhiTextureViewHandle normalViewZHistory,
                                                       const uint32_t width, const uint32_t height) const {
    if (!emissiveHistory.isValid() || !normalViewZHistory.isValid() || width == 0u || height == 0u) {
        return false;
    }

    std::array<RhiColorAttachment, 2u> attachments{};
    attachments[0].view = emissiveHistory;
    attachments[1].view = normalViewZHistory;
    for (RhiColorAttachment& attachment : attachments) {
        attachment.loadOp = RhiLoadOp::Clear;
        attachment.storeOp = RhiStoreOp::Store;
        attachment.clearColor[0] = 0.0f;
        attachment.clearColor[1] = 0.0f;
        attachment.clearColor[2] = 0.0f;
        attachment.clearColor[3] = 0.0f;
    }

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "RTGI.EmissiveTemporal.HistoryInitialize";
    renderingInfo.renderArea = {0u, 0u, width, height};
    renderingInfo.colorAttachments = attachments.data();
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(attachments.size());
    commandList.beginRendering(renderingInfo);
    commandList.endRendering();
    return true;
}

bool RtgiEmissiveTemporalPass::recordTemporal(RhiCommandList& commandList, const FrameContext& ctx,
                                              const Settings& settings, const bool historyInputEnabled,
                                              const uint32_t readGeneration, const uint32_t writeGeneration,
                                              const TemporalViews& views) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr || !std::isfinite(ctx.preExposure) ||
        ctx.preExposure <= 0.0f || !std::isfinite(ctx.previousPreExposure) || ctx.previousPreExposure <= 0.0f ||
        !ensurePipeline(*ctx.shared->rhiDevice)) {
        return false;
    }
    const TemporalExtent extent = ctx.temporalExtents.renderExtent;
    if (!ensureBindGroup(*ctx.shared->rhiDevice, views, extent.width, extent.height)) {
        return false;
    }

    const float preExposureRatio = ctx.preExposure / ctx.previousPreExposure;
    if (!std::isfinite(preExposureRatio) || preExposureRatio <= 0.0f) {
        return false;
    }

    RtgiEmissiveTemporalPushConstants pushConstants;
    pushConstants.renderExtentHistoryAndExposure =
        glm::vec4(static_cast<float>(extent.width), static_cast<float>(extent.height),
                  historyInputEnabled ? 1.0f : 0.0f, preExposureRatio);
    pushConstants.rejectionParameters =
        glm::vec4(settings.relativeDepthThreshold, settings.maximumViewZ, kNormalRejectionThreshold, 0.0f);

    commandList.setComputePipeline(m_pipeline);
    commandList.setBindGroup(0u, m_bindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch((extent.width + 7u) / 8u, (extent.height + 7u) / 8u, 1u);

    m_stats.dispatched = true;
    m_stats.historyInputEnabled = historyInputEnabled;
    m_stats.width = extent.width;
    m_stats.height = extent.height;
    m_stats.readGeneration = readGeneration;
    m_stats.writeGeneration = writeGeneration;
    m_stats.preExposureRatio = preExposureRatio;
    m_stats.relativeDepthThreshold = settings.relativeDepthThreshold;
    return true;
}

bool RtgiEmissiveTemporalPass::ensurePipeline(RhiDevice& rhiDevice) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyRhiResources();
    }
    if (m_pipeline.isValid()) {
        return true;
    }
    m_rhiDevice = &rhiDevice;

    const std::optional<std::string> source =
        renderer::rhi::loadShaderSource("assets/shaders/rtgi_emissive_temporal.comp");
    if (!source.has_value()) {
        destroyRhiResources();
        return false;
    }

    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "RTGI.EmissiveTemporal.Compute";
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
    layoutDesc.debugName = "RTGI.EmissiveTemporal.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 7u; ++binding) {
        layoutDesc.entries.push_back(
            {binding, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Compute), 1u});
    }
    for (uint32_t binding = 7u; binding < 9u; ++binding) {
        layoutDesc.entries.push_back({binding, RhiBindingType::StorageTexture, rhiFlag(RhiShaderStage::Compute), 1u});
    }
    m_bindGroupLayout = rhiDevice.createBindGroupLayout(layoutDesc);
    if (!m_bindGroupLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "RTGI.EmissiveTemporal.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(RtgiEmissiveTemporalPushConstants);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Compute);
    m_pipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_pipelineLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiComputePipelineDesc pipelineDesc;
    pipelineDesc.debugName = "RTGI.EmissiveTemporal.Pipeline";
    pipelineDesc.computeShader = m_shader;
    pipelineDesc.layout = m_pipelineLayout;
    m_pipeline = rhiDevice.createComputePipeline(pipelineDesc);
    if (!m_pipeline.isValid()) {
        destroyRhiResources();
        return false;
    }
    return true;
}

bool RtgiEmissiveTemporalPass::ensureHistoryResources(RhiDevice& rhiDevice, const uint32_t width,
                                                      const uint32_t height) {
    const bool matchingExtent = m_historyWidth == width && m_historyHeight == height;
    bool complete = matchingExtent;
    for (const HistoryGeneration& generation : m_historyGenerations) {
        complete = complete && generation.emissive.isValid() && generation.emissiveView.isValid() &&
                   generation.normalViewZ.isValid() && generation.normalViewZView.isValid();
    }
    if (complete) {
        return true;
    }

    destroyHistoryResources();
    m_rhiDevice = &rhiDevice;

    RhiTextureDesc textureDesc;
    textureDesc.dimension = RhiTextureDimension::Texture2D;
    textureDesc.width = width;
    textureDesc.height = height;
    textureDesc.depthOrLayers = 1u;
    textureDesc.mipLevels = 1u;
    textureDesc.sampleCount = 1u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::Storage) |
                        rhiFlag(RhiTextureUsage::ColorAttachment) | rhiFlag(RhiTextureUsage::TransferSrc);
    textureDesc.memoryCategory = RhiMemoryCategory::Nrd;

    const auto createTextureAndView = [&rhiDevice, &textureDesc](const char* debugName,
                                                                 const RhiTextureFormat format,
                                                                 RhiTextureHandle& texture,
                                                                 RhiTextureViewHandle& view) {
        textureDesc.debugName = debugName;
        textureDesc.format = format;
        texture = rhiDevice.createTexture(textureDesc, nullptr);
        if (!texture.isValid()) {
            return false;
        }
        RhiTextureViewDesc viewDesc;
        viewDesc.texture = texture;
        viewDesc.viewType = RhiTextureViewType::Texture2D;
        viewDesc.format = textureDesc.format;
        view = rhiDevice.createTextureView(viewDesc);
        return view.isValid();
    };

    for (uint32_t generation = 0u; generation < m_historyGenerations.size(); ++generation) {
        HistoryGeneration& history = m_historyGenerations[generation];
        if (!createTextureAndView(kEmissiveHistoryNames[generation], RhiTextureFormat::Rgba32Float,
                                  history.emissive, history.emissiveView) ||
            !createTextureAndView(kNormalViewZHistoryNames[generation], RhiTextureFormat::Rgba16Float,
                                  history.normalViewZ,
                                  history.normalViewZView)) {
            destroyHistoryResources();
            return false;
        }
    }

    m_historyWidth = width;
    m_historyHeight = height;
    m_generationInitialized = {};
    m_readGeneration = 0u;
    m_historyValid = false;
    return true;
}

bool RtgiEmissiveTemporalPass::ensureBindGroup(RhiDevice& rhiDevice, const TemporalViews& views,
                                               const uint32_t width, const uint32_t height) {
    const std::array<RhiTextureViewHandle, 9u> boundViews{
        views.currentEmissiveDirectRadiance, views.motion,          views.reprojectionCoverage,
        views.normalRoughness,                views.viewZ,           views.previousEmissive,
        views.previousNormalViewZ,            views.outputEmissive,  views.outputNormalViewZ,
    };
    const std::array<RhiTextureFormat, 9u> formats{
        RhiTextureFormat::Rgba16Float,  RhiTextureFormat::Rgba16Float, RhiTextureFormat::R8Unorm,
        RhiTextureFormat::Rgb10A2Unorm, RhiTextureFormat::R32Float,    RhiTextureFormat::Rgba32Float,
        RhiTextureFormat::Rgba16Float,  RhiTextureFormat::Rgba32Float, RhiTextureFormat::Rgba16Float,
    };
    for (uint32_t index = 0u; index < boundViews.size(); ++index) {
        const RhiTextureUsage usage = index < 7u ? RhiTextureUsage::Sampled : RhiTextureUsage::Storage;
        if (!textureViewMatches(rhiDevice, boundViews[index], formats[index], usage, width, height)) {
            return false;
        }
        for (uint32_t previous = 0u; previous < index; ++previous) {
            if (sameHandle(boundViews[index], boundViews[previous])) {
                return false;
            }
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
    for (uint32_t binding = 0u; binding < 7u; ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler = {boundViews[binding], m_sampler};
        bindGroupDesc.entries.push_back(entry);
    }
    for (uint32_t binding = 7u; binding < boundViews.size(); ++binding) {
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

void RtgiEmissiveTemporalPass::destroyHistoryResources() {
    if (m_rhiDevice != nullptr) {
        if (m_bindGroup.isValid()) {
            m_rhiDevice->destroyBindGroup(m_bindGroup);
            m_bindGroup = {};
        }
        for (HistoryGeneration& generation : m_historyGenerations) {
            if (generation.emissiveView.isValid()) {
                m_rhiDevice->destroyTextureView(generation.emissiveView);
            }
            if (generation.normalViewZView.isValid()) {
                m_rhiDevice->destroyTextureView(generation.normalViewZView);
            }
            if (generation.emissive.isValid()) {
                m_rhiDevice->destroyTexture(generation.emissive);
            }
            if (generation.normalViewZ.isValid()) {
                m_rhiDevice->destroyTexture(generation.normalViewZ);
            }
            generation = {};
        }
    }
    m_boundViews = {};
    m_generationInitialized = {};
    m_historyWidth = 0u;
    m_historyHeight = 0u;
    m_readGeneration = 0u;
    m_historyValid = false;
    m_framePending = false;
    m_pendingReadGeneration = 0u;
    m_pendingWriteGeneration = 0u;
    m_pendingReadInitialization = false;
}

void RtgiEmissiveTemporalPass::destroyRhiResources() {
    destroyHistoryResources();
    if (m_rhiDevice != nullptr) {
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
