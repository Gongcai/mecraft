#include "DeferredLightingPass.h"

#include "ClusteredLightingPass.h"
#include "../core/RenderScene.h"
#include "../debug/RenderDebugService.h"
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
constexpr size_t kLightingTextureCount = 20u;

[[nodiscard]] bool sameTextureHandle(const RhiTextureHandle lhs, const RhiTextureHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] bool sameTextureView(const RhiTextureViewHandle lhs,
                                   const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] bool sameTextureViews(
    const std::array<RhiTextureViewHandle, kLightingTextureCount>& lhs,
    const std::array<RhiTextureViewHandle, kLightingTextureCount>& rhs) {
    for (size_t index = 0u; index < lhs.size(); ++index) {
        if (!sameTextureView(lhs[index], rhs[index])) {
            return false;
        }
    }
    return true;
}

struct alignas(16) DeferredLightingCascadeParams {
    glm::mat4 viewProj;
    glm::vec4 splitParams;
    glm::vec4 depthExtent;
};
static_assert(sizeof(DeferredLightingCascadeParams) == 96u);

struct alignas(16) DeferredLightingParams {
    glm::mat4 viewProj;
    glm::mat4 invViewProj;
    glm::mat4 projection;
    glm::mat4 shadowViewProj;
    glm::mat4 shadowModelView;
    glm::mat4 shadowProjection;
    glm::mat4 shadowProjectionInverse;
    std::array<DeferredLightingCascadeParams, shadow::ShadowRenderer::CASCADE_COUNT> cascades;
    glm::vec4 cameraSkyIntensity;
    glm::vec4 sunDirectionMoonVisibility;
    glm::vec4 moonDirection;
    glm::vec4 shadowDirectionDistance;
    glm::vec4 sunLightShadowExtent;
    glm::vec4 moonLightShadowTexelSize;
    glm::vec4 skyAmbientShadowSoftness;
    glm::vec4 shadowTintPcssStrength;
    glm::vec4 horizonColorConstantBias;
    glm::vec4 cloudWeatherSlopeBias;
    glm::vec4 fogColorNormalOffset;
    glm::vec4 lighting0;
    glm::vec4 lighting1;
    glm::vec4 lighting2;
    glm::vec4 atmosphere0;
    glm::vec4 weather0;
    glm::vec4 weather1;
    glm::vec4 weather2;
    glm::vec4 cloud0;
    glm::vec4 cloud1;
    glm::vec4 cloud2;
    glm::vec4 fogParams;
    glm::ivec4 flags0;
    glm::ivec4 flags1;
    glm::ivec4 flags2;
    glm::ivec4 flags3;
    glm::ivec4 flags4;
    glm::ivec4 flags5;
    glm::uvec4 clusterGrid;
    glm::vec4 clusterDepth;
    glm::uvec4 clusterRenderExtent;
};
static_assert(sizeof(DeferredLightingParams) == 1328u);
} // namespace

void DeferredLightingPass::init(ResourceMgr& resourceMgr) {
    m_lightmapDayTexture = resourceMgr.getLightmapDay();
    m_lightmapNightTexture = resourceMgr.getLightmapNight();
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
    m_rippleNormalTexture = resourceMgr.getTexture2DHandle("shader_ripple_normal");
}

void DeferredLightingPass::shutdown() {
    destroyRhiResources();
    m_shadowRenderer = nullptr;
    m_clusteredLightingPass = nullptr;
    m_lightmapDayTexture = {};
    m_lightmapNightTexture = {};
    m_noiseTexture = {};
    m_rippleNormalTexture = {};
}

bool DeferredLightingPass::execute(RhiCommandList& commandList,
                                   const FrameContext& ctx,
                                   const RenderSettings& settings,
                                   DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        m_shadowRenderer == nullptr) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const bool clusteredLightingActive =
        rhiDevice.backend() == RhiBackend::Vulkan;
    if (clusteredLightingActive &&
        (m_clusteredLightingPass == nullptr ||
         !m_clusteredLightingPass->consumerBindGroupLayout().isValid() ||
         !m_clusteredLightingPass->consumerBindGroup().isValid())) {
        return false;
    }
    const bool useTemporalSsao =
        settings.ssao.enabled && settings.ssao.temporalEnabled &&
        !requiresTemporalReset(ctx.temporalResetReasons);
    if (!ensureRhiPipeline(rhiDevice) ||
        !targets.ensureSceneLightingTextureView(rhiDevice) ||
        !targets.ensureGBufferTextureViews(rhiDevice) ||
        !targets.ensureVolumetricFogTextureViews(rhiDevice) ||
        !(useTemporalSsao
              ? targets.ensureSsaoTemporalTextureView(rhiDevice)
              : targets.ensureSsaoFilteredTextureView(rhiDevice)) ||
        !targets.ensureSkyCaptureTextureView(rhiDevice) ||
        !ensureExternalTextureViews(rhiDevice)) {
        return false;
    }

    const std::array<RhiTextureViewHandle, kLightingTextureCount> views = {
        targets.albedoTextureViewHandle(),
        targets.normalAoTextureViewHandle(),
        targets.voxelLightTextureViewHandle(),
        targets.materialTextureViewHandle(),
        targets.materialAuxTextureViewHandle(),
        targets.depthTextureViewHandle(),
        m_lightmapDayTextureView,
        m_lightmapNightTextureView,
        useTemporalSsao
            ? targets.ssaoTemporalTextureViewHandle()
            : targets.ssaoFilteredTextureViewHandle(),
        targets.skyCaptureTextureViewHandle(),
        m_noiseTextureView,
        targets.atmosphereLutTextureViewHandle(),
        targets.csmShadowDepthComparisonArrayTextureViewHandle(),
        targets.csmShadowDepthArrayTextureViewHandle(),
        targets.csmShadowDepthAllComparisonArrayTextureViewHandle(),
        targets.csmShadowDepthAllArrayTextureViewHandle(),
        targets.csmShadowColor0ArrayTextureViewHandle(),
        targets.csmShadowColor1ArrayTextureViewHandle(),
        m_rippleNormalTextureView,
        targets.f0MetallicTextureViewHandle()
    };
    if (!ensureRhiBindGroup(rhiDevice, views)) {
        return false;
    }

    const bool volumetricFogActive =
        (ctx.volumetric.lightEnabled ||
         (ctx.volumetric.fogEnabled && ctx.volumetric.fogDensityScale > 0.001f)) &&
        settings.volumetric.fogEnabled;
    DeferredLightingParams params{};
    const bool projectionJitter = usesTemporalProjectionJitter(
        settings.upscale.type, settings.taa.enabled);
    params.viewProj = projectionJitter
        ? ctx.camera.jitteredViewProj
        : ctx.camera.viewProj;
    params.invViewProj = projectionJitter
        ? ctx.camera.jitteredInvViewProj
        : ctx.camera.invViewProj;
    params.projection = ctx.camera.projection;
    params.shadowViewProj = m_shadowRenderer->viewProj();
    params.shadowModelView = m_shadowRenderer->modelView();
    params.shadowProjection = m_shadowRenderer->projection();
    params.shadowProjectionInverse = m_shadowRenderer->projectionInverse();
    for (int index = 0; index < shadow::ShadowRenderer::CASCADE_COUNT; ++index) {
        const shadow::ShadowRenderer::Cascade& cascade = m_shadowRenderer->cascade(index);
        DeferredLightingCascadeParams& cascadeParams =
            params.cascades[static_cast<size_t>(index)];
        cascadeParams.viewProj = cascade.viewProj;
        cascadeParams.splitParams = glm::vec4(cascade.splitNear,
                                               cascade.splitFar,
                                               cascade.texelWorldSize,
                                               index >= 2 ? 0.5f : 1.0f);
        cascadeParams.depthExtent = glm::vec4(cascade.depthExtent, 0.0f, 0.0f, 0.0f);
    }
    params.cameraSkyIntensity = glm::vec4(ctx.camera.position, ctx.skyIntensity);
    params.sunDirectionMoonVisibility = glm::vec4(ctx.skyColors.sunDirection,
                                                  ctx.skyColors.moonVisibility);
    params.moonDirection = glm::vec4(ctx.skyColors.moonDirection, 0.0f);
    params.shadowDirectionDistance = glm::vec4(
        m_shadowRenderer->lightDirection(),
        std::max(64.0f, m_shadowRenderer->shadowDistance()));
    params.sunLightShadowExtent = glm::vec4(ctx.skyColors.sunLightColor,
                                            m_shadowRenderer->shadowExtent());
    params.moonLightShadowTexelSize = glm::vec4(ctx.skyColors.moonLightColor,
                                                m_shadowRenderer->texelWorldSize());
    params.skyAmbientShadowSoftness = glm::vec4(ctx.skyColors.skyAmbientColor,
                                                settings.shadow.softness);
    params.shadowTintPcssStrength = glm::vec4(ctx.skyColors.shadowTintColor,
                                              settings.shadow.pcssStrength);
    params.horizonColorConstantBias = glm::vec4(ctx.skyColors.horizonScatterColor,
                                                settings.shadow.constantBias);
    params.cloudWeatherSlopeBias = glm::vec4(ctx.skyIlluminance.cloudDynamicWeather,
                                             settings.shadow.slopeBias);
    params.fogColorNormalOffset = glm::vec4(ctx.fog.color, settings.shadow.normalOffset);
    params.lighting0 = glm::vec4(settings.postProcess.shadowTintStrength,
                                 settings.postProcess.directSunStrength,
                                 settings.postProcess.skyAmbientStrength,
                                 settings.weather.skylightScale);
    params.lighting1 = glm::vec4(settings.postProcess.minimumAmbient,
                                 settings.postProcess.shadowMinLight,
                                 settings.postProcess.shadowContrast,
                                 settings.postProcess.blockLightStrength);
    params.lighting2 = glm::vec4(settings.postProcess.fakeBounceStrength,
                                 settings.postProcess.albedoDesaturation,
                                 settings.postProcess.shadowDesaturation,
                                 settings.shadow.contactShadowStrength);
    params.atmosphere0 = glm::vec4(ctx.atmosphere.sunWarmth,
                                   ctx.atmosphere.skyCoolness,
                                   ctx.atmosphere.aerialStrength,
                                   ctx.atmosphere.horizonScatterStrength);
    params.weather0 = glm::vec4(ctx.weather.wetness,
                                ctx.weather.storm,
                                ctx.weather.aerialReduction,
                                ctx.weather.lightningFlash);
    params.weather1 = glm::vec4(ctx.weather.surfaceWetness,
                                ctx.weather.skyWetness,
                                ctx.weather.fogWetness,
                                ctx.weather.cloudWetness);
    params.weather2 = glm::vec4(ctx.atmosphere.directWeatherOcclusion,
                                ctx.weather.precipitation,
                                ctx.shaderTime,
                                ctx.cloud.timeScale);
    params.cloud0 = glm::vec4(ctx.cloud.shadowStrength,
                              ctx.cloud.shadowScale,
                              ctx.cloud.shadowSpeed,
                              ctx.cloud.coverage);
    params.cloud1 = glm::vec4(ctx.cloud.density,
                              ctx.cloud.height,
                              ctx.cloud.thickness,
                              ctx.cloud.planarCoverage);
    params.cloud2 = glm::vec4(ctx.cloud.planarDensity,
                              ctx.cloud.planarAltitude,
                              ctx.fog.startDistance,
                              ctx.fog.endDistance);
    params.fogParams = glm::vec4(ctx.fog.density, 0.0f, 0.0f, 0.0f);
    params.flags0 = glm::ivec4(1,
                               settings.postProcess.aerialPerspectiveEnabled ? 1 : 0,
                               volumetricFogActive ? 1 : 0,
                               ctx.volumetric.lightEnabled ? 1 : 0);
    params.flags1 = glm::ivec4(ctx.atmosphere.directWeatherOcclusionOverride,
                               settings.shadow.enabled ? 1 : 0,
                               settings.shadow.softShadowsEnabled ? 1 : 0,
                               settings.shadow.pcssShadowsEnabled ? 1 : 0);
    params.flags2 = glm::ivec4(settings.shadow.contactShadowsEnabled ? 1 : 0,
                               ctx.cloud.shadowsEnabled ? 1 : 0,
                               ctx.moonShadowActive ? 1 : 0,
                               settings.ssao.enabled ? 1 : 0);
    params.flags3 = glm::ivec4(ctx.eyeInWater ? 1 : 0,
                               m_heldBlockLightValue,
                               0,
                               ctx.fog.enabled ? 1 : 0);
    params.flags4 = glm::ivec4(0,
                               settings.debug.deferredLightDebugMode,
                               settings.debug.derivativeStrictMode ? 1 : 0,
                               settings.weather.rainLinesEnabled ? 1 : 0);
    params.flags5 = glm::ivec4(settings.weather.surfaceRipplesEnabled ? 1 : 0,
                               shadow::ShadowRenderer::CASCADE_COUNT,
                               0,
                               0);
    if (clusteredLightingActive) {
        const renderer::contracts::ClusterGrid& clusterGrid =
            m_clusteredLightingPass->grid();
        params.clusterGrid = {
            clusterGrid.tileCountX, clusterGrid.tileCountY,
            clusterGrid.depthSliceCount,
            renderer::contracts::kClusterTileWidth};
        params.clusterDepth = {
            clusterGrid.nearPlane, clusterGrid.farPlane,
            clusterGrid.depthLogScale, clusterGrid.depthLogBias};
        params.clusterRenderExtent = {
            clusterGrid.renderWidth, clusterGrid.renderHeight, 0u, 0u};
    }

    const bool clearForDebug = settings.debug.deferredLightDebugMode > 0;
    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.sceneLightingTextureViewHandle();
    colorAttachment.loadOp = clearForDebug ? RhiLoadOp::Clear : RhiLoadOp::Load;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "DeferredLighting";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Lighting)
        : GpuTimerSegmentToken{};
    commandList.bufferBarrier({m_uniformBuffer, RhiResourceState::UniformBuffer,
                               RhiResourceState::TransferDst});
    commandList.updateBuffer(m_uniformBuffer, 0u, &params, sizeof(params));
    commandList.bufferBarrier({m_uniformBuffer, RhiResourceState::TransferDst,
                               RhiResourceState::UniformBuffer});
    commandList.beginRendering(renderingInfo);
    commandList.setGraphicsPipeline(m_pipeline);
    commandList.setBindGroup(0u, m_bindGroup);
    if (clusteredLightingActive) {
        commandList.setBindGroup(
            1u, m_clusteredLightingPass->consumerBindGroup());
    }
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool DeferredLightingPass::ensureRhiPipeline(RhiDevice& rhiDevice) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyRhiResources();
    }
    if (m_pipeline.isValid()) {
        return true;
    }
    m_rhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/deferred_lighting.vert");
    renderer::rhi::RhiShaderSourceOptions fragmentOptions;
    if (rhiDevice.backend() == RhiBackend::Vulkan) {
        fragmentOptions.preprocessorDefinitions.emplace_back(
            "MECRAFT_CLUSTERED_LIGHTING");
    }
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource(
            "assets/shaders/deferred_lighting.frag", fragmentOptions);
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "DeferredLighting.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_vertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "DeferredLighting.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_vertexShader.isValid() || !m_fragmentShader.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiBufferDesc uniformBufferDesc;
    uniformBufferDesc.debugName = "DeferredLighting.Params";
    uniformBufferDesc.size = sizeof(DeferredLightingParams);
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

    auto createSampler = [&](const RhiFilter filter,
                             const RhiAddressMode addressMode,
                             const RhiBorderColor borderColor,
                             const bool compareEnabled) {
        RhiSamplerDesc samplerDesc;
        samplerDesc.minFilter = filter;
        samplerDesc.magFilter = filter;
        samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
        samplerDesc.addressU = addressMode;
        samplerDesc.addressV = addressMode;
        samplerDesc.addressW = addressMode;
        samplerDesc.borderColor = borderColor;
        samplerDesc.compareEnabled = compareEnabled;
        samplerDesc.compareOp = RhiCompareOp::LessOrEqual;
        return rhiDevice.createSampler(samplerDesc);
    };
    m_nearestClampSampler = createSampler(RhiFilter::Nearest,
                                           RhiAddressMode::ClampToEdge,
                                           RhiBorderColor::TransparentBlack,
                                           false);
    m_linearClampSampler = createSampler(RhiFilter::Linear,
                                          RhiAddressMode::ClampToEdge,
                                          RhiBorderColor::TransparentBlack,
                                          false);
    m_linearRepeatSampler = createSampler(RhiFilter::Linear,
                                           RhiAddressMode::Repeat,
                                           RhiBorderColor::TransparentBlack,
                                           false);
    m_nearestBorderSampler = createSampler(RhiFilter::Nearest,
                                            RhiAddressMode::ClampToBorder,
                                            RhiBorderColor::OpaqueWhite,
                                            false);
    m_compareBorderSampler = createSampler(RhiFilter::Linear,
                                            RhiAddressMode::ClampToBorder,
                                            RhiBorderColor::OpaqueWhite,
                                            true);
    if (!m_nearestClampSampler.isValid() || !m_linearClampSampler.isValid() ||
        !m_linearRepeatSampler.isValid() || !m_nearestBorderSampler.isValid() ||
        !m_compareBorderSampler.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "DeferredLighting.BindGroupLayout";
    for (uint32_t binding = 0u; binding < kLightingTextureCount; ++binding) {
        const RhiShaderStageFlags visibility = binding == 9u
            ? rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment)
            : rhiFlag(RhiShaderStage::Fragment);
        bindGroupLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            visibility,
            1u
        });
    }
    bindGroupLayoutDesc.entries.push_back({
        static_cast<uint32_t>(kLightingTextureCount),
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
    pipelineLayoutDesc.debugName = "DeferredLighting.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    if (rhiDevice.backend() == RhiBackend::Vulkan) {
        if (m_clusteredLightingPass == nullptr ||
            !m_clusteredLightingPass->consumerBindGroupLayout().isValid()) {
            destroyRhiResources();
            return false;
        }
        pipelineLayoutDesc.bindGroupLayouts.push_back(
            m_clusteredLightingPass->consumerBindGroupLayout());
    }
    m_pipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_pipelineLayout.isValid()) {
        destroyRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "DeferredLighting.Pipeline";
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

bool DeferredLightingPass::ensureExternalTextureViews(RhiDevice& rhiDevice) {
    return ensureTextureView(rhiDevice,
                             m_lightmapDayTexture,
                             RhiTextureFormat::Rgba8Unorm,
                             m_lightmapDayViewTexture,
                             m_lightmapDayTextureView) &&
           ensureTextureView(rhiDevice,
                             m_lightmapNightTexture,
                             RhiTextureFormat::Rgba8Unorm,
                             m_lightmapNightViewTexture,
                             m_lightmapNightTextureView) &&
           ensureTextureView(rhiDevice,
                             m_noiseTexture,
                             RhiTextureFormat::Rgba8Unorm,
                             m_noiseViewTexture,
                             m_noiseTextureView) &&
           ensureTextureView(rhiDevice,
                             m_rippleNormalTexture,
                             RhiTextureFormat::Rgba8Unorm,
                             m_rippleNormalViewTexture,
                             m_rippleNormalTextureView);
}

bool DeferredLightingPass::ensureTextureView(RhiDevice& rhiDevice,
                                             const RhiTextureHandle texture,
                                             const RhiTextureFormat format,
                                             RhiTextureHandle& viewTexture,
                                             RhiTextureViewHandle& textureView) {
    if (textureView.isValid() && sameTextureHandle(viewTexture, texture)) {
        return true;
    }
    if (textureView.isValid()) {
        destroyRhiBindGroup();
        rhiDevice.destroyTextureView(textureView);
        textureView = {};
        viewTexture = {};
    }
    if (!texture.isValid()) {
        return false;
    }

    RhiTextureViewDesc viewDesc;
    viewDesc.texture = texture;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = format;
    viewDesc.baseMip = 0u;
    viewDesc.mipCount = 1u;
    viewDesc.baseLayer = 0u;
    viewDesc.layerCount = 1u;
    textureView = rhiDevice.createTextureView(viewDesc);
    if (!textureView.isValid()) {
        return false;
    }
    viewTexture = texture;
    return true;
}

bool DeferredLightingPass::ensureRhiBindGroup(
    RhiDevice& rhiDevice,
    const std::array<RhiTextureViewHandle, 20>& views) {
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_bindGroup.isValid() && sameTextureViews(m_boundViews, views)) {
        return true;
    }
    destroyRhiBindGroup();

    const RhiSamplerHandle samplers[kLightingTextureCount] = {
        m_nearestClampSampler,
        m_nearestClampSampler,
        m_nearestClampSampler,
        m_nearestClampSampler,
        m_nearestClampSampler,
        m_nearestClampSampler,
        m_linearClampSampler,
        m_linearClampSampler,
        m_linearClampSampler,
        m_linearClampSampler,
        m_linearRepeatSampler,
        m_linearClampSampler,
        m_compareBorderSampler,
        m_nearestBorderSampler,
        m_compareBorderSampler,
        m_nearestBorderSampler,
        m_nearestBorderSampler,
        m_nearestBorderSampler,
        m_linearRepeatSampler,
        m_nearestClampSampler
    };

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_bindGroupLayout;
    for (uint32_t binding = 0u; binding < kLightingTextureCount; ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler = samplers[binding];
        bindGroupDesc.entries.push_back(entry);
    }

    RhiBindGroupEntry uniformEntry;
    uniformEntry.binding = static_cast<uint32_t>(kLightingTextureCount);
    uniformEntry.resource.buffer.buffer = m_uniformBuffer;
    uniformEntry.resource.buffer.offset = 0u;
    uniformEntry.resource.buffer.range = sizeof(DeferredLightingParams);
    bindGroupDesc.entries.push_back(uniformEntry);

    m_bindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_bindGroup.isValid()) {
        return false;
    }
    m_boundViews = views;
    return true;
}

void DeferredLightingPass::destroyRhiBindGroup() {
    if (m_rhiDevice != nullptr && m_bindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_bindGroup);
    }
    m_bindGroup = {};
    m_boundViews = {};
}

void DeferredLightingPass::destroyExternalTextureViews() {
    if (m_rhiDevice != nullptr) {
        const RhiTextureViewHandle views[] = {
            m_lightmapDayTextureView,
            m_lightmapNightTextureView,
            m_noiseTextureView,
            m_rippleNormalTextureView
        };
        for (const RhiTextureViewHandle view : views) {
            if (view.isValid()) {
                m_rhiDevice->destroyTextureView(view);
            }
        }
    }
    m_lightmapDayViewTexture = {};
    m_lightmapNightViewTexture = {};
    m_noiseViewTexture = {};
    m_rippleNormalViewTexture = {};
    m_lightmapDayTextureView = {};
    m_lightmapNightTextureView = {};
    m_noiseTextureView = {};
    m_rippleNormalTextureView = {};
}

void DeferredLightingPass::destroyRhiResources() {
    destroyRhiBindGroup();
    destroyExternalTextureViews();
    if (m_rhiDevice != nullptr) {
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
        const RhiSamplerHandle samplers[] = {
            m_nearestClampSampler,
            m_linearClampSampler,
            m_linearRepeatSampler,
            m_nearestBorderSampler,
            m_compareBorderSampler
        };
        for (const RhiSamplerHandle sampler : samplers) {
            if (sampler.isValid()) {
                m_rhiDevice->destroySampler(sampler);
            }
        }
    }

    m_uniformBuffer = {};
    m_nearestClampSampler = {};
    m_linearClampSampler = {};
    m_linearRepeatSampler = {};
    m_nearestBorderSampler = {};
    m_compareBorderSampler = {};
    m_bindGroupLayout = {};
    m_pipelineLayout = {};
    m_vertexShader = {};
    m_fragmentShader = {};
    m_pipeline = {};
    m_rhiDevice = nullptr;
}
