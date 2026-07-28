#include "DebugPass.h"

#include "../core/RenderScene.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../shadow/ShadowRenderer.h"
#include "../targets/DeferredRenderTargets.h"
#include "../../resource/ResourceMgr.h"

#include <algorithm>
#include <cstddef>
#include <optional>

#include <glm/glm.hpp>

namespace {
constexpr size_t kDebugTextureCount = DebugPass::kTextureCount;

[[nodiscard]] bool sameTextureHandle(const RhiTextureHandle lhs, const RhiTextureHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] bool sameGraphTextureHandle(const RgTextureHandle lhs,
                                          const RgTextureHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] bool sameTextureView(const RhiTextureViewHandle lhs,
                                   const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] bool sameTextureViews(
    const std::array<RhiTextureViewHandle, kDebugTextureCount>& lhs,
    const std::array<RhiTextureViewHandle, kDebugTextureCount>& rhs) {
    for (size_t index = 0u; index < lhs.size(); ++index) {
        if (!sameTextureView(lhs[index], rhs[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool usesMaterialAux(const int mode) {
    return mode == 26 || mode == 27 || mode == 80;
}

[[nodiscard]] bool usesShadowNoise(const int mode) {
    return mode == 21 || mode == 22 || mode == 34 || mode == 35;
}

[[nodiscard]] bool usesReflectionHistory(const int mode) {
    return mode == 28;
}

[[nodiscard]] bool usesCloudHistory(const int mode) {
    return mode == 29;
}

[[nodiscard]] bool usesSceneComposite(const int mode) {
    return mode == 11 || mode == 78;
}

[[nodiscard]] bool usesSceneResolved(const int mode) {
    return mode == 31 || mode == 79;
}

struct alignas(16) DebugCascadeParams {
    glm::mat4 viewProj;
    glm::vec4 splitParams;
    glm::vec4 depthExtent;
};
static_assert(sizeof(DebugCascadeParams) == 96u);

struct alignas(16) DebugParams {
    glm::mat4 shadowModelView;
    glm::mat4 shadowProjection;
    glm::mat4 shadowProjectionInverse;
    glm::mat4 invViewProj;
    std::array<DebugCascadeParams, shadow::ShadowRenderer::CASCADE_COUNT> cascades;
    glm::vec4 cameraNear;
    glm::vec4 sunDirectionFar;
    glm::vec4 moonDirectionShadowExtent;
    glm::vec4 shadowDirectionTexelSize;
    glm::vec4 sunLightColorShadowMapSize;
    glm::vec4 skyAmbientColorShadowDistance;
    glm::vec4 horizonColorConstantBias;
    glm::vec4 fogColorSlopeBias;
    glm::vec4 shadowParams;
    glm::ivec4 flags0;
    glm::ivec4 flags1;
};
static_assert(sizeof(DebugParams) == 816u);
} // namespace

void DebugPass::init(ResourceMgr& resourceMgr) {
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
}

void DebugPass::shutdown() {
    destroyRhiResources();
    m_noiseTexture = {};
    m_shadowRenderer = nullptr;
}

RgPassHandle DebugPass::addGraphPass(
    RenderGraph& graph,
    const FrameContext& ctx,
    const RenderSettings& settings,
    DeferredRenderTargets& targets,
    const GraphResources& resources,
    const RgPassHandle dependency) {
    if (!dependency.isValid() || !resources.output.isValid() ||
        m_shadowRenderer == nullptr) {
        return {};
    }
    for (const RgTextureHandle texture : resources.textures) {
        if (!texture.isValid()) {
            return {};
        }
    }

    RenderGraphPassBuilder debug = graph.addPass(
        {"Deferred.DebugView", RgPassType::Graphics,
         RhiQueueType::Graphics});
    debug.dependsOn(dependency);

    // Shader bindings may intentionally reference one graph texture more than once.
    // Render Graph access declarations must contain one non-overlapping read per texture.
    struct DeclaredTexture {
        RgTextureHandle handle;
        bool depthRead = false;
    };
    std::array<DeclaredTexture, kDebugTextureCount> declaredTextures{};
    std::size_t declaredTextureCount = 0u;
    for (std::size_t index = 0u; index < resources.textures.size(); ++index) {
        const bool depthTexture = index == 4u || index == 5u || index == 9u ||
                                  index == 16u ||
                                  (index == 13u && settings.debug.viewMode == 19);
        const RgTextureHandle texture = resources.textures[index];
        auto existing = std::find_if(
            declaredTextures.begin(),
            declaredTextures.begin() + static_cast<std::ptrdiff_t>(declaredTextureCount),
            [texture](const DeclaredTexture& declared) {
                return sameGraphTextureHandle(declared.handle, texture);
            });
        if (existing !=
            declaredTextures.begin() + static_cast<std::ptrdiff_t>(declaredTextureCount)) {
            existing->depthRead = existing->depthRead || depthTexture;
            continue;
        }
        declaredTextures[declaredTextureCount++] = {texture, depthTexture};
    }
    for (std::size_t index = 0u; index < declaredTextureCount; ++index) {
        debug.readTexture(
            declaredTextures[index].handle,
            declaredTextures[index].depthRead ? RhiResourceState::DepthRead
                                              : RhiResourceState::ShaderRead);
    }
    debug.writeTexture(resources.output, RhiResourceState::RenderTarget)
        .setExecute([this, frame = &ctx, frameSettings = settings,
                     frameTargets = &targets](RgPassContext& pass) {
            return recordGraphPass(
                *frame, frameSettings, *frameTargets,
                static_cast<int>(frame->temporalExtents.renderExtent.width),
                static_cast<int>(frame->temporalExtents.renderExtent.height),
                pass.commandList());
        });
    return debug.handle();
}

bool DebugPass::recordGraphPass(const FrameContext& ctx,
                                const RenderSettings& settings,
                                DeferredRenderTargets& targets,
                                const int width,
                                const int height,
                                RhiCommandList& commandList) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        !ctx.sceneCaptureColorView.isValid() || m_shadowRenderer == nullptr) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const int debugViewMode = settings.debug.viewMode;
    if (!ensureRhiPipeline(rhiDevice) ||
        !targets.ensureGBufferTextureViews(rhiDevice) ||
        !targets.ensureVolumetricFogTextureViews(rhiDevice) ||
        !targets.ensureSsaoFilteredTextureView(rhiDevice) ||
        !targets.ensureSceneLightingTextureView(rhiDevice) ||
        !targets.ensureSceneCompositeTextureView(rhiDevice) ||
        !targets.ensureSceneResolvedTextureView(rhiDevice) ||
        !targets.ensureTransparentCompositeTextureViews(rhiDevice) ||
        !targets.ensureHalfResTextureView(rhiDevice) ||
        !targets.ensureSkyCaptureTextureView(rhiDevice) ||
        !targets.ensureVelocityTextureView(rhiDevice) ||
        !targets.ensureHistorySceneTextureViews(rhiDevice) ||
        !targets.ensureHistoryDepthTextureViews(rhiDevice) ||
        !targets.ensureReflectionTextureView(rhiDevice) ||
        !targets.ensureHistoryReflectionTextureViews(rhiDevice) ||
        !targets.ensureCloudTextureView(rhiDevice) ||
        !targets.ensureHistoryCloudTextureViews(rhiDevice) ||
        !targets.ensureTemporalCurrentTextureView(rhiDevice) ||
        !targets.ensureSsgiTextureView(rhiDevice) ||
        !targets.ensureReactiveMaskTextureView(rhiDevice) ||
        !targets.ensureTransparencyMaskTextureView(rhiDevice) ||
        !ensureNoiseTextureView(rhiDevice)) {
        return false;
    }
    RhiTextureViewHandle binding13 = targets.historySceneTexturePrevViewHandle();
    if (usesShadowNoise(debugViewMode)) {
        binding13 = m_noiseTextureView;
    } else if (usesMaterialAux(debugViewMode)) {
        binding13 = targets.materialAuxTextureViewHandle();
    } else if (debugViewMode == 19) {
        binding13 = targets.historyDepthTexturePrevViewHandle();
    }

    RhiTextureViewHandle binding14 = targets.reflectionTextureViewHandle();
    if (usesReflectionHistory(debugViewMode)) {
        binding14 = targets.historyReflectionTexturePrevViewHandle();
    }

    RhiTextureViewHandle binding15 = targets.cloudTextureViewHandle();
    if (usesSceneResolved(debugViewMode)) {
        binding15 = targets.currentSceneColorTextureViewHandle();
    } else if (usesSceneComposite(debugViewMode)) {
        binding15 = targets.sceneCompositeTextureViewHandle();
    } else if (usesCloudHistory(debugViewMode)) {
        binding15 = targets.historyCloudTexturePrevViewHandle();
    }

    const std::array<RhiTextureViewHandle, kDebugTextureCount> views = {
        targets.albedoTextureViewHandle(),
        targets.normalAoTextureViewHandle(),
        targets.voxelLightTextureViewHandle(),
        targets.materialTextureViewHandle(),
        targets.depthTextureViewHandle(),
        targets.csmShadowDepthArrayTextureViewHandle(),
        targets.ssaoFilteredTextureViewHandle(),
        targets.sceneLightingTextureViewHandle(),
        targets.transparentCompositeTextureViewHandle(),
        targets.transparentCompositeDepthTextureViewHandle(),
        targets.halfResTextureViewHandle(),
        targets.skyCaptureTextureViewHandle(),
        targets.velocityTextureViewHandle(),
        binding13,
        binding14,
        binding15,
        targets.csmShadowDepthArrayTextureViewHandle(),
        targets.temporalCurrentTextureViewHandle(),
        targets.ssgiTextureViewHandle(),
        targets.csmShadowColor0ArrayTextureViewHandle(),
        targets.csmShadowColor1ArrayTextureViewHandle(),
        targets.reactiveMaskTextureViewHandle(),
        targets.transparencyMaskTextureViewHandle()
    };
    if (!ensureRhiBindGroup(rhiDevice, debugViewMode, views)) {
        return false;
    }

    DebugParams params{};
    params.shadowModelView = m_shadowRenderer->modelView();
    params.shadowProjection = m_shadowRenderer->projection();
    params.shadowProjectionInverse = m_shadowRenderer->projectionInverse();
    params.invViewProj = ctx.camera.invViewProj;
    for (int index = 0; index < shadow::ShadowRenderer::CASCADE_COUNT; ++index) {
        const shadow::ShadowRenderer::Cascade& cascade = m_shadowRenderer->cascade(index);
        DebugCascadeParams& cascadeParams = params.cascades[static_cast<size_t>(index)];
        cascadeParams.viewProj = cascade.viewProj;
        cascadeParams.splitParams = glm::vec4(cascade.splitNear,
                                               cascade.splitFar,
                                               cascade.texelWorldSize,
                                               index >= 2 ? 0.5f : 1.0f);
        cascadeParams.depthExtent = glm::vec4(cascade.depthExtent, 0.0f, 0.0f, 0.0f);
    }
    params.cameraNear = glm::vec4(ctx.camera.position, ctx.camera.nearPlane);
    params.sunDirectionFar = glm::vec4(ctx.skyColors.sunDirection, ctx.camera.farPlane);
    params.moonDirectionShadowExtent = glm::vec4(ctx.skyColors.moonDirection,
                                                  m_shadowRenderer->shadowExtent());
    params.shadowDirectionTexelSize = glm::vec4(m_shadowRenderer->lightDirection(),
                                                 m_shadowRenderer->texelWorldSize());
    params.sunLightColorShadowMapSize = glm::vec4(
        ctx.skyColors.sunLightColor,
        static_cast<float>(settings.shadow.resolution));
    params.skyAmbientColorShadowDistance = glm::vec4(
        ctx.skyColors.skyAmbientColor,
        std::max(64.0f, settings.shadow.distance));
    params.horizonColorConstantBias = glm::vec4(ctx.skyColors.horizonScatterColor,
                                                settings.shadow.constantBias);
    params.fogColorSlopeBias = glm::vec4(ctx.fog.color, settings.shadow.slopeBias);
    params.shadowParams = glm::vec4(settings.shadow.normalOffset, 0.0f, 0.0f, 0.0f);
    params.flags0 = glm::ivec4(ctx.moonShadowActive ? 1 : 0,
                               shadow::ShadowRenderer::CASCADE_COUNT,
                               debugViewMode,
                               static_cast<int>(ctx.frameIndex & 0x7fffffffULL));
    params.flags1 = glm::ivec4(settings.volumetric.freezeBias ? 1 : 0, 0, 0, 0);

    RhiColorAttachment colorAttachment;
    colorAttachment.view = ctx.sceneCaptureColorView;
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "Debug.SceneCapture";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, width)),
        static_cast<uint32_t>(std::max(1, height))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

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
    return true;
}

bool DebugPass::ensureRhiPipeline(RhiDevice& rhiDevice) {
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
        renderer::rhi::loadShaderSource("assets/shaders/deferred_debug.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "Debug.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_vertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "Debug.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_vertexShader.isValid() || !m_fragmentShader.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiBufferDesc uniformBufferDesc;
    uniformBufferDesc.debugName = "Debug.Params";
    uniformBufferDesc.size = sizeof(DebugParams);
    uniformBufferDesc.usage = rhiFlag(RhiBufferUsage::Uniform) |
                              rhiFlag(RhiBufferUsage::TransferDst);
    uniformBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    uniformBufferDesc.initialState = RhiResourceState::UniformBuffer;
    uniformBufferDesc.memoryCategory = RhiMemoryCategory::Uniform;
    m_uniformBuffer = rhiDevice.createBuffer(uniformBufferDesc, nullptr, 0u);
    if (!m_uniformBuffer.isValid()) {
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
    m_nearestSampler = rhiDevice.createSampler(samplerDesc);

    samplerDesc.minFilter = RhiFilter::Linear;
    samplerDesc.magFilter = RhiFilter::Linear;
    m_linearSampler = rhiDevice.createSampler(samplerDesc);

    samplerDesc.addressU = RhiAddressMode::Repeat;
    samplerDesc.addressV = RhiAddressMode::Repeat;
    samplerDesc.addressW = RhiAddressMode::Repeat;
    m_noiseSampler = rhiDevice.createSampler(samplerDesc);
    if (!m_nearestSampler.isValid() || !m_linearSampler.isValid() ||
        !m_noiseSampler.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "Debug.BindGroupLayout";
    for (uint32_t binding = 0u; binding < kDebugTextureCount; ++binding) {
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    bindGroupLayoutDesc.entries.push_back({
        static_cast<uint32_t>(kDebugTextureCount),
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
    pipelineLayoutDesc.debugName = "Debug.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    m_pipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_pipelineLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "Debug.Pipeline";
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

bool DebugPass::ensureNoiseTextureView(RhiDevice& rhiDevice) {
    if (m_noiseTextureView.isValid() &&
        sameTextureHandle(m_noiseViewTexture, m_noiseTexture)) {
        return true;
    }
    if (m_noiseTextureView.isValid()) {
        destroyRhiBindGroup();
        rhiDevice.destroyTextureView(m_noiseTextureView);
        m_noiseTextureView = {};
        m_noiseViewTexture = {};
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
    m_noiseViewTexture = m_noiseTexture;
    return true;
}

bool DebugPass::ensureRhiBindGroup(
    RhiDevice& rhiDevice,
    const int debugViewMode,
    const std::array<RhiTextureViewHandle, kDebugTextureCount>& views) {
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_bindGroup.isValid() && m_boundDebugViewMode == debugViewMode &&
        sameTextureViews(m_boundViews, views)) {
        return true;
    }
    destroyRhiBindGroup();

    std::array<RhiSamplerHandle, kDebugTextureCount> samplers;
    samplers.fill(m_linearSampler);
    const size_t nearestBindings[] = {
        0u, 1u, 2u, 3u, 4u, 5u, 9u, 12u, 16u, 19u, 20u, 21u, 22u
    };
    for (const size_t binding : nearestBindings) {
        samplers[binding] = m_nearestSampler;
    }
    samplers[13] = usesShadowNoise(debugViewMode)
        ? m_noiseSampler
        : ((usesMaterialAux(debugViewMode) || debugViewMode == 19)
               ? m_nearestSampler
               : m_linearSampler);
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_bindGroupLayout;
    for (uint32_t binding = 0u; binding < kDebugTextureCount; ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler = samplers[binding];
        bindGroupDesc.entries.push_back(entry);
    }

    RhiBindGroupEntry uniformEntry;
    uniformEntry.binding = static_cast<uint32_t>(kDebugTextureCount);
    uniformEntry.resource.buffer.buffer = m_uniformBuffer;
    uniformEntry.resource.buffer.offset = 0u;
    uniformEntry.resource.buffer.range = sizeof(DebugParams);
    bindGroupDesc.entries.push_back(uniformEntry);

    m_bindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_bindGroup.isValid()) {
        return false;
    }
    m_boundViews = views;
    m_boundDebugViewMode = debugViewMode;
    return true;
}

void DebugPass::destroyRhiBindGroup() {
    if (m_rhiDevice != nullptr && m_bindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_bindGroup);
    }
    m_bindGroup = {};
    m_boundViews = {};
    m_boundDebugViewMode = -1;
}

void DebugPass::destroyRhiResources() {
    destroyRhiBindGroup();
    if (m_rhiDevice != nullptr) {
        if (m_noiseTextureView.isValid()) {
            m_rhiDevice->destroyTextureView(m_noiseTextureView);
        }
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
        if (m_nearestSampler.isValid()) {
            m_rhiDevice->destroySampler(m_nearestSampler);
        }
        if (m_linearSampler.isValid()) {
            m_rhiDevice->destroySampler(m_linearSampler);
        }
        if (m_noiseSampler.isValid()) {
            m_rhiDevice->destroySampler(m_noiseSampler);
        }
    }

    m_noiseViewTexture = {};
    m_noiseTextureView = {};
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
