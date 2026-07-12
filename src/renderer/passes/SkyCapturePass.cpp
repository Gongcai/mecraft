#include "SkyCapturePass.h"
#include "../targets/DeferredRenderTargets.h"
#include "../renderers/GameplaySkyRenderer.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiShaderSourceLoader.h"
#include "../../resource/ResourceMgr.h"
#include "../../world/DayNightSystem.h"
#include "../../world/WeatherSystem.h"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace {
struct MetadataPushConstants {
    glm::vec4 sunDirectionAltitude;
    glm::vec4 cloudDynamicWeatherMoonFlux;
};
using RawPushConstants = MetadataPushConstants;

[[nodiscard]] bool sameView(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
} // namespace

void SkyCapturePass::init(ResourceMgr& /*resourceMgr*/) {
    // No shaders to load — GameplaySkyRenderer manages its own.
}

void SkyCapturePass::shutdown() {
    destroyMetadataResources();
}

void SkyCapturePass::execute(const DayNightSystem& dayNightSystem, const WeatherSystem& weatherSystem,
                              RhiDevice& rhiDevice,
                              RhiCommandListPool& commandListPool,
                              DeferredRenderTargets& targets,
                              GameplaySkyRenderer& skyRenderer, ResourceMgr& resourceMgr,
                              float cameraY, float shaderTime, const glm::vec3& cameraPos,
                              float cloudTimeScale) {
    if (!targets.ensureSkyCaptureTextureView(rhiDevice) ||
        !targets.atmosphereLutTextureViewHandle().isValid() ||
        !ensureMetadataResources(rhiDevice, targets.atmosphereLutTextureViewHandle())) {
        return;
    }

    const float cameraAltitude = cameraY;
    const RhiTextureHandle atmosphereLut = targets.atmosphereLutTextureHandle();
    if (!atmosphereLut.isValid()) {
        return;
    }
    const int moonPhase = dayNightSystem.getMoonPhaseIndex();

    // DerivativeMain MoonFlux: phase factor ranges 0.2 (full moon) to 1.2 (new moon).
    constexpr float kNightBrightness = 0.0005f;
    const float moonPhaseFlux = (static_cast<float>(std::abs(moonPhase - 4)) * 0.25f + 0.2f) * kNightBrightness;

    // Weather state for SkyCapture modulation
    const WeatherState& weather = weatherSystem.getRenderState();
    const float weatherWetness = weather.wetness;
    const float weatherStorm = weather.storm;

    const auto skyColors = skyRenderer.computeSkyColors(dayNightSystem);
    auto illum = skyRenderer.computeSkyIlluminance(skyColors, weatherWetness, weatherStorm);

    // DerivativeMain worldTime: 24000 ticks/day, our timeOfDay is in seconds with 1200s/day.
    const int worldDay = dayNightSystem.getElapsedDays();
    const int worldTime = static_cast<int>(dayNightSystem.getTimeOfDay() * 20.0f);
    illum.cloudDynamicWeather = GameplaySkyRenderer::computeCloudDynamicWeather(worldDay, worldTime);

    const float cloudWetness = std::clamp(weatherWetness + weatherStorm * (4.0f / 3.0f), 0.0f, 1.0f);
    float cloudHeight = 1000.0f + cloudWetness * (800.0f - 1000.0f);
    const float cloudThickness = 1400.0f + cloudWetness * (3000.0f - 1400.0f);
    const float cloudCoverage = std::clamp(1.0f + cloudWetness * 0.2f, 0.0f, 1.5f);
    const float cloudDensity = 0.85f + weatherWetness * 0.35f + weatherStorm * 0.55f;
    const RhiTextureHandle noiseTexture = resourceMgr.getTexture2DHandle("shader_noise2d");
    if (!noiseTexture.isValid()) {
        return;
    }
    RhiCommandList* commandListStorage =
        commandListPool.acquire(RhiCommandListType::Graphics);
    if (commandListStorage == nullptr ||
        !commandListStorage->begin({"RenderPass.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& commandList = *commandListStorage;

    RhiColorAttachment rawAttachment;
    rawAttachment.view = targets.skyCaptureTextureViewHandle();
    rawAttachment.loadOp = RhiLoadOp::Load;
    rawAttachment.storeOp = RhiStoreOp::Store;
    RhiRenderingInfo rawRendering;
    rawRendering.debugName = "SkyCapture.Raw";
    rawRendering.renderArea = {0, 0, static_cast<uint32_t>(targets.skyCaptureWidth()),
                               static_cast<uint32_t>(std::min(targets.skyCaptureHeight(), 258))};
    rawRendering.colorAttachments = &rawAttachment;
    rawRendering.colorAttachmentCount = 1u;
    targets.transitionTexture(commandList,
                              targets.skyCaptureTextureHandle(),
                              RhiResourceState::RenderTarget);
    commandList.beginRendering(rawRendering);
    commandList.setGraphicsPipeline(m_rawPipeline);
    commandList.setBindGroup(0u, m_metadataBindGroup);
    const RawPushConstants rawPushConstants{
        glm::vec4(skyColors.sunDirection, cameraAltitude),
        glm::vec4(std::clamp(weatherWetness + weatherStorm, 0.0f, 1.0f), moonPhaseFlux, 0.0f, 0.0f)
    };
    commandList.pushConstants(&rawPushConstants, sizeof(rawPushConstants),
                              rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();

    // Cloudy sky radiance (rows 258..513)
    const GameplaySkyRenderer::CloudySkyCaptureParams cloudyParams{
        cameraAltitude,
        moonPhaseFlux,
        shaderTime,
        cloudTimeScale,
        cloudCoverage,
        cloudDensity,
        cloudHeight,
        cloudThickness,
        0.5f,
        1.0f,
        7000.0f,
        weatherWetness,
        weatherStorm,
        std::clamp(weatherWetness + weatherStorm, 0.0f, 1.0f),
        std::clamp(weatherWetness * 0.35f + weatherStorm * 0.65f, 0.0f, 1.0f),
        std::clamp(weatherWetness + weatherStorm * (4.0f / 3.0f), 0.0f, 1.0f),
        std::clamp(weatherWetness + weatherStorm * 0.3f, 0.0f, 1.0f),
        std::clamp(weatherWetness + weatherStorm, 0.0f, 1.0f),
        cameraPos
    };
    skyRenderer.renderCloudySkyCapture(skyColors,
                                        commandList,
                                        targets.skyCaptureTextureViewHandle(),
                                        targets.skyCaptureWidth(),
                                        targets.skyCaptureHeight(),
                                        targets.atmosphereLutTextureViewHandle(), noiseTexture,
                                        illum, cloudyParams);

    RhiColorAttachment metadataAttachment;
    metadataAttachment.view = targets.skyCaptureTextureViewHandle();
    metadataAttachment.loadOp = RhiLoadOp::Load;
    metadataAttachment.storeOp = RhiStoreOp::Store;
    RhiRenderingInfo metadataRendering;
    metadataRendering.debugName = "SkyCapture.Metadata";
    metadataRendering.renderArea = {targets.skyCaptureWidth() - 1, 0, 1u, 6u};
    metadataRendering.colorAttachments = &metadataAttachment;
    metadataRendering.colorAttachmentCount = 1u;
    commandList.beginRendering(metadataRendering);
    commandList.setGraphicsPipeline(m_metadataPipeline);
    commandList.setBindGroup(0u, m_metadataBindGroup);
    const MetadataPushConstants metadataPushConstants{
        glm::vec4(skyColors.sunDirection, cameraAltitude),
        glm::vec4(illum.cloudDynamicWeather, moonPhaseFlux)
    };
    commandList.pushConstants(&metadataPushConstants, sizeof(metadataPushConstants),
                              rhiFlag(RhiShaderStage::Fragment));
    commandList.draw(3u, 1u, 0u, 0u);
    commandList.endRendering();
    targets.transitionTexture(commandList,
                              targets.skyCaptureTextureHandle(),
                              RhiResourceState::ShaderRead);
    if (!commandList.end()) {
        std::abort();
    }
    {
        RhiCommandList* submittedCommandLists[] = {&commandList};
        if (!rhiDevice.submit({"RenderPass.Submit", submittedCommandLists, 1u})) {
            std::abort();
        }
    }
}

bool SkyCapturePass::ensureMetadataResources(RhiDevice& rhiDevice,
                                             const RhiTextureViewHandle atmosphereLutView) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) destroyMetadataResources();
    m_rhiDevice = &rhiDevice;
    if (!m_metadataPipeline.isValid()) {
        const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/fullscreen_triangle_rhi.vert");
        const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/sky_capture_metadata_rhi.frag");
        const auto rawFragmentSource = renderer::rhi::loadShaderSource("assets/shaders/sky_capture_raw_rhi.frag");
        if (!vertexSource || !fragmentSource || !rawFragmentSource) return false;
        RhiShaderDesc shaderDesc;
        shaderDesc.debugName = "SkyCapture.Metadata.Vertex";
        shaderDesc.stage = RhiShaderStage::Vertex;
        shaderDesc.source = vertexSource->c_str();
        shaderDesc.sourceSize = vertexSource->size();
        m_metadataVertexShader = rhiDevice.createShader(shaderDesc);
        shaderDesc.debugName = "SkyCapture.Metadata.Fragment";
        shaderDesc.stage = RhiShaderStage::Fragment;
        shaderDesc.source = fragmentSource->c_str();
        shaderDesc.sourceSize = fragmentSource->size();
        m_metadataFragmentShader = rhiDevice.createShader(shaderDesc);
        shaderDesc.debugName = "SkyCapture.Raw.Fragment";
        shaderDesc.source = rawFragmentSource->c_str();
        shaderDesc.sourceSize = rawFragmentSource->size();
        m_rawFragmentShader = rhiDevice.createShader(shaderDesc);
        RhiSamplerDesc samplerDesc;
        samplerDesc.minFilter = RhiFilter::Linear;
        samplerDesc.magFilter = RhiFilter::Linear;
        samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
        m_metadataSampler = rhiDevice.createSampler(samplerDesc);
        RhiBindGroupLayoutDesc bindGroupLayoutDesc;
        bindGroupLayoutDesc.debugName = "SkyCapture.Metadata.BindGroupLayout";
        bindGroupLayoutDesc.entries.push_back({0u, RhiBindingType::CombinedTextureSampler,
                                               rhiFlag(RhiShaderStage::Fragment), 1u});
        m_metadataBindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
        RhiPipelineLayoutDesc pipelineLayoutDesc;
        pipelineLayoutDesc.debugName = "SkyCapture.Metadata.PipelineLayout";
        pipelineLayoutDesc.bindGroupLayouts.push_back(m_metadataBindGroupLayout);
        pipelineLayoutDesc.pushConstantBytes = sizeof(MetadataPushConstants);
        pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Fragment);
        m_metadataPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
        RhiGraphicsPipelineDesc pipelineDesc;
        pipelineDesc.debugName = "SkyCapture.Metadata.Pipeline";
        pipelineDesc.vertexShader = m_metadataVertexShader;
        pipelineDesc.fragmentShader = m_metadataFragmentShader;
        pipelineDesc.layout = m_metadataPipelineLayout;
        pipelineDesc.raster.cullMode = RhiCullMode::None;
        pipelineDesc.depthStencil.depthTestEnabled = false;
        pipelineDesc.depthStencil.depthWriteEnabled = false;
        pipelineDesc.colorFormats.push_back(RhiTextureFormat::Rgba16Float);
        pipelineDesc.blend.attachments.push_back({});
        m_metadataPipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
        pipelineDesc.debugName = "SkyCapture.Raw.Pipeline";
        pipelineDesc.fragmentShader = m_rawFragmentShader;
        m_rawPipeline = rhiDevice.createGraphicsPipeline(pipelineDesc);
        if (!m_metadataVertexShader.isValid() || !m_metadataFragmentShader.isValid() ||
            !m_rawFragmentShader.isValid() || !m_rawPipeline.isValid() ||
            !m_metadataSampler.isValid() || !m_metadataBindGroupLayout.isValid() ||
            !m_metadataPipelineLayout.isValid() || !m_metadataPipeline.isValid()) {
            destroyMetadataResources();
            return false;
        }
    }
    if (m_metadataBindGroup.isValid() && sameView(m_boundAtmosphereLutView, atmosphereLutView)) return true;
    if (m_metadataBindGroup.isValid()) rhiDevice.destroyBindGroup(m_metadataBindGroup);
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_metadataBindGroupLayout;
    RhiBindGroupEntry entry;
    entry.binding = 0u;
    entry.resource.combinedTextureSampler = {atmosphereLutView, m_metadataSampler};
    bindGroupDesc.entries.push_back(entry);
    m_metadataBindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    m_boundAtmosphereLutView = atmosphereLutView;
    return m_metadataBindGroup.isValid();
}

void SkyCapturePass::destroyMetadataResources() {
    if (m_rhiDevice) {
        if (m_metadataBindGroup.isValid()) m_rhiDevice->destroyBindGroup(m_metadataBindGroup);
        if (m_rawPipeline.isValid()) m_rhiDevice->destroyPipeline(m_rawPipeline);
        if (m_metadataPipeline.isValid()) m_rhiDevice->destroyPipeline(m_metadataPipeline);
        if (m_metadataPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_metadataPipelineLayout);
        if (m_metadataBindGroupLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_metadataBindGroupLayout);
        if (m_metadataSampler.isValid()) m_rhiDevice->destroySampler(m_metadataSampler);
        if (m_metadataFragmentShader.isValid()) m_rhiDevice->destroyShader(m_metadataFragmentShader);
        if (m_rawFragmentShader.isValid()) m_rhiDevice->destroyShader(m_rawFragmentShader);
        if (m_metadataVertexShader.isValid()) m_rhiDevice->destroyShader(m_metadataVertexShader);
    }
    m_metadataBindGroup = {};
    m_rawPipeline = {};
    m_metadataPipeline = {};
    m_metadataPipelineLayout = {};
    m_metadataBindGroupLayout = {};
    m_metadataSampler = {};
    m_metadataFragmentShader = {};
    m_rawFragmentShader = {};
    m_metadataVertexShader = {};
    m_boundAtmosphereLutView = {};
    m_rhiDevice = nullptr;
}
