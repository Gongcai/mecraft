#include "TerrainRhiPipelineSet.h"

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiShaderSourceLoader.h"
#include "renderer/targets/DeferredRenderTargets.h"
#include "resource/ResourceMgr.h"
#include "resource/TextureAtlas.h"

#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <string>

namespace {
constexpr std::array<uint32_t, 7> kGBufferTextureBindings = {
    0u, 3u, 4u, 9u, 10u, 11u, 12u
};
constexpr std::array<uint32_t, 10> kTransparentTextureBindings = {
    0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 14u
};
constexpr std::array<uint32_t, 9> kWaterTextureBindings = {
    0u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u
};

template <typename Handle>
bool sameHandle(const Handle lhs, const Handle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

struct alignas(16) TerrainGBufferParams {
    glm::mat4 viewProj = glm::mat4(1.0f);
    glm::vec4 cameraAnimation = glm::vec4(0.0f);
    glm::vec4 surfaceParams = glm::vec4(0.0f);
    glm::ivec4 materialFlags = glm::ivec4(0);
    glm::ivec4 weatherFlags = glm::ivec4(0);
};

static_assert(sizeof(TerrainGBufferParams) == 128u,
              "Terrain GBuffer parameters must match the std140 shader block");

struct alignas(16) TerrainShadowParams {
    glm::mat4 modelView = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    glm::vec4 lightAnimation = glm::vec4(0.0f);
    glm::vec4 timePass = glm::vec4(0.0f);
};

static_assert(sizeof(TerrainShadowParams) == 160u,
              "Terrain shadow parameters must match the std140 shader block");

struct alignas(16) TerrainLitParams {
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 viewProj = glm::mat4(1.0f);
    glm::vec4 cameraAnimation = glm::vec4(0.0f);
    glm::vec4 fogColorStart = glm::vec4(0.0f);
    glm::vec4 fogParams = glm::vec4(0.0f);
    glm::vec4 sunLightColor = glm::vec4(0.0f);
    glm::vec4 skyAmbientColor = glm::vec4(0.0f);
    glm::vec4 shadowTintColor = glm::vec4(0.0f);
    glm::vec4 horizonScatterColor = glm::vec4(0.0f);
    glm::vec4 sunDirection = glm::vec4(0.0f);
    glm::vec4 moonDirection = glm::vec4(0.0f);
    glm::vec4 moonLightColor = glm::vec4(0.0f);
    glm::vec4 lightingParams0 = glm::vec4(0.0f);
    glm::vec4 lightingParams1 = glm::vec4(0.0f);
    glm::vec4 atmosphereParams0 = glm::vec4(0.0f);
    glm::vec4 atmosphereParams1 = glm::vec4(0.0f);
    glm::vec4 atmosphereParams2 = glm::vec4(0.0f);
    glm::vec4 atmosphereParams3 = glm::vec4(0.0f);
    glm::vec4 waterAbsorption = glm::vec4(1.0f);
    glm::vec4 waterLayers = glm::vec4(0.0f);
    glm::ivec4 controlFlags0 = glm::ivec4(0);
    glm::ivec4 controlFlags1 = glm::ivec4(0);
    glm::ivec4 controlFlags2 = glm::ivec4(0);
    glm::ivec4 waterFlags = glm::ivec4(0);
};

static_assert(sizeof(TerrainLitParams) == 480u,
              "Terrain lit parameters must match the std140 shader block");

struct alignas(16) TerrainWaterParams {
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 viewProj = glm::mat4(1.0f);
    glm::mat4 invViewProj = glm::mat4(1.0f);
    glm::vec4 cameraNear = glm::vec4(0.0f);
    glm::vec4 absorptionFar = glm::vec4(0.0f);
    glm::vec4 sunDirectionAnimation = glm::vec4(0.0f);
    glm::vec4 moonDirectionTime = glm::vec4(0.0f);
    glm::vec4 sunLightSkyIntensity = glm::vec4(0.0f);
    glm::vec4 moonLightVisibility = glm::vec4(0.0f);
    glm::vec4 skyAmbientWeather = glm::vec4(0.0f);
    glm::vec4 wetness = glm::vec4(0.0f);
    glm::vec4 waveParams = glm::vec4(0.0f);
    glm::vec4 waterLayers = glm::vec4(0.0f);
    glm::ivec4 controlFlags0 = glm::ivec4(0);
    glm::ivec4 controlFlags1 = glm::ivec4(0);
    glm::ivec4 controlFlags2 = glm::ivec4(0);
};

static_assert(sizeof(TerrainWaterParams) == 400u,
              "Terrain water parameters must match the std140 shader block");

struct alignas(16) TerrainForwardParams {
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 viewProj = glm::mat4(1.0f);
    glm::vec4 animationSky = glm::vec4(0.0f);
    glm::vec4 fogColorStart = glm::vec4(0.0f);
    glm::vec4 fogParams = glm::vec4(0.0f);
    glm::ivec4 controlFlags = glm::ivec4(0);
};

static_assert(sizeof(TerrainForwardParams) == 192u,
              "Terrain forward parameters must match the std140 shader block");

void setPackedTerrainVertexInput(RhiGraphicsPipelineDesc& pipelineDesc) {
    pipelineDesc.vertexInput.bindings.push_back({
        0u,
        static_cast<uint32_t>(sizeof(PackedBlockVertex)),
        RhiVertexInputRate::Vertex
    });
    for (uint32_t attribute = 0u; attribute < 4u; ++attribute) {
        pipelineDesc.vertexInput.attributes.push_back({
            11u + attribute,
            0u,
            RhiVertexFormat::Uint,
            attribute * static_cast<uint32_t>(sizeof(uint32_t))
        });
    }
}
} // namespace

TerrainRhiPipelineSet::TerrainRhiPipelineSet() = default;

TerrainRhiPipelineSet::~TerrainRhiPipelineSet() {
    shutdown();
}

void TerrainRhiPipelineSet::init(RhiDevice& rhiDevice) {
    if (m_rhiDevice != &rhiDevice) {
        shutdown();
        m_rhiDevice = &rhiDevice;
    }
}

void TerrainRhiPipelineSet::shutdown() {
    destroyForwardResources();
    destroyWaterResources();
    destroyTransparentResources();
    destroyShadowResources();
    destroyGBufferResources();
    m_rhiDevice = nullptr;
}

bool TerrainRhiPipelineSet::prepareGBuffer(RhiCommandList& commandList,
                                           ResourceMgr& resourceMgr,
                                           const TerrainFrameData& frame,
                                           const TerrainRenderSettings& settings) {
    if (m_rhiDevice == nullptr || !ensureGBufferPipeline(resourceMgr) ||
        !ensureGBufferTextureViews(resourceMgr) || !ensureGBufferBindGroups()) {
        return false;
    }

    const bool normalMapsEnabled = m_hasNormalMaps && settings.blockMaterialMapsEnabled &&
                                   settings.blockNormalMapsEnabled;
    const bool specularMapsEnabled = m_hasSpecularMaps && settings.blockMaterialMapsEnabled &&
                                     settings.blockSpecularMapsEnabled;
    const bool parallaxEnabled = normalMapsEnabled && settings.blockParallaxMapsEnabled &&
                                 settings.blockParallaxDepth > 0.0f;

    TerrainGBufferParams baseParams;
    baseParams.viewProj = frame.viewProj;
    baseParams.cameraAnimation = glm::vec4(frame.cameraPos, frame.animationTime);
    baseParams.surfaceParams = glm::vec4(
        frame.shaderTime,
        frame.surfaceWetness,
        parallaxEnabled ? settings.blockParallaxDepth : 0.0f,
        0.0f);
    baseParams.materialFlags = glm::ivec4(
        0,
        normalMapsEnabled ? 1 : 0,
        specularMapsEnabled ? 1 : 0,
        parallaxEnabled ? 1 : 0);
    baseParams.weatherFlags = glm::ivec4(
        settings.rainWetSurfacesEnabled ? 1 : 0,
        settings.rainSurfaceRipplesEnabled ? 1 : 0,
        0,
        0);

    TerrainGBufferParams cutoutParams = baseParams;
    cutoutParams.materialFlags.x = 1;
    commandList.bufferBarrier({m_gbufferParamsBuffers[0], RhiResourceState::UniformBuffer,
                               RhiResourceState::TransferDst});
    commandList.bufferBarrier({m_gbufferParamsBuffers[1], RhiResourceState::UniformBuffer,
                               RhiResourceState::TransferDst});
    commandList.updateBuffer(m_gbufferParamsBuffers[0], 0u, &baseParams, sizeof(baseParams));
    commandList.updateBuffer(m_gbufferParamsBuffers[1], 0u, &cutoutParams, sizeof(cutoutParams));
    commandList.bufferBarrier({m_gbufferParamsBuffers[0], RhiResourceState::TransferDst,
                               RhiResourceState::UniformBuffer});
    commandList.bufferBarrier({m_gbufferParamsBuffers[1], RhiResourceState::TransferDst,
                               RhiResourceState::UniformBuffer});
    return true;
}

bool TerrainRhiPipelineSet::prepareShadow(RhiCommandList& commandList,
                                          ResourceMgr& resourceMgr,
                                          const TerrainShadowFrameData& frame) {
    if (m_rhiDevice == nullptr || !ensureShadowPipeline(resourceMgr) ||
        !ensureShadowTextureViews(resourceMgr) || !ensureShadowBindGroup()) {
        return false;
    }

    TerrainShadowParams params;
    params.modelView = frame.modelView;
    params.projection = frame.projection;
    params.lightAnimation = glm::vec4(frame.lightDirection, frame.animationTime);
    params.timePass = glm::vec4(frame.shaderTime,
                                static_cast<float>(frame.passMode),
                                0.0f,
                                0.0f);
    commandList.bufferBarrier({m_shadowParamsBuffer, RhiResourceState::UniformBuffer,
                               RhiResourceState::TransferDst});
    commandList.updateBuffer(m_shadowParamsBuffer, 0u, &params, sizeof(params));
    commandList.bufferBarrier({m_shadowParamsBuffer, RhiResourceState::TransferDst,
                               RhiResourceState::UniformBuffer});
    return true;
}

bool TerrainRhiPipelineSet::prepareTransparent(
    RhiCommandList& commandList,
    ResourceMgr& resourceMgr,
    DeferredRenderTargets& targets,
    const TerrainFrameData& frame,
    const TerrainRenderSettings& settings,
    const int heldBlockLightValue,
    const bool volumetricFogShadersReady) {
    if (m_rhiDevice == nullptr || !ensureTransparentPipeline(resourceMgr) ||
        !ensureTransparentTextureViews(resourceMgr, targets) ||
        !ensureTransparentBindGroup()) {
        return false;
    }

    const bool volumetricFogActive =
        (settings.volumetricLightEnabled ||
         (settings.volumetricFogEnabled && settings.volumetricFogStrength > 0.001f)) &&
        volumetricFogShadersReady;

    TerrainLitParams params;
    params.view = frame.view;
    params.viewProj = frame.viewProj;
    params.cameraAnimation = glm::vec4(frame.cameraPos, frame.animationTime);
    params.fogColorStart = glm::vec4(frame.fog.color, frame.fog.start);
    params.fogParams = glm::vec4(
        frame.fog.end,
        frame.fog.density,
        frame.skyLighting.skyIntensity,
        frame.skyLighting.moonVisibility);
    params.sunLightColor = glm::vec4(frame.skyLighting.sunLightColor, 0.0f);
    params.skyAmbientColor = glm::vec4(frame.skyLighting.skyAmbientColor, 0.0f);
    params.shadowTintColor = glm::vec4(frame.skyLighting.shadowTintColor, 0.0f);
    params.horizonScatterColor = glm::vec4(frame.skyLighting.horizonScatterColor, 0.0f);
    params.sunDirection = glm::vec4(frame.skyLighting.sunDirection, 0.0f);
    params.moonDirection = glm::vec4(frame.skyLighting.moonDirection, 0.0f);
    params.moonLightColor = glm::vec4(
        frame.skyLighting.moonLightColor,
        frame.skyLighting.moonPhaseFlux);
    params.lightingParams0 = glm::vec4(
        settings.directSunStrength,
        settings.skyAmbientStrength,
        settings.weatherSkylightScale,
        settings.minimumAmbient);
    params.lightingParams1 = glm::vec4(
        settings.blockLightStrength,
        settings.fakeBounceStrength,
        settings.albedoDesaturation,
        settings.shadowDesaturation);
    params.atmosphereParams0 = glm::vec4(
        frame.atmosphere.sunWarmth,
        frame.atmosphere.skyCoolness,
        frame.atmosphere.aerialStrength,
        frame.atmosphere.horizonScatterStrength);
    params.atmosphereParams1 = glm::vec4(
        frame.atmosphere.weatherWetness,
        frame.atmosphere.weatherStorm,
        frame.atmosphere.aerialReduction,
        frame.atmosphere.lightningFlash);
    params.atmosphereParams2 = glm::vec4(
        frame.atmosphere.surfaceWetness,
        frame.atmosphere.skyWetness,
        frame.atmosphere.fogWetness,
        frame.atmosphere.cloudWetness);
    params.atmosphereParams3 = glm::vec4(frame.atmosphere.precipitation, 0.0f, 0.0f, 0.0f);
    params.waterAbsorption = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    params.controlFlags0 = glm::ivec4(1, 1, 0, 1);
    params.controlFlags1 = glm::ivec4(
        1,
        frame.fog.enabled ? 1 : 0,
        frame.fog.mode,
        0);
    params.controlFlags2 = glm::ivec4(
        settings.aerialPerspectiveEnabled ? 1 : 0,
        settings.volumetricLightEnabled ? 1 : 0,
        volumetricFogActive ? 1 : 0,
        heldBlockLightValue);
    commandList.bufferBarrier({m_transparentParamsBuffer, RhiResourceState::UniformBuffer,
                               RhiResourceState::TransferDst});
    commandList.updateBuffer(m_transparentParamsBuffer, 0u, &params, sizeof(params));
    commandList.bufferBarrier({m_transparentParamsBuffer, RhiResourceState::TransferDst,
                               RhiResourceState::UniformBuffer});
    return true;
}

bool TerrainRhiPipelineSet::prepareWater(
    RhiCommandList& commandList,
    ResourceMgr& resourceMgr,
    DeferredRenderTargets& targets,
    const TerrainWaterFrameData& frame) {
    if (m_rhiDevice == nullptr || !ensureWaterPipeline(resourceMgr) ||
        !ensureWaterTextureViews(resourceMgr, targets) || !ensureWaterBindGroup()) {
        return false;
    }

    const TextureAnimationInfo still = resourceMgr.getTextureAnimation("water_still");
    const TextureAnimationInfo flow = resourceMgr.getTextureAnimation("water_flow");

    TerrainWaterParams params;
    params.view = frame.view;
    params.viewProj = frame.viewProj;
    params.invViewProj = frame.invViewProj;
    params.cameraNear = glm::vec4(frame.cameraPos, frame.nearPlane);
    params.absorptionFar = glm::vec4(0.4f, 0.14f, 0.08f, frame.farPlane);
    params.sunDirectionAnimation = glm::vec4(frame.sunDirection, frame.animationTime);
    params.moonDirectionTime = glm::vec4(frame.moonDirection, frame.shaderTime);
    params.sunLightSkyIntensity = glm::vec4(frame.sunLightColor, frame.skyIntensity);
    params.moonLightVisibility = glm::vec4(frame.moonLightColor, frame.moonVisibility);
    params.skyAmbientWeather = glm::vec4(frame.skyAmbientColor, frame.weatherWetness);
    params.wetness = glm::vec4(
        frame.skyWetness,
        frame.fogWetness,
        frame.cloudWetness,
        frame.surfaceWetness);
    params.waveParams = glm::vec4(1.0f, 1.0f, 1.33f, frame.moonPhaseFlux);
    params.waterLayers = glm::vec4(
        static_cast<float>(still.firstLayer),
        static_cast<float>(std::max(1, still.frameCount)),
        static_cast<float>(flow.firstLayer),
        static_cast<float>(std::max(1, flow.frameCount)));
    params.controlFlags0 = glm::ivec4(
        frame.skyCaptureEnabled ? 1 : 0,
        frame.compositeInputsEnabled ? 1 : 0,
        frame.compositeInputsEnabled ? 1 : 0,
        frame.depthSofteningEnabled ? 1 : 0);
    params.controlFlags1 = glm::ivec4(
        frame.volumetricFogActive ? 1 : 0,
        static_cast<int>(frame.frameIndex & 0x7fffffffULL),
        frame.freezeBias ? 1 : 0,
        frame.rainSurfaceRipplesEnabled ? 1 : 0);
    params.controlFlags2 = glm::ivec4(frame.eyeInWater ? 1 : 0, 0, 0, 0);
    commandList.bufferBarrier({m_waterParamsBuffer, RhiResourceState::UniformBuffer,
                               RhiResourceState::TransferDst});
    commandList.updateBuffer(m_waterParamsBuffer, 0u, &params, sizeof(params));
    commandList.bufferBarrier({m_waterParamsBuffer, RhiResourceState::TransferDst,
                               RhiResourceState::UniformBuffer});
    return true;
}

bool TerrainRhiPipelineSet::prepareForward(RhiCommandList& commandList,
                                           ResourceMgr& resourceMgr,
                                           const TerrainFrameData& frame) {
    if (m_rhiDevice == nullptr || !ensureForwardPipeline(resourceMgr) ||
        !ensureForwardTextureViews(resourceMgr) || !ensureForwardBindGroups()) {
        return false;
    }

    TerrainForwardParams baseParams;
    baseParams.view = frame.view;
    baseParams.viewProj = frame.viewProj;
    baseParams.animationSky = glm::vec4(
        frame.animationTime,
        frame.skyLighting.skyIntensity,
        0.0f,
        0.0f);
    baseParams.fogColorStart = glm::vec4(frame.fog.color, frame.fog.start);
    baseParams.fogParams = glm::vec4(frame.fog.end, frame.fog.density, 0.0f, 0.0f);
    baseParams.controlFlags = glm::ivec4(
        0,
        frame.fog.enabled ? 1 : 0,
        frame.fog.mode,
        0);
    TerrainForwardParams transparentParams = baseParams;
    transparentParams.controlFlags.x = 1;
    commandList.bufferBarrier({m_forwardParamsBuffers[0], RhiResourceState::UniformBuffer,
                               RhiResourceState::TransferDst});
    commandList.bufferBarrier({m_forwardParamsBuffers[1], RhiResourceState::UniformBuffer,
                               RhiResourceState::TransferDst});
    commandList.updateBuffer(m_forwardParamsBuffers[0], 0u, &baseParams, sizeof(baseParams));
    commandList.updateBuffer(m_forwardParamsBuffers[1], 0u, &transparentParams, sizeof(transparentParams));
    commandList.bufferBarrier({m_forwardParamsBuffers[0], RhiResourceState::TransferDst,
                               RhiResourceState::UniformBuffer});
    commandList.bufferBarrier({m_forwardParamsBuffers[1], RhiResourceState::TransferDst,
                               RhiResourceState::UniformBuffer});
    return true;
}

bool TerrainRhiPipelineSet::ensureGBufferPipeline(ResourceMgr& resourceMgr) {
    const bool hasNormalMaps = resourceMgr.hasBlockNormalMaps();
    const bool hasSpecularMaps = resourceMgr.hasBlockSpecularMaps();
    const float anisotropy = std::max(1.0f, resourceMgr.getAtlasAnisotropy());
    if (m_gbufferOpaquePipeline.isValid() && m_gbufferCutoutPipeline.isValid() &&
        hasNormalMaps == m_hasNormalMaps && hasSpecularMaps == m_hasSpecularMaps &&
        anisotropy == m_samplerAnisotropy) {
        return true;
    }

    destroyGBufferResources();
    if (m_rhiDevice == nullptr) {
        return false;
    }
    m_hasNormalMaps = hasNormalMaps;
    m_hasSpecularMaps = hasSpecularMaps;
    m_samplerAnisotropy = anisotropy;

    renderer::rhi::RhiShaderSourceOptions sourceOptions;
    sourceOptions.preprocessorDefinitions.push_back("RHI_TERRAIN_MDI");
    if (m_hasNormalMaps) {
        sourceOptions.preprocessorDefinitions.push_back("RHI_TERRAIN_NORMAL_MAPS");
    }
    if (m_hasSpecularMaps) {
        sourceOptions.preprocessorDefinitions.push_back("RHI_TERRAIN_SPECULAR_MAPS");
    }
    const std::optional<std::string> vertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/chunk_gbuffer.vert",
        sourceOptions);
    const std::optional<std::string> fragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/chunk_gbuffer.frag",
        sourceOptions);
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        destroyGBufferResources();
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "Terrain.GBuffer.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_gbufferVertexShader = m_rhiDevice->createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "Terrain.GBuffer.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_gbufferFragmentShader = m_rhiDevice->createShader(fragmentDesc);
    if (!m_gbufferVertexShader.isValid() || !m_gbufferFragmentShader.isValid()) {
        destroyGBufferResources();
        return false;
    }

    RhiBufferDesc paramsDesc;
    paramsDesc.debugName = "Terrain.GBuffer.Params";
    paramsDesc.size = sizeof(TerrainGBufferParams);
    paramsDesc.usage = rhiFlag(RhiBufferUsage::Uniform) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    paramsDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    paramsDesc.initialState = RhiResourceState::UniformBuffer;
    paramsDesc.memoryCategory = RhiMemoryCategory::Uniform;
    for (RhiBufferHandle& buffer : m_gbufferParamsBuffers) {
        buffer = m_rhiDevice->createBuffer(paramsDesc, nullptr, 0u);
    }
    if (!m_gbufferParamsBuffers[0].isValid() || !m_gbufferParamsBuffers[1].isValid()) {
        destroyGBufferResources();
        return false;
    }

    RhiSamplerDesc blockSamplerDesc;
    blockSamplerDesc.minFilter = RhiFilter::Nearest;
    blockSamplerDesc.magFilter = RhiFilter::Nearest;
    blockSamplerDesc.mipmapMode = RhiMipmapMode::Linear;
    blockSamplerDesc.addressU = RhiAddressMode::Repeat;
    blockSamplerDesc.addressV = RhiAddressMode::Repeat;
    blockSamplerDesc.addressW = RhiAddressMode::Repeat;
    blockSamplerDesc.maxAnisotropy = m_samplerAnisotropy;
    m_blockSampler = m_rhiDevice->createSampler(blockSamplerDesc);

    RhiSamplerDesc linearClampDesc;
    linearClampDesc.minFilter = RhiFilter::Linear;
    linearClampDesc.magFilter = RhiFilter::Linear;
    linearClampDesc.mipmapMode = RhiMipmapMode::Nearest;
    m_linearClampSampler = m_rhiDevice->createSampler(linearClampDesc);

    RhiSamplerDesc linearRepeatDesc = linearClampDesc;
    linearRepeatDesc.addressU = RhiAddressMode::Repeat;
    linearRepeatDesc.addressV = RhiAddressMode::Repeat;
    linearRepeatDesc.addressW = RhiAddressMode::Repeat;
    m_linearRepeatSampler = m_rhiDevice->createSampler(linearRepeatDesc);
    if (!m_blockSampler.isValid() || !m_linearClampSampler.isValid() ||
        !m_linearRepeatSampler.isValid()) {
        destroyGBufferResources();
        return false;
    }

    RhiBindGroupLayoutDesc metadataLayoutDesc;
    metadataLayoutDesc.debugName = "Terrain.MetadataLayout";
    metadataLayoutDesc.entries.push_back({
        0u,
        RhiBindingType::StorageBuffer,
        rhiFlag(RhiShaderStage::Vertex),
        1u
    });
    m_metadataLayout = m_rhiDevice->createBindGroupLayout(metadataLayoutDesc);

    RhiBindGroupLayoutDesc materialLayoutDesc;
    materialLayoutDesc.debugName = "Terrain.GBufferMaterialLayout";
    for (size_t slot = 0u; slot < 5u; ++slot) {
        materialLayoutDesc.entries.push_back({
            kGBufferTextureBindings[slot],
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    if (m_hasNormalMaps) {
        materialLayoutDesc.entries.push_back({
            11u,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    if (m_hasSpecularMaps) {
        materialLayoutDesc.entries.push_back({
            12u,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    materialLayoutDesc.entries.push_back({
        13u,
        RhiBindingType::UniformBuffer,
        rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    m_gbufferMaterialLayout = m_rhiDevice->createBindGroupLayout(materialLayoutDesc);
    if (!m_metadataLayout.isValid() || !m_gbufferMaterialLayout.isValid()) {
        destroyGBufferResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "Terrain.GBufferPipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_metadataLayout);
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_gbufferMaterialLayout);
    m_gbufferPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    if (!m_gbufferPipelineLayout.isValid()) {
        destroyGBufferResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = m_gbufferVertexShader;
    pipelineDesc.fragmentShader = m_gbufferFragmentShader;
    pipelineDesc.layout = m_gbufferPipelineLayout;
    setPackedTerrainVertexInput(pipelineDesc);
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::Less;
    pipelineDesc.colorFormats = {
        RhiTextureFormat::Rgba8Unorm,
        RhiTextureFormat::Rgb10A2Unorm,
        RhiTextureFormat::Rg8Unorm,
        RhiTextureFormat::Rgba8Unorm,
        RhiTextureFormat::Rgba8Unorm
    };
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    pipelineDesc.debugName = "Terrain.GBuffer.OpaquePipeline";
    pipelineDesc.raster.cullMode = RhiCullMode::Back;
    m_gbufferOpaquePipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    pipelineDesc.debugName = "Terrain.GBuffer.CutoutPipeline";
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    m_gbufferCutoutPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    if (!m_gbufferOpaquePipeline.isValid() || !m_gbufferCutoutPipeline.isValid()) {
        destroyGBufferResources();
        return false;
    }
    return true;
}

bool TerrainRhiPipelineSet::ensureGBufferTextureViews(ResourceMgr& resourceMgr) {
    const TextureArray& albedo = resourceMgr.getTextureArray();
    if (!ensureTextureView(0u, albedo.texture, RhiTextureViewType::Texture2DArray) ||
        !ensureTextureView(1u, resourceMgr.getGrassColormap(), RhiTextureViewType::Texture2D) ||
        !ensureTextureView(2u, resourceMgr.getFoliageColormap(), RhiTextureViewType::Texture2D) ||
        !ensureTextureView(3u, resourceMgr.getTexture2DHandle("shader_noise2d"), RhiTextureViewType::Texture2D) ||
        !ensureTextureView(4u, resourceMgr.getTexture2DHandle("shader_ripple_normal"), RhiTextureViewType::Texture2D)) {
        return false;
    }
    if (m_hasNormalMaps &&
        !ensureTextureView(5u,
                           resourceMgr.getBlockNormalTextureArray().texture,
                           RhiTextureViewType::Texture2DArray)) {
        return false;
    }
    if (m_hasSpecularMaps &&
        !ensureTextureView(6u,
                           resourceMgr.getBlockSpecularTextureArray().texture,
                           RhiTextureViewType::Texture2DArray)) {
        return false;
    }
    return true;
}

bool TerrainRhiPipelineSet::ensureGBufferBindGroups() {
    bool viewsChanged = false;
    for (size_t slot = 0u; slot < 5u; ++slot) {
        viewsChanged = viewsChanged || !sameHandle(m_gbufferBoundViews[slot], m_gbufferTextureViews[slot]);
    }
    if (m_hasNormalMaps) {
        viewsChanged = viewsChanged || !sameHandle(m_gbufferBoundViews[5], m_gbufferTextureViews[5]);
    }
    if (m_hasSpecularMaps) {
        viewsChanged = viewsChanged || !sameHandle(m_gbufferBoundViews[6], m_gbufferTextureViews[6]);
    }
    if (m_gbufferBindGroups[0].isValid() && m_gbufferBindGroups[1].isValid() && !viewsChanged) {
        return true;
    }

    destroyGBufferBindGroups();
    for (size_t pass = 0u; pass < m_gbufferBindGroups.size(); ++pass) {
        RhiBindGroupDesc desc;
        desc.layout = m_gbufferMaterialLayout;
        for (size_t slot = 0u; slot < 5u; ++slot) {
            RhiBindGroupEntry entry;
            entry.binding = kGBufferTextureBindings[slot];
            entry.resource.combinedTextureSampler.textureView = m_gbufferTextureViews[slot];
            entry.resource.combinedTextureSampler.sampler = slot == 0u
                ? m_blockSampler
                : (slot <= 2u ? m_linearClampSampler : m_linearRepeatSampler);
            desc.entries.push_back(entry);
        }
        if (m_hasNormalMaps) {
            RhiBindGroupEntry entry;
            entry.binding = 11u;
            entry.resource.combinedTextureSampler.textureView = m_gbufferTextureViews[5];
            entry.resource.combinedTextureSampler.sampler = m_blockSampler;
            desc.entries.push_back(entry);
        }
        if (m_hasSpecularMaps) {
            RhiBindGroupEntry entry;
            entry.binding = 12u;
            entry.resource.combinedTextureSampler.textureView = m_gbufferTextureViews[6];
            entry.resource.combinedTextureSampler.sampler = m_blockSampler;
            desc.entries.push_back(entry);
        }
        RhiBindGroupEntry paramsEntry;
        paramsEntry.binding = 13u;
        paramsEntry.resource.buffer.buffer = m_gbufferParamsBuffers[pass];
        paramsEntry.resource.buffer.range = sizeof(TerrainGBufferParams);
        desc.entries.push_back(paramsEntry);
        m_gbufferBindGroups[pass] = m_rhiDevice->createBindGroup(desc);
    }
    if (!m_gbufferBindGroups[0].isValid() || !m_gbufferBindGroups[1].isValid()) {
        destroyGBufferBindGroups();
        return false;
    }
    m_gbufferBoundViews = m_gbufferTextureViews;
    return true;
}

bool TerrainRhiPipelineSet::ensureTextureView(const size_t slot,
                                              const RhiTextureHandle texture,
                                              const RhiTextureViewType viewType) {
    if (m_rhiDevice == nullptr || slot >= m_gbufferTextureViews.size() || !texture.isValid()) {
        return false;
    }
    if (sameHandle(m_gbufferViewTextures[slot], texture) && m_gbufferTextureViews[slot].isValid()) {
        return true;
    }
    if (m_gbufferTextureViews[slot].isValid()) {
        m_rhiDevice->destroyTextureView(m_gbufferTextureViews[slot]);
    }

    RhiTextureViewDesc desc;
    desc.texture = texture;
    desc.viewType = viewType;
    desc.format = RhiTextureFormat::Rgba8Unorm;
    desc.mipCount = kRhiRemainingMipLevels;
    desc.layerCount = kRhiRemainingArrayLayers;
    m_gbufferTextureViews[slot] = m_rhiDevice->createTextureView(desc);
    if (!m_gbufferTextureViews[slot].isValid()) {
        std::cerr << "TerrainRhiPipelineSet: failed to create GBuffer texture view"
                  << " slot=" << slot
                  << " texture=" << texture.index << ':' << texture.generation
                  << " viewType=" << static_cast<uint32_t>(viewType) << '\n';
        m_gbufferViewTextures[slot] = {};
        return false;
    }
    m_gbufferViewTextures[slot] = texture;
    return true;
}

bool TerrainRhiPipelineSet::ensureForwardPipeline(ResourceMgr& resourceMgr) {
    const float anisotropy = std::max(1.0f, resourceMgr.getAtlasAnisotropy());
    if (m_forwardOpaquePipeline.isValid() && m_forwardCutoutPipeline.isValid() &&
        m_forwardTransparentPipeline.isValid() && anisotropy == m_forwardSamplerAnisotropy) {
        return true;
    }

    destroyForwardResources();
    if (m_rhiDevice == nullptr) {
        return false;
    }
    m_forwardSamplerAnisotropy = anisotropy;

    renderer::rhi::RhiShaderSourceOptions sourceOptions;
    sourceOptions.preprocessorDefinitions.push_back("RHI_TERRAIN_FORWARD_MDI");
    const std::optional<std::string> vertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/chunk_lit.vert",
        sourceOptions);
    const std::optional<std::string> fragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/forward_basic_terrain.frag",
        sourceOptions);
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        destroyForwardResources();
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "Terrain.Forward.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_forwardVertexShader = m_rhiDevice->createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "Terrain.Forward.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_forwardFragmentShader = m_rhiDevice->createShader(fragmentDesc);
    if (!m_forwardVertexShader.isValid() || !m_forwardFragmentShader.isValid()) {
        destroyForwardResources();
        return false;
    }

    RhiBufferDesc paramsDesc;
    paramsDesc.debugName = "Terrain.Forward.Params";
    paramsDesc.size = sizeof(TerrainForwardParams);
    paramsDesc.usage = rhiFlag(RhiBufferUsage::Uniform) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    paramsDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    paramsDesc.initialState = RhiResourceState::UniformBuffer;
    paramsDesc.memoryCategory = RhiMemoryCategory::Uniform;
    for (RhiBufferHandle& buffer : m_forwardParamsBuffers) {
        buffer = m_rhiDevice->createBuffer(paramsDesc, nullptr, 0u);
    }
    if (!m_forwardParamsBuffers[0].isValid() || !m_forwardParamsBuffers[1].isValid()) {
        destroyForwardResources();
        return false;
    }

    RhiSamplerDesc blockSamplerDesc;
    blockSamplerDesc.minFilter = RhiFilter::Nearest;
    blockSamplerDesc.magFilter = RhiFilter::Nearest;
    blockSamplerDesc.mipmapMode = RhiMipmapMode::Linear;
    blockSamplerDesc.addressU = RhiAddressMode::Repeat;
    blockSamplerDesc.addressV = RhiAddressMode::Repeat;
    blockSamplerDesc.addressW = RhiAddressMode::Repeat;
    blockSamplerDesc.maxAnisotropy = m_forwardSamplerAnisotropy;
    m_forwardBlockSampler = m_rhiDevice->createSampler(blockSamplerDesc);

    RhiSamplerDesc linearClampDesc;
    linearClampDesc.minFilter = RhiFilter::Linear;
    linearClampDesc.magFilter = RhiFilter::Linear;
    linearClampDesc.mipmapMode = RhiMipmapMode::Nearest;
    m_forwardLinearClampSampler = m_rhiDevice->createSampler(linearClampDesc);
    if (!m_forwardBlockSampler.isValid() || !m_forwardLinearClampSampler.isValid()) {
        destroyForwardResources();
        return false;
    }

    RhiBindGroupLayoutDesc metadataLayoutDesc;
    metadataLayoutDesc.debugName = "Terrain.ForwardMetadataLayout";
    metadataLayoutDesc.entries.push_back({
        0u,
        RhiBindingType::StorageBuffer,
        rhiFlag(RhiShaderStage::Vertex),
        1u
    });
    m_forwardMetadataLayout = m_rhiDevice->createBindGroupLayout(metadataLayoutDesc);

    RhiBindGroupLayoutDesc materialLayoutDesc;
    materialLayoutDesc.debugName = "Terrain.ForwardMaterialLayout";
    for (uint32_t binding = 0u; binding < 5u; ++binding) {
        materialLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    materialLayoutDesc.entries.push_back({
        5u,
        RhiBindingType::UniformBuffer,
        rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    m_forwardMaterialLayout = m_rhiDevice->createBindGroupLayout(materialLayoutDesc);
    if (!m_forwardMetadataLayout.isValid() || !m_forwardMaterialLayout.isValid()) {
        destroyForwardResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "Terrain.ForwardPipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_forwardMetadataLayout);
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_forwardMaterialLayout);
    m_forwardPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    if (!m_forwardPipelineLayout.isValid()) {
        destroyForwardResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = m_forwardVertexShader;
    pipelineDesc.fragmentShader = m_forwardFragmentShader;
    pipelineDesc.layout = m_forwardPipelineLayout;
    setPackedTerrainVertexInput(pipelineDesc);
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::Less;
    pipelineDesc.colorFormats = {RhiTextureFormat::Rgba16Float};
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.debugName = "Terrain.Forward.OpaquePipeline";
    m_forwardOpaquePipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);

    pipelineDesc.debugName = "Terrain.Forward.CutoutPipeline";
    m_forwardCutoutPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);

    pipelineDesc.depthStencil.depthWriteEnabled = false;
    RhiBlendAttachmentState blend;
    blend.blendEnabled = true;
    blend.srcColor = RhiBlendFactor::SrcAlpha;
    blend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    blend.srcAlpha = RhiBlendFactor::SrcAlpha;
    blend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    pipelineDesc.blend.attachments.push_back(blend);
    pipelineDesc.debugName = "Terrain.Forward.TransparentPipeline";
    m_forwardTransparentPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    if (!m_forwardOpaquePipeline.isValid() || !m_forwardCutoutPipeline.isValid() ||
        !m_forwardTransparentPipeline.isValid()) {
        destroyForwardResources();
        return false;
    }
    return true;
}

bool TerrainRhiPipelineSet::ensureForwardTextureViews(ResourceMgr& resourceMgr) {
    return ensureForwardTextureView(
               0u,
               resourceMgr.getTextureArray().texture,
               RhiTextureViewType::Texture2DArray) &&
           ensureForwardTextureView(
               1u,
               resourceMgr.getLightmapDay(),
               RhiTextureViewType::Texture2D) &&
           ensureForwardTextureView(
               2u,
               resourceMgr.getLightmapNight(),
               RhiTextureViewType::Texture2D) &&
           ensureForwardTextureView(
               3u,
               resourceMgr.getGrassColormap(),
               RhiTextureViewType::Texture2D) &&
           ensureForwardTextureView(
               4u,
               resourceMgr.getFoliageColormap(),
               RhiTextureViewType::Texture2D);
}

bool TerrainRhiPipelineSet::ensureForwardBindGroups() {
    bool viewsChanged = false;
    for (size_t slot = 0u; slot < m_forwardTextureViews.size(); ++slot) {
        viewsChanged = viewsChanged || !sameHandle(m_forwardBoundViews[slot], m_forwardTextureViews[slot]);
    }
    if (m_forwardBindGroups[0].isValid() && m_forwardBindGroups[1].isValid() && !viewsChanged) {
        return true;
    }

    destroyForwardBindGroups();
    for (size_t pass = 0u; pass < m_forwardBindGroups.size(); ++pass) {
        RhiBindGroupDesc desc;
        desc.layout = m_forwardMaterialLayout;
        for (size_t slot = 0u; slot < m_forwardTextureViews.size(); ++slot) {
            RhiBindGroupEntry entry;
            entry.binding = static_cast<uint32_t>(slot);
            entry.resource.combinedTextureSampler.textureView = m_forwardTextureViews[slot];
            entry.resource.combinedTextureSampler.sampler = slot == 0u
                ? m_forwardBlockSampler
                : m_forwardLinearClampSampler;
            desc.entries.push_back(entry);
        }
        RhiBindGroupEntry paramsEntry;
        paramsEntry.binding = 5u;
        paramsEntry.resource.buffer.buffer = m_forwardParamsBuffers[pass];
        paramsEntry.resource.buffer.range = sizeof(TerrainForwardParams);
        desc.entries.push_back(paramsEntry);
        m_forwardBindGroups[pass] = m_rhiDevice->createBindGroup(desc);
    }
    if (!m_forwardBindGroups[0].isValid() || !m_forwardBindGroups[1].isValid()) {
        destroyForwardBindGroups();
        return false;
    }
    m_forwardBoundViews = m_forwardTextureViews;
    return true;
}

bool TerrainRhiPipelineSet::ensureForwardTextureView(
    const size_t slot,
    const RhiTextureHandle texture,
    const RhiTextureViewType viewType) {
    if (m_rhiDevice == nullptr || slot >= m_forwardTextureViews.size() || !texture.isValid()) {
        return false;
    }
    if (sameHandle(m_forwardViewTextures[slot], texture) && m_forwardTextureViews[slot].isValid()) {
        return true;
    }
    if (m_forwardTextureViews[slot].isValid()) {
        m_rhiDevice->destroyTextureView(m_forwardTextureViews[slot]);
    }

    RhiTextureViewDesc desc;
    desc.texture = texture;
    desc.viewType = viewType;
    desc.format = RhiTextureFormat::Rgba8Unorm;
    desc.mipCount = kRhiRemainingMipLevels;
    desc.layerCount = kRhiRemainingArrayLayers;
    m_forwardTextureViews[slot] = m_rhiDevice->createTextureView(desc);
    if (!m_forwardTextureViews[slot].isValid()) {
        m_forwardViewTextures[slot] = {};
        return false;
    }
    m_forwardViewTextures[slot] = texture;
    return true;
}

void TerrainRhiPipelineSet::destroyForwardBindGroups() {
    if (m_rhiDevice != nullptr) {
        for (RhiBindGroupHandle& bindGroup : m_forwardBindGroups) {
            if (bindGroup.isValid()) {
                m_rhiDevice->destroyBindGroup(bindGroup);
            }
            bindGroup = {};
        }
    }
    m_forwardBoundViews = {};
}

void TerrainRhiPipelineSet::destroyForwardTextureViews() {
    if (m_rhiDevice != nullptr) {
        for (RhiTextureViewHandle& view : m_forwardTextureViews) {
            if (view.isValid()) {
                m_rhiDevice->destroyTextureView(view);
            }
            view = {};
        }
    }
    m_forwardViewTextures = {};
}

void TerrainRhiPipelineSet::destroyForwardResources() {
    destroyForwardBindGroups();
    destroyForwardTextureViews();
    if (m_rhiDevice != nullptr) {
        if (m_forwardOpaquePipeline.isValid()) m_rhiDevice->destroyPipeline(m_forwardOpaquePipeline);
        if (m_forwardCutoutPipeline.isValid()) m_rhiDevice->destroyPipeline(m_forwardCutoutPipeline);
        if (m_forwardTransparentPipeline.isValid()) m_rhiDevice->destroyPipeline(m_forwardTransparentPipeline);
        if (m_forwardPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_forwardPipelineLayout);
        if (m_forwardMaterialLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_forwardMaterialLayout);
        if (m_forwardMetadataLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_forwardMetadataLayout);
        if (m_forwardFragmentShader.isValid()) m_rhiDevice->destroyShader(m_forwardFragmentShader);
        if (m_forwardVertexShader.isValid()) m_rhiDevice->destroyShader(m_forwardVertexShader);
        for (RhiBufferHandle& buffer : m_forwardParamsBuffers) {
            if (buffer.isValid()) m_rhiDevice->destroyBuffer(buffer);
            buffer = {};
        }
        if (m_forwardBlockSampler.isValid()) m_rhiDevice->destroySampler(m_forwardBlockSampler);
        if (m_forwardLinearClampSampler.isValid()) m_rhiDevice->destroySampler(m_forwardLinearClampSampler);
    }
    m_forwardOpaquePipeline = {};
    m_forwardCutoutPipeline = {};
    m_forwardTransparentPipeline = {};
    m_forwardPipelineLayout = {};
    m_forwardMaterialLayout = {};
    m_forwardMetadataLayout = {};
    m_forwardFragmentShader = {};
    m_forwardVertexShader = {};
    m_forwardBlockSampler = {};
    m_forwardLinearClampSampler = {};
    m_forwardSamplerAnisotropy = 1.0f;
}

bool TerrainRhiPipelineSet::ensureWaterPipeline(ResourceMgr& resourceMgr) {
    const float anisotropy = std::max(1.0f, resourceMgr.getAtlasAnisotropy());
    if (m_waterPipeline.isValid() && anisotropy == m_waterSamplerAnisotropy) {
        return true;
    }

    destroyWaterResources();
    if (m_rhiDevice == nullptr) {
        return false;
    }
    m_waterSamplerAnisotropy = anisotropy;

    renderer::rhi::RhiShaderSourceOptions sourceOptions;
    sourceOptions.preprocessorDefinitions.push_back("RHI_TERRAIN_WATER_MDI");
    const std::optional<std::string> vertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/chunk_lit.vert",
        sourceOptions);
    const std::optional<std::string> fragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/water_composite.frag",
        sourceOptions);
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        destroyWaterResources();
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "Terrain.Water.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_waterVertexShader = m_rhiDevice->createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "Terrain.Water.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_waterFragmentShader = m_rhiDevice->createShader(fragmentDesc);
    if (!m_waterVertexShader.isValid() || !m_waterFragmentShader.isValid()) {
        destroyWaterResources();
        return false;
    }

    RhiBufferDesc paramsDesc;
    paramsDesc.debugName = "Terrain.Water.Params";
    paramsDesc.size = sizeof(TerrainWaterParams);
    paramsDesc.usage = rhiFlag(RhiBufferUsage::Uniform) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    paramsDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    paramsDesc.initialState = RhiResourceState::UniformBuffer;
    paramsDesc.memoryCategory = RhiMemoryCategory::Uniform;
    m_waterParamsBuffer = m_rhiDevice->createBuffer(paramsDesc, nullptr, 0u);
    if (!m_waterParamsBuffer.isValid()) {
        destroyWaterResources();
        return false;
    }

    RhiSamplerDesc blockSamplerDesc;
    blockSamplerDesc.minFilter = RhiFilter::Nearest;
    blockSamplerDesc.magFilter = RhiFilter::Nearest;
    blockSamplerDesc.mipmapMode = RhiMipmapMode::Linear;
    blockSamplerDesc.addressU = RhiAddressMode::Repeat;
    blockSamplerDesc.addressV = RhiAddressMode::Repeat;
    blockSamplerDesc.addressW = RhiAddressMode::Repeat;
    blockSamplerDesc.maxAnisotropy = m_waterSamplerAnisotropy;
    m_waterBlockSampler = m_rhiDevice->createSampler(blockSamplerDesc);

    RhiSamplerDesc linearClampDesc;
    linearClampDesc.minFilter = RhiFilter::Linear;
    linearClampDesc.magFilter = RhiFilter::Linear;
    linearClampDesc.mipmapMode = RhiMipmapMode::Nearest;
    m_waterLinearClampSampler = m_rhiDevice->createSampler(linearClampDesc);

    RhiSamplerDesc linearRepeatDesc = linearClampDesc;
    linearRepeatDesc.addressU = RhiAddressMode::Repeat;
    linearRepeatDesc.addressV = RhiAddressMode::Repeat;
    linearRepeatDesc.addressW = RhiAddressMode::Repeat;
    m_waterLinearRepeatSampler = m_rhiDevice->createSampler(linearRepeatDesc);

    RhiSamplerDesc nearestClampDesc;
    nearestClampDesc.minFilter = RhiFilter::Nearest;
    nearestClampDesc.magFilter = RhiFilter::Nearest;
    nearestClampDesc.mipmapMode = RhiMipmapMode::Nearest;
    m_waterNearestClampSampler = m_rhiDevice->createSampler(nearestClampDesc);
    if (!m_waterBlockSampler.isValid() || !m_waterLinearClampSampler.isValid() ||
        !m_waterLinearRepeatSampler.isValid() || !m_waterNearestClampSampler.isValid()) {
        destroyWaterResources();
        return false;
    }

    RhiBindGroupLayoutDesc metadataLayoutDesc;
    metadataLayoutDesc.debugName = "Terrain.WaterMetadataLayout";
    metadataLayoutDesc.entries.push_back({
        0u,
        RhiBindingType::StorageBuffer,
        rhiFlag(RhiShaderStage::Vertex),
        1u
    });
    m_waterMetadataLayout = m_rhiDevice->createBindGroupLayout(metadataLayoutDesc);

    RhiBindGroupLayoutDesc materialLayoutDesc;
    materialLayoutDesc.debugName = "Terrain.WaterMaterialLayout";
    for (const uint32_t binding : kWaterTextureBindings) {
        materialLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    materialLayoutDesc.entries.push_back({
        13u,
        RhiBindingType::UniformBuffer,
        rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    m_waterMaterialLayout = m_rhiDevice->createBindGroupLayout(materialLayoutDesc);
    if (!m_waterMetadataLayout.isValid() || !m_waterMaterialLayout.isValid()) {
        destroyWaterResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "Terrain.WaterPipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_waterMetadataLayout);
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_waterMaterialLayout);
    m_waterPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    if (!m_waterPipelineLayout.isValid()) {
        destroyWaterResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "Terrain.Water.Pipeline";
    pipelineDesc.vertexShader = m_waterVertexShader;
    pipelineDesc.fragmentShader = m_waterFragmentShader;
    pipelineDesc.layout = m_waterPipelineLayout;
    setPackedTerrainVertexInput(pipelineDesc);
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::Less;
    pipelineDesc.colorFormats = {
        RhiTextureFormat::Rgba16Float,
        RhiTextureFormat::R8Unorm,
        RhiTextureFormat::R8Unorm
    };
    RhiBlendAttachmentState maskBlend;
    maskBlend.blendEnabled = true;
    maskBlend.srcColor = RhiBlendFactor::One;
    maskBlend.dstColor = RhiBlendFactor::One;
    maskBlend.colorOp = RhiBlendOp::Max;
    maskBlend.srcAlpha = RhiBlendFactor::One;
    maskBlend.dstAlpha = RhiBlendFactor::One;
    maskBlend.alphaOp = RhiBlendOp::Max;
    pipelineDesc.blend.attachments = {{}, maskBlend, maskBlend};
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    m_waterPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    if (!m_waterPipeline.isValid()) {
        destroyWaterResources();
        return false;
    }
    return true;
}

bool TerrainRhiPipelineSet::ensureWaterTextureViews(
    ResourceMgr& resourceMgr,
    DeferredRenderTargets& targets) {
    return ensureWaterTextureView(
               0u,
               resourceMgr.getTextureArray().texture,
               RhiTextureViewType::Texture2DArray,
               RhiTextureFormat::Rgba8Unorm) &&
           ensureWaterTextureView(
               1u,
               targets.depthTextureHandle(),
               RhiTextureViewType::Texture2D,
               RhiTextureFormat::Depth32Float) &&
           ensureWaterTextureView(
               2u,
               targets.skyCaptureTextureHandle(),
               RhiTextureViewType::Texture2D,
               RhiTextureFormat::Rgba16Float) &&
           ensureWaterTextureView(
               3u,
               targets.sceneResolvedTextureHandle(),
               RhiTextureViewType::Texture2D,
               RhiTextureFormat::Rgba16Float) &&
           ensureWaterTextureView(
               4u,
               resourceMgr.getTexture2DHandle("shader_noise2d"),
               RhiTextureViewType::Texture2D,
               RhiTextureFormat::Rgba8Unorm) &&
           ensureWaterTextureView(
               5u,
               targets.reflectionTextureHandle(),
               RhiTextureViewType::Texture2D,
               RhiTextureFormat::Rgba16Float) &&
           ensureWaterTextureView(
               6u,
               targets.atmosphereLutTextureHandle(),
               RhiTextureViewType::Texture3D,
               RhiTextureFormat::Rgba32Float) &&
           ensureWaterTextureView(
               7u,
               targets.halfResTextureHandle(),
               RhiTextureViewType::Texture2D,
               RhiTextureFormat::Rgba16Float) &&
           ensureWaterTextureView(
               8u,
               resourceMgr.getTexture2DHandle("shader_ripple_normal"),
               RhiTextureViewType::Texture2D,
               RhiTextureFormat::Rgba8Unorm);
}

bool TerrainRhiPipelineSet::ensureWaterBindGroup() {
    bool viewsChanged = false;
    for (size_t slot = 0u; slot < m_waterTextureViews.size(); ++slot) {
        viewsChanged = viewsChanged || !sameHandle(m_waterBoundViews[slot], m_waterTextureViews[slot]);
    }
    if (m_waterBindGroup.isValid() && !viewsChanged) {
        return true;
    }

    destroyWaterBindGroup();
    RhiBindGroupDesc desc;
    desc.layout = m_waterMaterialLayout;
    for (size_t slot = 0u; slot < m_waterTextureViews.size(); ++slot) {
        RhiBindGroupEntry entry;
        entry.binding = kWaterTextureBindings[slot];
        entry.resource.combinedTextureSampler.textureView = m_waterTextureViews[slot];
        if (slot == 0u) {
            entry.resource.combinedTextureSampler.sampler = m_waterBlockSampler;
        } else if (slot == 1u) {
            entry.resource.combinedTextureSampler.sampler = m_waterNearestClampSampler;
        } else if (slot == 4u || slot == 8u) {
            entry.resource.combinedTextureSampler.sampler = m_waterLinearRepeatSampler;
        } else {
            entry.resource.combinedTextureSampler.sampler = m_waterLinearClampSampler;
        }
        desc.entries.push_back(entry);
    }
    RhiBindGroupEntry paramsEntry;
    paramsEntry.binding = 13u;
    paramsEntry.resource.buffer.buffer = m_waterParamsBuffer;
    paramsEntry.resource.buffer.range = sizeof(TerrainWaterParams);
    desc.entries.push_back(paramsEntry);
    m_waterBindGroup = m_rhiDevice->createBindGroup(desc);
    if (!m_waterBindGroup.isValid()) {
        return false;
    }
    m_waterBoundViews = m_waterTextureViews;
    return true;
}

bool TerrainRhiPipelineSet::ensureWaterTextureView(
    const size_t slot,
    const RhiTextureHandle texture,
    const RhiTextureViewType viewType,
    const RhiTextureFormat format) {
    if (m_rhiDevice == nullptr || slot >= m_waterTextureViews.size() || !texture.isValid()) {
        return false;
    }
    if (sameHandle(m_waterViewTextures[slot], texture) && m_waterTextureViews[slot].isValid()) {
        return true;
    }
    if (m_waterTextureViews[slot].isValid()) {
        m_rhiDevice->destroyTextureView(m_waterTextureViews[slot]);
    }

    RhiTextureViewDesc desc;
    desc.texture = texture;
    desc.viewType = viewType;
    desc.format = format;
    desc.mipCount = kRhiRemainingMipLevels;
    desc.layerCount = kRhiRemainingArrayLayers;
    m_waterTextureViews[slot] = m_rhiDevice->createTextureView(desc);
    if (!m_waterTextureViews[slot].isValid()) {
        m_waterViewTextures[slot] = {};
        return false;
    }
    m_waterViewTextures[slot] = texture;
    return true;
}

void TerrainRhiPipelineSet::destroyWaterBindGroup() {
    if (m_rhiDevice != nullptr && m_waterBindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_waterBindGroup);
    }
    m_waterBindGroup = {};
    m_waterBoundViews = {};
}

void TerrainRhiPipelineSet::destroyWaterTextureViews() {
    if (m_rhiDevice != nullptr) {
        for (RhiTextureViewHandle& view : m_waterTextureViews) {
            if (view.isValid()) {
                m_rhiDevice->destroyTextureView(view);
            }
            view = {};
        }
    }
    m_waterViewTextures = {};
}

void TerrainRhiPipelineSet::destroyWaterResources() {
    destroyWaterBindGroup();
    destroyWaterTextureViews();
    if (m_rhiDevice != nullptr) {
        if (m_waterPipeline.isValid()) m_rhiDevice->destroyPipeline(m_waterPipeline);
        if (m_waterPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_waterPipelineLayout);
        if (m_waterMaterialLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_waterMaterialLayout);
        if (m_waterMetadataLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_waterMetadataLayout);
        if (m_waterFragmentShader.isValid()) m_rhiDevice->destroyShader(m_waterFragmentShader);
        if (m_waterVertexShader.isValid()) m_rhiDevice->destroyShader(m_waterVertexShader);
        if (m_waterParamsBuffer.isValid()) m_rhiDevice->destroyBuffer(m_waterParamsBuffer);
        if (m_waterBlockSampler.isValid()) m_rhiDevice->destroySampler(m_waterBlockSampler);
        if (m_waterLinearClampSampler.isValid()) m_rhiDevice->destroySampler(m_waterLinearClampSampler);
        if (m_waterLinearRepeatSampler.isValid()) m_rhiDevice->destroySampler(m_waterLinearRepeatSampler);
        if (m_waterNearestClampSampler.isValid()) m_rhiDevice->destroySampler(m_waterNearestClampSampler);
    }
    m_waterPipeline = {};
    m_waterPipelineLayout = {};
    m_waterMaterialLayout = {};
    m_waterMetadataLayout = {};
    m_waterFragmentShader = {};
    m_waterVertexShader = {};
    m_waterParamsBuffer = {};
    m_waterBlockSampler = {};
    m_waterLinearClampSampler = {};
    m_waterLinearRepeatSampler = {};
    m_waterNearestClampSampler = {};
    m_waterSamplerAnisotropy = 1.0f;
}

bool TerrainRhiPipelineSet::ensureTransparentPipeline(ResourceMgr& resourceMgr) {
    const float anisotropy = std::max(1.0f, resourceMgr.getAtlasAnisotropy());
    if (m_transparentPipeline.isValid() && anisotropy == m_transparentSamplerAnisotropy) {
        return true;
    }

    destroyTransparentResources();
    if (m_rhiDevice == nullptr) {
        return false;
    }
    m_transparentSamplerAnisotropy = anisotropy;

    renderer::rhi::RhiShaderSourceOptions sourceOptions;
    sourceOptions.preprocessorDefinitions.push_back("RHI_TERRAIN_LIT_MDI");
    const std::optional<std::string> vertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/chunk_lit.vert",
        sourceOptions);
    const std::optional<std::string> fragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/transparent_composite.frag",
        sourceOptions);
    if (!vertexSource.has_value() || !fragmentSource.has_value()) {
        destroyTransparentResources();
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "Terrain.Transparent.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_transparentVertexShader = m_rhiDevice->createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "Terrain.Transparent.Fragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = fragmentSource->c_str();
    fragmentDesc.sourceSize = fragmentSource->size();
    m_transparentFragmentShader = m_rhiDevice->createShader(fragmentDesc);
    if (!m_transparentVertexShader.isValid() || !m_transparentFragmentShader.isValid()) {
        destroyTransparentResources();
        return false;
    }

    RhiBufferDesc paramsDesc;
    paramsDesc.debugName = "Terrain.Transparent.Params";
    paramsDesc.size = sizeof(TerrainLitParams);
    paramsDesc.usage = rhiFlag(RhiBufferUsage::Uniform) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    paramsDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    paramsDesc.initialState = RhiResourceState::UniformBuffer;
    paramsDesc.memoryCategory = RhiMemoryCategory::Uniform;
    m_transparentParamsBuffer = m_rhiDevice->createBuffer(paramsDesc, nullptr, 0u);
    if (!m_transparentParamsBuffer.isValid()) {
        destroyTransparentResources();
        return false;
    }

    RhiSamplerDesc blockSamplerDesc;
    blockSamplerDesc.minFilter = RhiFilter::Nearest;
    blockSamplerDesc.magFilter = RhiFilter::Nearest;
    blockSamplerDesc.mipmapMode = RhiMipmapMode::Linear;
    blockSamplerDesc.addressU = RhiAddressMode::Repeat;
    blockSamplerDesc.addressV = RhiAddressMode::Repeat;
    blockSamplerDesc.addressW = RhiAddressMode::Repeat;
    blockSamplerDesc.maxAnisotropy = m_transparentSamplerAnisotropy;
    m_transparentBlockSampler = m_rhiDevice->createSampler(blockSamplerDesc);

    RhiSamplerDesc linearClampDesc;
    linearClampDesc.minFilter = RhiFilter::Linear;
    linearClampDesc.magFilter = RhiFilter::Linear;
    linearClampDesc.mipmapMode = RhiMipmapMode::Nearest;
    m_transparentLinearClampSampler = m_rhiDevice->createSampler(linearClampDesc);

    RhiSamplerDesc linearRepeatDesc = linearClampDesc;
    linearRepeatDesc.addressU = RhiAddressMode::Repeat;
    linearRepeatDesc.addressV = RhiAddressMode::Repeat;
    linearRepeatDesc.addressW = RhiAddressMode::Repeat;
    m_transparentLinearRepeatSampler = m_rhiDevice->createSampler(linearRepeatDesc);

    RhiSamplerDesc nearestClampDesc;
    nearestClampDesc.minFilter = RhiFilter::Nearest;
    nearestClampDesc.magFilter = RhiFilter::Nearest;
    nearestClampDesc.mipmapMode = RhiMipmapMode::Nearest;
    m_transparentNearestClampSampler = m_rhiDevice->createSampler(nearestClampDesc);
    if (!m_transparentBlockSampler.isValid() || !m_transparentLinearClampSampler.isValid() ||
        !m_transparentLinearRepeatSampler.isValid() ||
        !m_transparentNearestClampSampler.isValid()) {
        destroyTransparentResources();
        return false;
    }

    RhiBindGroupLayoutDesc metadataLayoutDesc;
    metadataLayoutDesc.debugName = "Terrain.TransparentMetadataLayout";
    metadataLayoutDesc.entries.push_back({
        0u,
        RhiBindingType::StorageBuffer,
        rhiFlag(RhiShaderStage::Vertex),
        1u
    });
    m_transparentMetadataLayout = m_rhiDevice->createBindGroupLayout(metadataLayoutDesc);

    RhiBindGroupLayoutDesc materialLayoutDesc;
    materialLayoutDesc.debugName = "Terrain.TransparentMaterialLayout";
    for (const uint32_t binding : kTransparentTextureBindings) {
        materialLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    materialLayoutDesc.entries.push_back({
        15u,
        RhiBindingType::UniformBuffer,
        rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    m_transparentMaterialLayout = m_rhiDevice->createBindGroupLayout(materialLayoutDesc);
    if (!m_transparentMetadataLayout.isValid() || !m_transparentMaterialLayout.isValid()) {
        destroyTransparentResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "Terrain.TransparentPipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_transparentMetadataLayout);
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_transparentMaterialLayout);
    m_transparentPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    if (!m_transparentPipelineLayout.isValid()) {
        destroyTransparentResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "Terrain.Transparent.Pipeline";
    pipelineDesc.vertexShader = m_transparentVertexShader;
    pipelineDesc.fragmentShader = m_transparentFragmentShader;
    pipelineDesc.layout = m_transparentPipelineLayout;
    setPackedTerrainVertexInput(pipelineDesc);
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::LessOrEqual;
    RhiBlendAttachmentState blend;
    blend.blendEnabled = true;
    blend.srcColor = RhiBlendFactor::SrcAlpha;
    blend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    blend.srcAlpha = RhiBlendFactor::SrcAlpha;
    blend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    pipelineDesc.blend.attachments = {blend};
    RhiBlendAttachmentState maskBlend;
    maskBlend.blendEnabled = true;
    maskBlend.srcColor = RhiBlendFactor::One;
    maskBlend.dstColor = RhiBlendFactor::One;
    maskBlend.colorOp = RhiBlendOp::Max;
    maskBlend.srcAlpha = RhiBlendFactor::One;
    maskBlend.dstAlpha = RhiBlendFactor::One;
    maskBlend.alphaOp = RhiBlendOp::Max;
    pipelineDesc.blend.attachments.push_back(maskBlend);
    pipelineDesc.blend.attachments.push_back(maskBlend);
    pipelineDesc.colorFormats = {
        RhiTextureFormat::Rgba16Float,
        RhiTextureFormat::R8Unorm,
        RhiTextureFormat::R8Unorm
    };
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    m_transparentPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    if (!m_transparentPipeline.isValid()) {
        destroyTransparentResources();
        return false;
    }
    return true;
}

bool TerrainRhiPipelineSet::ensureTransparentTextureViews(
    ResourceMgr& resourceMgr,
    DeferredRenderTargets& targets) {
    return ensureTransparentTextureView(
               0u,
               resourceMgr.getTextureArray().texture,
               RhiTextureViewType::Texture2DArray,
               RhiTextureFormat::Rgba8Unorm) &&
           ensureTransparentTextureView(
               1u,
               resourceMgr.getLightmapDay(),
               RhiTextureViewType::Texture2D,
               RhiTextureFormat::Rgba8Unorm) &&
           ensureTransparentTextureView(
               2u,
               resourceMgr.getLightmapNight(),
               RhiTextureViewType::Texture2D,
               RhiTextureFormat::Rgba8Unorm) &&
           ensureTransparentTextureView(
               3u,
               resourceMgr.getGrassColormap(),
               RhiTextureViewType::Texture2D,
               RhiTextureFormat::Rgba8Unorm) &&
           ensureTransparentTextureView(
               4u,
               resourceMgr.getFoliageColormap(),
               RhiTextureViewType::Texture2D,
               RhiTextureFormat::Rgba8Unorm) &&
           ensureTransparentTextureView(
               5u,
               targets.depthTextureHandle(),
               RhiTextureViewType::Texture2D,
               RhiTextureFormat::Depth32Float) &&
           ensureTransparentTextureView(
               6u,
               targets.skyCaptureTextureHandle(),
               RhiTextureViewType::Texture2D,
               RhiTextureFormat::Rgba16Float) &&
           ensureTransparentTextureView(
               7u,
               targets.sceneCompositeTextureHandle(),
               RhiTextureViewType::Texture2D,
               RhiTextureFormat::Rgba16Float) &&
           ensureTransparentTextureView(
               8u,
               resourceMgr.getTexture2DHandle("shader_noise2d"),
               RhiTextureViewType::Texture2D,
               RhiTextureFormat::Rgba8Unorm) &&
           ensureTransparentTextureView(
               9u,
               targets.atmosphereLutTextureHandle(),
               RhiTextureViewType::Texture3D,
               RhiTextureFormat::Rgba32Float);
}

bool TerrainRhiPipelineSet::ensureTransparentBindGroup() {
    bool viewsChanged = false;
    for (size_t slot = 0u; slot < m_transparentTextureViews.size(); ++slot) {
        viewsChanged = viewsChanged ||
                       !sameHandle(m_transparentBoundViews[slot], m_transparentTextureViews[slot]);
    }
    if (m_transparentBindGroup.isValid() && !viewsChanged) {
        return true;
    }

    destroyTransparentBindGroup();
    RhiBindGroupDesc desc;
    desc.layout = m_transparentMaterialLayout;
    for (size_t slot = 0u; slot < m_transparentTextureViews.size(); ++slot) {
        RhiBindGroupEntry entry;
        entry.binding = kTransparentTextureBindings[slot];
        entry.resource.combinedTextureSampler.textureView = m_transparentTextureViews[slot];
        if (slot == 0u) {
            entry.resource.combinedTextureSampler.sampler = m_transparentBlockSampler;
        } else if (slot == 5u) {
            entry.resource.combinedTextureSampler.sampler = m_transparentNearestClampSampler;
        } else if (slot == 8u) {
            entry.resource.combinedTextureSampler.sampler = m_transparentLinearRepeatSampler;
        } else {
            entry.resource.combinedTextureSampler.sampler = m_transparentLinearClampSampler;
        }
        desc.entries.push_back(entry);
    }
    RhiBindGroupEntry paramsEntry;
    paramsEntry.binding = 15u;
    paramsEntry.resource.buffer.buffer = m_transparentParamsBuffer;
    paramsEntry.resource.buffer.range = sizeof(TerrainLitParams);
    desc.entries.push_back(paramsEntry);
    m_transparentBindGroup = m_rhiDevice->createBindGroup(desc);
    if (!m_transparentBindGroup.isValid()) {
        return false;
    }
    m_transparentBoundViews = m_transparentTextureViews;
    return true;
}

bool TerrainRhiPipelineSet::ensureTransparentTextureView(
    const size_t slot,
    const RhiTextureHandle texture,
    const RhiTextureViewType viewType,
    const RhiTextureFormat format) {
    if (m_rhiDevice == nullptr || slot >= m_transparentTextureViews.size() ||
        !texture.isValid()) {
        return false;
    }
    if (sameHandle(m_transparentViewTextures[slot], texture) &&
        m_transparentTextureViews[slot].isValid()) {
        return true;
    }
    if (m_transparentTextureViews[slot].isValid()) {
        m_rhiDevice->destroyTextureView(m_transparentTextureViews[slot]);
    }

    RhiTextureViewDesc desc;
    desc.texture = texture;
    desc.viewType = viewType;
    desc.format = format;
    desc.mipCount = kRhiRemainingMipLevels;
    desc.layerCount = kRhiRemainingArrayLayers;
    m_transparentTextureViews[slot] = m_rhiDevice->createTextureView(desc);
    if (!m_transparentTextureViews[slot].isValid()) {
        m_transparentViewTextures[slot] = {};
        return false;
    }
    m_transparentViewTextures[slot] = texture;
    return true;
}

void TerrainRhiPipelineSet::destroyTransparentBindGroup() {
    if (m_rhiDevice != nullptr && m_transparentBindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_transparentBindGroup);
    }
    m_transparentBindGroup = {};
    m_transparentBoundViews = {};
}

void TerrainRhiPipelineSet::destroyTransparentTextureViews() {
    if (m_rhiDevice != nullptr) {
        for (RhiTextureViewHandle& view : m_transparentTextureViews) {
            if (view.isValid()) {
                m_rhiDevice->destroyTextureView(view);
            }
            view = {};
        }
    }
    m_transparentViewTextures = {};
}

void TerrainRhiPipelineSet::destroyTransparentResources() {
    destroyTransparentBindGroup();
    destroyTransparentTextureViews();
    if (m_rhiDevice != nullptr) {
        if (m_transparentPipeline.isValid()) m_rhiDevice->destroyPipeline(m_transparentPipeline);
        if (m_transparentPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_transparentPipelineLayout);
        if (m_transparentMaterialLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_transparentMaterialLayout);
        if (m_transparentMetadataLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_transparentMetadataLayout);
        if (m_transparentFragmentShader.isValid()) m_rhiDevice->destroyShader(m_transparentFragmentShader);
        if (m_transparentVertexShader.isValid()) m_rhiDevice->destroyShader(m_transparentVertexShader);
        if (m_transparentParamsBuffer.isValid()) m_rhiDevice->destroyBuffer(m_transparentParamsBuffer);
        if (m_transparentBlockSampler.isValid()) m_rhiDevice->destroySampler(m_transparentBlockSampler);
        if (m_transparentLinearClampSampler.isValid()) m_rhiDevice->destroySampler(m_transparentLinearClampSampler);
        if (m_transparentLinearRepeatSampler.isValid()) m_rhiDevice->destroySampler(m_transparentLinearRepeatSampler);
        if (m_transparentNearestClampSampler.isValid()) m_rhiDevice->destroySampler(m_transparentNearestClampSampler);
    }
    m_transparentPipeline = {};
    m_transparentPipelineLayout = {};
    m_transparentMaterialLayout = {};
    m_transparentMetadataLayout = {};
    m_transparentFragmentShader = {};
    m_transparentVertexShader = {};
    m_transparentParamsBuffer = {};
    m_transparentBlockSampler = {};
    m_transparentLinearClampSampler = {};
    m_transparentLinearRepeatSampler = {};
    m_transparentNearestClampSampler = {};
    m_transparentSamplerAnisotropy = 1.0f;
}

bool TerrainRhiPipelineSet::ensureShadowPipeline(ResourceMgr& resourceMgr) {
    const float anisotropy = std::max(1.0f, resourceMgr.getAtlasAnisotropy());
    if (m_shadowOpaquePipeline.isValid() && m_shadowCutoutPipeline.isValid() &&
        m_shadowTransparentPipeline.isValid() && anisotropy == m_shadowSamplerAnisotropy) {
        return true;
    }

    destroyShadowResources();
    if (m_rhiDevice == nullptr) {
        return false;
    }
    m_shadowSamplerAnisotropy = anisotropy;

    renderer::rhi::RhiShaderSourceOptions sourceOptions;
    sourceOptions.preprocessorDefinitions.push_back("RHI_TERRAIN_SHADOW_MDI");
    renderer::rhi::RhiShaderSourceOptions depthSourceOptions = sourceOptions;
    depthSourceOptions.preprocessorDefinitions.push_back("RHI_TERRAIN_SHADOW_DEPTH_ONLY");
    const std::optional<std::string> vertexSource = renderer::rhi::loadShaderSource(
        "assets/shaders/shadow_depth.vert",
        sourceOptions);
    const std::optional<std::string> depthFragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/shadow_depth.frag",
        depthSourceOptions);
    const std::optional<std::string> colorFragmentSource = renderer::rhi::loadShaderSource(
        "assets/shaders/shadow_depth.frag",
        sourceOptions);
    if (!vertexSource.has_value() || !depthFragmentSource.has_value() ||
        !colorFragmentSource.has_value()) {
        destroyShadowResources();
        return false;
    }

    RhiShaderDesc vertexDesc;
    vertexDesc.debugName = "Terrain.Shadow.Vertex";
    vertexDesc.stage = RhiShaderStage::Vertex;
    vertexDesc.source = vertexSource->c_str();
    vertexDesc.sourceSize = vertexSource->size();
    m_shadowVertexShader = m_rhiDevice->createShader(vertexDesc);

    RhiShaderDesc fragmentDesc;
    fragmentDesc.debugName = "Terrain.Shadow.DepthFragment";
    fragmentDesc.stage = RhiShaderStage::Fragment;
    fragmentDesc.source = depthFragmentSource->c_str();
    fragmentDesc.sourceSize = depthFragmentSource->size();
    m_shadowDepthFragmentShader = m_rhiDevice->createShader(fragmentDesc);
    fragmentDesc.debugName = "Terrain.Shadow.ColorFragment";
    fragmentDesc.source = colorFragmentSource->c_str();
    fragmentDesc.sourceSize = colorFragmentSource->size();
    m_shadowColorFragmentShader = m_rhiDevice->createShader(fragmentDesc);
    if (!m_shadowVertexShader.isValid() || !m_shadowDepthFragmentShader.isValid() ||
        !m_shadowColorFragmentShader.isValid()) {
        destroyShadowResources();
        return false;
    }

    RhiBufferDesc paramsDesc;
    paramsDesc.debugName = "Terrain.Shadow.Params";
    paramsDesc.size = sizeof(TerrainShadowParams);
    paramsDesc.usage = rhiFlag(RhiBufferUsage::Uniform) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    paramsDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    paramsDesc.initialState = RhiResourceState::UniformBuffer;
    paramsDesc.memoryCategory = RhiMemoryCategory::Uniform;
    m_shadowParamsBuffer = m_rhiDevice->createBuffer(paramsDesc, nullptr, 0u);
    if (!m_shadowParamsBuffer.isValid()) {
        destroyShadowResources();
        return false;
    }

    RhiSamplerDesc blockSamplerDesc;
    blockSamplerDesc.minFilter = RhiFilter::Nearest;
    blockSamplerDesc.magFilter = RhiFilter::Nearest;
    blockSamplerDesc.mipmapMode = RhiMipmapMode::Linear;
    blockSamplerDesc.addressU = RhiAddressMode::Repeat;
    blockSamplerDesc.addressV = RhiAddressMode::Repeat;
    blockSamplerDesc.addressW = RhiAddressMode::Repeat;
    blockSamplerDesc.maxAnisotropy = m_shadowSamplerAnisotropy;
    m_shadowBlockSampler = m_rhiDevice->createSampler(blockSamplerDesc);

    RhiSamplerDesc linearClampDesc;
    linearClampDesc.minFilter = RhiFilter::Linear;
    linearClampDesc.magFilter = RhiFilter::Linear;
    linearClampDesc.mipmapMode = RhiMipmapMode::Nearest;
    m_shadowLinearClampSampler = m_rhiDevice->createSampler(linearClampDesc);

    RhiSamplerDesc linearRepeatDesc = linearClampDesc;
    linearRepeatDesc.addressU = RhiAddressMode::Repeat;
    linearRepeatDesc.addressV = RhiAddressMode::Repeat;
    linearRepeatDesc.addressW = RhiAddressMode::Repeat;
    m_shadowLinearRepeatSampler = m_rhiDevice->createSampler(linearRepeatDesc);
    if (!m_shadowBlockSampler.isValid() || !m_shadowLinearClampSampler.isValid() ||
        !m_shadowLinearRepeatSampler.isValid()) {
        destroyShadowResources();
        return false;
    }

    RhiBindGroupLayoutDesc metadataLayoutDesc;
    metadataLayoutDesc.debugName = "Terrain.ShadowMetadataLayout";
    metadataLayoutDesc.entries.push_back({
        0u,
        RhiBindingType::StorageBuffer,
        rhiFlag(RhiShaderStage::Vertex),
        1u
    });
    m_shadowMetadataLayout = m_rhiDevice->createBindGroupLayout(metadataLayoutDesc);

    RhiBindGroupLayoutDesc materialLayoutDesc;
    materialLayoutDesc.debugName = "Terrain.ShadowMaterialLayout";
    for (uint32_t binding = 0u; binding < 4u; ++binding) {
        materialLayoutDesc.entries.push_back({
            binding,
            RhiBindingType::CombinedTextureSampler,
            rhiFlag(RhiShaderStage::Fragment),
            1u
        });
    }
    materialLayoutDesc.entries.push_back({
        4u,
        RhiBindingType::UniformBuffer,
        rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment),
        1u
    });
    m_shadowMaterialLayout = m_rhiDevice->createBindGroupLayout(materialLayoutDesc);
    if (!m_shadowMetadataLayout.isValid() || !m_shadowMaterialLayout.isValid()) {
        destroyShadowResources();
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "Terrain.ShadowPipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_shadowMetadataLayout);
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_shadowMaterialLayout);
    m_shadowPipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);
    if (!m_shadowPipelineLayout.isValid()) {
        destroyShadowResources();
        return false;
    }

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = m_shadowVertexShader;
    pipelineDesc.fragmentShader = m_shadowDepthFragmentShader;
    pipelineDesc.layout = m_shadowPipelineLayout;
    setPackedTerrainVertexInput(pipelineDesc);
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.depthStencil.depthCompare = RhiCompareOp::Less;
    pipelineDesc.depthFormat = RhiTextureFormat::Depth32Float;
    pipelineDesc.raster.cullMode = RhiCullMode::Back;
    pipelineDesc.debugName = "Terrain.Shadow.OpaquePipeline";
    m_shadowOpaquePipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);

    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.raster.depthBiasEnabled = true;
    pipelineDesc.raster.depthBiasConstantFactor = 4.0f;
    pipelineDesc.raster.depthBiasSlopeFactor = 2.0f;
    pipelineDesc.debugName = "Terrain.Shadow.CutoutPipeline";
    m_shadowCutoutPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);

    pipelineDesc.raster.depthBiasEnabled = false;
    pipelineDesc.raster.depthBiasConstantFactor = 0.0f;
    pipelineDesc.raster.depthBiasSlopeFactor = 0.0f;
    pipelineDesc.colorFormats = {
        RhiTextureFormat::Rgba8Unorm,
        RhiTextureFormat::Rgba16Float
    };
    pipelineDesc.fragmentShader = m_shadowColorFragmentShader;
    pipelineDesc.debugName = "Terrain.Shadow.TransparentPipeline";
    m_shadowTransparentPipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    if (!m_shadowOpaquePipeline.isValid() || !m_shadowCutoutPipeline.isValid() ||
        !m_shadowTransparentPipeline.isValid()) {
        destroyShadowResources();
        return false;
    }
    return true;
}

bool TerrainRhiPipelineSet::ensureShadowTextureViews(ResourceMgr& resourceMgr) {
    return ensureShadowTextureView(
               0u,
               resourceMgr.getTextureArray().texture,
               RhiTextureViewType::Texture2DArray) &&
           ensureShadowTextureView(
               1u,
               resourceMgr.getTexture2DHandle("shader_noise2d"),
               RhiTextureViewType::Texture2D) &&
           ensureShadowTextureView(
               2u,
               resourceMgr.getGrassColormap(),
               RhiTextureViewType::Texture2D) &&
           ensureShadowTextureView(
               3u,
               resourceMgr.getFoliageColormap(),
               RhiTextureViewType::Texture2D);
}

bool TerrainRhiPipelineSet::ensureShadowBindGroup() {
    bool viewsChanged = false;
    for (size_t slot = 0u; slot < m_shadowTextureViews.size(); ++slot) {
        viewsChanged = viewsChanged ||
                       !sameHandle(m_shadowBoundViews[slot], m_shadowTextureViews[slot]);
    }
    if (m_shadowBindGroup.isValid() && !viewsChanged) {
        return true;
    }

    destroyShadowBindGroup();
    RhiBindGroupDesc desc;
    desc.layout = m_shadowMaterialLayout;
    for (uint32_t binding = 0u; binding < m_shadowTextureViews.size(); ++binding) {
        RhiBindGroupEntry entry;
        entry.binding = binding;
        entry.resource.combinedTextureSampler.textureView = m_shadowTextureViews[binding];
        entry.resource.combinedTextureSampler.sampler = binding == 0u
            ? m_shadowBlockSampler
            : (binding == 1u ? m_shadowLinearRepeatSampler : m_shadowLinearClampSampler);
        desc.entries.push_back(entry);
    }
    RhiBindGroupEntry paramsEntry;
    paramsEntry.binding = 4u;
    paramsEntry.resource.buffer.buffer = m_shadowParamsBuffer;
    paramsEntry.resource.buffer.range = sizeof(TerrainShadowParams);
    desc.entries.push_back(paramsEntry);
    m_shadowBindGroup = m_rhiDevice->createBindGroup(desc);
    if (!m_shadowBindGroup.isValid()) {
        return false;
    }
    m_shadowBoundViews = m_shadowTextureViews;
    return true;
}

bool TerrainRhiPipelineSet::ensureShadowTextureView(
    const size_t slot,
    const RhiTextureHandle texture,
    const RhiTextureViewType viewType) {
    if (m_rhiDevice == nullptr || slot >= m_shadowTextureViews.size() || !texture.isValid()) {
        return false;
    }
    if (sameHandle(m_shadowViewTextures[slot], texture) && m_shadowTextureViews[slot].isValid()) {
        return true;
    }
    if (m_shadowTextureViews[slot].isValid()) {
        m_rhiDevice->destroyTextureView(m_shadowTextureViews[slot]);
    }

    RhiTextureViewDesc desc;
    desc.texture = texture;
    desc.viewType = viewType;
    desc.format = RhiTextureFormat::Rgba8Unorm;
    desc.mipCount = kRhiRemainingMipLevels;
    desc.layerCount = kRhiRemainingArrayLayers;
    m_shadowTextureViews[slot] = m_rhiDevice->createTextureView(desc);
    if (!m_shadowTextureViews[slot].isValid()) {
        m_shadowViewTextures[slot] = {};
        return false;
    }
    m_shadowViewTextures[slot] = texture;
    return true;
}

void TerrainRhiPipelineSet::destroyShadowBindGroup() {
    if (m_rhiDevice != nullptr && m_shadowBindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_shadowBindGroup);
    }
    m_shadowBindGroup = {};
    m_shadowBoundViews = {};
}

void TerrainRhiPipelineSet::destroyShadowTextureViews() {
    if (m_rhiDevice != nullptr) {
        for (RhiTextureViewHandle& view : m_shadowTextureViews) {
            if (view.isValid()) {
                m_rhiDevice->destroyTextureView(view);
            }
            view = {};
        }
    }
    m_shadowViewTextures = {};
}

void TerrainRhiPipelineSet::destroyShadowResources() {
    destroyShadowBindGroup();
    destroyShadowTextureViews();
    if (m_rhiDevice != nullptr) {
        if (m_shadowOpaquePipeline.isValid()) m_rhiDevice->destroyPipeline(m_shadowOpaquePipeline);
        if (m_shadowCutoutPipeline.isValid()) m_rhiDevice->destroyPipeline(m_shadowCutoutPipeline);
        if (m_shadowTransparentPipeline.isValid()) m_rhiDevice->destroyPipeline(m_shadowTransparentPipeline);
        if (m_shadowPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_shadowPipelineLayout);
        if (m_shadowMaterialLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_shadowMaterialLayout);
        if (m_shadowMetadataLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_shadowMetadataLayout);
        if (m_shadowColorFragmentShader.isValid()) m_rhiDevice->destroyShader(m_shadowColorFragmentShader);
        if (m_shadowDepthFragmentShader.isValid()) m_rhiDevice->destroyShader(m_shadowDepthFragmentShader);
        if (m_shadowVertexShader.isValid()) m_rhiDevice->destroyShader(m_shadowVertexShader);
        if (m_shadowParamsBuffer.isValid()) m_rhiDevice->destroyBuffer(m_shadowParamsBuffer);
        if (m_shadowBlockSampler.isValid()) m_rhiDevice->destroySampler(m_shadowBlockSampler);
        if (m_shadowLinearClampSampler.isValid()) m_rhiDevice->destroySampler(m_shadowLinearClampSampler);
        if (m_shadowLinearRepeatSampler.isValid()) m_rhiDevice->destroySampler(m_shadowLinearRepeatSampler);
    }
    m_shadowOpaquePipeline = {};
    m_shadowCutoutPipeline = {};
    m_shadowTransparentPipeline = {};
    m_shadowPipelineLayout = {};
    m_shadowMaterialLayout = {};
    m_shadowMetadataLayout = {};
    m_shadowColorFragmentShader = {};
    m_shadowDepthFragmentShader = {};
    m_shadowVertexShader = {};
    m_shadowParamsBuffer = {};
    m_shadowBlockSampler = {};
    m_shadowLinearClampSampler = {};
    m_shadowLinearRepeatSampler = {};
    m_shadowSamplerAnisotropy = 1.0f;
}

void TerrainRhiPipelineSet::destroyGBufferBindGroups() {
    if (m_rhiDevice != nullptr) {
        for (RhiBindGroupHandle& bindGroup : m_gbufferBindGroups) {
            if (bindGroup.isValid()) {
                m_rhiDevice->destroyBindGroup(bindGroup);
            }
            bindGroup = {};
        }
    }
    m_gbufferBoundViews = {};
}

void TerrainRhiPipelineSet::destroyGBufferTextureViews() {
    if (m_rhiDevice != nullptr) {
        for (RhiTextureViewHandle& view : m_gbufferTextureViews) {
            if (view.isValid()) {
                m_rhiDevice->destroyTextureView(view);
            }
            view = {};
        }
    }
    m_gbufferViewTextures = {};
}

void TerrainRhiPipelineSet::destroyGBufferResources() {
    destroyGBufferBindGroups();
    destroyGBufferTextureViews();
    if (m_rhiDevice != nullptr) {
        if (m_gbufferOpaquePipeline.isValid()) m_rhiDevice->destroyPipeline(m_gbufferOpaquePipeline);
        if (m_gbufferCutoutPipeline.isValid()) m_rhiDevice->destroyPipeline(m_gbufferCutoutPipeline);
        if (m_gbufferPipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_gbufferPipelineLayout);
        if (m_gbufferMaterialLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_gbufferMaterialLayout);
        if (m_metadataLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_metadataLayout);
        if (m_gbufferFragmentShader.isValid()) m_rhiDevice->destroyShader(m_gbufferFragmentShader);
        if (m_gbufferVertexShader.isValid()) m_rhiDevice->destroyShader(m_gbufferVertexShader);
        for (RhiBufferHandle& buffer : m_gbufferParamsBuffers) {
            if (buffer.isValid()) m_rhiDevice->destroyBuffer(buffer);
            buffer = {};
        }
        if (m_blockSampler.isValid()) m_rhiDevice->destroySampler(m_blockSampler);
        if (m_linearClampSampler.isValid()) m_rhiDevice->destroySampler(m_linearClampSampler);
        if (m_linearRepeatSampler.isValid()) m_rhiDevice->destroySampler(m_linearRepeatSampler);
    }
    m_gbufferOpaquePipeline = {};
    m_gbufferCutoutPipeline = {};
    m_gbufferPipelineLayout = {};
    m_gbufferMaterialLayout = {};
    m_metadataLayout = {};
    m_gbufferFragmentShader = {};
    m_gbufferVertexShader = {};
    m_blockSampler = {};
    m_linearClampSampler = {};
    m_linearRepeatSampler = {};
    m_hasNormalMaps = false;
    m_hasSpecularMaps = false;
    m_samplerAnisotropy = 1.0f;
}
