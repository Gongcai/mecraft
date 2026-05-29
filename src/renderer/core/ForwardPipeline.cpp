#include "ForwardPipeline.h"
#include "RenderScene.h"
#include "../mesh/TerrainRenderCache.h"
#include "../mesh/WorldRenderBuffer.h"
#include "../targets/CommonFrameTargets.h"
#include "../renderers/GameplaySkyRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../engine/camera/Camera.h"
#include "../../world/World.h"
#include "../../world/DayNightSystem.h"

#include <glad/glad.h>

ForwardPipeline::ForwardPipeline() = default;
ForwardPipeline::~ForwardPipeline() = default;

void ForwardPipeline::init(SharedRenderResources& shared) {
    m_terrainRenderer = shared.terrain;
    m_terrainCache = shared.terrainCache;
    m_worldRenderBuffer = shared.worldRenderBuffer;
    m_commonTargets = shared.commonTargets;
    m_skyRenderer = shared.sky;
    m_resourceMgr = shared.resources;
    m_initialized = true;
}

void ForwardPipeline::shutdown() {
    m_terrainRenderer = nullptr;
    m_terrainCache = nullptr;
    m_worldRenderBuffer = nullptr;
    m_commonTargets = nullptr;
    m_skyRenderer = nullptr;
    m_resourceMgr = nullptr;
    m_transparentBatch.clear();
    m_transparentPassPlan = {};
    m_transparentEntries.clear();
    m_initialized = false;
}

FrameOutput ForwardPipeline::renderFrame(const FrameContext& ctx, const RenderSettings& settings) {
    if (!m_initialized || !ctx.world) {
        return {};
    }

    // 1. Sky background (sun, moon, clouds, gradient)
    renderSky(ctx);

    // 2. Opaque + cutout terrain
    renderTerrain(ctx, settings);

    // 3. Transparent terrain (water, glass, etc.)
    renderTransparent(ctx, settings);

    // 4. Build and return frame output
    return buildFrameOutput(ctx);
}

// ============================================================================
// Sky rendering
// ============================================================================

void ForwardPipeline::renderSky(const FrameContext& ctx) {
    if (!m_skyRenderer || !ctx.world || !ctx.cameraPtr) return;

    const auto& dayNight = ctx.world->getDayNightSystem();
    const float aspect = (ctx.frameHeight > 0)
        ? static_cast<float>(ctx.frameWidth) / static_cast<float>(ctx.frameHeight)
        : 1.0f;

    // Forward mode: pass 0 for skyCaptureTexture — sky gradient mode doesn't read it
    m_skyRenderer->render(*ctx.cameraPtr, aspect, dayNight, 0);
}

// ============================================================================
// Terrain rendering (opaque + cutout)
// ============================================================================

void ForwardPipeline::renderTerrain(const FrameContext& ctx, const RenderSettings& settings) {
    if (!m_terrainRenderer || !m_resourceMgr || !m_worldRenderBuffer) return;

    auto& terrain = *m_terrainRenderer;
    auto& worldBuffer = *m_worldRenderBuffer;

    // Terrain cache maintenance
    if (m_terrainCache) {
        m_terrainCache->releaseStaleMdiAllocations(*ctx.world);
        m_terrainCache->drainMeshingResults(*ctx.world);
    }
    worldBuffer.beginFrame();
    terrain.clearTransparentBatches();

    // Get forward basic shader
    Shader* terrainShader = m_resourceMgr->getShader("forward_basic_terrain");
    if (!terrainShader) return;

    // Build terrain frame data from FrameContext
    TerrainFrameData tfd = buildTerrainFrameData(ctx);

    // Frustum + camera
    terrain.setCameraPos(ctx.camera.position);
    terrain.updateFrustum(ctx.camera.viewProj);

    // Terrain render settings
    TerrainRenderSettings trs = buildTerrainRenderSettings(settings);

    // Bind lightweight forward state
    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    terrain.bindBasicForwardState(tfd, texArray, *terrainShader,
                                   ctx.eyeInWater, 0/*heldBlockLight*/,
                                   m_resourceMgr, trs);

    // Submit meshing jobs
    if (m_terrainCache) {
        m_terrainCache->submitMeshingJobs(*ctx.world, ctx.camera.position);
    }

    // Render opaque + collect cutout/transparent entries
    std::vector<ChunkRenderEntry> cutoutEntries;
    std::vector<ChunkRenderEntry> transparentEntries;
    terrain.renderOpaqueChunksAndCollectPasses(*ctx.world, cutoutEntries, transparentEntries, true);
    terrain.syncTransparentBatches();

    // Save transparent batch for transparent pass
    m_transparentBatch = terrain.transparentBatches();
    m_transparentPassPlan = terrain.transparentPassPlan();
    m_transparentEntries = transparentEntries;

    // Flush MDI opaque + render cutout
    worldBuffer.flushOpaque();
    terrain.renderCutoutChunks(cutoutEntries, *terrainShader);

    // Unbind textures (units 0-4)
    glBindVertexArray(0);
    for (int i = 4; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(i == 0 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D, 0);
    }
}

// ============================================================================
// Transparent rendering (water, glass, etc.)
// ============================================================================

void ForwardPipeline::renderTransparent(const FrameContext& ctx, const RenderSettings& settings) {
    if (!m_terrainRenderer || !m_worldRenderBuffer) return;
    if (!m_transparentPassPlan.hasAny()) return;

    Shader* shader = m_resourceMgr->getShader("forward_basic_terrain");
    if (!shader) return;

    auto& terrain = *m_terrainRenderer;
    auto& worldBuffer = *m_worldRenderBuffer;

    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    TerrainFrameData tfd = buildTerrainFrameData(ctx);
    TerrainRenderSettings trs = buildTerrainRenderSettings(settings);

    terrain.bindBasicForwardState(tfd, texArray, *shader,
                                   ctx.eyeInWater, 0, m_resourceMgr, trs);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // MDI path: sort back-to-front, flush transparent
    shader->setInt("uForceBaseLod", 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    for (const auto& entry : m_transparentBatch) {
        worldBuffer.addTransparent(entry.range);
    }
    worldBuffer.flushTransparent();

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    shader->setInt("uForceBaseLod", 0);

    // Unbind
    glBindVertexArray(0);
    for (int i = 4; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(i == 0 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D, 0);
    }
}

// ============================================================================
// Frame output
// ============================================================================

FrameOutput ForwardPipeline::buildFrameOutput(const FrameContext& ctx) {
    FrameOutput output{};
    if (m_commonTargets) {
        output.sceneColorTex = m_commonTargets->sceneColorTexture();
        output.sceneDepthTex = m_commonTargets->sceneDepthTexture();
    }
    // Forward pipeline does not produce deferred inputs
    output.hasDeferredInputs = false;
    output.hasDebugView = false;
    output.skipPostProcess = false;
    return output;
}

// ============================================================================
// Data conversion helpers
// ============================================================================

TerrainFrameData ForwardPipeline::buildTerrainFrameData(const FrameContext& ctx) {
    TerrainFrameData tfd{};
    tfd.view = ctx.camera.view;
    tfd.viewProj = ctx.camera.viewProj;
    tfd.cameraPos = ctx.camera.position;
    tfd.animationTime = ctx.animationTime;
    tfd.shaderTime = ctx.shaderTime;
    tfd.surfaceWetness = ctx.weather.surfaceWetness;

    // Fog
    tfd.fog.enabled = ctx.fog.enabled;
    tfd.fog.mode = ctx.fog.mode;
    tfd.fog.color = ctx.fog.color;
    tfd.fog.start = ctx.fog.startDistance;
    tfd.fog.end = ctx.fog.endDistance;
    tfd.fog.density = ctx.fog.density;

    // Sky lighting
    tfd.skyLighting.cameraPos = ctx.camera.position;
    tfd.skyLighting.sunDirection = ctx.skyColors.sunDirection;
    tfd.skyLighting.moonDirection = ctx.skyColors.moonDirection;
    tfd.skyLighting.sunLightColor = ctx.skyColors.sunLightColor;
    tfd.skyLighting.moonLightColor = ctx.skyColors.moonLightColor;
    tfd.skyLighting.skyAmbientColor = ctx.skyColors.skyAmbientColor;
    tfd.skyLighting.shadowTintColor = ctx.skyColors.shadowTintColor;
    tfd.skyLighting.horizonScatterColor = ctx.skyColors.horizonScatterColor;
    tfd.skyLighting.skyIntensity = ctx.skyIntensity;
    tfd.skyLighting.moonVisibility = ctx.skyColors.moonVisibility;
    tfd.skyLighting.directIlluminance = ctx.skyIlluminance.directIlluminance;
    tfd.skyLighting.skyIlluminance = ctx.skyIlluminance.skyIlluminance;
    tfd.skyLighting.sunIlluminance = ctx.skyIlluminance.sunIlluminance;
    tfd.skyLighting.moonIlluminance = ctx.skyIlluminance.moonIlluminance;
    tfd.skyLighting.cloudDynamicWeather = ctx.skyIlluminance.cloudDynamicWeather;

    // Atmosphere
    tfd.atmosphere.aerialStrength = ctx.atmosphere.aerialStrength;
    tfd.atmosphere.horizonScatterStrength = ctx.atmosphere.horizonScatterStrength;
    tfd.atmosphere.sunWarmth = ctx.atmosphere.sunWarmth;
    tfd.atmosphere.skyCoolness = ctx.atmosphere.skyCoolness;
    tfd.atmosphere.weatherWetness = ctx.weather.wetness;
    tfd.atmosphere.weatherStorm = ctx.weather.storm;
    tfd.atmosphere.aerialReduction = ctx.weather.aerialReduction;
    tfd.atmosphere.lightningFlash = ctx.weather.lightningFlash;
    tfd.atmosphere.surfaceWetness = ctx.weather.surfaceWetness;
    tfd.atmosphere.skyWetness = ctx.weather.skyWetness;
    tfd.atmosphere.fogWetness = ctx.weather.fogWetness;
    tfd.atmosphere.cloudWetness = ctx.weather.cloudWetness;
    tfd.atmosphere.precipitation = ctx.weather.precipitation;
    tfd.atmosphere.directWeatherOcclusion = ctx.atmosphere.directWeatherOcclusion;
    tfd.atmosphere.directWeatherOcclusionOverride = ctx.atmosphere.directWeatherOcclusionOverride;

    return tfd;
}

TerrainRenderSettings ForwardPipeline::buildTerrainRenderSettings(const RenderSettings& settings) {
    TerrainRenderSettings trs{};
    trs.rainWetSurfacesEnabled = settings.weather.wetSurfacesEnabled;
    trs.rainSurfaceRipplesEnabled = settings.weather.surfaceRipplesEnabled;
    trs.aerialPerspectiveEnabled = settings.postProcess.aerialPerspectiveEnabled;
    trs.volumetricLightEnabled = false; // Forward has no volumetric
    trs.volumetricFogEnabled = false;
    trs.volumetricFogStrength = 0.0f;
    trs.directSunStrength = settings.postProcess.directSunStrength;
    trs.skyAmbientStrength = settings.postProcess.skyAmbientStrength;
    trs.weatherSkylightScale = settings.weather.skylightScale;
    trs.minimumAmbient = settings.postProcess.minimumAmbient;
    trs.blockLightStrength = settings.postProcess.blockLightStrength;
    trs.fakeBounceStrength = settings.postProcess.fakeBounceStrength;
    trs.albedoDesaturation = settings.postProcess.albedoDesaturation;
    trs.shadowDesaturation = settings.postProcess.shadowDesaturation;
    return trs;
}
