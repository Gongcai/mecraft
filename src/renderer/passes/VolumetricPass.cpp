#include "VolumetricPass.h"
#include "../core/RenderScene.h"
#include "../debug/RenderDebugService.h"
#include "../targets/DeferredRenderTargets.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../../resource/ResourceMgr.h"
#include "../shadow/ShadowRenderer.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <optional>

namespace {
[[nodiscard]] bool sameTextureView(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

template <size_t Count>
[[nodiscard]] bool sameTextureViews(const std::array<RhiTextureViewHandle, Count>& lhs,
                                    const std::array<RhiTextureViewHandle, Count>& rhs) {
    for (size_t i = 0u; i < lhs.size(); ++i) {
        if (!sameTextureView(lhs[i], rhs[i])) {
            return false;
        }
    }
    return true;
}

struct alignas(16) FogCascadeParams {
    glm::mat4 viewProj;
    glm::vec4 splitAndScale;
    glm::vec4 depthExtent;
};
static_assert(sizeof(FogCascadeParams) == 96u);

struct alignas(16) VolumetricFogParams {
    glm::mat4 invViewProj;
    glm::mat4 shadowViewProj;
    glm::mat4 shadowModelView;
    glm::mat4 shadowProjection;
    glm::mat4 shadowProjectionInverse;
    FogCascadeParams cascades[shadow::ShadowRenderer::CASCADE_COUNT];
    glm::vec4 cameraPosSkyIntensity;
    glm::vec4 jitterTimeMoonFlux;
    glm::vec4 sunDirection;
    glm::vec4 shadowLightDirection;
    glm::vec4 sunLightColor;
    glm::vec4 moonLightColor;
    glm::vec4 horizonScatterColor;
    glm::vec4 atmosphere;
    glm::vec4 fog;
    glm::vec4 shadow0;
    glm::vec4 shadow1;
    glm::vec4 cloud0;
    glm::vec4 cloud1;
    glm::vec4 cloudDynamicWeather;
    glm::vec4 water;
    glm::vec4 vfog0;
    glm::vec4 vfog1;
    glm::ivec4 flags0;
    glm::ivec4 flags1;
    glm::ivec4 flags2;
    glm::ivec4 flags3;
};
static_assert(sizeof(VolumetricFogParams) == 1040u);
} // namespace

void VolumetricPass::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
    m_noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
}

void VolumetricPass::shutdown() {
    destroyFogRhiResources();
    destroyFogNoiseTextureView();
    destroyTemporalRhiResources();
    destroyCompositeRhiResources();
    m_shadowRenderer = nullptr;
    m_resourceMgr = nullptr;
    m_noiseTexture = {};
    m_hasRenderedFog = false;
    m_graphFramePrepared = false;
    m_graphWritesHistory = false;
    m_pendingRenderedFog = false;
    m_pendingCameraPos = glm::vec3(0.0f);
    m_pendingWeatherSignal = 0.0f;
}

void VolumetricPass::invalidateHistory() {
    m_hasRenderedFog = false;
    m_graphFramePrepared = false;
    m_graphWritesHistory = false;
    m_pendingRenderedFog = false;
}

bool VolumetricPass::shouldRenderFog(const FrameContext& ctx, const RenderSettings& settings,
                                     const bool hasPreviousFrame) const {
    const bool underwaterVolumetricActive = ctx.eyeInWater && settings.volumetric.uwLightEnabled;
    if (underwaterVolumetricActive || !settings.volumetric.temporalEnabled || !hasPreviousFrame || !m_hasRenderedFog) {
        return true;
    }

    const int updateInterval = std::clamp(settings.volumetric.updateInterval, 1, 8);
    if (updateInterval <= 1) {
        return true;
    }

    const glm::vec3 cameraDelta = ctx.camera.position - m_lastCameraPos;
    const bool movedFar = glm::dot(cameraDelta, cameraDelta) > 4.0f;
    const float weatherSignal =
        ctx.weather.wetness + ctx.weather.storm + ctx.weather.fogWetness + ctx.weather.lightningFlash * 4.0f;
    const bool weatherChanged = std::abs(weatherSignal - m_lastWeatherSignal) > 0.02f;
    if (movedFar || weatherChanged) {
        return true;
    }

    return (ctx.frameIndex % static_cast<uint64_t>(updateInterval)) == 0;
}

RgPassHandle VolumetricPass::addGraphPreparationPasses(RenderGraph& graph, const FrameContext& ctx,
                                                       const RenderSettings& settings, DeferredRenderTargets& targets,
                                                       const bool hasPreviousFrame, const GraphResources& resources,
                                                       const RgPassHandle dependency) {
    m_graphFramePrepared = false;
    m_graphWritesHistory = false;
    if (!dependency.isValid() || !resources.depth.isValid() || !resources.halfRes.isValid()) {
        return {};
    }

    const bool renderCurrentFog = shouldRenderFog(ctx, settings, hasPreviousFrame);
    const bool useTemporalVolumetric = settings.volumetric.temporalEnabled && hasPreviousFrame && hasTemporalShader();
    RgPassHandle tail = dependency;
    if (renderCurrentFog) {
        if (!resources.skyCapture.isValid() || !resources.noise.isValid() || !resources.atmosphereLut.isValid() ||
            !resources.shadowDepthOpaque.isValid() || !resources.shadowDepthAll.isValid() ||
            !resources.shadowColor0.isValid() || !resources.shadowColor1.isValid()) {
            return {};
        }
        RenderGraphPassBuilder fog = graph.addPass({"Volumetric.Fog", RgPassType::Graphics, RhiQueueType::Graphics,
                                                    /*threadSafeRecord=*/true});
        fog.dependsOn(tail)
            .readTexture(resources.depth, RhiResourceState::DepthRead)
            .readTexture(resources.skyCapture, RhiResourceState::ShaderRead)
            .readTexture(resources.noise, RhiResourceState::ShaderRead)
            .readTexture(resources.atmosphereLut, RhiResourceState::ShaderRead)
            .readTexture(resources.shadowDepthOpaque, RhiResourceState::DepthRead)
            .readTexture(resources.shadowDepthAll, RhiResourceState::DepthRead)
            .readTexture(resources.shadowColor0, RhiResourceState::ShaderRead)
            .readTexture(resources.shadowColor1, RhiResourceState::ShaderRead)
            .writeTexture(resources.halfRes, RhiResourceState::RenderTarget)
            .setExecute([this, frame = &ctx, frameSettings = settings, frameTargets = &targets](RgPassContext& pass) {
                return recordFogPass(pass.commandList(), *frame, frameSettings, *frameTargets);
            });
        tail = fog.handle();
    } else {
        if (!resources.historyPrevious.isValid() || !resources.historyCurrent.isValid()) {
            return {};
        }
        RenderGraphPassBuilder reuse = graph.addPass(
            {"Volumetric.HistoryReuse", RgPassType::Copy, RhiQueueType::Graphics, /*threadSafeRecord=*/true});
        reuse.dependsOn(tail)
            .readTexture(resources.historyPrevious, RhiResourceState::TransferSrc)
            .writeTexture(resources.halfRes, RhiResourceState::TransferDst)
            .writeTexture(resources.historyCurrent, RhiResourceState::TransferDst)
            .setExecute([frameTargets = &targets](RgPassContext& pass) {
                RhiTextureBlit halfResBlit;
                halfResBlit.src = frameTargets->historyVolumetricTexturePrevHandle();
                halfResBlit.dst = frameTargets->halfResTextureHandle();
                pass.commandList().blitTexture(halfResBlit);

                RhiTextureBlit historyBlit;
                historyBlit.src = frameTargets->historyVolumetricTexturePrevHandle();
                historyBlit.dst = frameTargets->historyVolumetricTextureHandle();
                pass.commandList().blitTexture(historyBlit);
                return true;
            });
        tail = reuse.handle();
    }

    if (renderCurrentFog && useTemporalVolumetric) {
        if (!resources.historyPrevious.isValid() || !resources.historyCurrent.isValid() ||
            !resources.velocity.isValid() || !resources.historyDepthPrevious.isValid()) {
            return {};
        }
        RenderGraphPassBuilder temporal = graph.addPass(
            {"Volumetric.Temporal", RgPassType::Graphics, RhiQueueType::Graphics, /*threadSafeRecord=*/true});
        temporal.dependsOn(tail)
            .readTexture(resources.halfRes, RhiResourceState::ShaderRead)
            .readTexture(resources.historyPrevious, RhiResourceState::ShaderRead)
            .readTexture(resources.velocity, RhiResourceState::ShaderRead)
            .readTexture(resources.depth, RhiResourceState::DepthRead)
            .readTexture(resources.historyDepthPrevious, RhiResourceState::DepthRead)
            .writeTexture(resources.historyCurrent, RhiResourceState::RenderTarget)
            .setExecute([this, frame = &ctx, frameSettings = settings, frameTargets = &targets](RgPassContext& pass) {
                return recordTemporalPass(pass.commandList(), *frame, frameSettings, *frameTargets);
            });
        tail = temporal.handle();
    }

    m_graphFramePrepared = true;
    m_graphWritesHistory = !renderCurrentFog || useTemporalVolumetric;
    m_pendingRenderedFog = renderCurrentFog;
    m_pendingCameraPos = ctx.camera.position;
    m_pendingWeatherSignal =
        ctx.weather.wetness + ctx.weather.storm + ctx.weather.fogWetness + ctx.weather.lightningFlash * 4.0f;
    return tail;
}

RgPassHandle VolumetricPass::addGraphCompositePass(RenderGraph& graph, const FrameContext& ctx,
                                                   const RenderSettings& settings, DeferredRenderTargets& targets,
                                                   const bool hasPreviousFrame, const GraphResources& resources,
                                                   const RgPassHandle dependency) {
    if (!m_graphFramePrepared || !dependency.isValid() || !resources.depth.isValid() ||
        !resources.sceneComposite.isValid() || !resources.sceneResolved.isValid()) {
        return {};
    }

    const bool useTemporalVolumetric = settings.volumetric.temporalEnabled && hasPreviousFrame && hasTemporalShader();
    const RgTextureHandle volumetricInput = useTemporalVolumetric ? resources.historyCurrent : resources.halfRes;
    if (!volumetricInput.isValid()) {
        return {};
    }
    RenderGraphPassBuilder composite = graph.addPass(
        {"Volumetric.Composite", RgPassType::Graphics, RhiQueueType::Graphics, /*threadSafeRecord=*/true});
    composite.dependsOn(dependency)
        .readTexture(resources.sceneComposite, RhiResourceState::ShaderRead)
        .readTexture(volumetricInput, RhiResourceState::ShaderRead)
        .readTexture(resources.depth, RhiResourceState::DepthRead)
        .writeTexture(resources.sceneResolved, RhiResourceState::RenderTarget)
        .setExecute([this, frame = &ctx, frameSettings = settings, frameTargets = &targets,
                     hasPreviousFrame](RgPassContext& pass) {
            return recordCompositePass(pass.commandList(), *frame, frameSettings, *frameTargets, hasPreviousFrame);
        });

    return composite.handle();
}

void VolumetricPass::finishGraphExecution(const bool succeeded) {
    if (m_graphFramePrepared && succeeded && m_pendingRenderedFog) {
        m_lastCameraPos = m_pendingCameraPos;
        m_lastWeatherSignal = m_pendingWeatherSignal;
        m_hasRenderedFog = true;
    }
    m_graphFramePrepared = false;
    m_graphWritesHistory = false;
    m_pendingRenderedFog = false;
    m_pendingCameraPos = glm::vec3(0.0f);
    m_pendingWeatherSignal = 0.0f;
}

bool VolumetricPass::recordFogPass(RhiCommandList& commandList, const FrameContext& ctx, const RenderSettings& settings,
                                   DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr || m_shadowRenderer == nullptr) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!targets.ensureHalfResTextureView(rhiDevice) || !targets.ensureGBufferTextureViews(rhiDevice) ||
        !targets.ensureSkyCaptureTextureView(rhiDevice) || !targets.ensureVolumetricFogTextureViews(rhiDevice) ||
        !ensureFogNoiseTextureView(rhiDevice)) {
        return false;
    }

    const std::array<RhiTextureViewHandle, 10> views = {targets.depthTextureViewHandle(),
                                                        targets.skyCaptureTextureViewHandle(),
                                                        m_fogNoiseTextureView,
                                                        targets.atmosphereLutTextureViewHandle(),
                                                        targets.csmShadowDepthComparisonArrayTextureViewHandle(),
                                                        targets.csmShadowDepthArrayTextureViewHandle(),
                                                        targets.csmShadowDepthAllComparisonArrayTextureViewHandle(),
                                                        targets.csmShadowDepthAllArrayTextureViewHandle(),
                                                        targets.csmShadowColor0ArrayTextureViewHandle(),
                                                        targets.csmShadowColor1ArrayTextureViewHandle()};
    if (!ensureFogRhiPipeline(rhiDevice) || !ensureFogBindGroup(rhiDevice, views)) {
        return false;
    }

    int volumetricDebugMode = 0;
    if (settings.debug.viewMode >= 46 && settings.debug.viewMode <= 77) {
        volumetricDebugMode = settings.debug.viewMode - 45;
    }

    VolumetricFogParams params{};
    params.invViewProj = ctx.camera.invViewProj;
    params.shadowViewProj = m_shadowRenderer->viewProj();
    params.shadowModelView = m_shadowRenderer->modelView();
    params.shadowProjection = m_shadowRenderer->projection();
    params.shadowProjectionInverse = m_shadowRenderer->projectionInverse();
    for (int cascadeIndex = 0; cascadeIndex < shadow::ShadowRenderer::CASCADE_COUNT; ++cascadeIndex) {
        const shadow::ShadowRenderer::Cascade& cascade = m_shadowRenderer->cascade(cascadeIndex);
        params.cascades[cascadeIndex].viewProj = cascade.viewProj;
        params.cascades[cascadeIndex].splitAndScale =
            glm::vec4(cascade.splitNear, cascade.splitFar, cascade.texelWorldSize, cascadeIndex >= 2 ? 0.5f : 1.0f);
        params.cascades[cascadeIndex].depthExtent = glm::vec4(cascade.depthExtent, 0.0f, 0.0f, 0.0f);
    }
    params.cameraPosSkyIntensity = glm::vec4(ctx.camera.position, ctx.skyIntensity);
    params.jitterTimeMoonFlux = glm::vec4(ctx.jitter.projectionOffset, ctx.shaderTime, 0.0f);
    params.sunDirection = glm::vec4(ctx.skyColors.sunDirection, 0.0f);
    params.shadowLightDirection = glm::vec4(m_shadowRenderer->lightDirection(), 0.0f);
    params.sunLightColor = glm::vec4(ctx.skyColors.sunLightColor, 0.0f);
    params.moonLightColor = glm::vec4(ctx.skyColors.moonLightColor, 0.0f);
    params.horizonScatterColor = glm::vec4(ctx.skyColors.horizonScatterColor, 0.0f);
    params.atmosphere = glm::vec4(ctx.atmosphere.aerialStrength, ctx.atmosphere.horizonScatterStrength,
                                  ctx.weather.skyWetness, ctx.weather.storm);
    params.fog = glm::vec4(ctx.volumetric.fogStrength, ctx.volumetric.baseDensity, ctx.volumetric.maxDistance,
                           ctx.weather.lightningFlash);
    params.shadow0 = glm::vec4(std::max(64.0f, m_shadowRenderer->shadowDistance()), m_shadowRenderer->shadowExtent(),
                               m_shadowRenderer->texelWorldSize(), settings.shadow.constantBias);
    params.shadow1 = glm::vec4(settings.shadow.slopeBias, settings.volumetric.shadowBiasScale, 0.0f, 0.0f);
    params.cloud0 = glm::vec4(ctx.cloud.coverage, ctx.cloud.density, ctx.cloud.height, ctx.cloud.thickness);
    params.cloud1 = glm::vec4(ctx.weather.cloudWetness, ctx.cloud.planarCoverage, ctx.cloud.planarDensity,
                              ctx.cloud.planarAltitude);
    params.cloudDynamicWeather = glm::vec4(ctx.skyIlluminance.cloudDynamicWeather, ctx.cloud.timeScale);
    params.water = glm::vec4(0.4f, 0.14f, 0.08f, ctx.volumetric.underwaterLightStrength);
    params.vfog0 = glm::vec4(ctx.volumetric.fogCenterHeight, ctx.volumetric.fogHeightSpread,
                             ctx.volumetric.fogNoiseScale, ctx.volumetric.fogLightStrength);
    params.vfog1 = glm::vec4(ctx.volumetric.fogDensityScale, ctx.cloud.shadowStrength, ctx.cloud.shadowScale,
                             ctx.cloud.shadowSpeed);
    params.flags0 = glm::ivec4(shadow::ShadowRenderer::CASCADE_COUNT, settings.shadow.enabled ? 1 : 0,
                               ctx.volumetric.lightEnabled ? 1 : 0, ctx.volumetric.fogEnabled ? 1 : 0);
    params.flags1 =
        glm::ivec4(ctx.moonShadowActive ? 1 : 0, 0, volumetricDebugMode, settings.volumetric.skyRayEnabled ? 1 : 0);
    params.flags2 =
        glm::ivec4(settings.volumetric.timeFadeEnabled ? 1 : 0, settings.volumetric.qualityTier,
                   ctx.volumetric.fogSamples, (volumetricDebugMode > 0 || settings.volumetric.freezeR1) ? 1 : 0);
    params.flags3 = glm::ivec4(static_cast<int>(ctx.frameIndex & 0x7fffffffULL), ctx.eyeInWater ? 1 : 0,
                               settings.volumetric.uwLightEnabled ? 1 : 0, ctx.cloud.shadowsEnabled ? 1 : 0);

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.halfResTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "VolumetricFog";
    renderingInfo.renderArea = {0, 0, static_cast<uint32_t>(std::max(1, targets.halfWidth())),
                                static_cast<uint32_t>(std::max(1, targets.halfHeight()))};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
                                                ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Volumetric)
                                                : GpuTimerSegmentToken{};
    commandList.bufferBarrier({m_fogUniformBuffer, RhiResourceState::UniformBuffer, RhiResourceState::TransferDst});
    commandList.updateBuffer(m_fogUniformBuffer, 0u, &params, sizeof(params));
    commandList.bufferBarrier({m_fogUniformBuffer, RhiResourceState::TransferDst, RhiResourceState::UniformBuffer});
    commandList.beginRendering(renderingInfo);
    commandList.setGraphicsPipeline(m_fogPipeline);
    commandList.setBindGroup(0u, m_fogBindGroup);
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool VolumetricPass::ensureFogNoiseTextureView(RhiDevice& rhiDevice) {
    if (m_fogNoiseViewDevice != nullptr && m_fogNoiseViewDevice != &rhiDevice) {
        destroyFogNoiseTextureView();
    }
    if (m_fogNoiseTextureView.isValid()) {
        return true;
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
    m_fogNoiseTextureView = rhiDevice.createTextureView(viewDesc);
    if (!m_fogNoiseTextureView.isValid()) {
        return false;
    }

    m_fogNoiseViewDevice = &rhiDevice;
    return true;
}

void VolumetricPass::destroyFogNoiseTextureView() {
    if (m_fogNoiseViewDevice != nullptr && m_fogNoiseTextureView.isValid()) {
        m_fogNoiseViewDevice->destroyTextureView(m_fogNoiseTextureView);
    }
    m_fogNoiseTextureView = {};
    m_fogNoiseViewDevice = nullptr;
}

bool VolumetricPass::ensureFogRhiPipeline(RhiDevice& rhiDevice) {
    if (m_fogRhiDevice != nullptr && m_fogRhiDevice != &rhiDevice) {
        destroyFogRhiResources();
    }
    if (m_fogPipeline.isValid()) {
        return true;
    }
    m_fogRhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/volumetric_fog.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "VolumetricFog.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_fogVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "VolumetricFog.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_fogFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_fogVertexShader.isValid() || !m_fogFragmentShader.isValid()) {
        destroyFogRhiResources();
        return false;
    }

    RhiBufferDesc uniformBufferDesc;
    uniformBufferDesc.debugName = "VolumetricFog.Params";
    uniformBufferDesc.size = sizeof(VolumetricFogParams);
    uniformBufferDesc.usage = rhiFlag(RhiBufferUsage::Uniform) | rhiFlag(RhiBufferUsage::TransferDst);
    uniformBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    uniformBufferDesc.initialState = RhiResourceState::UniformBuffer;
    uniformBufferDesc.memoryCategory = RhiMemoryCategory::Uniform;
    m_fogUniformBuffer = rhiDevice.createBuffer(uniformBufferDesc, nullptr, 0u);
    if (!m_fogUniformBuffer.isValid()) {
        destroyFogRhiResources();
        return false;
    }

    auto createSampler = [&](const RhiFilter filter, const RhiAddressMode addressMode, const RhiBorderColor borderColor,
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

    m_fogNearestClampSampler =
        createSampler(RhiFilter::Nearest, RhiAddressMode::ClampToEdge, RhiBorderColor::TransparentBlack, false);
    m_fogLinearClampSampler =
        createSampler(RhiFilter::Linear, RhiAddressMode::ClampToEdge, RhiBorderColor::TransparentBlack, false);
    m_fogLinearRepeatSampler =
        createSampler(RhiFilter::Linear, RhiAddressMode::Repeat, RhiBorderColor::TransparentBlack, false);
    m_fogNearestBorderSampler =
        createSampler(RhiFilter::Nearest, RhiAddressMode::ClampToBorder, RhiBorderColor::OpaqueWhite, false);
    m_fogCompareBorderSampler =
        createSampler(RhiFilter::Linear, RhiAddressMode::ClampToBorder, RhiBorderColor::OpaqueWhite, true);
    if (!m_fogNearestClampSampler.isValid() || !m_fogLinearClampSampler.isValid() ||
        !m_fogLinearRepeatSampler.isValid() || !m_fogNearestBorderSampler.isValid() ||
        !m_fogCompareBorderSampler.isValid()) {
        destroyFogRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "VolumetricFog.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 10u; ++binding) {
        bindGroupLayoutDesc.entries.push_back(
            {binding, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    }
    bindGroupLayoutDesc.entries.push_back({10u, RhiBindingType::UniformBuffer, rhiFlag(RhiShaderStage::Fragment), 1u});
    m_fogBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_fogBindGroupLayout.isValid()) {
        destroyFogRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "VolumetricFog.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_fogBindGroupLayout);
    m_fogPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_fogPipelineLayout.isValid()) {
        destroyFogRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "VolumetricFog.Pipeline";
    pipelineDesc.vertexShader = m_fogVertexShader;
    pipelineDesc.fragmentShader = m_fogFragmentShader;
    pipelineDesc.layout = m_fogPipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.blend.attachments.push_back({});
    m_fogPipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_fogPipeline.isValid()) {
        destroyFogRhiResources();
        return false;
    }

    return true;
}

bool VolumetricPass::ensureFogBindGroup(RhiDevice& rhiDevice, const std::array<RhiTextureViewHandle, 10>& views) {
    if (!ensureFogRhiPipeline(rhiDevice)) {
        return false;
    }
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_fogBindGroup.isValid() && sameTextureViews(m_fogBoundViews, views)) {
        return true;
    }

    destroyFogBindGroup();

    const RhiSamplerHandle samplers[10] = {
        m_fogNearestClampSampler,  m_fogLinearClampSampler,   m_fogLinearRepeatSampler,  m_fogLinearClampSampler,
        m_fogCompareBorderSampler, m_fogNearestBorderSampler, m_fogCompareBorderSampler, m_fogNearestBorderSampler,
        m_fogNearestBorderSampler, m_fogNearestBorderSampler};

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_fogBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler = samplers[binding];
        bindGroupDesc.entries.push_back(entry);
    }

    RhiBindGroupEntry uniformEntry;
    uniformEntry.binding = 10u;
    uniformEntry.resource.buffer.buffer = m_fogUniformBuffer;
    uniformEntry.resource.buffer.offset = 0u;
    uniformEntry.resource.buffer.range = sizeof(VolumetricFogParams);
    bindGroupDesc.entries.push_back(uniformEntry);

    m_fogBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_fogBindGroup.isValid()) {
        m_fogBoundViews = {};
        return false;
    }

    m_fogBoundViews = views;
    return true;
}

void VolumetricPass::destroyFogBindGroup() {
    if (m_fogRhiDevice != nullptr && m_fogBindGroup.isValid()) {
        m_fogRhiDevice->destroyBindGroup(m_fogBindGroup);
    }
    m_fogBindGroup = {};
    m_fogBoundViews = {};
}

void VolumetricPass::destroyFogRhiResources() {
    destroyFogBindGroup();
    if (m_fogRhiDevice != nullptr) {
        if (m_fogPipeline.isValid()) {
            m_fogRhiDevice->destroyPipeline(m_fogPipeline);
        }
        if (m_fogVertexShader.isValid()) {
            m_fogRhiDevice->destroyShader(m_fogVertexShader);
        }
        if (m_fogFragmentShader.isValid()) {
            m_fogRhiDevice->destroyShader(m_fogFragmentShader);
        }
        if (m_fogPipelineLayout.isValid()) {
            m_fogRhiDevice->destroyPipelineLayout(m_fogPipelineLayout);
        }
        if (m_fogBindGroupLayout.isValid()) {
            m_fogRhiDevice->destroyBindGroupLayout(m_fogBindGroupLayout);
        }
        if (m_fogUniformBuffer.isValid()) {
            m_fogRhiDevice->destroyBuffer(m_fogUniformBuffer);
        }
        const RhiSamplerHandle samplers[] = {m_fogNearestClampSampler, m_fogLinearClampSampler,
                                             m_fogLinearRepeatSampler, m_fogNearestBorderSampler,
                                             m_fogCompareBorderSampler};
        for (const RhiSamplerHandle sampler : samplers) {
            if (sampler.isValid()) {
                m_fogRhiDevice->destroySampler(sampler);
            }
        }
    }

    m_fogPipeline = {};
    m_fogVertexShader = {};
    m_fogFragmentShader = {};
    m_fogPipelineLayout = {};
    m_fogBindGroupLayout = {};
    m_fogUniformBuffer = {};
    m_fogNearestClampSampler = {};
    m_fogLinearClampSampler = {};
    m_fogLinearRepeatSampler = {};
    m_fogNearestBorderSampler = {};
    m_fogCompareBorderSampler = {};
    m_fogRhiDevice = nullptr;
}

bool VolumetricPass::recordTemporalPass(RhiCommandList& commandList, const FrameContext& ctx,
                                        const RenderSettings& settings, DeferredRenderTargets& targets) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!targets.ensureHistoryVolumetricTextureViews(rhiDevice) || !targets.ensureHistoryDepthTextureViews(rhiDevice) ||
        !targets.ensureHalfResTextureView(rhiDevice) || !targets.ensureVelocityTextureView(rhiDevice) ||
        !targets.ensureGBufferTextureViews(rhiDevice)) {
        return false;
    }

    const std::array<RhiTextureViewHandle, 5> views = {
        targets.halfResTextureViewHandle(), targets.historyVolumetricTexturePrevViewHandle(),
        targets.velocityTextureViewHandle(), targets.depthTextureViewHandle(),
        targets.historyDepthTexturePrevViewHandle()};
    if (!ensureTemporalRhiPipeline(rhiDevice) || !ensureTemporalBindGroup(rhiDevice, views)) {
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.historyVolumetricTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "VolumetricTemporal";
    renderingInfo.renderArea = {0, 0, static_cast<uint32_t>(std::max(1, targets.halfWidth())),
                                static_cast<uint32_t>(std::max(1, targets.halfHeight()))};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
                                                ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Volumetric)
                                                : GpuTimerSegmentToken{};
    commandList.beginRendering(renderingInfo);
    const glm::vec4 pushConstants[2] = {glm::vec4(static_cast<float>(std::max(1, targets.halfWidth())),
                                                  static_cast<float>(std::max(1, targets.halfHeight())),
                                                  settings.volumetric.temporalWeight, ctx.camera.nearPlane),
                                        glm::vec4(ctx.camera.farPlane, 0.0f, 0.0f, 0.0f)};
    commandList.setGraphicsPipeline(m_temporalPipeline);
    commandList.setBindGroup(0u, m_temporalBindGroup);
    commandList.pushConstants(pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool VolumetricPass::recordCompositePass(RhiCommandList& commandList, const FrameContext& ctx,
                                         const RenderSettings& settings, DeferredRenderTargets& targets,
                                         const bool hasPreviousFrame) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    const bool useTemporalVolumetric = settings.volumetric.temporalEnabled && hasPreviousFrame && hasTemporalShader();
    if (!targets.ensureSceneResolvedTextureView(rhiDevice) || !targets.ensureSceneCompositeTextureView(rhiDevice) ||
        !targets.ensureGBufferTextureViews(rhiDevice) ||
        !(useTemporalVolumetric ? targets.ensureHistoryVolumetricTextureView(rhiDevice)
                                : targets.ensureHalfResTextureView(rhiDevice))) {
        return false;
    }

    const std::array<RhiTextureViewHandle, 3> views = {
        targets.sceneCompositeTextureViewHandle(),
        useTemporalVolumetric ? targets.historyVolumetricTextureViewHandle() : targets.halfResTextureViewHandle(),
        targets.depthTextureViewHandle()};
    if (!ensureCompositeRhiPipeline(rhiDevice) || !ensureCompositeBindGroup(rhiDevice, views)) {
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = targets.sceneResolvedTextureViewHandle();
    colorAttachment.loadOp = RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "VolumetricComposite";
    renderingInfo.renderArea = {0, 0, static_cast<uint32_t>(std::max(1, targets.width())),
                                static_cast<uint32_t>(std::max(1, targets.height()))};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    const GpuTimerSegmentToken timerToken = ctx.debugService != nullptr
                                                ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Volumetric)
                                                : GpuTimerSegmentToken{};
    commandList.beginRendering(renderingInfo);

    const bool underwaterVolumetricActive = ctx.eyeInWater && settings.volumetric.uwLightEnabled;
    const bool volFogCompositeActive = (underwaterVolumetricActive || settings.volumetric.lightEnabled ||
                                        (settings.volumetric.fogEnabled && settings.volumetric.fogStrength > 0.001f));
    struct CompositePushConstants {
        glm::vec4 depthParams;
        glm::ivec4 flags;
    };
    const CompositePushConstants pushConstants{glm::vec4(ctx.camera.nearPlane, ctx.camera.farPlane, 0.0f, 0.0f),
                                               glm::ivec4(static_cast<int>(ctx.frameIndex & 0x7fffffffULL),
                                                          settings.volumetric.freezeBias ? 1 : 0,
                                                          ctx.eyeInWater ? 1 : 0, volFogCompositeActive ? 1 : 0)};
    commandList.setGraphicsPipeline(m_compositePipeline);
    commandList.setBindGroup(0u, m_compositeBindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, timerToken);
    }
    return true;
}

bool VolumetricPass::ensureCompositeRhiPipeline(RhiDevice& rhiDevice) {
    if (m_compositeRhiDevice != nullptr && m_compositeRhiDevice != &rhiDevice) {
        destroyCompositeRhiResources();
    }
    if (m_compositePipeline.isValid()) {
        return true;
    }
    m_compositeRhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/volumetric_composite.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "VolumetricComposite.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_compositeVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "VolumetricComposite.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_compositeFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_compositeVertexShader.isValid() || !m_compositeFragmentShader.isValid()) {
        destroyCompositeRhiResources();
        return false;
    }

    RhiSamplerDesc nearestSamplerDesc;
    nearestSamplerDesc.minFilter = RhiFilter::Nearest;
    nearestSamplerDesc.magFilter = RhiFilter::Nearest;
    nearestSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    nearestSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_compositeNearestSampler = rhiDevice.createSampler(nearestSamplerDesc);

    RhiSamplerDesc linearSamplerDesc;
    linearSamplerDesc.minFilter = RhiFilter::Linear;
    linearSamplerDesc.magFilter = RhiFilter::Linear;
    linearSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    linearSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_compositeLinearSampler = rhiDevice.createSampler(linearSamplerDesc);
    if (!m_compositeNearestSampler.isValid() || !m_compositeLinearSampler.isValid()) {
        destroyCompositeRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "VolumetricComposite.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 3u; ++binding) {
        bindGroupLayoutDesc.entries.push_back(
            {binding, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    }
    m_compositeBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_compositeBindGroupLayout.isValid()) {
        destroyCompositeRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "VolumetricComposite.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_compositeBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4) + sizeof(glm::ivec4));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_compositePipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_compositePipelineLayout.isValid()) {
        destroyCompositeRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "VolumetricComposite.Pipeline";
    pipelineDesc.vertexShader = m_compositeVertexShader;
    pipelineDesc.fragmentShader = m_compositeFragmentShader;
    pipelineDesc.layout = m_compositePipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.blend.attachments.push_back({});
    m_compositePipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_compositePipeline.isValid()) {
        destroyCompositeRhiResources();
        return false;
    }

    return true;
}

bool VolumetricPass::ensureCompositeBindGroup(RhiDevice& rhiDevice, const std::array<RhiTextureViewHandle, 3>& views) {
    if (!ensureCompositeRhiPipeline(rhiDevice)) {
        return false;
    }
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_compositeBindGroup.isValid() && sameTextureViews(m_compositeBoundViews, views)) {
        return true;
    }

    destroyCompositeBindGroup();

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_compositeBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler =
            binding == 2u ? m_compositeNearestSampler : m_compositeLinearSampler;
        bindGroupDesc.entries.push_back(entry);
    }

    m_compositeBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_compositeBindGroup.isValid()) {
        m_compositeBoundViews = {};
        return false;
    }

    m_compositeBoundViews = views;
    return true;
}

void VolumetricPass::destroyCompositeBindGroup() {
    if (m_compositeRhiDevice != nullptr && m_compositeBindGroup.isValid()) {
        m_compositeRhiDevice->destroyBindGroup(m_compositeBindGroup);
    }
    m_compositeBindGroup = {};
    m_compositeBoundViews = {};
}

void VolumetricPass::destroyCompositeRhiResources() {
    destroyCompositeBindGroup();
    if (m_compositeRhiDevice != nullptr) {
        if (m_compositePipeline.isValid()) {
            m_compositeRhiDevice->destroyPipeline(m_compositePipeline);
        }
        if (m_compositeVertexShader.isValid()) {
            m_compositeRhiDevice->destroyShader(m_compositeVertexShader);
        }
        if (m_compositeFragmentShader.isValid()) {
            m_compositeRhiDevice->destroyShader(m_compositeFragmentShader);
        }
        if (m_compositePipelineLayout.isValid()) {
            m_compositeRhiDevice->destroyPipelineLayout(m_compositePipelineLayout);
        }
        if (m_compositeBindGroupLayout.isValid()) {
            m_compositeRhiDevice->destroyBindGroupLayout(m_compositeBindGroupLayout);
        }
        if (m_compositeNearestSampler.isValid()) {
            m_compositeRhiDevice->destroySampler(m_compositeNearestSampler);
        }
        if (m_compositeLinearSampler.isValid()) {
            m_compositeRhiDevice->destroySampler(m_compositeLinearSampler);
        }
    }

    m_compositePipeline = {};
    m_compositeVertexShader = {};
    m_compositeFragmentShader = {};
    m_compositePipelineLayout = {};
    m_compositeBindGroupLayout = {};
    m_compositeNearestSampler = {};
    m_compositeLinearSampler = {};
    m_compositeRhiDevice = nullptr;
}

bool VolumetricPass::ensureTemporalRhiPipeline(RhiDevice& rhiDevice) {
    if (m_temporalRhiDevice != nullptr && m_temporalRhiDevice != &rhiDevice) {
        destroyTemporalRhiResources();
    }
    if (m_temporalPipeline.isValid()) {
        return true;
    }
    m_temporalRhiDevice = &rhiDevice;

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/volumetric_temporal.frag");
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "VolumetricTemporal.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_temporalVertexShader = rhiDevice.createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "VolumetricTemporal.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_temporalFragmentShader = rhiDevice.createShader(fragmentDesc);
    if (!m_temporalVertexShader.isValid() || !m_temporalFragmentShader.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiSamplerDesc nearestSamplerDesc;
    nearestSamplerDesc.minFilter = RhiFilter::Nearest;
    nearestSamplerDesc.magFilter = RhiFilter::Nearest;
    nearestSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    nearestSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    nearestSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_temporalNearestSampler = rhiDevice.createSampler(nearestSamplerDesc);

    RhiSamplerDesc linearSamplerDesc;
    linearSamplerDesc.minFilter = RhiFilter::Linear;
    linearSamplerDesc.magFilter = RhiFilter::Linear;
    linearSamplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    linearSamplerDesc.addressU = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressV = RhiAddressMode::ClampToEdge;
    linearSamplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_temporalLinearSampler = rhiDevice.createSampler(linearSamplerDesc);
    if (!m_temporalNearestSampler.isValid() || !m_temporalLinearSampler.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "VolumetricTemporal.BindGroupLayout";
    for (uint32_t binding = 0u; binding < 5u; ++binding) {
        bindGroupLayoutDesc.entries.push_back(
            {binding, RhiBindingType::CombinedTextureSampler, rhiFlag(RhiShaderStage::Fragment), 1u});
    }
    m_temporalBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
    if (!m_temporalBindGroupLayout.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "VolumetricTemporal.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_temporalBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = static_cast<uint32_t>(sizeof(glm::vec4) * 2u);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
    m_temporalPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_temporalPipelineLayout.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "VolumetricTemporal.Pipeline";
    pipelineDesc.vertexShader = m_temporalVertexShader;
    pipelineDesc.fragmentShader = m_temporalFragmentShader;
    pipelineDesc.layout = m_temporalPipelineLayout;
    pipelineDesc.topology = RhiPrimitiveTopology::TriangleList;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
    pipelineDesc.blend.attachments.push_back({});
    m_temporalPipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
    if (!m_temporalPipeline.isValid()) {
        destroyTemporalRhiResources();
        return false;
    }

    return true;
}

bool VolumetricPass::ensureTemporalBindGroup(RhiDevice& rhiDevice, const std::array<RhiTextureViewHandle, 5>& views) {
    if (!ensureTemporalRhiPipeline(rhiDevice)) {
        return false;
    }
    for (const RhiTextureViewHandle view : views) {
        if (!view.isValid()) {
            return false;
        }
    }
    if (m_temporalBindGroup.isValid() && sameTextureViews(m_temporalBoundViews, views)) {
        return true;
    }

    destroyTemporalBindGroup();

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_temporalBindGroupLayout;
    for (uint32_t binding = 0u; binding < static_cast<uint32_t>(views.size()); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = views[binding];
        entry.resource.combinedTextureSampler.sampler =
            binding == 1u ? m_temporalLinearSampler : m_temporalNearestSampler;
        bindGroupDesc.entries.push_back(entry);
    }

    m_temporalBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!m_temporalBindGroup.isValid()) {
        m_temporalBoundViews = {};
        return false;
    }

    m_temporalBoundViews = views;
    return true;
}

void VolumetricPass::destroyTemporalBindGroup() {
    if (m_temporalRhiDevice != nullptr && m_temporalBindGroup.isValid()) {
        m_temporalRhiDevice->destroyBindGroup(m_temporalBindGroup);
    }
    m_temporalBindGroup = {};
    m_temporalBoundViews = {};
}

void VolumetricPass::destroyTemporalRhiResources() {
    destroyTemporalBindGroup();
    if (m_temporalRhiDevice != nullptr) {
        if (m_temporalPipeline.isValid()) {
            m_temporalRhiDevice->destroyPipeline(m_temporalPipeline);
        }
        if (m_temporalVertexShader.isValid()) {
            m_temporalRhiDevice->destroyShader(m_temporalVertexShader);
        }
        if (m_temporalFragmentShader.isValid()) {
            m_temporalRhiDevice->destroyShader(m_temporalFragmentShader);
        }
        if (m_temporalPipelineLayout.isValid()) {
            m_temporalRhiDevice->destroyPipelineLayout(m_temporalPipelineLayout);
        }
        if (m_temporalBindGroupLayout.isValid()) {
            m_temporalRhiDevice->destroyBindGroupLayout(m_temporalBindGroupLayout);
        }
        if (m_temporalNearestSampler.isValid()) {
            m_temporalRhiDevice->destroySampler(m_temporalNearestSampler);
        }
        if (m_temporalLinearSampler.isValid()) {
            m_temporalRhiDevice->destroySampler(m_temporalLinearSampler);
        }
    }

    m_temporalPipeline = {};
    m_temporalVertexShader = {};
    m_temporalFragmentShader = {};
    m_temporalPipelineLayout = {};
    m_temporalBindGroupLayout = {};
    m_temporalNearestSampler = {};
    m_temporalLinearSampler = {};
    m_temporalRhiDevice = nullptr;
}
