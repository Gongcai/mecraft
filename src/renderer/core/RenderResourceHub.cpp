//
// Created by Caiwe on 2026/3/21.
//

#include "RenderResourceHub.h"

#include "../../Diagnostics.h"
#include "../renderers/HumanoidRenderer.h"
#include "../renderers/DropRenderer.h"
#include "../rhi/RhiDevice.h"
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
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <array>
#include <vector>

namespace {

struct MeshingCandidate {
    int64_t chunkKey = 0;
    Chunk* chunk = nullptr;
    int scy = 0; // Sub-chunk index
    float distanceSq = 0.0f;
    std::shared_ptr<Chunk> chunkRef;
    std::shared_ptr<Chunk> neighborPosX;
    std::shared_ptr<Chunk> neighborNegX;
    std::shared_ptr<Chunk> neighborPosZ;
    std::shared_ptr<Chunk> neighborNegZ;
};

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
} // namespace

RenderResourceHub::~RenderResourceHub() {
    shutdown();
}

bool RenderResourceHub::init(GameResources& resources, ThreadPool& threadPool, RhiDevice& rhiDevice,
                             RhiCommandListPool& commandListPool) {
    m_resources = &resources;
    m_rhiDevice = &rhiDevice;
    m_commandListPool = &commandListPool;

    // R8: Overlay initialization removed — handled by BlockInteractionOverlayRenderer
    m_terrainRhiPipelines.init(*m_rhiDevice);
    if (!m_worldRenderBuffer.init(*m_rhiDevice)) {
        return false;
    }
    if (!m_terrainCache.init(m_rhiDevice)) {
        return false;
    }
    if (!m_terrainCache.setOpacityMicromapSource(resources.blockTextures.terrainOpacityMicromapSource())) {
        return false;
    }
    m_terrainCache.setWorldRenderBuffer(&m_worldRenderBuffer);
    m_terrainCache.setChunkMeshingService(&m_meshingService);
    m_terrainCache.setRegionChunkSize(m_regionChunkSize);
    m_terrainRenderer.init();
    m_terrainRenderer.setWorldRenderBuffer(&m_worldRenderBuffer);
    m_terrainRenderer.setTerrainRenderCache(&m_terrainCache);
#ifdef MECRAFT_DEBUG
    m_terrainRenderer.setChunkCullingDebugEnabled(m_chunkCullingDebugEnabled);
#endif
    // Phase 5c: Inject WaterCompositePass dependencies
    if (!m_deferredTargets.init(*m_rhiDevice, *m_commandListPool)) {
        return false;
    }
    const std::string atmosphereLutPath = resolveAtmosphereFinalLutPath();
    m_deferredTargets.loadAtmosphereLut(atmosphereLutPath.c_str());
    m_gameplaySkyRenderer.init(*m_rhiDevice);
    if (!m_meshingSubmitBudgetOverridden) {
        const int workerCount = std::max(1, threadPool.numWorkers());
        m_meshingSubmitBudget = 2 + std::max(0, workerCount - 1);
        m_meshingMaxInFlight = std::max(4, workerCount * 2);
#ifndef MECRAFT_DEBUG
        m_meshingSubmitTimeBudgetMs = 2.0;
        m_meshingDrainBudget = std::max(4, workerCount * 2);
        m_meshingDrainTimeBudgetMs = 2.5;
#else
        m_meshingSubmitTimeBudgetMs = 0.5;
        m_meshingDrainBudget = 1;
        m_meshingDrainTimeBudgetMs = 0.5;
#endif
    }
    m_terrainCache.setMeshingBudgets(m_meshingSubmitBudget, m_meshingMaxInFlight,
                                     static_cast<float>(m_meshingSubmitTimeBudgetMs), m_meshingDrainBudget,
                                     static_cast<float>(m_meshingDrainTimeBudgetMs), m_meshingDrainVertexBudget);
    m_meshingService.start(&threadPool);
    return true;
}

void RenderResourceHub::shutdown() {
    m_mdiMeshAllocations.clear();
    m_gameplaySkyRenderer.shutdown();
    m_deferredTargets.shutdown();
    // Always shutdown owned terrain cache and meshing service to prevent thread/lifecycle leaks
    m_terrainCache.shutdown();
    m_meshingService.shutdown();
    m_worldRenderBuffer.shutdown();
    m_terrainRhiPipelines.shutdown();
    m_meshingInFlight.clear();
    m_deferredMeshResults.clear();
    m_rhiDevice = nullptr;
    m_commandListPool = nullptr;
}

RhiDevice& RenderResourceHub::rhiDevice() {
    assert(m_rhiDevice != nullptr);
    return *m_rhiDevice;
}

const RhiDevice& RenderResourceHub::rhiDevice() const {
    assert(m_rhiDevice != nullptr);
    return *m_rhiDevice;
}

RhiCommandListPool& RenderResourceHub::commandListPool() {
    assert(m_commandListPool != nullptr);
    return *m_commandListPool;
}

void RenderResourceHub::setMeshingSubmitBudget(const int budget) {
    if (m_terrainStreamingService) {
        m_terrainStreamingService->setMeshingSubmitBudget(budget);
        return;
    }
    m_meshingSubmitBudget = std::max(1, budget);
    m_meshingSubmitBudgetOverridden = true;
    m_terrainCache.setMeshingBudgets(m_meshingSubmitBudget, m_meshingMaxInFlight,
                                     static_cast<float>(m_meshingSubmitTimeBudgetMs), m_meshingDrainBudget,
                                     static_cast<float>(m_meshingDrainTimeBudgetMs), m_meshingDrainVertexBudget);
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
    }
}

void RenderResourceHub::setAtlasAnisotropy(const float anisotropy) {
    if (m_resources == nullptr) {
        return;
    }
    m_resources->blockTextures.setAnisotropy(anisotropy);
}

const RenderSettings& RenderResourceHub::getSettings() const {
    return m_settings;
}

float RenderResourceHub::getAtlasAnisotropy() const {
    if (m_resources == nullptr) {
        return 1.0f;
    }
    return m_resources->blockTextures.anisotropy();
}

float RenderResourceHub::getAtlasMaxAnisotropy() const {
    if (m_resources == nullptr) {
        return 1.0f;
    }
    return m_resources->blockTextures.maxAnisotropy();
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
    CullingFrameStats stats;
    stats.regionTests = m_terrainRenderer.regionTestsThisFrame();
    stats.regionPassed = m_terrainRenderer.regionPassedThisFrame();
    stats.columnTests = m_terrainRenderer.columnTestsThisFrame();
    stats.columnPassed = m_terrainRenderer.columnPassedThisFrame();
    stats.chunkTests = m_terrainRenderer.chunkTestsThisFrame();
    stats.chunkPassed = m_terrainRenderer.chunkPassedThisFrame();
    stats.chunkCulled = m_terrainRenderer.chunkCulledThisFrame();

    const auto& planeCulls = m_terrainRenderer.chunkCulledByPlaneThisFrame();
    for (size_t i = 0; i < planeCulls.size(); ++i) {
        stats.chunkCulledByPlane[i] = planeCulls[i];
    }
    return stats;
}

GpuFrameStats RenderResourceHub::getGpuFrameStats() const {
    if (m_debugService) {
        return m_debugService->getGpuFrameStats();
    }
    return {};
}

ShadowFrameStats RenderResourceHub::getShadowFrameStats() const {
    if (m_debugService) {
        return m_debugService->getShadowFrameStats();
    }
    return {};
}

RenderWorkStats RenderResourceHub::getRenderWorkStats() const {
    RenderWorkStats stats;
    const auto& sceneStats = m_worldRenderBuffer.sceneFrameStats();
    stats.blockVertexBytes = sizeof(PackedBlockVertex);
    stats.opaqueCommands = sceneStats.opaqueCommands;
    stats.cutoutCommands = sceneStats.cutoutCommands;
    stats.transparentCommands = sceneStats.transparentCommands + sceneStats.waterCommands;
    const auto& transparentPassPlan = m_terrainRenderer.transparentPassPlan();
    stats.transparentGenericCommands = transparentPassPlan.genericCommands;
    stats.transparentWaterCommands = transparentPassPlan.waterCommands;
    stats.opaqueLogicalCommands = sceneStats.opaqueLogicalCommands;
    stats.cutoutLogicalCommands = sceneStats.cutoutLogicalCommands;
    stats.transparentLogicalCommands = sceneStats.transparentLogicalCommands;
    stats.opaquePoolCapacityVertices = m_worldRenderBuffer.opaqueCapacityVertices();
    stats.cutoutPoolCapacityVertices = m_worldRenderBuffer.cutoutCapacityVertices();
    stats.transparentPoolCapacityVertices = m_worldRenderBuffer.transparentCapacityVertices();
    stats.opaquePoolUsedVertices = m_worldRenderBuffer.opaqueUsedVertices();
    stats.cutoutPoolUsedVertices = m_worldRenderBuffer.cutoutUsedVertices();
    stats.transparentPoolUsedVertices = m_worldRenderBuffer.transparentUsedVertices();
    stats.opaquePoolUsedBytes = stats.opaquePoolUsedVertices * stats.blockVertexBytes;
    stats.cutoutPoolUsedBytes = stats.cutoutPoolUsedVertices * stats.blockVertexBytes;
    stats.transparentPoolUsedBytes = stats.transparentPoolUsedVertices * stats.blockVertexBytes;
    stats.opaquePoolCapacityBytes = stats.opaquePoolCapacityVertices * stats.blockVertexBytes;
    stats.cutoutPoolCapacityBytes = stats.cutoutPoolCapacityVertices * stats.blockVertexBytes;
    stats.transparentPoolCapacityBytes = stats.transparentPoolCapacityVertices * stats.blockVertexBytes;
    stats.terrainPoolUsedBytes = stats.opaquePoolUsedBytes + stats.cutoutPoolUsedBytes + stats.transparentPoolUsedBytes;
    stats.terrainPoolCapacityBytes =
        stats.opaquePoolCapacityBytes + stats.cutoutPoolCapacityBytes + stats.transparentPoolCapacityBytes;
    stats.terrainMetadataBytes = m_worldRenderBuffer.metadataBytes();
    stats.terrainMetadataSlots = m_worldRenderBuffer.metadataSlotCount();
    stats.terrainMetadataFreeSlots = m_worldRenderBuffer.metadataFreeSlotCount();
    stats.opaquePoolFragmentation = m_worldRenderBuffer.opaqueFragmentationRatio();
    stats.cutoutPoolFragmentation = m_worldRenderBuffer.cutoutFragmentationRatio();
    stats.transparentPoolFragmentation = m_worldRenderBuffer.transparentFragmentationRatio();
    stats.opaqueVertices = sceneStats.opaqueVertices;
    stats.cutoutVertices = sceneStats.cutoutVertices;
    stats.transparentVertices = sceneStats.transparentVertices + sceneStats.waterVertices;
    stats.transparentGenericVertices = transparentPassPlan.genericVertices;
    stats.transparentWaterVertices = transparentPassPlan.waterVertices;
    stats.opaqueVertexReadBytes = stats.opaqueVertices * stats.blockVertexBytes;
    stats.cutoutVertexReadBytes = stats.cutoutVertices * stats.blockVertexBytes;
    stats.transparentVertexReadBytes = stats.transparentVertices * stats.blockVertexBytes;
    stats.terrainVertexReadBytes =
        stats.opaqueVertexReadBytes + stats.cutoutVertexReadBytes + stats.transparentVertexReadBytes;
    stats.cutoutCandidates = m_terrainRenderer.cutoutCandidatesThisFrame();
    stats.cutoutSkippedByDistance = m_terrainRenderer.cutoutSkippedByDistanceThisFrame();
    stats.mdiSubChunkTests = m_terrainRenderer.mdiSubChunkTestsThisFrame();
    stats.mdiSubChunksCulled = m_terrainRenderer.mdiSubChunksCulledThisFrame();
    stats.meshUploadBytesThisFrame = m_meshUploadBytesThisFrame;
    stats.meshUploadVerticesThisFrame = m_meshUploadVerticesThisFrame;
    stats.meshUploadDeferredCount = m_meshUploadDeferredCount;
    stats.worldBufferExpandCount = m_worldBufferExpandCountThisFrame;
    stats.worldBufferUploadMs = m_worldBufferUploadMsThisFrame;
    if (m_terrainStreamingService) {
        const TerrainRenderCache& cache = m_terrainStreamingService->terrainCache();
        stats.meshUploadBytesThisFrame = static_cast<size_t>(cache.meshUploadBytesThisFrame());
        stats.meshUploadVerticesThisFrame = static_cast<size_t>(cache.meshUploadVerticesThisFrame());
        stats.meshUploadDeferredCount = static_cast<size_t>(cache.meshUploadDeferredCount());
        stats.worldBufferExpandCount = static_cast<size_t>(cache.worldBufferExpandCountThisFrame());
        stats.worldBufferUploadMs = cache.worldBufferUploadMsThisFrame();
    }
    const TerrainRenderCache& activeTerrainCache =
        m_terrainStreamingService != nullptr ? m_terrainStreamingService->terrainCache() : m_terrainCache;
    const TerrainBlasStats blasStats = activeTerrainCache.blasCache().stats();
    stats.terrainBlasSupported = blasStats.supported;
    stats.terrainBlasHealthy = blasStats.healthy;
    stats.terrainActiveBlas = blasStats.activeBlasCount;
    stats.terrainPendingBlasBuilds = blasStats.pendingBuildCount;
    stats.terrainPendingBlasCompactions = blasStats.pendingCompactionCount;
    stats.terrainRetiredBlasTasks = blasStats.retiredTaskCount;
    stats.terrainBlasBuildsThisFrame = blasStats.buildsRecordedThisFrame;
    stats.terrainBlasCompactionsThisFrame = blasStats.compactionsRecordedThisFrame;
    stats.terrainBlasPrimitives = blasStats.activePrimitiveCount;
    stats.terrainBlasGeometryBytes = blasStats.activeGeometryBytes;
    stats.terrainBlasPrimitiveMetadataBytes = blasStats.activePrimitiveMetadataBytes;
    stats.terrainBlasBytes = blasStats.activeBlasBytes;
    stats.terrainBlasScratchPeakBytes = blasStats.scratchPeakBytesThisFrame;
    return stats;
}

void RenderResourceHub::setGpuTimerEnabled(const bool enabled) {
    if (m_debugService) {
        m_debugService->setGpuTimerEnabled(enabled);
    }
}

bool RenderResourceHub::isGpuTimerEnabled() const {
    return m_debugService ? m_debugService->isGpuTimerEnabled() : false;
}

void RenderResourceHub::setCutoutDistanceLimitEnabled(const bool enabled) {
    m_terrainRenderer.setCutoutDistanceLimitEnabled(enabled);
}

bool RenderResourceHub::isCutoutDistanceLimitEnabled() const {
    return m_terrainRenderer.cutoutDistanceLimitEnabled();
}

void RenderResourceHub::setCutoutRenderDistanceChunks(const float distanceChunks) {
    m_terrainRenderer.setCutoutRenderDistanceChunks(std::clamp(distanceChunks, 1.0f, 32.0f));
}

float RenderResourceHub::getCutoutRenderDistanceChunks() const {
    return m_terrainRenderer.cutoutRenderDistanceChunks();
}

const std::array<float, RenderResourceHub::MESHING_HISTORY_SIZE>&
RenderResourceHub::getMeshingSubmittedHistory() const {
    if (m_terrainStreamingService) {
        return m_terrainStreamingService->getMeshingSubmittedHistory();
    }
    return m_meshingSubmittedHistory;
}

const std::array<float, RenderResourceHub::MESHING_HISTORY_SIZE>&
RenderResourceHub::getMeshingCompletedHistory() const {
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

int RenderResourceHub::getTerrainRhiSubmitCount() const {
    return m_worldRenderBuffer.sceneFrameStats().rhiSubmitCount;
}
