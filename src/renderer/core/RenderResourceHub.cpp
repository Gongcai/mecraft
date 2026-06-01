//
// Created by Caiwe on 2026/3/21.
//

#include "RenderResourceHub.h"

#include "../renderers/HumanoidRenderer.h"
#include "../renderers/DropRenderer.h"
#include "../../particle/ParticleSystem.h"
#include "../mesh/ChunkMesher.h"
#include "../../ecs/GameplayRegistry.h"
#include "../shadow/ShadowMatrices.h"
#include "../shadow/ShadowCasterCuller.h"
#include "../../Paths.h"
#include "engine/platform/Time.h"
#include "../../world/DropSystem.h"
#include "../../world/World.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>
#include <array>
#include <vector>

namespace {

struct MeshingCandidate {
    int64_t chunkKey = 0;
    Chunk* chunk = nullptr;
    int scy = 0;  // Sub-chunk index
    float distanceSq = 0.0f;
    std::shared_ptr<Chunk> chunkRef;
    std::shared_ptr<Chunk> neighborPosX;
    std::shared_ptr<Chunk> neighborNegX;
    std::shared_ptr<Chunk> neighborPosZ;
    std::shared_ptr<Chunk> neighborNegZ;
};

void expandBounds(glm::vec3& minBounds, glm::vec3& maxBounds, bool& hasBounds,
                  const glm::vec3& candidateMin, const glm::vec3& candidateMax) {
    if (!hasBounds) {
        minBounds = candidateMin;
        maxBounds = candidateMax;
        hasBounds = true;
        return;
    }

    minBounds.x = std::min(minBounds.x, candidateMin.x);
    minBounds.y = std::min(minBounds.y, candidateMin.y);
    minBounds.z = std::min(minBounds.z, candidateMin.z);
    maxBounds.x = std::max(maxBounds.x, candidateMax.x);
    maxBounds.y = std::max(maxBounds.y, candidateMax.y);
    maxBounds.z = std::max(maxBounds.z, candidateMax.z);
}

#ifdef MECRAFT_DEBUG
constexpr FrustumPlane kPlaneFromIndex(const size_t index) {
    return static_cast<FrustumPlane>(index);
}
#endif

std::string resolveAtmosphereFinalLutPath() {
    const std::array<const char*, 4> candidates = {
        TEXTURES_DIR "/atmosphere/Final.lut",
        SHADERPACK_FINAL_LUT_PATH,
        "assets/textures/atmosphere/Final.lut",
        "assets/textures/shaderpacks/Atmosphere/Final.lut",
    };

    std::error_code ec;
    for (const char* candidate : candidates) {
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
        ec.clear();
    }
    return candidates.front();
}
}

RenderResourceHub::~RenderResourceHub() {
    shutdown();
}

void RenderResourceHub::init(ResourceMgr &resourceMgr) {
    m_resourceMgr = &resourceMgr;
    m_chunkForwardShader = resourceMgr.getShader("chunk_lit");
    m_transparentCompositeShader = resourceMgr.getShader("transparent_composite");
    if (m_transparentCompositeShader == nullptr) {
        m_transparentCompositeShader = m_chunkForwardShader;
    }
    m_chunkShader = m_chunkForwardShader;
    m_chunkGBufferShader = resourceMgr.getShader("chunk_gbuffer");
    m_shadowDepthShader = resourceMgr.getShader("shadow_depth");
    m_entityShadowShader = resourceMgr.getShader("entity_shadow");
    m_particleGBufferShader = resourceMgr.getShader("particle_gbuffer");

    //m_uiShader = resourceMgr.getShader("ui");
    // R8: Overlay initialization removed — handled by BlockInteractionOverlayRenderer
    m_worldRenderBuffer.init();
    m_terrainCache.init();
    m_terrainCache.setWorldRenderBuffer(&m_worldRenderBuffer);
    m_terrainCache.setChunkMeshingService(&m_meshingService);
    m_terrainCache.setRegionChunkSize(m_regionChunkSize);
    m_terrainCache.setUseMultiDrawIndirect(m_useMultiDrawIndirect);
    m_terrainRenderer.init(resourceMgr);
    m_terrainRenderer.setWorldRenderBuffer(&m_worldRenderBuffer);
    m_terrainRenderer.setTerrainRenderCache(&m_terrainCache);
    m_terrainRenderer.setUseMultiDrawIndirect(m_useMultiDrawIndirect);
    m_terrainRenderer.setCutoutDistanceLimitEnabled(m_cutoutDistanceLimitEnabled);
    m_terrainRenderer.setCutoutRenderDistanceChunks(m_cutoutRenderDistanceChunks);
#ifdef MECRAFT_DEBUG
    m_terrainRenderer.setChunkCullingDebugEnabled(m_chunkCullingDebugEnabled);
#endif
    // Phase 5c: Inject WaterCompositePass dependencies
    m_deferredTargets.init();
    const std::string atmosphereLutPath = resolveAtmosphereFinalLutPath();
    m_deferredTargets.loadAtmosphereLut(atmosphereLutPath.c_str());
    m_gameplaySkyRenderer.init(resourceMgr);
#ifdef MECRAFT_DEBUG
    if (m_debugService) {
        m_debugService->init();
    } else {
        initGpuTimers();
    }
#endif
    m_threadPool.start();
    if (!m_meshingSubmitBudgetOverridden) {
        const int workerCount = std::max(1, m_threadPool.numWorkers());
        m_meshingSubmitBudget = 2 + std::max(0, workerCount - 1);
        m_meshingMaxInFlight = std::max(4, workerCount * 2);
#ifndef MECRAFT_DEBUG
        m_meshingSubmitTimeBudgetMs = 1.0;
        m_meshingDrainBudget = std::max(2, workerCount);
        m_meshingDrainTimeBudgetMs = 1.25;
#else
        m_meshingSubmitTimeBudgetMs = 0.5;
        m_meshingDrainBudget = 1;
        m_meshingDrainTimeBudgetMs = 0.5;
#endif
    }
    m_terrainCache.setMeshingBudgets(m_meshingSubmitBudget,
                                     m_meshingMaxInFlight,
                                     static_cast<float>(m_meshingSubmitTimeBudgetMs),
                                     m_meshingDrainBudget,
                                     static_cast<float>(m_meshingDrainTimeBudgetMs),
                                     m_meshingDrainVertexBudget);
    m_meshingService.start(&m_threadPool);
}

void RenderResourceHub::shutdown() {
#ifdef MECRAFT_DEBUG
    if (m_debugService) {
        m_debugService->shutdown();
    } else {
        shutdownGpuTimers();
    }
#endif
    m_mdiMeshAllocations.clear();
    m_gameplaySkyRenderer.shutdown();
    m_deferredTargets.shutdown();
    // Only shutdown legacy terrain cache if service is not injected
    if (!m_terrainStreamingService) {
        m_terrainCache.shutdown();
        m_meshingService.shutdown();
    }
    m_worldRenderBuffer.shutdown();
    m_threadPool.shutdown();
    m_meshingInFlight.clear();
    m_deferredMeshResults.clear();
    // R5: Only cleanup overlay GL resources when no external overlay renderer is injected
    if (!m_overlayRenderer) {
        if (m_outlineVbo != 0) {
            glDeleteBuffers(1, &m_outlineVbo);
            m_outlineVbo = 0;
        }
        if (m_outlineVao != 0) {
            glDeleteVertexArrays(1, &m_outlineVao);
            m_outlineVao = 0;
        }
        if (m_breakOverlayVbo != 0) {
            glDeleteBuffers(1, &m_breakOverlayVbo);
            m_breakOverlayVbo = 0;
        }
        if (m_breakOverlayVao != 0) {
            glDeleteVertexArrays(1, &m_breakOverlayVao);
            m_breakOverlayVao = 0;
        }
        if (m_breakOverlayCrossVbo != 0) {
            glDeleteBuffers(1, &m_breakOverlayCrossVbo);
            m_breakOverlayCrossVbo = 0;
        }
        if (m_breakOverlayCrossVao != 0) {
            glDeleteVertexArrays(1, &m_breakOverlayCrossVao);
            m_breakOverlayCrossVao = 0;
        }
        m_breakOverlayVertexCount = 0;
        m_breakOverlayCrossVertexCount = 0;
    }
    m_chunkShader = nullptr;
    m_chunkForwardShader = nullptr;
    m_transparentCompositeShader = nullptr;
    m_chunkGBufferShader = nullptr;
    m_shadowDepthShader = nullptr;
    m_entityGBufferShader = nullptr;
    m_entityShadowShader = nullptr;
    m_particleGBufferShader = nullptr;
    m_outlineShader = nullptr;
    m_breakOverlayShader = nullptr;
    m_deferredFrameActive = false;
}

void RenderResourceHub::setMeshingSubmitBudget(const int budget) {
    if (m_terrainStreamingService) {
        m_terrainStreamingService->setMeshingSubmitBudget(budget);
        return;
    }
    m_meshingSubmitBudget = std::max(1, budget);
    m_meshingSubmitBudgetOverridden = true;
    m_terrainCache.setMeshingBudgets(m_meshingSubmitBudget,
                                     m_meshingMaxInFlight,
                                     static_cast<float>(m_meshingSubmitTimeBudgetMs),
                                     m_meshingDrainBudget,
                                     static_cast<float>(m_meshingDrainTimeBudgetMs),
                                     m_meshingDrainVertexBudget);
}

void RenderResourceHub::setRegionChunkSize(const int chunkSize) {
    if (m_terrainStreamingService) {
        m_terrainStreamingService->setRegionChunkSize(chunkSize);
        return;
    }
    m_regionChunkSize = std::max(1, chunkSize);
    m_terrainCache.setRegionChunkSize(m_regionChunkSize);
}

void RenderResourceHub::setTerrainStreamingService(TerrainStreamingService* svc) {
    m_terrainStreamingService = svc;
    if (svc) {
        // Update TerrainRenderer to use the service's cache
        m_terrainRenderer.setTerrainRenderCache(&svc->terrainCache());
        // Update WorldRenderBuffer reference in the service's cache
        svc->terrainCache().setWorldRenderBuffer(&m_worldRenderBuffer);
        svc->terrainCache().setUseMultiDrawIndirect(m_useMultiDrawIndirect);
    }
}

void RenderResourceHub::setAtlasAnisotropy(const float anisotropy) {
    if (m_resourceMgr == nullptr) {
        return;
    }
    m_resourceMgr->setAtlasAnisotropy(anisotropy);
}

void RenderResourceHub::setFogEnabled(const bool enabled) {
    m_fogSettings.enabled = enabled;
}

void RenderResourceHub::setFogMode(const FogMode mode) {
    m_fogSettings.mode = mode;
}

void RenderResourceHub::setFogColor(const glm::vec3& color) {
    m_fogSettings.color.x = std::clamp(color.x, 0.0f, 1.0f);
    m_fogSettings.color.y = std::clamp(color.y, 0.0f, 1.0f);
    m_fogSettings.color.z = std::clamp(color.z, 0.0f, 1.0f);
}

void RenderResourceHub::setFogLinearDistances(const float startDistance, const float endDistance) {
    const float startClamped = std::max(0.0f, startDistance);
    const float endClamped = std::max(startClamped + 0.1f, endDistance);
    m_fogSettings.startDistance = startClamped;
    m_fogSettings.endDistance = endClamped;
}

void RenderResourceHub::setFogDensity(const float density) {
    m_fogSettings.density = std::max(0.0001f, density);
}

void RenderResourceHub::setFogAutoDistanceEnabled(const bool enabled) {
    m_fogSettings.autoDistanceByRenderDistance = enabled;
}

void RenderResourceHub::setFogAutoEndOffsetChunks(const float offsetChunks) {
    m_fogSettings.autoEndOffsetChunks = std::clamp(offsetChunks, -2.0f, 1.0f);
}

void RenderResourceHub::setFogAutoFadeWidthChunks(const float fadeWidthChunks) {
    m_fogSettings.autoFadeWidthChunks = std::clamp(fadeWidthChunks, 0.25f, 4.0f);
}

RenderResourceHub::FogSettings RenderResourceHub::getFogSettings() const {
    return m_fogSettings;
}

void RenderResourceHub::setHeldBlockLightValue(const int value) {
    m_heldBlockLightValue = std::clamp(value, 0, 15);
}

void RenderResourceHub::setDebugLightMode(const int mode) {
    m_debugLightMode = std::clamp(mode, 0, 3);
}

int RenderResourceHub::getDebugLightMode() const {
    return m_debugLightMode;
}

void RenderResourceHub::setSettings(const RenderSettings& settings) {
    m_settings = settings;
    // Shadow clamps
    m_settings.shadow.resolution = std::clamp(m_settings.shadow.resolution, 256, 8192);
    m_settings.shadow.distance = std::clamp(m_settings.shadow.distance, 64.0f, 512.0f);
    m_settings.shadow.softness = std::clamp(m_settings.shadow.softness, 0.1f, 8.0f);
    m_settings.shadow.pcssStrength = std::clamp(m_settings.shadow.pcssStrength, 0.0f, 1.5f);
    m_settings.shadow.constantBias = std::clamp(m_settings.shadow.constantBias, 0.0f, 0.01f);
    m_settings.shadow.slopeBias = std::clamp(m_settings.shadow.slopeBias, 0.0f, 0.03f);
    m_settings.shadow.normalOffset = std::clamp(m_settings.shadow.normalOffset, 0.0f, 0.25f);
    m_settings.shadow.contactShadowStrength = std::clamp(m_settings.shadow.contactShadowStrength, 0.0f, 1.0f);
    // Cloud clamps
    m_settings.cloud.shadowStrength = std::clamp(m_settings.cloud.shadowStrength, 0.0f, 1.0f);
    m_settings.cloud.shadowScale = std::clamp(m_settings.cloud.shadowScale, 0.0005f, 0.04f);
    m_settings.cloud.shadowSpeed = std::clamp(m_settings.cloud.shadowSpeed, 0.0f, 0.20f);
    m_settings.cloud.timeScale = std::clamp(m_settings.cloud.timeScale, 0.05f, 2.0f);
    m_settings.cloud.sceneCloudCompositeStrength = std::clamp(m_settings.cloud.sceneCloudCompositeStrength, 0.0f, 1.0f);
    // Post-process clamps
    m_settings.postProcess.bloomThreshold = std::clamp(m_settings.postProcess.bloomThreshold, 0.0f, 4.0f);
    m_settings.postProcess.bloomStrength = std::clamp(m_settings.postProcess.bloomStrength, 0.0f, 20.0f);
    m_settings.postProcess.autoExposureMin = std::clamp(m_settings.postProcess.autoExposureMin, 0.001f, 64.0f);
    m_settings.postProcess.autoExposureMax = std::clamp(m_settings.postProcess.autoExposureMax, m_settings.postProcess.autoExposureMin, 64.0f);
    m_settings.postProcess.autoExposureSpeed = std::clamp(m_settings.postProcess.autoExposureSpeed, 0.05f, 12.0f);
    m_settings.postProcess.autoExposureBias = std::clamp(m_settings.postProcess.autoExposureBias, -3.0f, 3.0f);
    m_settings.postProcess.sunRayStrength = std::clamp(m_settings.postProcess.sunRayStrength, 0.0f, 1.0f);
    m_settings.postProcess.tonemapMode = std::clamp(m_settings.postProcess.tonemapMode, 0, 5);
    m_settings.postProcess.colorTemperature = std::clamp(m_settings.postProcess.colorTemperature, 0.0f, 2.0f);
    m_settings.postProcess.vibrance = std::clamp(m_settings.postProcess.vibrance, -1.0f, 1.0f);
    m_settings.postProcess.highlightCompression = std::clamp(m_settings.postProcess.highlightCompression, 0.0f, 1.5f);
    m_settings.postProcess.filmEmulationStrength = std::clamp(m_settings.postProcess.filmEmulationStrength, 0.0f, 1.0f);
    m_settings.postProcess.redModifierStrength = std::clamp(m_settings.postProcess.redModifierStrength, 0.0f, 1.0f);
    m_settings.postProcess.colorLumaR = std::clamp(m_settings.postProcess.colorLumaR, 0.5f, 1.5f);
    m_settings.postProcess.colorLumaG = std::clamp(m_settings.postProcess.colorLumaG, 0.5f, 1.5f);
    m_settings.postProcess.colorLumaB = std::clamp(m_settings.postProcess.colorLumaB, 0.5f, 1.5f);
    m_settings.postProcess.albedoDesaturation = std::clamp(m_settings.postProcess.albedoDesaturation, 0.0f, 0.8f);
    m_settings.postProcess.sunWarmth = std::clamp(m_settings.postProcess.sunWarmth, 0.0f, 1.5f);
    m_settings.postProcess.skyCoolness = std::clamp(m_settings.postProcess.skyCoolness, 0.0f, 1.0f);
    m_settings.postProcess.shadowDesaturation = std::clamp(m_settings.postProcess.shadowDesaturation, 0.0f, 1.0f);
    m_settings.postProcess.splitToneStrength = std::clamp(m_settings.postProcess.splitToneStrength, 0.0f, 1.0f);
    m_settings.postProcess.vignetteStrength = std::clamp(m_settings.postProcess.vignetteStrength, 0.0f, 0.5f);
    m_settings.postProcess.shadowTintStrength = std::clamp(m_settings.postProcess.shadowTintStrength, 0.0f, 1.0f);
    m_settings.postProcess.directSunStrength = std::clamp(m_settings.postProcess.directSunStrength, 0.0f, 4.0f);
    m_settings.postProcess.skyAmbientStrength = std::clamp(m_settings.postProcess.skyAmbientStrength, 0.0f, 2.5f);
    m_settings.postProcess.minimumAmbient = std::clamp(m_settings.postProcess.minimumAmbient, 0.0f, 0.6f);
    m_settings.postProcess.shadowMinLight = std::clamp(m_settings.postProcess.shadowMinLight, 0.0f, 0.6f);
    m_settings.postProcess.shadowContrast = std::clamp(m_settings.postProcess.shadowContrast, 0.25f, 3.0f);
    m_settings.postProcess.blockLightStrength = std::clamp(m_settings.postProcess.blockLightStrength, 0.0f, 3.0f);
    m_settings.postProcess.fakeBounceStrength = std::clamp(m_settings.postProcess.fakeBounceStrength, 0.0f, 0.5f);
    m_settings.postProcess.aerialStrength = std::clamp(m_settings.postProcess.aerialStrength, 0.0f, 2.0f);
    m_settings.postProcess.horizonScatterStrength = std::clamp(m_settings.postProcess.horizonScatterStrength, 0.0f, 2.0f);
    m_settings.postProcess.noiseDitherStrength = std::clamp(m_settings.postProcess.noiseDitherStrength, 0.0f, 0.08f);
    m_settings.postProcess.sharpenStrength = std::clamp(m_settings.postProcess.sharpenStrength, 0.0f, 1.0f);
    m_settings.postProcess.exposure = std::clamp(m_settings.postProcess.exposure, 0.1f, 50.0f);
    m_settings.postProcess.gamma = std::clamp(m_settings.postProcess.gamma, 1.0f, 3.5f);
    m_settings.postProcess.saturation = std::clamp(m_settings.postProcess.saturation, 0.0f, 3.0f);
    m_settings.postProcess.contrast = std::clamp(m_settings.postProcess.contrast, 0.25f, 3.0f);
    // Reflection clamps
    m_settings.reflection.sceneReflectionCompositeStrength = std::clamp(m_settings.reflection.sceneReflectionCompositeStrength, 0.0f, 1.0f);
    // Volumetric clamps
    m_settings.volumetric.qualityTier = std::clamp(m_settings.volumetric.qualityTier, 0, 3);
    m_settings.volumetric.shadowBiasScale = std::clamp(m_settings.volumetric.shadowBiasScale, 0.0f, 4.0f);
    m_settings.volumetric.fogStrength = std::clamp(m_settings.volumetric.fogStrength, 0.0f, 2.0f);
    m_settings.volumetric.underwaterLightStrength = std::clamp(m_settings.volumetric.underwaterLightStrength, 0.0f, 2.0f);
    // SSAO clamps
    m_settings.ssao.radius = std::clamp(m_settings.ssao.radius, 0.1f, 16.0f);
    m_settings.ssao.strength = std::clamp(m_settings.ssao.strength, 0.0f, 4.0f);
    // Debug clamps
    m_settings.debug.viewMode = std::clamp(m_settings.debug.viewMode, 0, 80);
    m_settings.debug.reflectionDebugMode = std::clamp(m_settings.debug.reflectionDebugMode, 0, 30);
    m_settings.debug.disableGreedyMeshing = false;
    ChunkMesher::setDebugDisableGreedyMeshing(m_settings.debug.disableGreedyMeshing);
}

const RenderSettings& RenderResourceHub::getSettings() const {
    return m_settings;
}

float RenderResourceHub::getAtlasAnisotropy() const {
    if (m_resourceMgr == nullptr) {
        return 1.0f;
    }
    return m_resourceMgr->getAtlasAnisotropy();
}

float RenderResourceHub::getAtlasMaxAnisotropy() const {
    if (m_resourceMgr == nullptr) {
        return 1.0f;
    }
    return m_resourceMgr->getAtlasMaxAnisotropy();
}

#ifdef MECRAFT_DEBUG
void RenderResourceHub::setChunkCullingDebugEnabled(const bool enabled) {
    m_chunkCullingDebugEnabled = enabled;
    m_terrainRenderer.setChunkCullingDebugEnabled(enabled);
}

int RenderResourceHub::getMeshingSubmitBudget() const {
    return m_terrainStreamingService ? m_terrainStreamingService->meshingSubmitBudget() : m_meshingSubmitBudget;
}

int RenderResourceHub::getRegionChunkSize() const {
    return m_terrainStreamingService ? m_terrainStreamingService->regionChunkSize() : m_regionChunkSize;
}

bool RenderResourceHub::isChunkCullingDebugEnabled() const {
    return m_chunkCullingDebugEnabled;
}

RenderResourceHub::MeshingFrameStats RenderResourceHub::getMeshingFrameStats() const {
    if (m_terrainStreamingService) {
        return m_terrainStreamingService->getMeshingFrameStats();
    }
    MeshingFrameStats stats;
    stats.submitBudget = m_meshingSubmitBudget;
    stats.submitted = m_meshingSubmittedThisFrame;
    stats.completed = m_meshingCompletedThisFrame;
    stats.inFlight = static_cast<int>(m_meshingInFlight.size());
    stats.staleDropped = m_meshingStaleDroppedThisFrame;
    stats.deferredResults = m_terrainCache.deferredMeshResultCount();
    stats.lastBuildMs = m_lastMeshingBuildMs;
    stats.averageBuildMs = m_meshingCompletedThisFrame > 0
        ? (m_meshingBuildMsThisFrame / static_cast<double>(m_meshingCompletedThisFrame))
        : 0.0;
    stats.lastOpaqueFacesBeforeGreedy = m_lastOpaqueFacesBeforeGreedy;
    stats.lastOpaqueFacesAfterGreedy = m_lastOpaqueFacesAfterGreedy;
    stats.lastTransparentFacesBeforeGreedy = m_lastTransparentFacesBeforeGreedy;
    stats.lastTransparentFacesAfterGreedy = m_lastTransparentFacesAfterGreedy;
    stats.lastOpaqueVertexCount = m_lastOpaqueVertexCount;
    return stats;
}

CullingFrameStats RenderResourceHub::getCullingFrameStats() const {
    if (m_debugService) {
        return m_debugService->getCullingFrameStats();
    }
    CullingFrameStats stats;
    stats.regionTests = m_regionTestsThisFrame;
    stats.regionPassed = m_regionPassedThisFrame;
    stats.columnTests = m_columnTestsThisFrame;
    stats.columnPassed = m_columnPassedThisFrame;
    stats.chunkTests = m_chunkTestsThisFrame;
    stats.chunkPassed = m_chunkPassedThisFrame;
    stats.chunkCulled = m_chunkCulledThisFrame;
    stats.chunkCulledByPlane = m_chunkCulledByPlaneThisFrame;
    return stats;
}

GpuFrameStats RenderResourceHub::getGpuFrameStats() const {
    if (m_debugService) {
        return m_debugService->getGpuFrameStats();
    }
    return m_gpuFrameStats;
}

RenderWorkStats RenderResourceHub::getRenderWorkStats() const {
    if (m_debugService) {
        return m_debugService->getRenderWorkStats();
    }
    RenderWorkStats stats;
    stats.blockVertexBytes = sizeof(BlockVertex);
    stats.opaqueCommands = m_worldRenderBuffer.opaqueCommandCount();
    stats.cutoutCommands = m_worldRenderBuffer.cutoutCommandCount();
    stats.transparentCommands = m_worldRenderBuffer.transparentCommandCount();
    stats.transparentGenericCommands = m_transparentPassPlan.genericCommands;
    stats.transparentWaterCommands = m_transparentPassPlan.waterCommands;
    stats.opaqueLogicalCommands = m_worldRenderBuffer.opaqueLogicalCommandCount();
    stats.cutoutLogicalCommands = m_worldRenderBuffer.cutoutLogicalCommandCount();
    stats.transparentLogicalCommands = m_worldRenderBuffer.transparentLogicalCommandCount();
    stats.opaquePoolCapacityVertices = m_worldRenderBuffer.opaqueCapacityVertices();
    stats.cutoutPoolCapacityVertices = m_worldRenderBuffer.cutoutCapacityVertices();
    stats.transparentPoolCapacityVertices = m_worldRenderBuffer.transparentCapacityVertices();
    stats.opaquePoolUsedVertices = m_worldRenderBuffer.opaqueUsedVertices();
    stats.cutoutPoolUsedVertices = m_worldRenderBuffer.cutoutUsedVertices();
    stats.transparentPoolUsedVertices = m_worldRenderBuffer.transparentUsedVertices();
    stats.opaquePoolFragmentation = m_worldRenderBuffer.opaqueFragmentationRatio();
    stats.cutoutPoolFragmentation = m_worldRenderBuffer.cutoutFragmentationRatio();
    stats.transparentPoolFragmentation = m_worldRenderBuffer.transparentFragmentationRatio();
    stats.opaqueVertices = m_worldRenderBuffer.opaqueVertexCount();
    stats.cutoutVertices = m_worldRenderBuffer.cutoutVertexCount();
    stats.transparentVertices = m_worldRenderBuffer.transparentVertexCount();
    stats.transparentGenericVertices = m_transparentPassPlan.genericVertices;
    stats.transparentWaterVertices = m_transparentPassPlan.waterVertices;
    stats.cutoutCandidates = m_cutoutCandidatesThisFrame;
    stats.cutoutSkippedByDistance = m_cutoutSkippedByDistanceThisFrame;
    stats.mdiSubChunkTests = m_mdiSubChunkTestsThisFrame;
    stats.mdiSubChunksCulled = m_mdiSubChunksCulledThisFrame;
    stats.meshUploadBytesThisFrame = m_meshUploadBytesThisFrame;
    stats.meshUploadVerticesThisFrame = m_meshUploadVerticesThisFrame;
    stats.meshUploadDeferredCount = m_meshUploadDeferredCount;
    stats.worldBufferExpandCount = m_worldBufferExpandCountThisFrame;
    stats.worldBufferUploadMs = m_worldBufferUploadMsThisFrame;
    return stats;
}

void RenderResourceHub::setGpuTimerEnabled(const bool enabled) {
    if (m_debugService) {
        m_debugService->setGpuTimerEnabled(enabled);
        return;
    }
    m_gpuTimerEnabled = enabled;
}

bool RenderResourceHub::isGpuTimerEnabled() const {
    if (m_debugService) {
        return m_debugService->isGpuTimerEnabled();
    }
    return m_gpuTimerEnabled;
}

void RenderResourceHub::setCutoutDistanceLimitEnabled(const bool enabled) {
    m_cutoutDistanceLimitEnabled = enabled;
    m_terrainRenderer.setCutoutDistanceLimitEnabled(enabled);
}

bool RenderResourceHub::isCutoutDistanceLimitEnabled() const {
    return m_cutoutDistanceLimitEnabled;
}

void RenderResourceHub::setCutoutRenderDistanceChunks(const float distanceChunks) {
    m_cutoutRenderDistanceChunks = std::clamp(distanceChunks, 1.0f, 32.0f);
    m_terrainRenderer.setCutoutRenderDistanceChunks(m_cutoutRenderDistanceChunks);
}

float RenderResourceHub::getCutoutRenderDistanceChunks() const {
    return m_cutoutRenderDistanceChunks;
}

const std::array<float, RenderResourceHub::MESHING_HISTORY_SIZE>& RenderResourceHub::getMeshingSubmittedHistory() const {
    if (m_terrainStreamingService) {
        return m_terrainStreamingService->getMeshingSubmittedHistory();
    }
    return m_meshingSubmittedHistory;
}

const std::array<float, RenderResourceHub::MESHING_HISTORY_SIZE>& RenderResourceHub::getMeshingCompletedHistory() const {
    if (m_terrainStreamingService) {
        return m_terrainStreamingService->getMeshingCompletedHistory();
    }
    return m_meshingCompletedHistory;
}

const std::array<float, RenderResourceHub::MESHING_HISTORY_SIZE>& RenderResourceHub::getMeshingInFlightHistory() const {
    if (m_terrainStreamingService) {
        return m_terrainStreamingService->getMeshingInFlightHistory();
    }
    return m_meshingInFlightHistory;
}

size_t RenderResourceHub::getMeshingHistoryCount() const {
    if (m_terrainStreamingService) {
        return m_terrainStreamingService->getMeshingHistoryCount();
    }
    return m_meshingHistoryCount;
}
#endif

void RenderResourceHub::drawFullscreen(Shader& shader) const {
    shader.use();
    glBindVertexArray(m_deferredTargets.fullscreenVao());
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

glm::vec3 RenderResourceHub::currentShadowLightDirection(const World& world, bool* moonShadowActive) const {
    const GameplaySkyRenderer::SkyColors skyColors = m_gameplaySkyRenderer.computeSkyColors(world.getDayNightSystem());
    // Use a temporary ShadowRenderer to compute light direction without modifying state.
    shadow::ShadowRenderer temp;
    return temp.computeLightDirection(skyColors, moonShadowActive);
}

void RenderResourceHub::captureCurrentFramebuffer() {
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_capturedFramebuffer);
    glGetIntegerv(GL_VIEWPORT, m_capturedViewport);
}

void RenderResourceHub::restoreCapturedFramebufferViewport(const Window& window) {
    const int fallbackWidth = std::max(1, window.getWidth());
    const int fallbackHeight = std::max(1, window.getHeight());
    const int width = m_capturedViewport[2] > 0 ? m_capturedViewport[2] : fallbackWidth;
    const int height = m_capturedViewport[3] > 0 ? m_capturedViewport[3] : fallbackHeight;
    m_deferredTargets.bindDefaultLike(m_capturedFramebuffer, width, height);
}

void RenderResourceHub::collectAndDrawOpaqueChunks(const World& world,
                                                  std::vector<ChunkRenderEntry>& cutoutEntries,
                                                  std::vector<ChunkRenderEntry>& transparentEntries,
                                                  const bool frustumCull,
                                                  const float maxCameraDistance,
                                                  shadow::ShadowCasterCuller* shadowCuller) {
    syncChunkRenderColumns(world);
    TerrainRenderCache& cache = m_terrainStreamingService ? m_terrainStreamingService->terrainCache() : m_terrainCache;
    std::vector<ChunkRenderColumnCache>& chunkRenderColumns = cache.chunkRenderColumns();
    if (chunkRenderColumns.empty()) {
        return;
    }

    GLuint lastOpaqueVao = 0;
    const bool distanceCull = maxCameraDistance > 0.0f || shadowCuller != nullptr;
    const float maxCameraDistanceSq = maxCameraDistance * maxCameraDistance;

    // When a shadow culler is provided, use Iris BoxCuller AABB cube semantics.
    // Otherwise, fall back to the original XZ clamped distance check.
    auto boundsWithinCameraDistance = [&](const glm::vec3& boundsMin, const glm::vec3& boundsMax) {
        if (!distanceCull) {
            return true;
        }
        if (shadowCuller) {
            const bool visible = shadowCuller->isAabbVisible(boundsMin, boundsMax);
            if (visible) {
                // Compute distance from camera to AABB center for debug
                const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
                const float dist = glm::length(center - m_cameraPos);
                shadowCuller->recordVisible(dist);
            } else {
                shadowCuller->recordCulled();
            }
            return visible;
        }
        const float clampedX = std::clamp(m_cameraPos.x, boundsMin.x, boundsMax.x);
        const float clampedZ = std::clamp(m_cameraPos.z, boundsMin.z, boundsMax.z);
        const float dx = clampedX - m_cameraPos.x;
        const float dz = clampedZ - m_cameraPos.z;
        return dx * dx + dz * dz <= maxCameraDistanceSq;
    };

    size_t regionBegin = 0;
    while (regionBegin < chunkRenderColumns.size()) {
        size_t regionEnd = regionBegin + 1;
        const ChunkRenderColumnCache& regionFirst = chunkRenderColumns[regionBegin];
        while (regionEnd < chunkRenderColumns.size()) {
            const ChunkRenderColumnCache& candidate = chunkRenderColumns[regionEnd];
            if (candidate.regionX != regionFirst.regionX || candidate.regionZ != regionFirst.regionZ) {
                break;
            }
            ++regionEnd;
        }

        bool regionHasBounds = false;
        glm::vec3 regionMin(0.0f);
        glm::vec3 regionMax(0.0f);
        int regionCandidateCount = 0;
        for (size_t i = regionBegin; i < regionEnd; ++i) {
            ChunkRenderColumnCache& column = chunkRenderColumns[i];
            refreshChunkRenderColumnCache(column);
            if (!column.columnHasBounds) {
                continue;
            }
            expandBounds(regionMin, regionMax, regionHasBounds, column.columnBoundsMin, column.columnBoundsMax);
            regionCandidateCount += (column.aggregatedPresent ? 1 : 0) + column.transparentCount;
        }

        if (!regionHasBounds) {
            regionBegin = regionEnd;
            continue;
        }

        if (!boundsWithinCameraDistance(regionMin, regionMax)) {
            regionBegin = regionEnd;
            continue;
        }

#ifdef MECRAFT_DEBUG
        ++m_regionTestsThisFrame;
        FrustumPlane culledPlane = FrustumPlane::Count;
        if (frustumCull && !isChunkInFrustum(regionMin, regionMax, m_chunkCullingDebugEnabled ? &culledPlane : nullptr)) {
            if (m_chunkCullingDebugEnabled) {
                recordChunkCull(culledPlane, regionCandidateCount);
            }
            regionBegin = regionEnd;
            continue;
        }
        ++m_regionPassedThisFrame;
#else
        if (frustumCull && !isChunkInFrustum(regionMin, regionMax)) {
            regionBegin = regionEnd;
            continue;
        }
#endif

        for (size_t i = regionBegin; i < regionEnd; ++i) {
            ChunkRenderColumnCache& column = chunkRenderColumns[i];
            if (column.chunk == nullptr || !column.columnHasBounds) {
                continue;
            }

            const int columnCandidateCount = (column.aggregatedPresent ? 1 : 0) + column.transparentCount;

            if (!boundsWithinCameraDistance(column.columnBoundsMin, column.columnBoundsMax)) {
                continue;
            }

#ifdef MECRAFT_DEBUG
            ++m_columnTestsThisFrame;
            FrustumPlane columnCulledPlane = FrustumPlane::Count;
            if (frustumCull && !isChunkInFrustum(column.columnBoundsMin, column.columnBoundsMax,
                                  m_chunkCullingDebugEnabled ? &columnCulledPlane : nullptr)) {
                if (m_chunkCullingDebugEnabled) {
                    recordChunkCull(columnCulledPlane, columnCandidateCount);
                }
                continue;
            }
            ++m_columnPassedThisFrame;
#else
            if (frustumCull && !isChunkInFrustum(column.columnBoundsMin, column.columnBoundsMax)) {
                continue;
            }
#endif

            if (m_useMultiDrawIndirect) {
                // MDI path: iterate per-sub-chunk meshes directly.
                const glm::ivec3 offset = column.chunk->getWorldOffset();
                const float cutoutLimitBlocks = m_cutoutRenderDistanceChunks * static_cast<float>(Chunk::SIZE_X);
                const float cutoutLimitSq = cutoutLimitBlocks * cutoutLimitBlocks;
                for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
                    const SubChunk* sc = column.chunk->getSubChunk(scy);
                    if (!sc) continue;
                    const SubChunkMesh& mesh = sc->getMesh();
                    if (!mesh.inGlobalPool) continue;
                    if (mesh.opaqueRange.vertexCount == 0 &&
                        mesh.cutoutRange.vertexCount == 0 &&
                        mesh.cutoutDistanceRange.vertexCount == 0 &&
                        mesh.transparentRange.vertexCount == 0 &&
                        mesh.waterRange.vertexCount == 0) {
                        continue;
                    }

#ifdef MECRAFT_DEBUG
                    ++m_mdiSubChunkTestsThisFrame;
                    ++m_chunkTestsThisFrame;
#endif
                    const int yBase = scy * SubChunk::SIZE;
                    const glm::vec3 fallbackMin(
                        static_cast<float>(offset.x),
                        static_cast<float>(offset.y + yBase),
                        static_cast<float>(offset.z));
                    const glm::vec3 fallbackMax(
                        static_cast<float>(offset.x + Chunk::SIZE_X),
                        static_cast<float>(offset.y + yBase + SubChunk::SIZE),
                        static_cast<float>(offset.z + Chunk::SIZE_Z));
                    const glm::vec3 boundsMin = mesh.hasBounds ? mesh.boundsMin : fallbackMin;
                    const glm::vec3 boundsMax = mesh.hasBounds ? mesh.boundsMax : fallbackMax;
                    if (!boundsWithinCameraDistance(boundsMin, boundsMax)) {
                        continue;
                    }
#ifdef MECRAFT_DEBUG
                    FrustumPlane subChunkCulledPlane = FrustumPlane::Count;
                    if (frustumCull && !isChunkInFrustum(boundsMin, boundsMax,
                                          m_chunkCullingDebugEnabled ? &subChunkCulledPlane : nullptr)) {
                        ++m_mdiSubChunksCulledThisFrame;
                        if (m_chunkCullingDebugEnabled) {
                            recordChunkCull(subChunkCulledPlane, 1);
                        }
                        continue;
                    }
                    ++m_chunkPassedThisFrame;
#else
                    if (frustumCull && !isChunkInFrustum(boundsMin, boundsMax)) {
                        continue;
                    }
#endif

                    if (mesh.opaqueRange.vertexCount > 0) {
                        m_worldRenderBuffer.addOpaque(mesh.opaqueRange);
                    }
                    if (mesh.cutoutRange.vertexCount > 0) {
                        m_worldRenderBuffer.addCutout(mesh.cutoutRange);
                    }
                    if (mesh.cutoutDistanceRange.vertexCount > 0) {
                        const glm::vec3 sectionCenter(
                            static_cast<float>(offset.x) + Chunk::SIZE_X * 0.5f,
                            static_cast<float>(offset.y + yBase) + SubChunk::SIZE * 0.5f,
                            static_cast<float>(offset.z) + Chunk::SIZE_Z * 0.5f);
                        const glm::vec2 toCameraXZ(sectionCenter.x - m_cameraPos.x,
                                                   sectionCenter.z - m_cameraPos.z);
                        const float distanceSq = glm::dot(toCameraXZ, toCameraXZ);
#ifdef MECRAFT_DEBUG
                        ++m_cutoutCandidatesThisFrame;
#endif
                        if (!m_cutoutDistanceLimitEnabled || distanceSq <= cutoutLimitSq) {
                            m_worldRenderBuffer.addCutout(mesh.cutoutDistanceRange);
                        }
#ifdef MECRAFT_DEBUG
                        else {
                            ++m_cutoutSkippedByDistanceThisFrame;
                        }
#endif
                    }
                    if (mesh.transparentRange.vertexCount > 0) {
                        const glm::vec3 sectionCenter(
                            static_cast<float>(offset.x) + Chunk::SIZE_X * 0.5f,
                            static_cast<float>(offset.y + yBase) + SubChunk::SIZE * 0.5f,
                            static_cast<float>(offset.z) + Chunk::SIZE_Z * 0.5f);
                        const glm::vec3 toCamera = sectionCenter - m_cameraPos;
                        addTransparentBatch(mesh.transparentRange, glm::dot(toCamera, toCamera), TransparentBatchKind::Generic);
                    }
                    if (mesh.waterRange.vertexCount > 0) {
                        const glm::vec3 sectionCenter(
                            static_cast<float>(offset.x) + Chunk::SIZE_X * 0.5f,
                            static_cast<float>(offset.y + yBase) + SubChunk::SIZE * 0.5f,
                            static_cast<float>(offset.z) + Chunk::SIZE_Z * 0.5f);
                        const glm::vec3 toCamera = sectionCenter - m_cameraPos;
                        addTransparentBatch(mesh.waterRange, glm::dot(toCamera, toCamera), TransparentBatchKind::Water);
                    }
                }
            } else {
                // Old path: draw from column aggregate.
                if (column.aggregatedPresent) {
#ifdef MECRAFT_DEBUG
                    ++m_chunkTestsThisFrame;
                    if (frustumCull && !isChunkInFrustum(column.aggregatedBoundsMin, column.aggregatedBoundsMax,
                                          m_chunkCullingDebugEnabled ? &culledPlane : nullptr)) {
                        if (m_chunkCullingDebugEnabled) {
                            recordChunkCull(culledPlane, 1);
                        }
                    } else {
                        ++m_chunkPassedThisFrame;
#else
                    if (!frustumCull || isChunkInFrustum(column.aggregatedBoundsMin, column.aggregatedBoundsMax)) {
#endif
                        const SubChunkMesh& mesh = column.chunk->getColumnMesh();

                        if (column.aggregatedHasOpaque && mesh.vertexCount > 0) {
                            if (lastOpaqueVao != mesh.vao) {
                                glBindVertexArray(mesh.vao);
                                lastOpaqueVao = mesh.vao;
                            }
                            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.vertexCount));
                            ++drawCallCount;
                        }

                        if (column.aggregatedHasCutout &&
                            (mesh.cutoutVertexCount > 0 || mesh.cutoutDistanceVertexCount > 0)) {
                            cutoutEntries.push_back({column.chunk, -1, true});
                        }
                    }
                }

                for (int transparentIndex = 0; transparentIndex < column.transparentCount; ++transparentIndex) {
                    const int scy = column.transparentScys[transparentIndex];
                    const TransparentSubChunkCache& transparent = column.transparentSubChunks[scy];

#ifdef MECRAFT_DEBUG
                    ++m_chunkTestsThisFrame;
                    if (frustumCull && !isChunkInFrustum(transparent.boundsMin, transparent.boundsMax,
                                          m_chunkCullingDebugEnabled ? &culledPlane : nullptr)) {
                        if (m_chunkCullingDebugEnabled) {
                            recordChunkCull(culledPlane, 1);
                        }
                        continue;
                    }
                    ++m_chunkPassedThisFrame;
#else
                    if (frustumCull && !isChunkInFrustum(transparent.boundsMin, transparent.boundsMax)) {
                        continue;
                    }
#endif

                    transparentEntries.push_back({column.chunk, scy, false});
                }
            }
        }

        regionBegin = regionEnd;
    }
}

void RenderResourceHub::syncChunkRenderColumns(const World& world) {
    if (m_terrainStreamingService) {
        m_terrainStreamingService->terrainCache().syncChunkRenderColumns(world);
    } else {
        m_terrainCache.syncChunkRenderColumns(world);
    }
    m_chunkRenderColumns.clear();
}

void RenderResourceHub::releaseMdiAllocation(const SubChunkGpuKey& key) {
    if (m_terrainStreamingService) {
        m_terrainStreamingService->releaseMdiAllocation(key);
        return;
    }
    m_terrainCache.releaseMdiAllocation(key);
    m_mdiMeshAllocations = m_terrainCache.mdiMeshAllocations();
}

void RenderResourceHub::releaseStaleMdiAllocations(const World& world) {
    if (m_terrainStreamingService) {
        m_terrainStreamingService->releaseStaleMdiAllocations(world);
        return;
    }
    m_terrainCache.releaseStaleMdiAllocations(world);
    m_mdiMeshAllocations = m_terrainCache.mdiMeshAllocations();
}

void RenderResourceHub::refreshChunkRenderColumnCache(ChunkRenderColumnCache& column) {
    if (m_terrainStreamingService) {
        m_terrainStreamingService->terrainCache().refreshChunkRenderColumnCache(column);
    } else {
        m_terrainCache.refreshChunkRenderColumnCache(column);
    }
}

void RenderResourceHub::syncTerrainCacheFrameStats() {
    // When service is injected, stats are accessed through the service's cache
    if (m_terrainStreamingService) {
        return;
    }
#ifdef MECRAFT_DEBUG
    m_meshingSubmittedThisFrame = m_terrainCache.meshingSubmittedThisFrame();
    m_meshingCompletedThisFrame = m_terrainCache.meshingCompletedThisFrame();
    m_meshingStaleDroppedThisFrame = m_terrainCache.meshingStaleDroppedThisFrame();
    m_meshingBuildMsThisFrame = m_terrainCache.meshingBuildMsThisFrame();
    m_lastMeshingBuildMs = m_terrainCache.lastMeshingBuildMs();
    m_lastOpaqueFacesBeforeGreedy = m_terrainCache.lastOpaqueFacesBeforeGreedy();
    m_lastOpaqueFacesAfterGreedy = m_terrainCache.lastOpaqueFacesAfterGreedy();
    m_lastTransparentFacesBeforeGreedy = m_terrainCache.lastTransparentFacesBeforeGreedy();
    m_lastTransparentFacesAfterGreedy = m_terrainCache.lastTransparentFacesAfterGreedy();
    m_lastOpaqueVertexCount = m_terrainCache.lastOpaqueVertexCount();
#endif
    m_meshUploadVerticesThisFrame = static_cast<size_t>(m_terrainCache.meshUploadVerticesThisFrame());
    m_meshUploadBytesThisFrame = static_cast<size_t>(m_terrainCache.meshUploadBytesThisFrame());
    m_meshUploadDeferredCount = static_cast<size_t>(m_terrainCache.meshUploadDeferredCount());
    m_worldBufferUploadMsThisFrame = m_terrainCache.worldBufferUploadMsThisFrame();
    m_worldBufferExpandCountThisFrame = static_cast<size_t>(m_terrainCache.worldBufferExpandCountThisFrame());
}

void RenderResourceHub::syncTerrainRendererFrameStats() {
#ifdef MECRAFT_DEBUG
    m_regionTestsThisFrame = m_terrainRenderer.regionTestsThisFrame();
    m_regionPassedThisFrame = m_terrainRenderer.regionPassedThisFrame();
    m_columnTestsThisFrame = m_terrainRenderer.columnTestsThisFrame();
    m_columnPassedThisFrame = m_terrainRenderer.columnPassedThisFrame();
    m_chunkTestsThisFrame = m_terrainRenderer.chunkTestsThisFrame();
    m_chunkPassedThisFrame = m_terrainRenderer.chunkPassedThisFrame();
    m_chunkCulledThisFrame = m_terrainRenderer.chunkCulledThisFrame();
    m_chunkCulledByPlaneThisFrame = m_terrainRenderer.chunkCulledByPlaneThisFrame();
    m_cutoutCandidatesThisFrame = m_terrainRenderer.cutoutCandidatesThisFrame();
    m_cutoutSkippedByDistanceThisFrame = m_terrainRenderer.cutoutSkippedByDistanceThisFrame();
    m_mdiSubChunkTestsThisFrame = m_terrainRenderer.mdiSubChunkTestsThisFrame();
    m_mdiSubChunksCulledThisFrame = m_terrainRenderer.mdiSubChunksCulledThisFrame();
#endif
}

void RenderResourceHub::drawCutoutChunks(const std::vector<ChunkRenderEntry>& cutoutEntries) {
    if (m_useMultiDrawIndirect) {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        m_chunkShader->setInt("uForceBaseLod", 1);
        m_worldRenderBuffer.flushCutout();
        m_chunkShader->setInt("uForceBaseLod", 0);
        return;
    }

    if (cutoutEntries.empty()) {
        return;
    }

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    m_chunkShader->setInt("uForceBaseLod", 1);

    for (const ChunkRenderEntry& entry : cutoutEntries) {
        if (entry.chunk == nullptr) continue;

        const SubChunkMesh* mesh = nullptr;
        if (entry.aggregated) {
            mesh = &entry.chunk->getColumnMesh();
        } else {
            const SubChunk* sc = entry.chunk->getSubChunk(entry.scy);
            if (!sc) continue;
            mesh = &sc->getMesh();
        }
        if (mesh->cutoutVertexCount > 0) {
            glBindVertexArray(mesh->cutoutVao);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->cutoutVertexCount));
            ++drawCallCount;
        }

        if (mesh->cutoutDistanceVertexCount > 0) {
            bool shouldDrawDistanceCutout = true;
            if (m_cutoutDistanceLimitEnabled) {
                const glm::vec3 center = (mesh->boundsMin + mesh->boundsMax) * 0.5f;
                const glm::vec2 toCameraXZ(center.x - m_cameraPos.x, center.z - m_cameraPos.z);
                const float distanceSq = glm::dot(toCameraXZ, toCameraXZ);
                const float cutoutLimitBlocks = m_cutoutRenderDistanceChunks * static_cast<float>(Chunk::SIZE_X);
                shouldDrawDistanceCutout = distanceSq <= cutoutLimitBlocks * cutoutLimitBlocks;
            }

            if (shouldDrawDistanceCutout) {
                glBindVertexArray(mesh->cutoutDistanceVao);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->cutoutDistanceVertexCount));
                ++drawCallCount;
            }
        }
    }
    m_chunkShader->setInt("uForceBaseLod", 0);
}

void RenderResourceHub::addTransparentBatch(const GpuMeshRange& range,
                                   const float distanceSq,
                                   const TransparentBatchKind kind) {
    m_terrainCache.addTransparentBatch(range, distanceSq, kind);
}

void RenderResourceHub::clearTransparentBatches() {
    m_terrainCache.clearTransparentBatches();
    m_deferredTransparentBatch.clear();
    m_transparentPassPlan.clear();
}

void RenderResourceHub::syncTransparentBatches() {
    m_deferredTransparentBatch = m_terrainCache.deferredTransparentBatch();
    m_transparentPassPlan = m_terrainCache.transparentPassPlan();
}

void RenderResourceHub::drawTransparentChunks(const std::vector<ChunkRenderEntry>& transparentEntries) {
    if (m_useMultiDrawIndirect) {
        if (!m_transparentPassPlan.hasAny()) return;

        m_chunkShader->setInt("uForceBaseLod", 1);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);

        // Sort back-to-front
        std::sort(m_deferredTransparentBatch.begin(), m_deferredTransparentBatch.end(),
                  [](const DrawBatchEntry& a, const DrawBatchEntry& b) {
                      return a.distanceSq > b.distanceSq;
                  });

        for (const DrawBatchEntry& entry : m_deferredTransparentBatch) {
            // Legacy path: all transparent (including water) in one list
            m_worldRenderBuffer.addTransparent(entry.range);
        }
#ifdef MECRAFT_DEBUG
        beginGpuTimer(GpuTimerPass::Transparent);
#endif
        m_worldRenderBuffer.flushTransparent();
#ifdef MECRAFT_DEBUG
        endGpuTimer(GpuTimerPass::Transparent);
#endif

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        m_chunkShader->setInt("uForceBaseLod", 0);
        return;
    }

    if (transparentEntries.empty()) {
        return;
    }

    // Sort by distance (back-to-front) for alpha blending
    struct TransparentSubChunkItem {
        const ChunkRenderEntry* entry = nullptr;
        float distanceSq = 0.0f;
    };

    std::vector<TransparentSubChunkItem> items;
    items.reserve(transparentEntries.size());

    for (const ChunkRenderEntry& entry : transparentEntries) {
        if (entry.chunk == nullptr) continue;

        const glm::ivec3 offset = entry.chunk->getWorldOffset();
        const int yBase = entry.scy * SubChunk::SIZE;
        const glm::vec3 sectionCenter(
            static_cast<float>(offset.x) + Chunk::SIZE_X * 0.5f,
            static_cast<float>(yBase + offset.y) + SubChunk::SIZE * 0.5f,
            static_cast<float>(offset.z) + Chunk::SIZE_Z * 0.5f);
        const glm::vec3 toCamera = sectionCenter - m_cameraPos;
        items.push_back({&entry, glm::dot(toCamera, toCamera)});
    }

    std::sort(items.begin(), items.end(),
              [](const TransparentSubChunkItem& a, const TransparentSubChunkItem& b) {
                  return a.distanceSq > b.distanceSq;
              });

    m_chunkShader->setInt("uForceBaseLod", 1);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

#ifdef MECRAFT_DEBUG
    beginGpuTimer(GpuTimerPass::Transparent);
#endif
    for (const TransparentSubChunkItem& item : items) {
        const ChunkRenderEntry& entry = *item.entry;
        const SubChunk* sc = entry.chunk->getSubChunk(entry.scy);
        if (!sc) continue;
        const SubChunkMesh& mesh = sc->getMesh();
        if (mesh.transparentVertexCount == 0) continue;

        glBindVertexArray(mesh.transparentVao);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.transparentVertexCount));
        ++drawCallCount;
    }
#ifdef MECRAFT_DEBUG
    endGpuTimer(GpuTimerPass::Transparent);
#endif

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    m_chunkShader->setInt("uForceBaseLod", 0);
}

// Transparent shadow pass: writes water/glass depth to DepthAll + color to Color0/Color1.
// This is the Mecraft CSM equivalent of DerivativeMain shadowtex0 + shadowcolor0/1.
void RenderResourceHub::executeTransparentShadowChunks(const std::vector<ChunkRenderEntry>& /*transparentEntries*/) {
    // Phase 7b: Delegated to ShadowPass (inline in execute())
}

#ifdef MECRAFT_DEBUG
bool RenderResourceHub::isChunkInFrustum(const glm::vec3 &chunkMin, const glm::vec3 &chunkMax) const {
    return isChunkInFrustum(chunkMin, chunkMax, nullptr);
}

void RenderResourceHub::recordChunkCull(const FrustumPlane plane, const int count) {
    if (!m_chunkCullingDebugEnabled || count <= 0) {
        return;
    }

    m_chunkCulledThisFrame += count;
    const size_t planeIndex = static_cast<size_t>(plane);
    if (planeIndex < m_chunkCulledByPlaneThisFrame.size()) {
        m_chunkCulledByPlaneThisFrame[planeIndex] += count;
    }
}

void RenderResourceHub::initGpuTimers() {
    if (m_gpuTimersInitialized) {
        return;
    }

    m_gpuFrameStats.supported = GLAD_GL_VERSION_3_3 != 0;
    if (!m_gpuFrameStats.supported) {
        return;
    }

    for (auto& slot : m_gpuTimerQueries) {
        glGenQueries(static_cast<GLsizei>(slot.size()), slot.data());
    }
    for (auto& issued : m_gpuTimerIssued) {
        issued.fill(false);
    }
    m_gpuTimersInitialized = true;
}

void RenderResourceHub::shutdownGpuTimers() {
    if (!m_gpuTimersInitialized) {
        return;
    }
    if (m_gpuTimerActive) {
        glEndQuery(GL_TIME_ELAPSED);
        m_gpuTimerActive = false;
    }

    for (auto& slot : m_gpuTimerQueries) {
        glDeleteQueries(static_cast<GLsizei>(slot.size()), slot.data());
        slot.fill(0);
    }
    for (auto& issued : m_gpuTimerIssued) {
        issued.fill(false);
    }
    m_gpuTimersInitialized = false;
    m_gpuFrameStats.valid = false;
}

void RenderResourceHub::beginGpuTimerFrame() {
    if (m_debugService) {
        m_debugService->beginFrame();
        return;
    }
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled) {
        m_gpuFrameStats.supported = m_gpuTimersInitialized && m_gpuFrameStats.supported;
        m_gpuFrameStats.valid = false;
        m_gpuTimerCanIssueThisFrame = false;
        return;
    }

    const size_t readIndex = (m_gpuTimerWriteIndex + 1) % GPU_TIMER_RING_SIZE;
    bool allIssued = false;
    for (const bool issued : m_gpuTimerIssued[readIndex]) {
        allIssued = allIssued || issued;
    }

    if (allIssued) {
        bool allAvailable = true;
        for (size_t pass = 0; pass < static_cast<size_t>(GpuTimerPass::Count); ++pass) {
            if (!m_gpuTimerIssued[readIndex][pass]) {
                continue;
            }
            GLint available = GL_FALSE;
            glGetQueryObjectiv(m_gpuTimerQueries[readIndex][pass], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available == GL_FALSE) {
                allAvailable = false;
                break;
            }
        }

        if (allAvailable) {
            auto readMs = [&](const GpuTimerPass pass) {
                const size_t passIndex = static_cast<size_t>(pass);
                if (!m_gpuTimerIssued[readIndex][passIndex]) {
                    return 0.0;
                }
                GLuint64 elapsedNs = 0;
                glGetQueryObjectui64v(m_gpuTimerQueries[readIndex][passIndex], GL_QUERY_RESULT, &elapsedNs);
                return static_cast<double>(elapsedNs) / 1000000.0;
            };

            m_gpuFrameStats.supported = true;
            m_gpuFrameStats.valid = true;
            m_gpuFrameStats.gbufferMs = readMs(GpuTimerPass::GBuffer);
            m_gpuFrameStats.shadowMs = readMs(GpuTimerPass::Shadow);
            m_gpuFrameStats.ssaoMs = readMs(GpuTimerPass::Ssao);
            m_gpuFrameStats.lightingMs = readMs(GpuTimerPass::Lighting);
            m_gpuFrameStats.transparentMs = readMs(GpuTimerPass::Transparent);
            m_gpuFrameStats.volumetricMs = readMs(GpuTimerPass::Volumetric);
            m_gpuFrameStats.reflectionMs = readMs(GpuTimerPass::Reflection);
            m_gpuFrameStats.cloudMs = readMs(GpuTimerPass::Cloud);
            m_gpuFrameStats.waterMs = readMs(GpuTimerPass::Water);
            m_gpuFrameStats.postMs = readMs(GpuTimerPass::Post);
            m_gpuTimerIssued[readIndex].fill(false);
        }
    }

    bool slotStillPending = false;
    for (const bool issued : m_gpuTimerIssued[readIndex]) {
        if (issued) {
            slotStillPending = true;
            break;
        }
    }
    m_gpuTimerCanIssueThisFrame = !slotStillPending;
    if (!m_gpuTimerCanIssueThisFrame) {
        return;
    }

    if (m_gpuTimerActive) {
        glEndQuery(GL_TIME_ELAPSED);
        m_gpuTimerActive = false;
    }
    m_gpuTimerWriteIndex = (m_gpuTimerWriteIndex + 1) % GPU_TIMER_RING_SIZE;
    m_gpuTimerIssued[m_gpuTimerWriteIndex].fill(false);
}

void RenderResourceHub::beginGpuTimer(const GpuTimerPass pass) {
    if (m_debugService) {
        m_debugService->beginGpuTimer(pass);
        return;
    }
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled || !m_gpuTimerCanIssueThisFrame || m_gpuTimerActive) {
        return;
    }

    const size_t passIndex = static_cast<size_t>(pass);
    glBeginQuery(GL_TIME_ELAPSED, m_gpuTimerQueries[m_gpuTimerWriteIndex][passIndex]);
    m_gpuTimerActive = true;
    m_activeGpuTimerPass = pass;
}

void RenderResourceHub::endGpuTimer(const GpuTimerPass pass) {
    if (m_debugService) {
        m_debugService->endGpuTimer(pass);
        return;
    }
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled || !m_gpuTimerActive || m_activeGpuTimerPass != pass) {
        return;
    }

    glEndQuery(GL_TIME_ELAPSED);
    m_gpuTimerIssued[m_gpuTimerWriteIndex][static_cast<size_t>(pass)] = true;
    m_gpuTimerActive = false;
}

bool RenderResourceHub::isChunkInFrustum(const glm::vec3 &chunkMin, const glm::vec3 &chunkMax, FrustumPlane* culledPlane) const {
#else
bool RenderResourceHub::isChunkInFrustum(const glm::vec3 &chunkMin, const glm::vec3 &chunkMax) const {
    constexpr FrustumPlane* culledPlane = nullptr;
#endif
    for (const Plane& plane : m_frustumPlanes) {
        const glm::vec3 positive(
            plane.n.x >= 0.0f ? chunkMax.x : chunkMin.x,
            plane.n.y >= 0.0f ? chunkMax.y : chunkMin.y,
            plane.n.z >= 0.0f ? chunkMax.z : chunkMin.z
        );

        if (glm::dot(plane.n, positive) + plane.d < 0.0f) {
#ifdef MECRAFT_DEBUG
            if (culledPlane != nullptr) {
                *culledPlane = kPlaneFromIndex(static_cast<size_t>(&plane - m_frustumPlanes.data()));
            }
#endif
            return false;
        }
    }

#ifdef MECRAFT_DEBUG
    if (culledPlane != nullptr) {
        *culledPlane = FrustumPlane::Count;
    }
#endif

    return true;
}

int RenderResourceHub::getDrawCallCount() const {
    return drawCallCount;
}

int RenderResourceHub::getGlSubmitCount() const {
    if (m_useMultiDrawIndirect) {
        return m_worldRenderBuffer.glSubmitCount();
    }
    return drawCallCount;
}
