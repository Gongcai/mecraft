//
// Created by Caiwe on 2026/3/21.
//

#include "Renderer.h"
#include "SettingsMapper.h"

#include "../renderers/HumanoidRenderer.h"
#include "../renderers/DropRenderer.h"
#include "../../particle/ParticleSystem.h"
#include "../mesh/ChunkMesher.h"
#include "../../ecs/GameplayRegistry.h"
#include "../shadow/ShadowMatrices.h"
#include "../shadow/ShadowCasterCuller.h"
#include "../../Paths.h"
#include "engine/platform/Time.h"
#include "../../world/block/BlockSelection.h"
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
int floorDiv(const int value, const int divisor) {
    int q = value / divisor;
    const int r = value % divisor;
    if (r != 0 && ((r > 0) != (divisor > 0))) {
        --q;
    }
    return q;
}

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

uint64_t hashCombine64(uint64_t seed, const uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
}

uint64_t meshFingerprint(const SubChunkMesh& mesh) {
    uint64_t hash = 1469598103934665603ULL;
    hash = hashCombine64(hash, mesh.vertexCount);
    hash = hashCombine64(hash, mesh.cutoutVertexCount);
    hash = hashCombine64(hash, mesh.cutoutDistanceVertexCount);
    hash = hashCombine64(hash, mesh.transparentVertexCount);
    hash = hashCombine64(hash, mesh.waterVertexCount);
    hash = hashCombine64(hash, mesh.opaqueRange.generation);
    hash = hashCombine64(hash, mesh.cutoutRange.generation);
    hash = hashCombine64(hash, mesh.cutoutDistanceRange.generation);
    hash = hashCombine64(hash, mesh.transparentRange.generation);
    hash = hashCombine64(hash, mesh.waterRange.generation);
    hash = hashCombine64(hash, mesh.hasBounds ? 1ULL : 0ULL);
    return hash;
}

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

constexpr FrustumPlane kPlaneFromIndex(const size_t index) {
    return static_cast<FrustumPlane>(index);
}


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

Renderer::~Renderer() {
    shutdown();
}

void Renderer::init(ResourceMgr &resourceMgr) {
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

    // Phase 9a: DeferredPipeline owns all extracted passes
    m_deferredPipeline = std::make_unique<DeferredPipeline>();
    m_deferredPipeline->init(resourceMgr, &m_shadowRenderer);

    //m_uiShader = resourceMgr.getShader("ui");
    // R5: Overlay shaders and meshes are only loaded when no external overlay renderer is injected
    if (!m_overlayRenderer) {
        m_outlineShader = resourceMgr.getShader("outline");
        m_breakOverlayShader = resourceMgr.getShader("break_overlay");
        initOutlineMesh();
        initBreakOverlayMesh();
    }
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
    // Phase 7b: Inject ShadowPass dependencies
    if (m_deferredPipeline->shadowPass()) {
        m_deferredPipeline->shadowPass()->setTerrainRenderer(&m_terrainRenderer);
        m_deferredPipeline->shadowPass()->setWorldRenderBuffer(&m_worldRenderBuffer);
    }
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

void Renderer::shutdown() {
#ifdef MECRAFT_DEBUG
    if (m_debugService) {
        m_debugService->shutdown();
    } else {
        shutdownGpuTimers();
    }
#endif
    m_mdiMeshAllocations.clear();
    if (m_deferredPipeline) { m_deferredPipeline->shutdown(); m_deferredPipeline.reset(); }
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

void Renderer::renderOpaqueAndCutout(const World& world, const Camera& camera, const Window& window) {
    beginFrame(camera, window);
    m_currentFrameData = buildRenderFrameData(world);
    m_fogSettings.color = m_currentFrameData.skyColors.fog;
    m_currentFrameDataValid = true;
    if (m_pipelineSettings.mode == RenderPipelineMode::HybridDeferred &&
        renderWorldDeferred(world, camera, window, m_currentFrameData)) {
        return;
    }
    m_gameplaySkyRenderer.render(camera, window.getAspectRatio(), world.getDayNightSystem(), m_deferredTargets.skyCaptureTexture());
    m_chunkShader = m_chunkForwardShader;
    m_deferredFrameActive = false;
    renderWorldForward(world, m_currentFrameData);
}

void Renderer::renderTransparentAndOverlays(const World& world, const BlockTargetRenderData& target, const BlockBreakRenderData& blockBreak, const Window& window) {
    renderTransparentCompositePass(world, window);

    if (m_deferredFrameActive &&
        m_deferredTargets.isReady() &&
        (m_pipelineSettings.debugViewMode == 0 || m_deferredPipeline == nullptr || m_deferredPipeline->debugPass() == nullptr)) {
        const int capturedWidth = m_capturedViewport[2] > 0 ? m_capturedViewport[2] : window.getWidth();
        const int capturedHeight = m_capturedViewport[3] > 0 ? m_capturedViewport[3] : window.getHeight();
        m_deferredTargets.copyFramebufferColorToSceneResolved(m_capturedFramebuffer, capturedWidth, capturedHeight);
        updateDeferredHistoryTargets();
        restoreCapturedFramebufferViewport(window);
    }

    // R5: Delegate to overlay renderer if available, otherwise use legacy methods
    if (m_overlayRenderer) {
        m_overlayRenderer->render(world, m_projection * m_view, target, blockBreak);
    } else {
        renderBlockBreakOverlay(world, blockBreak);
        renderBlockOutline(world, target);
    }
#ifdef MECRAFT_DEBUG
    beginGpuTimer(GpuTimerPass::Post);
#endif
    endFrame(window);
#ifdef MECRAFT_DEBUG
    endGpuTimer(GpuTimerPass::Post);
#endif
}

void Renderer::renderForwardSceneObjects(const World& world, const Camera& camera, const Window& window) {
    if (m_deferredFrameActive || m_pipelineSettings.deferredLightDebugMode > 0 ||
        m_pipelineSettings.reflectionDebugMode > 0) {
        return;
    }

    if (m_dropRenderer != nullptr && m_dropSystem != nullptr) {
        m_dropRenderer->render(*m_dropSystem, camera, window);
    }

    if (m_humanoidRenderer != nullptr && m_gameplayRegistry != nullptr) {
        const auto mode = m_renderLocalPlayerModel
            ? HumanoidRenderer::kRenderAll
            : HumanoidRenderer::kRenderMobsOnly;
        m_humanoidRenderer->render(*m_gameplayRegistry, camera, window, mode);
    }

    if (m_pipelineSettings.sceneParticlesEnabled && m_particleSystem != nullptr) {
        m_particleSystem->render(camera.getProjectionMatrix(window.getAspectRatio()),
                                 camera.getViewMatrix());
    }
}

void Renderer::renderWaterCompositePass(const World& world, const Window& window, const bool preTemporalResolve) {
    // Phase 5c: Delegated to WaterCompositePass::execute()
    if (!m_deferredPipeline->waterCompositePass()) {
        return;
    }
    FrameContext waterCtx = buildFrameContextFromRenderFrameData(
        m_currentFrameDataValid ? m_currentFrameData : buildRenderFrameData(world));
    RenderSettings waterRs = buildRenderSettingsFromPipelineSettings();
    const bool volumetricFogActive = !preTemporalResolve &&
                                     (m_pipelineSettings.volumetricLightEnabled ||
                                      (m_pipelineSettings.volumetricFogEnabled &&
                                       m_pipelineSettings.volumetricFogStrength > 0.001f)) &&
                                     m_deferredPipeline->volumetricPass() && m_deferredPipeline->volumetricPass()->hasShaders();
    const bool waterRenderedBeforeTemporal = m_deferredPipeline->waterCompositePass()->execute(
        waterCtx, waterRs, m_deferredTargets, world,
        window.getWidth(), window.getHeight(),
        m_deferredFrameActive, preTemporalResolve,
        m_capturedFramebuffer, m_capturedViewport,
        m_pipelineSettings.transparentCompositeEnabled,
        m_pipelineSettings.waterEffectsEnabled,
        m_pipelineSettings.rainSurfaceRipplesEnabled,
        volumetricFogActive,
        m_useMultiDrawIndirect,
        m_worldRenderBuffer,
        m_deferredTransparentBatch,
        m_transparentPassPlan,
        m_deferredTransparentEntries);
    if (waterRenderedBeforeTemporal) {
        m_waterRenderedBeforeTemporal = true;
    }
}

void Renderer::renderTransparentCompositePass(const World& world, const Window& window) {
    if (m_deferredFrameActive && m_pipelineSettings.deferredLightDebugMode > 0) {
        restoreCapturedFramebufferViewport(window);
        return;
    }

    const bool forwardLegacy = m_pipelineSettings.mode == RenderPipelineMode::ForwardLegacy;

    // Step 1: Render water with dedicated shader. In the deferred/TAA path it
    // has already been rendered into SceneResolved before temporal resolve so
    // water/opaque contact edges share the same jittered depth and history.
    if (!forwardLegacy && !m_waterRenderedBeforeTemporal) {
        renderWaterCompositePass(world, window);
    }

    // Step 2: Render generic transparent
    if (m_deferredFrameActive) {
        restoreCapturedFramebufferViewport(window);
    }

    const bool hasTransparentForCurrentPath = forwardLegacy
        ? m_transparentPassPlan.hasAny()
        : m_transparentPassPlan.hasGeneric();
    if (!hasTransparentForCurrentPath) {
        if (m_deferredFrameActive) {
            restoreCapturedFramebufferViewport(window);
        }
        return;
    }

    m_chunkShader = forwardLegacy
        ? m_chunkForwardShader
        : (m_transparentCompositeShader != nullptr ? m_transparentCompositeShader : m_chunkForwardShader);
    if (m_chunkShader != nullptr && m_resourceMgr != nullptr) {
        const bool deferredInputsEnabled = !forwardLegacy && m_deferredFrameActive && m_deferredTargets.isReady();
        const bool compositeInputsEnabled = deferredInputsEnabled && m_pipelineSettings.transparentCompositeEnabled;
        const int capturedWidth = m_capturedViewport[2] > 0 ? m_capturedViewport[2] : window.getWidth();
        const int capturedHeight = m_capturedViewport[3] > 0 ? m_capturedViewport[3] : window.getHeight();
        if (deferredInputsEnabled) {
            m_deferredTargets.copyFramebufferColorToSceneResolved(m_capturedFramebuffer, capturedWidth, capturedHeight);
        }
        if (compositeInputsEnabled) {
            m_deferredTargets.copySceneResolvedToTransparentComposite();
            m_deferredTargets.copyDepthToTransparentComposite();
            m_deferredTargets.bindTransparentComposite();
        } else if (m_deferredFrameActive) {
            restoreCapturedFramebufferViewport(window);
        }

        const TextureArray& texArray = m_resourceMgr->getTextureArray();
        const RenderFrameData frame = m_currentFrameDataValid ? m_currentFrameData : buildRenderFrameData(world);
        bindChunkRenderState(frame, texArray);
        bindTransparentCompositeInputs(*m_chunkShader, deferredInputsEnabled, compositeInputsEnabled);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        // Deferred renders water through WaterComposite. Forward legacy keeps
        // water in the transparent list for vanilla-style alpha blending.
        if (m_useMultiDrawIndirect) {
            if (!m_deferredTransparentBatch.empty()) {
                m_chunkShader->setInt("uForceBaseLod", 1);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);

                std::sort(m_deferredTransparentBatch.begin(), m_deferredTransparentBatch.end(),
                          [](const DrawBatchEntry& a, const DrawBatchEntry& b) {
                              return a.distanceSq > b.distanceSq;
                          });

                for (const auto& entry : m_deferredTransparentBatch) {
                    if (forwardLegacy || entry.kind == TransparentBatchKind::Generic) {
                        m_worldRenderBuffer.addTransparent(entry.range);
                    }
                }
#ifdef MECRAFT_DEBUG
                beginGpuTimer(GpuTimerPass::Transparent);
#endif
                m_worldRenderBuffer.flushTransparent();
#ifdef MECRAFT_DEBUG
                endGpuTimer(GpuTimerPass::Transparent);
#endif
                m_chunkShader->setInt("uForceBaseLod", 0);
                glDepthMask(GL_TRUE);
                glDisable(GL_BLEND);
            }
        } else {
            // Non-MDI: deferred filters to generic transparent only; forward
            // legacy renders water as regular transparent terrain.
            std::vector<ChunkRenderEntry> genericEntries;
            for (const auto& entry : m_deferredTransparentEntries) {
                if (!entry.chunk) continue;
                const SubChunk* sc = entry.chunk->getSubChunk(entry.scy);
                if (!sc) continue;
                const SubChunkMesh& mesh = sc->getMesh();
                if (forwardLegacy || mesh.transparentVertexCount > mesh.waterVertexCount) {
                    genericEntries.push_back(entry);
                }
            }
            // Render generic entries with the composite shader, but without water layer effects
            bindWaterEffectUniforms(*m_chunkShader, false);
            renderTransparentChunks(genericEntries);
        }

        if (compositeInputsEnabled) {
            m_deferredTargets.blitTransparentCompositeTo(m_capturedFramebuffer, capturedWidth, capturedHeight);
            restoreCapturedFramebufferViewport(window);
        }
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }
}

void Renderer::setMeshingSubmitBudget(const int budget) {
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

void Renderer::setRegionChunkSize(const int chunkSize) {
    if (m_terrainStreamingService) {
        m_terrainStreamingService->setRegionChunkSize(chunkSize);
        return;
    }
    m_regionChunkSize = std::max(1, chunkSize);
    m_terrainCache.setRegionChunkSize(m_regionChunkSize);
}

void Renderer::setTerrainStreamingService(TerrainStreamingService* svc) {
    m_terrainStreamingService = svc;
    if (svc) {
        // Update TerrainRenderer to use the service's cache
        m_terrainRenderer.setTerrainRenderCache(&svc->terrainCache());
        // Update WorldRenderBuffer reference in the service's cache
        svc->terrainCache().setWorldRenderBuffer(&m_worldRenderBuffer);
        svc->terrainCache().setUseMultiDrawIndirect(m_useMultiDrawIndirect);
    }
}

void Renderer::setAtlasAnisotropy(const float anisotropy) {
    if (m_resourceMgr == nullptr) {
        return;
    }
    m_resourceMgr->setAtlasAnisotropy(anisotropy);
}

void Renderer::setFogEnabled(const bool enabled) {
    m_fogSettings.enabled = enabled;
}

void Renderer::setFogMode(const FogMode mode) {
    m_fogSettings.mode = mode;
}

void Renderer::setFogColor(const glm::vec3& color) {
    m_fogSettings.color.x = std::clamp(color.x, 0.0f, 1.0f);
    m_fogSettings.color.y = std::clamp(color.y, 0.0f, 1.0f);
    m_fogSettings.color.z = std::clamp(color.z, 0.0f, 1.0f);
}

void Renderer::setFogLinearDistances(const float startDistance, const float endDistance) {
    const float startClamped = std::max(0.0f, startDistance);
    const float endClamped = std::max(startClamped + 0.1f, endDistance);
    m_fogSettings.startDistance = startClamped;
    m_fogSettings.endDistance = endClamped;
}

void Renderer::setFogDensity(const float density) {
    m_fogSettings.density = std::max(0.0001f, density);
}

void Renderer::setFogAutoDistanceEnabled(const bool enabled) {
    m_fogSettings.autoDistanceByRenderDistance = enabled;
}

void Renderer::setFogAutoEndOffsetChunks(const float offsetChunks) {
    m_fogSettings.autoEndOffsetChunks = std::clamp(offsetChunks, -2.0f, 1.0f);
}

void Renderer::setFogAutoFadeWidthChunks(const float fadeWidthChunks) {
    m_fogSettings.autoFadeWidthChunks = std::clamp(fadeWidthChunks, 0.25f, 4.0f);
}

Renderer::FogSettings Renderer::getFogSettings() const {
    return m_fogSettings;
}

void Renderer::setHeldBlockLightValue(const int value) {
    m_heldBlockLightValue = std::clamp(value, 0, 15);
}

void Renderer::setDebugLightMode(const int mode) {
    m_debugLightMode = std::clamp(mode, 0, 3);
}

int Renderer::getDebugLightMode() const {
    return m_debugLightMode;
}

void Renderer::setRenderPipelineSettings(const RenderPipelineSettings& settings) {
    m_pipelineSettings = settings;
    m_pipelineSettings.shadowResolution = std::clamp(m_pipelineSettings.shadowResolution, 256, 8192);
    m_pipelineSettings.shadowDistance = std::clamp(m_pipelineSettings.shadowDistance, 64.0f, 512.0f);
    m_pipelineSettings.shadowSoftness = std::clamp(m_pipelineSettings.shadowSoftness, 0.1f, 8.0f);
    m_pipelineSettings.shadowPcssStrength = std::clamp(m_pipelineSettings.shadowPcssStrength, 0.0f, 1.5f);
    m_pipelineSettings.shadowConstantBias = std::clamp(m_pipelineSettings.shadowConstantBias, 0.0f, 0.01f);
    m_pipelineSettings.shadowSlopeBias = std::clamp(m_pipelineSettings.shadowSlopeBias, 0.0f, 0.03f);
    m_pipelineSettings.shadowNormalOffset = std::clamp(m_pipelineSettings.shadowNormalOffset, 0.0f, 0.25f);
    m_pipelineSettings.contactShadowStrength = std::clamp(m_pipelineSettings.contactShadowStrength, 0.0f, 1.0f);
    m_pipelineSettings.cloudShadowStrength = std::clamp(m_pipelineSettings.cloudShadowStrength, 0.0f, 1.0f);
    m_pipelineSettings.cloudShadowScale = std::clamp(m_pipelineSettings.cloudShadowScale, 0.0005f, 0.04f);
    m_pipelineSettings.cloudShadowSpeed = std::clamp(m_pipelineSettings.cloudShadowSpeed, 0.0f, 0.20f);
    m_pipelineSettings.cloudTimeScale = std::clamp(m_pipelineSettings.cloudTimeScale, 0.05f, 2.0f);
    m_pipelineSettings.bloomThreshold = std::clamp(m_pipelineSettings.bloomThreshold, 0.0f, 4.0f);
    m_pipelineSettings.bloomStrength = std::clamp(m_pipelineSettings.bloomStrength, 0.0f, 20.0f);
    m_pipelineSettings.autoExposureMin = std::clamp(m_pipelineSettings.autoExposureMin, 0.001f, 64.0f);
    m_pipelineSettings.autoExposureMax = std::clamp(m_pipelineSettings.autoExposureMax, m_pipelineSettings.autoExposureMin, 64.0f);
    m_pipelineSettings.autoExposureSpeed = std::clamp(m_pipelineSettings.autoExposureSpeed, 0.05f, 12.0f);
    m_pipelineSettings.autoExposureBias = std::clamp(m_pipelineSettings.autoExposureBias, -3.0f, 3.0f);
    m_pipelineSettings.sunRayStrength = std::clamp(m_pipelineSettings.sunRayStrength, 0.0f, 1.0f);
    m_pipelineSettings.volumetricQualityTier = std::clamp(m_pipelineSettings.volumetricQualityTier, 0, 3);
    m_pipelineSettings.volumetricShadowBiasScale = std::clamp(m_pipelineSettings.volumetricShadowBiasScale, 0.0f, 4.0f);
    m_pipelineSettings.sceneCloudCompositeStrength = std::clamp(m_pipelineSettings.sceneCloudCompositeStrength, 0.0f, 1.0f);
    m_pipelineSettings.sceneReflectionCompositeStrength = std::clamp(m_pipelineSettings.sceneReflectionCompositeStrength, 0.0f, 1.0f);
    m_pipelineSettings.debugViewMode = std::clamp(m_pipelineSettings.debugViewMode, 0, 80);
    m_pipelineSettings.reflectionDebugMode = std::clamp(m_pipelineSettings.reflectionDebugMode, 0, 30);

    m_pipelineSettings.tonemapMode = std::clamp(m_pipelineSettings.tonemapMode, 0, 5);
    m_pipelineSettings.debugDisableGreedyMeshing = false;
    ChunkMesher::setDebugDisableGreedyMeshing(m_pipelineSettings.debugDisableGreedyMeshing);
    m_pipelineSettings.colorTemperature = std::clamp(m_pipelineSettings.colorTemperature, 0.0f, 2.0f);
    m_pipelineSettings.vibrance = std::clamp(m_pipelineSettings.vibrance, -1.0f, 1.0f);
    m_pipelineSettings.highlightCompression = std::clamp(m_pipelineSettings.highlightCompression, 0.0f, 1.5f);
    m_pipelineSettings.filmEmulationStrength = std::clamp(m_pipelineSettings.filmEmulationStrength, 0.0f, 1.0f);
    m_pipelineSettings.redModifierStrength = std::clamp(m_pipelineSettings.redModifierStrength, 0.0f, 1.0f);
    m_pipelineSettings.colorLumaR = std::clamp(m_pipelineSettings.colorLumaR, 0.5f, 1.5f);
    m_pipelineSettings.colorLumaG = std::clamp(m_pipelineSettings.colorLumaG, 0.5f, 1.5f);
    m_pipelineSettings.colorLumaB = std::clamp(m_pipelineSettings.colorLumaB, 0.5f, 1.5f);
    m_pipelineSettings.albedoDesaturation = std::clamp(m_pipelineSettings.albedoDesaturation, 0.0f, 0.8f);
    m_pipelineSettings.sunWarmth = std::clamp(m_pipelineSettings.sunWarmth, 0.0f, 1.5f);
    m_pipelineSettings.skyCoolness = std::clamp(m_pipelineSettings.skyCoolness, 0.0f, 1.0f);
    m_pipelineSettings.shadowDesaturation = std::clamp(m_pipelineSettings.shadowDesaturation, 0.0f, 1.0f);
    m_pipelineSettings.splitToneStrength = std::clamp(m_pipelineSettings.splitToneStrength, 0.0f, 1.0f);
    m_pipelineSettings.vignetteStrength = std::clamp(m_pipelineSettings.vignetteStrength, 0.0f, 0.5f);
    m_pipelineSettings.shadowTintStrength = std::clamp(m_pipelineSettings.shadowTintStrength, 0.0f, 1.0f);
    m_pipelineSettings.directSunStrength = std::clamp(m_pipelineSettings.directSunStrength, 0.0f, 4.0f);
    m_pipelineSettings.skyAmbientStrength = std::clamp(m_pipelineSettings.skyAmbientStrength, 0.0f, 2.5f);
    m_pipelineSettings.minimumAmbient = std::clamp(m_pipelineSettings.minimumAmbient, 0.0f, 0.6f);
    m_pipelineSettings.shadowMinLight = std::clamp(m_pipelineSettings.shadowMinLight, 0.0f, 0.6f);
    m_pipelineSettings.shadowContrast = std::clamp(m_pipelineSettings.shadowContrast, 0.25f, 3.0f);
    m_pipelineSettings.blockLightStrength = std::clamp(m_pipelineSettings.blockLightStrength, 0.0f, 3.0f);
    m_pipelineSettings.fakeBounceStrength = std::clamp(m_pipelineSettings.fakeBounceStrength, 0.0f, 0.5f);
    m_pipelineSettings.aerialStrength = std::clamp(m_pipelineSettings.aerialStrength, 0.0f, 2.0f);
    m_pipelineSettings.horizonScatterStrength = std::clamp(m_pipelineSettings.horizonScatterStrength, 0.0f, 2.0f);
    m_pipelineSettings.volumetricFogStrength = std::clamp(m_pipelineSettings.volumetricFogStrength, 0.0f, 2.0f);
    m_pipelineSettings.underwaterVolumetricLightStrength =
        std::clamp(m_pipelineSettings.underwaterVolumetricLightStrength, 0.0f, 2.0f);
    m_pipelineSettings.noiseDitherStrength = std::clamp(m_pipelineSettings.noiseDitherStrength, 0.0f, 0.08f);
    m_pipelineSettings.sharpenStrength = std::clamp(m_pipelineSettings.sharpenStrength, 0.0f, 1.0f);
    m_pipelineSettings.ssaoRadius = std::clamp(m_pipelineSettings.ssaoRadius, 0.1f, 16.0f);
    m_pipelineSettings.ssaoStrength = std::clamp(m_pipelineSettings.ssaoStrength, 0.0f, 4.0f);
    m_pipelineSettings.exposure = std::clamp(m_pipelineSettings.exposure, 0.1f, 50.0f);
    m_pipelineSettings.gamma = std::clamp(m_pipelineSettings.gamma, 1.0f, 3.5f);
    m_pipelineSettings.saturation = std::clamp(m_pipelineSettings.saturation, 0.0f, 3.0f);
    m_pipelineSettings.contrast = std::clamp(m_pipelineSettings.contrast, 0.25f, 3.0f);
}

Renderer::RenderPipelineSettings Renderer::getRenderPipelineSettings() const {
    return m_pipelineSettings;
}

bool Renderer::isDeferredDebugViewActive() const {
    return m_pipelineSettings.mode == RenderPipelineMode::HybridDeferred &&
           m_pipelineSettings.debugViewMode > 0 &&
           m_deferredTargets.isReady() &&
           m_deferredPipeline && m_deferredPipeline->debugPass() != nullptr;
}

void Renderer::renderDeferredDebugOverlay(const Window& window) {
    if (!isDeferredDebugViewActive()) {
        return;
    }
    renderDeferredDebugView(0, window.getWidth(), window.getHeight());
}

bool Renderer::isHybridDeferredReady() const {
    return m_deferredTargets.isReady() &&
           m_chunkGBufferShader != nullptr &&
           m_shadowDepthShader != nullptr &&
           m_deferredPipeline->lightingPass() != nullptr &&
           m_deferredPipeline->debugPass() != nullptr &&
           m_deferredPipeline->ssaoPass() != nullptr &&
           m_deferredPipeline->velocityPass() != nullptr &&
           m_deferredPipeline->reflectionPass() != nullptr &&
           m_deferredPipeline->cloudPass() != nullptr;
}

Renderer::HeldItemShadowData Renderer::getHeldItemShadowData() const {
    HeldItemShadowData data{};
    const auto& settings = m_pipelineSettings;
    data.shadowsEnabled = settings.shadowsEnabled ? 1 : 0;
    data.softShadowsEnabled = settings.softShadowsEnabled ? 1 : 0;
    data.pcssShadowsEnabled = settings.pcssShadowsEnabled ? 1 : 0;
    data.shadowDistance = settings.shadowDistance;
    data.constantBias = settings.shadowConstantBias;
    data.slopeBias = settings.shadowSlopeBias;
    data.normalOffset = settings.shadowNormalOffset;
    data.softness = settings.shadowSoftness;
    data.pcssStrength = settings.shadowPcssStrength;
    data.cameraPos = m_cameraPos;
    data.skyIntensity = m_currentFrameData.skyIntensity;

    // Held items are rendered by Game after Renderer::renderTransparentAndOverlays()
    // calls endFrame(), which clears m_deferredFrameActive. Keep exposing the
    // latest CSM resources as long as the deferred targets are allocated.
    if (m_deferredTargets.isReady() && m_deferredTargets.csmShadowDepthComparisonTexture() != 0) {
        const auto& cascades = m_shadowRenderer.cascades();
        data.cascadeCount = static_cast<int>(cascades.size());
        data.sunDirection = m_shadowRenderer.lightDirection();
        for (int i = 0; i < data.cascadeCount && i < 4; ++i) {
            data.cascadeViewProj[i] = cascades[i].viewProj;
            data.cascadeSplitFar[i] = cascades[i].splitFar;
            data.cascadeTexelWorldSize[i] = cascades[i].texelWorldSize;
        }
        data.shadowTexture = m_deferredTargets.csmShadowDepthComparisonTexture();
        data.shadowDepthRaw = m_deferredTargets.csmShadowDepthTexture();
        data.shadowDepthAll = m_deferredTargets.csmShadowDepthAllComparisonTexture();
        data.shadowDepthAllRaw = m_deferredTargets.csmShadowDepthAllTexture();
        data.shadowColor0 = m_deferredTargets.csmShadowColor0Texture();
        data.shadowColor1 = m_deferredTargets.csmShadowColor1Texture();
    }
    return data;
}

float Renderer::getAtlasAnisotropy() const {
    if (m_resourceMgr == nullptr) {
        return 1.0f;
    }
    return m_resourceMgr->getAtlasAnisotropy();
}

float Renderer::getAtlasMaxAnisotropy() const {
    if (m_resourceMgr == nullptr) {
        return 1.0f;
    }
    return m_resourceMgr->getAtlasMaxAnisotropy();
}

#ifdef MECRAFT_DEBUG
void Renderer::setChunkCullingDebugEnabled(const bool enabled) {
    m_chunkCullingDebugEnabled = enabled;
    m_terrainRenderer.setChunkCullingDebugEnabled(enabled);
}

int Renderer::getMeshingSubmitBudget() const {
    return m_terrainStreamingService ? m_terrainStreamingService->meshingSubmitBudget() : m_meshingSubmitBudget;
}

int Renderer::getRegionChunkSize() const {
    return m_terrainStreamingService ? m_terrainStreamingService->regionChunkSize() : m_regionChunkSize;
}

bool Renderer::isChunkCullingDebugEnabled() const {
    return m_chunkCullingDebugEnabled;
}

Renderer::MeshingFrameStats Renderer::getMeshingFrameStats() const {
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

CullingFrameStats Renderer::getCullingFrameStats() const {
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

GpuFrameStats Renderer::getGpuFrameStats() const {
    if (m_debugService) {
        return m_debugService->getGpuFrameStats();
    }
    return m_gpuFrameStats;
}

RenderWorkStats Renderer::getRenderWorkStats() const {
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

void Renderer::setGpuTimerEnabled(const bool enabled) {
    if (m_debugService) {
        m_debugService->setGpuTimerEnabled(enabled);
        return;
    }
    m_gpuTimerEnabled = enabled;
}

bool Renderer::isGpuTimerEnabled() const {
    if (m_debugService) {
        return m_debugService->isGpuTimerEnabled();
    }
    return m_gpuTimerEnabled;
}

void Renderer::setCutoutDistanceLimitEnabled(const bool enabled) {
    m_cutoutDistanceLimitEnabled = enabled;
    m_terrainRenderer.setCutoutDistanceLimitEnabled(enabled);
}

bool Renderer::isCutoutDistanceLimitEnabled() const {
    return m_cutoutDistanceLimitEnabled;
}

void Renderer::setCutoutRenderDistanceChunks(const float distanceChunks) {
    m_cutoutRenderDistanceChunks = std::clamp(distanceChunks, 1.0f, 32.0f);
    m_terrainRenderer.setCutoutRenderDistanceChunks(m_cutoutRenderDistanceChunks);
}

float Renderer::getCutoutRenderDistanceChunks() const {
    return m_cutoutRenderDistanceChunks;
}

const std::array<float, Renderer::MESHING_HISTORY_SIZE>& Renderer::getMeshingSubmittedHistory() const {
    if (m_terrainStreamingService) {
        return m_terrainStreamingService->getMeshingSubmittedHistory();
    }
    return m_meshingSubmittedHistory;
}

const std::array<float, Renderer::MESHING_HISTORY_SIZE>& Renderer::getMeshingCompletedHistory() const {
    if (m_terrainStreamingService) {
        return m_terrainStreamingService->getMeshingCompletedHistory();
    }
    return m_meshingCompletedHistory;
}

const std::array<float, Renderer::MESHING_HISTORY_SIZE>& Renderer::getMeshingInFlightHistory() const {
    if (m_terrainStreamingService) {
        return m_terrainStreamingService->getMeshingInFlightHistory();
    }
    return m_meshingInFlightHistory;
}

size_t Renderer::getMeshingHistoryCount() const {
    if (m_terrainStreamingService) {
        return m_terrainStreamingService->getMeshingHistoryCount();
    }
    return m_meshingHistoryCount;
}
#endif

void Renderer::beginFrame(const Camera &camera, const Window &window) {
    ++m_frameCounter;
    // Use service's cache if available, otherwise use legacy cache
    if (m_terrainStreamingService) {
        m_terrainStreamingService->beginFrame();
    } else {
        m_terrainCache.beginFrame();
    }
    glClearColor(m_fogSettings.color.r, m_fogSettings.color.g, m_fogSettings.color.b, 1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_projection = camera.getProjectionMatrix(window.getAspectRatio());
    m_view = camera.getViewMatrix();
    m_cameraPos = camera.getPosition();
    m_nearPlane = camera.getNear();
    m_farPlane = camera.getFar();
    m_currentFrameDataValid = false;
    m_waterRenderedBeforeTemporal = false;
    m_deferredHistoryUpdatedThisFrame = false;
    updateFrustum(m_projection * m_view);
    drawCallCount = 0;
    m_terrainRenderer.resetDebugCounters();

#ifdef MECRAFT_DEBUG
    m_meshingSubmittedThisFrame = 0;
    m_meshingCompletedThisFrame = 0;
    m_meshingStaleDroppedThisFrame = 0;
    m_meshingBuildMsThisFrame = 0.0;
    m_regionTestsThisFrame = 0;
    m_regionPassedThisFrame = 0;
    m_columnTestsThisFrame = 0;
    m_columnPassedThisFrame = 0;
    m_chunkTestsThisFrame = 0;
    m_chunkPassedThisFrame = 0;
    m_chunkCulledThisFrame = 0;
    m_chunkCulledByPlaneThisFrame.fill(0);
    m_cutoutCandidatesThisFrame = 0;
    m_cutoutSkippedByDistanceThisFrame = 0;
    m_mdiSubChunkTestsThisFrame = 0;
    m_mdiSubChunksCulledThisFrame = 0;
    beginGpuTimerFrame();
#endif
    m_meshUploadVerticesThisFrame = 0;
    m_meshUploadBytesThisFrame = 0;
    m_meshUploadDeferredCount = 0;
    m_worldBufferExpandCountThisFrame = 0;
    m_worldBufferUploadMsThisFrame = 0.0;
}

void Renderer::renderWorld(const World& world) {
    const RenderFrameData frame = buildRenderFrameData(world);
    renderWorldForward(world, frame);
}

void Renderer::renderWorldForward(const World& world, const RenderFrameData& frame) {
    if (m_chunkShader == nullptr || m_resourceMgr == nullptr) {
        m_deferredTransparentEntries.clear();
        return;
    }

    releaseStaleMdiAllocations(world);
    drainMeshingResults(world);

    m_worldRenderBuffer.beginFrame();
    clearTransparentBatches();

    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    bindChunkRenderState(frame, texArray);
    submitMeshingJobs(world);

    std::vector<ChunkRenderEntry> cutoutEntries;
    cutoutEntries.reserve(world.getActiveChunks().size() * 2);
    m_deferredTransparentEntries.clear();
    m_deferredTransparentEntries.reserve(world.getActiveChunks().size() * 2);
    renderOpaqueChunksAndCollectPasses(world, cutoutEntries, m_deferredTransparentEntries);
    syncTransparentBatches();
    if (m_useMultiDrawIndirect) {
        m_worldRenderBuffer.flushOpaque();
    }
    renderCutoutChunks(cutoutEntries);

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void Renderer::bindChunkRenderState(const RenderFrameData& frame, const TextureArray& texArray) const {
    if (m_chunkShader == nullptr) {
        return;
    }
    bindChunkRenderStateForShader(frame, texArray, *m_chunkShader);
}

Renderer::RenderFrameData Renderer::buildRenderFrameData(const World& world) const {
    RenderFrameData frame;
    frame.view = m_view;
    frame.projection = m_projection;
    frame.viewProj = m_projection * m_view;
    frame.invViewProj = glm::inverse(frame.viewProj);
    frame.cameraPos = m_cameraPos;
    frame.nearPlane = m_nearPlane;
    frame.farPlane = m_farPlane;

    // Temporal foundation
    frame.frameIndex = m_frameCounter;
    frame.deltaTime = static_cast<float>(Time::deltaTime);

    // DerivativeMain shaders.properties:
    // frameX = frac(frameCounter / 1.3247179572 + 0.5) * 2.0 - 1.0
    // frameY = frac(frameCounter / 1.7548776662 + 0.5) * 2.0 - 1.0
    // taaOffset = vec2(frameX / viewWidth, frameY / viewHeight)
    {
        const float invW = 1.0f / static_cast<float>(std::max(1, m_deferredTargets.width()));
        const float invH = 1.0f / static_cast<float>(std::max(1, m_deferredTargets.height()));
        const float frameCounter = static_cast<float>(m_frameCounter);
        const float frameX = glm::fract(frameCounter / 1.3247179572f + 0.5f) * 2.0f - 1.0f;
        const float frameY = glm::fract(frameCounter / 1.7548776662f + 0.5f) * 2.0f - 1.0f;
        frame.jitter.x = frameX * invW;
        frame.jitter.y = frameY * invH;
        if (m_pipelineSettings.freezeTaaJitter) {
            frame.jitter = glm::vec2(0.0f);
        }
    }

    // DerivativeMain-style TAA jitter: bake sub-pixel offset into projection
    // so GBuffer vertices are shifted each frame. This is equivalent to
    // gl_Position.xy += taaOffset * gl_Position.w in the vertex shader.
    {
        glm::mat4 jitteredProj = m_projection;
        for (int column = 0; column < 4; ++column) {
            jitteredProj[column][0] += frame.jitter.x * m_projection[column][3];
            jitteredProj[column][1] += frame.jitter.y * m_projection[column][3];
        }
        frame.jitteredViewProj = jitteredProj * m_view;
        frame.jitteredInvViewProj = glm::inverse(frame.jitteredViewProj);
    }

    if (m_hasPreviousFrameData) {
        frame.previousJitter = m_previousFrameData.jitter;
        frame.previousView = m_previousFrameData.view;
        frame.previousProjection = m_previousFrameData.projection;
        frame.previousViewProj = m_previousFrameData.viewProj;
        frame.previousJitteredViewProj = m_previousFrameData.jitteredViewProj;
        frame.previousInvViewProj = m_previousFrameData.invViewProj;
    } else {
        frame.previousJitter = frame.jitter;
        frame.previousView = frame.view;
        frame.previousProjection = frame.projection;
        frame.previousViewProj = frame.viewProj;
        frame.previousJitteredViewProj = frame.jitteredViewProj;
        frame.previousInvViewProj = frame.invViewProj;
    }
    // Weather state now comes from World::WeatherSystem (single source of truth).
    // Dashboard writes to WeatherSystem; Renderer reads from it.
    const WeatherState& weather = world.getWeatherSystem().getRenderState();
    const WeatherDerived& weatherDerived = world.getWeatherSystem().getDerived();
    frame.weatherWetness = weather.wetness;
    frame.weatherStorm = weather.storm;
    frame.aerialReduction = weather.aerialReduction;
    frame.lightningFlash = weatherDerived.lightningFlash;
    frame.surfaceWetness = weatherDerived.surfaceWetness;
    frame.skyWetness = weatherDerived.skyWetness;
    frame.fogWetness = weatherDerived.fogWetness;
    frame.cloudWetness = weatherDerived.cloudWetness;
    frame.precipitation = weatherDerived.precipitation;
    frame.rainStrength = weatherDerived.rainStrength;
    frame.thunderStrength = weatherDerived.thunderStrength;

    frame.skyColors = m_gameplaySkyRenderer.computeSkyColors(world.getDayNightSystem());
    frame.skyIlluminance = m_gameplaySkyRenderer.computeSkyIlluminance(
        frame.skyColors,
        frame.weatherWetness,
        frame.weatherStorm);
    frame.skyIntensity = world.getDayNightSystem().getSkyIntensity();
    const double gameTime = Time::getGameTime();
    const double visualTime = Time::getRawTime();
    frame.animationTime = static_cast<float>(std::fmod(gameTime, 16.0));
    frame.shaderTime = static_cast<float>(std::fmod(visualTime, 8192.0));

    frame.fogEnabled = m_fogSettings.enabled;
    frame.fogMode = m_fogSettings.mode;
    frame.fogColor = m_fogSettings.color;
    frame.fogStart = m_fogSettings.startDistance;
    frame.fogEnd = m_fogSettings.endDistance;
    frame.fogDensity = m_fogSettings.density;
    if (m_fogSettings.autoDistanceByRenderDistance) {
        const float chunkSize = static_cast<float>(Chunk::SIZE_X);
        const float renderDistanceChunks = static_cast<float>(std::max(1, world.getRenderDistance()));
        // Circular loading: boundary at renderDistance chunks (Euclidean) from player.
        // fogEnd = where fog becomes fully opaque, should be before the chunk boundary
        // so that edge geometry is fully hidden.
        frame.fogEnd = std::max(0.0f, (renderDistanceChunks + m_fogSettings.autoEndOffsetChunks) * chunkSize);
        frame.fogStart = std::max(0.0f, frame.fogEnd - m_fogSettings.autoFadeWidthChunks * chunkSize);
    }
    frame.fogEnd = std::max(frame.fogEnd, frame.fogStart + 0.1f);
    frame.atmosphere.aerialStrength = m_pipelineSettings.aerialStrength;
    frame.atmosphere.horizonScatterStrength = m_pipelineSettings.horizonScatterStrength;
    frame.atmosphere.sunWarmth = m_pipelineSettings.sunWarmth;
    frame.atmosphere.skyCoolness = m_pipelineSettings.skyCoolness;
    frame.atmosphere.weatherWetness = frame.weatherWetness;
    frame.atmosphere.weatherStorm = frame.weatherStorm;
    frame.atmosphere.aerialReduction = frame.aerialReduction;
    frame.atmosphere.lightningFlash = frame.lightningFlash;
    frame.atmosphere.surfaceWetness = frame.surfaceWetness;
    frame.atmosphere.skyWetness = frame.skyWetness;
    frame.atmosphere.fogWetness = frame.fogWetness;
    frame.atmosphere.cloudWetness = frame.cloudWetness;
    frame.atmosphere.precipitation = frame.precipitation;
    // DerivativeMain default: mix(1.0, 0.03, skyWetness). Exposed for energy tuning.
    // slider < 0: auto mode (shader computes from skyWetness + procedural cloud shadow)
    // slider >= 0: manual override (shader bypasses all cloud shadow, returns slider value)
    frame.atmosphere.directWeatherOcclusionOverride = (m_pipelineSettings.directWeatherOcclusion >= 0.0f) ? 1 : 0;
    frame.atmosphere.directWeatherOcclusion = std::clamp(m_pipelineSettings.directWeatherOcclusion, 0.0f, 1.0f);
    frame.volumetric.lightEnabled = m_pipelineSettings.volumetricLightEnabled;
    frame.volumetric.uwLightEnabled = m_pipelineSettings.uwVolumetricLightEnabled;
    frame.volumetric.fogEnabled = m_pipelineSettings.volumetricFogEnabled;
    frame.volumetric.fogStrength = m_pipelineSettings.volumetricFogStrength;
    frame.volumetric.underwaterLightStrength = m_pipelineSettings.underwaterVolumetricLightStrength;
    frame.volumetric.fogCenterHeight = m_pipelineSettings.vfogCenterHeight;
    frame.volumetric.fogHeightSpread = m_pipelineSettings.vfogHeightSpread;
    frame.volumetric.fogNoiseScale = m_pipelineSettings.vfogNoiseScale;
    frame.volumetric.fogLightStrength = m_pipelineSettings.vfogLightStrength;
    frame.volumetric.fogDensityScale = m_pipelineSettings.vfogDensityScale;
    frame.volumetric.fogSamples = std::clamp(m_pipelineSettings.volumetricFogSamples, 2, 50);
    frame.cloud.shadowsEnabled = m_pipelineSettings.cloudShadowsEnabled;
    frame.cloud.shadowStrength = m_pipelineSettings.cloudShadowStrength;
    frame.cloud.shadowScale = m_pipelineSettings.cloudShadowScale;
    frame.cloud.shadowSpeed = m_pipelineSettings.cloudShadowSpeed;
    frame.cloud.timeScale = m_pipelineSettings.cloudTimeScale;
    // DerivativeMain VolumetricClouds.glsl:24: coverage = 1.0 (clear) to 1.2 (rain).
    // Shader uses uCloudCoverage directly — no additional weather amplification.
    const float cloudWetForCoverage = std::clamp(frame.weatherWetness + frame.weatherStorm * (4.0f / 3.0f), 0.0f, 1.0f);
    frame.cloud.coverage = std::clamp(1.0f + cloudWetForCoverage * 0.2f, 0.0f, 1.5f);
    frame.cloud.density = 0.85f + frame.weatherWetness * 0.35f + frame.weatherStorm * 0.55f;
    // DerivativeMain VolumetricClouds.glsl:53-68: wetness-based altitude/thickness interpolation
    float cloudWet = std::clamp(frame.cloudWetness, 0.0f, 1.0f);
    frame.cloud.height = 1000.0f + cloudWet * (800.0f - 1000.0f);    // clear 1000 → rain 800
    frame.cloud.thickness = 1400.0f + cloudWet * (3000.0f - 1400.0f); // clear 1400 → rain 3000
    // Storm altitude/thickness/density corrections are applied in shaders via
    // uCloudDynamicWeather.z to avoid double-application across render paths.
    frame.moonShadowActive = frame.skyColors.moonVisibility > frame.skyColors.sunVisibility;
    frame.eyeInWater = m_eyeInWater;
    return frame;
}

void Renderer::bindSkyLightingUniforms(Shader& shader, const RenderFrameData& frame) const {
    shader.setVec3("uCameraPos", frame.cameraPos);
    shader.setVec3("uSunDirection", frame.skyColors.sunDirection);
    shader.setVec3("uMoonDirection", frame.skyColors.moonDirection);
    shader.setVec3("uSunLightColor", frame.skyColors.sunLightColor);
    shader.setVec3("uMoonLightColor", frame.skyColors.moonLightColor);
    shader.setVec3("uSkyAmbientColor", frame.skyColors.skyAmbientColor);
    shader.setVec3("uShadowTintColor", frame.skyColors.shadowTintColor);
    shader.setVec3("uHorizonScatterColor", frame.skyColors.horizonScatterColor);
    shader.setFloat("uSkyIntensity", frame.skyIntensity);
    shader.setFloat("uMoonVisibility", frame.skyColors.moonVisibility);
    shader.setVec3("uDirectIlluminance", frame.skyIlluminance.directIlluminance);
    shader.setVec3("uSkyIlluminance", frame.skyIlluminance.skyIlluminance);
    shader.setVec3("uSunIlluminance", frame.skyIlluminance.sunIlluminance);
    shader.setVec3("uMoonIlluminance", frame.skyIlluminance.moonIlluminance);
    shader.setVec3("uCloudDynamicWeather", frame.skyIlluminance.cloudDynamicWeather);
    shader.setInt("uHeldBlockLightValue", m_heldBlockLightValue);
    shader.setInt("uHeldBlockLightValue2", 0); // Off-hand slot (unused until dual-wield)
}

void Renderer::bindFogUniforms(Shader& shader, const RenderFrameData& frame) const {
    shader.setInt("uFogEnabled", frame.fogEnabled ? 1 : 0);
    shader.setInt("uFogMode", static_cast<int>(frame.fogMode));
    shader.setVec3("uFogColor", frame.fogColor);
    shader.setFloat("uFogStart", frame.fogStart);
    shader.setFloat("uFogEnd", frame.fogEnd);
    shader.setFloat("uFogDensity", frame.fogDensity);
}

void Renderer::bindAtmosphereUniforms(Shader& shader, const RenderFrameData& frame) const {
    shader.setFloat("uAerialStrength", frame.atmosphere.aerialStrength);
    shader.setFloat("uHorizonScatterStrength", frame.atmosphere.horizonScatterStrength);
    shader.setFloat("uSunWarmth", frame.atmosphere.sunWarmth);
    shader.setFloat("uSkyCoolness", frame.atmosphere.skyCoolness);
    shader.setFloat("uWeatherWetness", frame.atmosphere.weatherWetness);
    shader.setFloat("uWeatherStorm", frame.atmosphere.weatherStorm);
    shader.setFloat("uAerialReduction", frame.atmosphere.aerialReduction);
    shader.setFloat("uLightningFlash", frame.atmosphere.lightningFlash);
    shader.setFloat("uSurfaceWetness", frame.atmosphere.surfaceWetness);
    shader.setFloat("uSkyWetness", frame.atmosphere.skyWetness);
    shader.setFloat("uFogWetness", frame.atmosphere.fogWetness);
    shader.setFloat("uCloudWetness", frame.atmosphere.cloudWetness);
    shader.setFloat("uPrecipitation", frame.atmosphere.precipitation);
    shader.setFloat("uDirectWeatherOcclusion", frame.atmosphere.directWeatherOcclusion);
    shader.setInt("uDirectWeatherOcclusionOverride", frame.atmosphere.directWeatherOcclusionOverride);
}

void Renderer::bindChunkRenderStateForShader(const RenderFrameData& frame, const TextureArray& texArray, Shader& shader) const {
    shader.use();
    shader.setMat4("view", frame.view);
    shader.setMat4("viewProj", frame.viewProj);
    shader.setInt("uUseModel", 0);
    shader.setInt("texArray", 0);
    shader.setInt("uLightmapDay", 1);
    shader.setInt("uLightmapNight", 2);
    shader.setInt("uGrassColormap", 3);
    shader.setInt("uFoliageColormap", 4);
    shader.setInt("uOpaqueDepthTex", 5);
    shader.setInt("uSkyCaptureTex", 6);
    shader.setInt("uSceneColorTex", 7);
    shader.setInt("uWaterNoiseTex", 8);
    shader.setInt("uNoiseTex", 9);
    shader.setInt("uRippleNormalTex", 10);
    shader.setInt("uSkyCaptureEnabled", m_deferredFrameActive ? 1 : 0);
    shader.setInt("uCompositeInputsEnabled", 0);
    shader.setInt("uWaterCompositeEnabled", 0);
    shader.setInt("uForceBaseLod", 0);
    shader.setInt("uDepthSofteningEnabled", 0);
    bindFogUniforms(shader, frame);
    shader.setFloat("uAnimationTime", frame.animationTime);
    shader.setFloat("uShaderTime", frame.shaderTime);
    shader.setFloat("uSurfaceWetness", frame.surfaceWetness);
    shader.setInt("uRainWetSurfacesEnabled", m_pipelineSettings.rainWetSurfacesEnabled ? 1 : 0);
    shader.setInt("uRainSurfaceRipplesEnabled", m_pipelineSettings.rainSurfaceRipplesEnabled ? 1 : 0);
    shader.setInt("uDebugLightMode", m_debugLightMode);
    bindSkyLightingUniforms(shader, frame);
    shader.setInt("uAerialPerspectiveEnabled", m_pipelineSettings.aerialPerspectiveEnabled ? 1 : 0);
    shader.setInt("uVolumetricLightEnabled", m_pipelineSettings.volumetricLightEnabled ? 1 : 0);
    // Aerial perspective mutual exclusion: skip when volumetric fog OR light is active.
    // Matches deferred_lighting.fs condition for consistent behavior across deferred/forward paths.
    const bool volFogActive = (m_pipelineSettings.volumetricLightEnabled ||
                               (m_pipelineSettings.volumetricFogEnabled &&
                                m_pipelineSettings.volumetricFogStrength > 0.001f)) &&
                              m_deferredPipeline->volumetricPass() && m_deferredPipeline->volumetricPass()->hasShaders();
    shader.setInt("uVolumetricFogActive", volFogActive ? 1 : 0);
    shader.setFloat("uDirectSunStrength", m_pipelineSettings.directSunStrength);
    shader.setFloat("uSkyAmbientStrength", m_pipelineSettings.skyAmbientStrength);
    shader.setFloat("uWeatherSkylightScale", m_pipelineSettings.weatherSkylightScale);
    shader.setFloat("uMinimumAmbient", m_pipelineSettings.minimumAmbient);
    shader.setFloat("uBlockLightStrength", m_pipelineSettings.blockLightStrength);
    shader.setFloat("uFakeBounceStrength", m_pipelineSettings.fakeBounceStrength);
    shader.setFloat("uAlbedoDesaturation", m_pipelineSettings.albedoDesaturation);
    shader.setFloat("uShadowDesaturation", m_pipelineSettings.shadowDesaturation);
    bindAtmosphereUniforms(shader, frame);
    shader.setInt("uAtmosphereLut", 14);
    shader.setVec3("uWaterAbsorption", glm::vec3(1.0f));
    bindWaterEffectUniforms(shader, false);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texArray.textureID);

    // Bind lightmap textures
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getLightmapDay());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getLightmapNight());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getGrassColormap());
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getFoliageColormap());
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, m_deferredFrameActive ? m_deferredTargets.skyCaptureTexture() : 0);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, m_resourceMgr != nullptr ? m_resourceMgr->getTexture2D("shader_noise2d") : 0);
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, m_resourceMgr != nullptr ? m_resourceMgr->getTexture2D("shader_ripple_normal") : 0);
    glActiveTexture(GL_TEXTURE14);
    glBindTexture(GL_TEXTURE_3D, m_deferredTargets.atmosphereLutTexture());
}

void Renderer::bindWaterEffectUniforms(Shader& shader, const bool enabled) const {
    shader.setInt("uWaterEffectsEnabled", enabled ? 1 : 0);
    if (m_resourceMgr == nullptr) {
        shader.setFloat("uWaterStillFirstLayer", 0.0f);
        shader.setFloat("uWaterStillLayerCount", 0.0f);
        shader.setFloat("uWaterFlowFirstLayer", 0.0f);
        shader.setFloat("uWaterFlowLayerCount", 0.0f);
        return;
    }

    const TextureAnimationInfo still = m_resourceMgr->getTextureAnimation("water_still");
    const TextureAnimationInfo flow = m_resourceMgr->getTextureAnimation("water_flow");
    shader.setFloat("uWaterStillFirstLayer", static_cast<float>(still.firstLayer));
    shader.setFloat("uWaterStillLayerCount", static_cast<float>(std::max(1, still.frameCount)));
    shader.setFloat("uWaterFlowFirstLayer", static_cast<float>(flow.firstLayer));
    shader.setFloat("uWaterFlowLayerCount", static_cast<float>(std::max(1, flow.frameCount)));
}

void Renderer::bindTransparentCompositeInputs(Shader& shader,
                                              const bool deferredInputsEnabled,
                                              const bool compositeInputsEnabled) const {
    shader.setInt("uCompositeInputsEnabled", compositeInputsEnabled ? 1 : 0);
    shader.setInt("uWaterCompositeEnabled", compositeInputsEnabled ? 1 : 0);
    if (deferredInputsEnabled) {
        shader.setInt("uDepthSofteningEnabled", 1);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, m_deferredTargets.depthTexture());
    }
    if (compositeInputsEnabled && m_resourceMgr != nullptr) {
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, m_deferredTargets.sceneResolvedTexture());
        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getTexture2D("shader_noise2d"));
    }
}

FrameContext Renderer::buildFrameContextFromRenderFrameData(const RenderFrameData& frame) const {
    FrameContext ctx{};
    ctx.camera.view = frame.view;
    ctx.camera.projection = frame.projection;
    ctx.camera.viewProj = frame.viewProj;
    ctx.camera.jitteredViewProj = frame.jitteredViewProj;
    ctx.camera.jitteredInvViewProj = frame.jitteredInvViewProj;
    ctx.camera.invViewProj = frame.invViewProj;
    ctx.camera.position = frame.cameraPos;
    ctx.camera.nearPlane = frame.nearPlane;
    ctx.camera.farPlane = frame.farPlane;
    ctx.previousViewProj = frame.previousViewProj;
    ctx.previousInvViewProj = frame.previousInvViewProj;
    ctx.previousJitteredViewProj = frame.previousJitteredViewProj;
    ctx.frameIndex = frame.frameIndex;
    ctx.deltaTime = frame.deltaTime;
    ctx.animationTime = frame.animationTime;
    ctx.shaderTime = frame.shaderTime;
    ctx.frameWidth = m_deferredTargets.width();
    ctx.frameHeight = m_deferredTargets.height();
    ctx.jitter = frame.jitter;
    ctx.prevJitter = frame.previousJitter;
    // Sky colors
    ctx.skyColors.top = frame.skyColors.top;
    ctx.skyColors.horizon = frame.skyColors.horizon;
    ctx.skyColors.fog = frame.skyColors.fog;
    ctx.skyColors.halo = frame.skyColors.halo;
    ctx.skyColors.sunDirection = frame.skyColors.sunDirection;
    ctx.skyColors.sunScatter = frame.skyColors.sunScatter;
    ctx.skyColors.sunLightColor = frame.skyColors.sunLightColor;
    ctx.skyColors.skyAmbientColor = frame.skyColors.skyAmbientColor;
    ctx.skyColors.shadowTintColor = frame.skyColors.shadowTintColor;
    ctx.skyColors.horizonScatterColor = frame.skyColors.horizonScatterColor;
    ctx.skyColors.cloudColor = frame.skyColors.cloudColor;
    ctx.skyColors.moonDirection = frame.skyColors.moonDirection;
    ctx.skyColors.moonLightColor = frame.skyColors.moonLightColor;
    ctx.skyColors.haloStrength = frame.skyColors.haloStrength;
    ctx.skyColors.horizonHaze = frame.skyColors.horizonHaze;
    ctx.skyColors.sunGlare = frame.skyColors.sunGlare;
    ctx.skyColors.sunVisibility = frame.skyColors.sunVisibility;
    ctx.skyColors.moonVisibility = frame.skyColors.moonVisibility;
    ctx.skyColors.dayFactor = frame.skyColors.dayFactor;
    ctx.skyColors.nightFactor = frame.skyColors.nightFactor;
    ctx.skyColors.horizonFactor = frame.skyColors.horizonFactor;
    ctx.skyColors.rainFactor = frame.skyColors.rainFactor;
    ctx.skyColors.wetnessFactor = frame.skyColors.wetnessFactor;
    ctx.skyColors.cloudinessFactor = frame.skyColors.cloudinessFactor;
    ctx.skyIlluminance.directIlluminance = frame.skyIlluminance.directIlluminance;
    ctx.skyIlluminance.skyIlluminance = frame.skyIlluminance.skyIlluminance;
    ctx.skyIlluminance.sunIlluminance = frame.skyIlluminance.sunIlluminance;
    ctx.skyIlluminance.moonIlluminance = frame.skyIlluminance.moonIlluminance;
    ctx.skyIlluminance.cloudDynamicWeather = frame.skyIlluminance.cloudDynamicWeather;
    ctx.skyIntensity = frame.skyIntensity;
    // Weather
    ctx.weather.wetness = frame.weatherWetness;
    ctx.weather.storm = frame.weatherStorm;
    ctx.weather.surfaceWetness = frame.surfaceWetness;
    ctx.weather.skyWetness = frame.skyWetness;
    ctx.weather.fogWetness = frame.fogWetness;
    ctx.weather.cloudWetness = frame.cloudWetness;
    ctx.weather.precipitation = frame.precipitation;
    ctx.weather.rainStrength = frame.rainStrength;
    ctx.weather.thunderStrength = frame.thunderStrength;
    ctx.weather.lightningFlash = frame.lightningFlash;
    ctx.weather.aerialReduction = frame.aerialReduction;
    // State
    ctx.eyeInWater = frame.eyeInWater;
    ctx.moonShadowActive = frame.moonShadowActive;
    ctx.hasPreviousFrame = m_hasPreviousFrameData;
    // Atmosphere
    ctx.atmosphere.aerialStrength = frame.atmosphere.aerialStrength;
    ctx.atmosphere.horizonScatterStrength = frame.atmosphere.horizonScatterStrength;
    ctx.atmosphere.sunWarmth = frame.atmosphere.sunWarmth;
    ctx.atmosphere.skyCoolness = frame.atmosphere.skyCoolness;
    ctx.atmosphere.directWeatherOcclusion = frame.atmosphere.directWeatherOcclusion;
    ctx.atmosphere.directWeatherOcclusionOverride = frame.atmosphere.directWeatherOcclusionOverride;
    // Fog
    ctx.fog.enabled = frame.fogEnabled;
    ctx.fog.mode = static_cast<int>(frame.fogMode);
    ctx.fog.color = frame.fogColor;
    ctx.fog.startDistance = frame.fogStart;
    ctx.fog.endDistance = frame.fogEnd;
    ctx.fog.density = frame.fogDensity;
    // Volumetric
    ctx.volumetric.lightEnabled = frame.volumetric.lightEnabled;
    ctx.volumetric.uwLightEnabled = frame.volumetric.uwLightEnabled;
    ctx.volumetric.fogEnabled = frame.volumetric.fogEnabled;
    ctx.volumetric.fogStrength = frame.volumetric.fogStrength;
    ctx.volumetric.underwaterLightStrength = frame.volumetric.underwaterLightStrength;
    ctx.volumetric.baseDensity = frame.volumetric.baseDensity;
    ctx.volumetric.maxDistance = frame.volumetric.maxDistance;
    ctx.volumetric.fogCenterHeight = frame.volumetric.fogCenterHeight;
    ctx.volumetric.fogHeightSpread = frame.volumetric.fogHeightSpread;
    ctx.volumetric.fogNoiseScale = frame.volumetric.fogNoiseScale;
    ctx.volumetric.fogLightStrength = frame.volumetric.fogLightStrength;
    ctx.volumetric.fogDensityScale = frame.volumetric.fogDensityScale;
    ctx.volumetric.fogSamples = frame.volumetric.fogSamples;
    // Cloud
    ctx.cloud.shadowsEnabled = frame.cloud.shadowsEnabled;
    ctx.cloud.shadowStrength = frame.cloud.shadowStrength;
    ctx.cloud.shadowScale = frame.cloud.shadowScale;
    ctx.cloud.shadowSpeed = frame.cloud.shadowSpeed;
    ctx.cloud.timeScale = frame.cloud.timeScale;
    ctx.cloud.coverage = frame.cloud.coverage;
    ctx.cloud.density = frame.cloud.density;
    ctx.cloud.height = frame.cloud.height;
    ctx.cloud.thickness = frame.cloud.thickness;
    ctx.cloud.planarCoverage = frame.cloud.planarCoverage;
    ctx.cloud.planarDensity = frame.cloud.planarDensity;
    ctx.cloud.planarAltitude = frame.cloud.planarAltitude;
    return ctx;
}

TerrainFrameData Renderer::buildTerrainFrameData(const RenderFrameData& frame) const {
    TerrainFrameData tfd{};
    tfd.view = frame.view;
    tfd.viewProj = frame.viewProj;
    tfd.cameraPos = frame.cameraPos;
    tfd.animationTime = frame.animationTime;
    tfd.shaderTime = frame.shaderTime;
    tfd.surfaceWetness = frame.surfaceWetness;
    tfd.fog.enabled = frame.fogEnabled;
    tfd.fog.mode = static_cast<int>(frame.fogMode);
    tfd.fog.color = frame.fogColor;
    tfd.fog.start = frame.fogStart;
    tfd.fog.end = frame.fogEnd;
    tfd.fog.density = frame.fogDensity;
    tfd.skyLighting.cameraPos = frame.cameraPos;
    tfd.skyLighting.sunDirection = frame.skyColors.sunDirection;
    tfd.skyLighting.moonDirection = frame.skyColors.moonDirection;
    tfd.skyLighting.sunLightColor = frame.skyColors.sunLightColor;
    tfd.skyLighting.moonLightColor = frame.skyColors.moonLightColor;
    tfd.skyLighting.skyAmbientColor = frame.skyColors.skyAmbientColor;
    tfd.skyLighting.shadowTintColor = frame.skyColors.shadowTintColor;
    tfd.skyLighting.horizonScatterColor = frame.skyColors.horizonScatterColor;
    tfd.skyLighting.skyIntensity = frame.skyIntensity;
    tfd.skyLighting.moonVisibility = frame.skyColors.moonVisibility;
    tfd.skyLighting.directIlluminance = frame.skyIlluminance.directIlluminance;
    tfd.skyLighting.skyIlluminance = frame.skyIlluminance.skyIlluminance;
    tfd.skyLighting.sunIlluminance = frame.skyIlluminance.sunIlluminance;
    tfd.skyLighting.moonIlluminance = frame.skyIlluminance.moonIlluminance;
    tfd.skyLighting.cloudDynamicWeather = frame.skyIlluminance.cloudDynamicWeather;
    tfd.atmosphere.aerialStrength = frame.atmosphere.aerialStrength;
    tfd.atmosphere.horizonScatterStrength = frame.atmosphere.horizonScatterStrength;
    tfd.atmosphere.sunWarmth = frame.atmosphere.sunWarmth;
    tfd.atmosphere.skyCoolness = frame.atmosphere.skyCoolness;
    tfd.atmosphere.weatherWetness = frame.atmosphere.weatherWetness;
    tfd.atmosphere.weatherStorm = frame.atmosphere.weatherStorm;
    tfd.atmosphere.aerialReduction = frame.atmosphere.aerialReduction;
    tfd.atmosphere.lightningFlash = frame.atmosphere.lightningFlash;
    tfd.atmosphere.surfaceWetness = frame.atmosphere.surfaceWetness;
    tfd.atmosphere.skyWetness = frame.atmosphere.skyWetness;
    tfd.atmosphere.fogWetness = frame.atmosphere.fogWetness;
    tfd.atmosphere.cloudWetness = frame.atmosphere.cloudWetness;
    tfd.atmosphere.precipitation = frame.atmosphere.precipitation;
    tfd.atmosphere.directWeatherOcclusion = frame.atmosphere.directWeatherOcclusion;
    tfd.atmosphere.directWeatherOcclusionOverride = frame.atmosphere.directWeatherOcclusionOverride;
    return tfd;
}

RenderSettings Renderer::buildRenderSettingsFromPipelineSettings() const {
    return settings_mapper::toRenderSettings(m_pipelineSettings);
}

bool Renderer::renderWorldDeferred(const World& world,
                                   const Camera& camera,
                                   const Window& window,
                                   const RenderFrameData& frame) {
    if (m_resourceMgr == nullptr ||
        m_chunkGBufferShader == nullptr ||
        m_deferredPipeline->lightingPass() == nullptr ||
        m_deferredPipeline->ssaoPass() == nullptr ||
        !m_deferredTargets.init()) {
        return false;
    }

    captureCurrentFramebuffer();
    if (!m_deferredTargets.ensureSize(window.getWidth(), window.getHeight(), m_pipelineSettings.shadowResolution)) {
        restoreCapturedFramebufferViewport(window);
        return false;
    }
    // After a resize/rebuild, history textures are freshly allocated and uninitialized.
    // Invalidate previous frame data so temporal passes skip the first frame.
    if (m_deferredTargets.consumeRebuiltFlag()) {
        m_hasPreviousFrameData = false;
    }
    clearDeferredAuxiliaryTargets();
    // Phase 7a: Sky capture
    if (m_deferredPipeline->skyCapturePass()) {
        m_deferredPipeline->skyCapturePass()->execute(world, m_deferredTargets, m_gameplaySkyRenderer,
                                   m_resourceMgr, m_cameraPos.y, m_currentFrameData.shaderTime,
                                   m_currentFrameData.cameraPos, m_pipelineSettings.cloudTimeScale);
    }

    m_deferredFrameActive = true;
#ifdef MECRAFT_DEBUG
    beginGpuTimer(GpuTimerPass::GBuffer);
#endif
    renderGBufferTerrain(world, frame);
    // Phase 7a: Entity and drop GBuffer
    {
        FrameContext gbufCtx = buildFrameContextFromRenderFrameData(frame);
        if (m_deferredPipeline->gbufferPass()) {
            m_deferredPipeline->gbufferPass()->executeEntities(world, gbufCtx, m_deferredTargets,
                                            m_humanoidRenderer, m_gameplayRegistry,
                                            m_renderLocalPlayerModel);
            m_deferredPipeline->gbufferPass()->executeDrops(world, gbufCtx, m_deferredTargets,
                                         m_dropRenderer, m_dropSystem);
        }
    }
#ifdef MECRAFT_DEBUG
    endGpuTimer(GpuTimerPass::GBuffer);
#endif
    // Phase 4: Velocity pass
    {
        FrameContext velCtx = buildFrameContextFromRenderFrameData(frame);
        RenderSettings velRs = buildRenderSettingsFromPipelineSettings();
        m_deferredPipeline->velocityPass()->execute(velCtx, velRs, m_deferredTargets);
    }
    if (m_pipelineSettings.shadowsEnabled && m_shadowDepthShader != nullptr) {
#ifdef MECRAFT_DEBUG
        beginGpuTimer(GpuTimerPass::Shadow);
#endif
        renderShadowMap(world, camera, frame);
#ifdef MECRAFT_DEBUG
        endGpuTimer(GpuTimerPass::Shadow);
#endif
    }
    if (m_deferredPipeline->ssaoPass()) {
#ifdef MECRAFT_DEBUG
        beginGpuTimer(GpuTimerPass::Ssao);
#endif
        FrameContext ssaoCtx = buildFrameContextFromRenderFrameData(frame);
        RenderSettings ssaoRs = buildRenderSettingsFromPipelineSettings();
        m_deferredPipeline->ssaoPass()->execute(ssaoCtx, ssaoRs, m_deferredTargets);
#ifdef MECRAFT_DEBUG
        endGpuTimer(GpuTimerPass::Ssao);
#endif
    }
    const int capturedWidth = m_capturedViewport[2] > 0 ? m_capturedViewport[2] : window.getWidth();
    const int capturedHeight = m_capturedViewport[3] > 0 ? m_capturedViewport[3] : window.getHeight();
    m_deferredTargets.copyFramebufferColorToSceneLighting(m_capturedFramebuffer, capturedWidth, capturedHeight);
#ifdef MECRAFT_DEBUG
    beginGpuTimer(GpuTimerPass::Lighting);
#endif
    // Phase 5a: Deferred lighting pass
    if (m_deferredPipeline->lightingPass()) {
        FrameContext lightCtx = buildFrameContextFromRenderFrameData(frame);
        RenderSettings lightRs = buildRenderSettingsFromPipelineSettings();
        m_deferredPipeline->lightingPass()->setHeldBlockLightValue(m_heldBlockLightValue);
        m_deferredPipeline->lightingPass()->execute(lightCtx, lightRs, m_deferredTargets);
    }
#ifdef MECRAFT_DEBUG
    endGpuTimer(GpuTimerPass::Lighting);
#endif
    if (m_pipelineSettings.deferredLightDebugMode > 0) {
        m_deferredTargets.copySceneLightingToSceneComposite();
        m_deferredTargets.copySceneCompositeToTransparentComposite();
        m_deferredTargets.copySceneCompositeToSceneResolved();
        updateDeferredHistoryTargets();
        m_deferredTargets.blitSceneResolvedTo(m_capturedFramebuffer, capturedWidth, capturedHeight);
        m_deferredTargets.blitDepthTo(m_capturedFramebuffer, capturedWidth, capturedHeight);
        restoreCapturedFramebufferViewport(window);
        return true;
    }
    if (m_deferredPipeline->reflectionPass()) {
#ifdef MECRAFT_DEBUG
        beginGpuTimer(GpuTimerPass::Reflection);
#endif
        FrameContext reflCtx = buildFrameContextFromRenderFrameData(frame);
        RenderSettings reflRs = buildRenderSettingsFromPipelineSettings();
        m_deferredPipeline->reflectionPass()->execute(reflCtx, reflRs, m_deferredTargets);
#ifdef MECRAFT_DEBUG
        endGpuTimer(GpuTimerPass::Reflection);
#endif
    }
    if (m_deferredPipeline->cloudPass()) {
#ifdef MECRAFT_DEBUG
        beginGpuTimer(GpuTimerPass::Cloud);
#endif
        FrameContext cloudCtx = buildFrameContextFromRenderFrameData(frame);
        RenderSettings cloudRs = buildRenderSettingsFromPipelineSettings();
        m_deferredPipeline->cloudPass()->execute(cloudCtx, cloudRs, m_deferredTargets);
#ifdef MECRAFT_DEBUG
        endGpuTimer(GpuTimerPass::Cloud);
#endif
    }
    // Phase 5b: Scene composite
    {
        FrameContext compCtx = buildFrameContextFromRenderFrameData(frame);
        RenderSettings compRs = buildRenderSettingsFromPipelineSettings();
        m_deferredPipeline->sceneCompositePass()->execute(compCtx, compRs, m_deferredTargets);
    }
    m_deferredTargets.copySceneCompositeToTransparentComposite();
    m_deferredTargets.copySceneCompositeToSceneResolved();
    if (m_pipelineSettings.reflectionDebugMode > 0) {
        updateDeferredHistoryTargets();
        m_deferredTargets.blitSceneResolvedTo(m_capturedFramebuffer, capturedWidth, capturedHeight);
        m_deferredTargets.blitDepthTo(m_capturedFramebuffer, capturedWidth, capturedHeight);
        restoreCapturedFramebufferViewport(window);
        return true;
    }

    // DerivativeMain renders water with taaOffset in gbuffers/composite before
    // the scene temporal pass. Do it before VFog so volumetric fog is applied
    // once to the combined opaque+water scene, matching composite1.fsh.
    if (m_pipelineSettings.taaEnabled) {
        renderWaterCompositePass(world, window, true);
    }

    // Particles: render into SceneComposite before volumetric fog composite
    // because the fog composite reads SceneComposite and writes SceneResolved.
    // Keep SceneResolved synchronized for the no-volumetric path.
    // Matches DerivativeMain "weather before deferred/temporal" convention.
    renderParticlesToSceneResolved(frame);
    m_deferredTargets.copySceneCompositeToSceneResolved();

    // DerivativeMain-style pipeline: VFog composited BEFORE TAA so the fog
    // participates in temporal accumulation. R1 dither + checkerboard upscale
    // provides per-frame variation that TAA resolves over multiple frames.
    // UW_VOLUMETRIC_LIGHT: underwater volumetric light runs even when overworld VFog
    // strength is zero — the underwater branch is independent of fog density settings.
    // Phase 5c: Volumetric fog
    if (m_deferredPipeline->volumetricPass()) {
#ifdef MECRAFT_DEBUG
        beginGpuTimer(GpuTimerPass::Volumetric);
#endif
        FrameContext volCtx = buildFrameContextFromRenderFrameData(frame);
        RenderSettings volRs = buildRenderSettingsFromPipelineSettings();
        m_deferredPipeline->volumetricPass()->execute(volCtx, volRs, m_deferredTargets, m_hasPreviousFrameData);
#ifdef MECRAFT_DEBUG
        endGpuTimer(GpuTimerPass::Volumetric);
#endif
    }

    // Phase 4: TAA resolve
    if (m_deferredPipeline->taaPass() && m_pipelineSettings.taaEnabled && m_hasPreviousFrameData) {
        FrameContext taaCtx = buildFrameContextFromRenderFrameData(frame);
        RenderSettings taaRs = buildRenderSettingsFromPipelineSettings();
        m_deferredPipeline->taaPass()->execute(taaCtx, taaRs, m_deferredTargets);
    }
    // Phase 4: Motion blur
    if (m_deferredPipeline->motionBlurPass() && m_pipelineSettings.motionBlurEnabled && m_hasPreviousFrameData) {
        FrameContext mbCtx = buildFrameContextFromRenderFrameData(frame);
        RenderSettings mbRs = buildRenderSettingsFromPipelineSettings();
        m_deferredPipeline->motionBlurPass()->execute(mbCtx, mbRs, m_deferredTargets);
    }
    // Phase 4: Depth of field
    if (m_deferredPipeline->dofPass() && m_pipelineSettings.dofEnabled) {
        FrameContext dofCtx = buildFrameContextFromRenderFrameData(frame);
        RenderSettings dofRs = buildRenderSettingsFromPipelineSettings();
        m_deferredPipeline->dofPass()->execute(dofCtx, dofRs, m_deferredTargets);
    }
    updateDeferredHistoryTargets();
    m_deferredTargets.copySceneResolvedToTransparentComposite();
    m_deferredTargets.blitSceneResolvedTo(m_capturedFramebuffer, capturedWidth, capturedHeight);
    m_deferredTargets.blitDepthTo(m_capturedFramebuffer, capturedWidth, capturedHeight);
    restoreCapturedFramebufferViewport(window);
    return true;
}

void Renderer::renderGBufferTerrain(const World& world, const RenderFrameData& frame) {
    m_deferredTargets.bindGBuffer();
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    constexpr GLfloat clearAlbedo[] = {0.0f, 0.0f, 0.0f, 0.0f};
    constexpr GLfloat clearNormal[] = {0.5f, 0.5f, 1.0f, 1.0f};
    constexpr GLfloat clearLight[] = {0.0f, 0.0f, 0.0f, 1.0f};
    constexpr GLfloat clearMaterial[] = {0.86f, 0.035f, 0.0f, 0.0f};
    constexpr GLfloat clearMaterialAux[] = {0.0f, 0.0f, 0.65f, 0.0f};
    glClearBufferfv(GL_COLOR, 0, clearAlbedo);
    glClearBufferfv(GL_COLOR, 1, clearNormal);
    glClearBufferfv(GL_COLOR, 2, clearLight);
    glClearBufferfv(GL_COLOR, 3, clearMaterial);
    glClearBufferfv(GL_COLOR, 4, clearMaterialAux);
    glClear(GL_DEPTH_BUFFER_BIT);

    releaseStaleMdiAllocations(world);
    drainMeshingResults(world);
    m_worldRenderBuffer.beginFrame();
    m_terrainRenderer.clearTransparentBatches();

    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    m_chunkShader = m_chunkGBufferShader;
    TerrainFrameData tfd = buildTerrainFrameData(frame);
    m_terrainRenderer.setCameraPos(m_cameraPos);
    m_terrainRenderer.updateFrustum(frame.viewProj);
    TerrainRenderSettings trs;
    trs.rainWetSurfacesEnabled = m_pipelineSettings.rainWetSurfacesEnabled;
    trs.rainSurfaceRipplesEnabled = m_pipelineSettings.rainSurfaceRipplesEnabled;
    trs.aerialPerspectiveEnabled = m_pipelineSettings.aerialPerspectiveEnabled;
    trs.volumetricLightEnabled = m_pipelineSettings.volumetricLightEnabled;
    trs.volumetricFogEnabled = m_pipelineSettings.volumetricFogEnabled;
    trs.volumetricFogStrength = m_pipelineSettings.volumetricFogStrength;
    trs.directSunStrength = m_pipelineSettings.directSunStrength;
    trs.skyAmbientStrength = m_pipelineSettings.skyAmbientStrength;
    trs.weatherSkylightScale = m_pipelineSettings.weatherSkylightScale;
    trs.minimumAmbient = m_pipelineSettings.minimumAmbient;
    trs.blockLightStrength = m_pipelineSettings.blockLightStrength;
    trs.fakeBounceStrength = m_pipelineSettings.fakeBounceStrength;
    trs.albedoDesaturation = m_pipelineSettings.albedoDesaturation;
    trs.shadowDesaturation = m_pipelineSettings.shadowDesaturation;
    const bool volFogShadersReady = m_deferredPipeline->volumetricPass() && m_deferredPipeline->volumetricPass()->hasShaders();
    m_terrainRenderer.bindChunkRenderState(tfd, texArray, *m_chunkGBufferShader,
                                            m_deferredFrameActive, m_debugLightMode,
                                            m_eyeInWater, m_heldBlockLightValue,
                                            m_deferredTargets, m_resourceMgr,
                                            volFogShadersReady, trs);
    if (m_pipelineSettings.taaEnabled) {
        m_chunkGBufferShader->setMat4("viewProj", frame.jitteredViewProj);
    }
    submitMeshingJobs(world);

    std::vector<ChunkRenderEntry> cutoutEntries;
    cutoutEntries.reserve(world.getActiveChunks().size() * 2);
    m_deferredTransparentEntries.clear();
    m_deferredTransparentEntries.reserve(world.getActiveChunks().size() * 2);
    m_terrainRenderer.renderOpaqueChunksAndCollectPasses(world, cutoutEntries,
                                                          m_deferredTransparentEntries,
                                                          true);
    m_terrainRenderer.syncTransparentBatches();
    m_deferredTransparentBatch = m_terrainRenderer.transparentBatches();
    m_transparentPassPlan = m_terrainRenderer.transparentPassPlan();
    syncTerrainRendererFrameStats();
    if (m_useMultiDrawIndirect) {
        m_worldRenderBuffer.flushOpaque();
    }
    m_terrainRenderer.renderCutoutChunks(cutoutEntries, *m_chunkShader);
    syncTerrainRendererFrameStats();
    if (!m_useMultiDrawIndirect) {
        drawCallCount += m_terrainRenderer.drawCallCount();
    }

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void Renderer::renderGBufferEntities(const World& /*world*/, const RenderFrameData& /*frame*/) {
    // Phase 7a: Delegated to GBufferPass::executeEntities()
}

void Renderer::renderGBufferDrops(const World& /*world*/, const RenderFrameData& /*frame*/) {
    // Phase 7a: Delegated to GBufferPass::executeDrops()
}

void Renderer::renderShadowEntities(const World& /*world*/, const glm::mat4& /*shadowViewProj*/) {
    // Phase 7b: Delegated to ShadowPass::renderShadowEntities()
}

void Renderer::renderShadowDrops(const World& /*world*/, const glm::mat4& /*shadowViewProj*/,
                                  const glm::mat4& /*shadowView*/, const glm::mat4& /*shadowProjection*/,
                                  float /*animationTime*/, float /*shaderTime*/) {
    // Phase 7b: Delegated to ShadowPass::renderShadowDrops()
}

void Renderer::renderShadowMap(const World& world, const Camera& /*camera*/, const RenderFrameData& frame) {
    // Phase 7b: Delegated to ShadowPass::execute()
    if (!m_deferredPipeline->shadowPass()) {
        return;
    }
    FrameContext shadowCtx = buildFrameContextFromRenderFrameData(frame);
    RenderSettings shadowRs = buildRenderSettingsFromPipelineSettings();
    auto result = m_deferredPipeline->shadowPass()->execute(
        shadowCtx, shadowRs, m_deferredTargets, world,
        m_deferredTransparentBatch, m_transparentPassPlan, m_useMultiDrawIndirect);
    m_deferredTransparentBatch = std::move(result.transparentBatch);
    m_transparentPassPlan = result.transparentPlan;
}

void Renderer::renderSsaoPass(const Camera& /*camera*/, const Window& /*window*/) {
    // Phase 3: Delegated to SsaoPass::execute()
}

void Renderer::renderDeferredLightingPass(const RenderFrameData& /*frame*/) {
    // Phase 5a: Delegated to DeferredLightingPass::execute()
}

void Renderer::renderSceneCompositePass(const RenderFrameData& /*frame*/) {
    // Phase 5b: Delegated to SceneCompositePass::execute()
}

void Renderer::clearDeferredAuxiliaryTargets() {
    m_deferredTargets.bindReflection();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_deferredTargets.bindCloud();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_deferredTargets.bindSceneComposite();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_deferredTargets.bindSceneResolved();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_deferredTargets.clearWeatherMask();

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderVelocityPass(const RenderFrameData& /*frame*/) {
    // Phase 4: Delegated to VelocityPass::execute()
}

void Renderer::updateDeferredHistoryTargets() {
    if (!m_deferredTargets.isReady()) {
        return;
    }
    if (m_deferredHistoryUpdatedThisFrame) {
        return;
    }

    m_deferredTargets.copySceneResolvedToHistory();
    m_deferredTargets.copyDepthToHistory();
    m_deferredTargets.copyReflectionToHistory();
    m_deferredTargets.copyCloudToHistory();
    if (!m_pipelineSettings.volumetricTemporalEnabled || !m_hasPreviousFrameData || !(m_deferredPipeline->volumetricPass() && m_deferredPipeline->volumetricPass()->hasTemporalShader())) {
        m_deferredTargets.copyVolumetricToHistory();
    }
    m_deferredTargets.swapHistory();
    m_deferredTargets.swapSsaoHistory();
    m_deferredHistoryUpdatedThisFrame = true;
}

void Renderer::renderReflectionPass(const RenderFrameData& /*frame*/) {
    // Phase 4: Delegated to ReflectionPass::execute()
}

void Renderer::renderCloudPass(const RenderFrameData& /*frame*/) {
    // Phase 5b: Delegated to CloudPass::execute()
}

void Renderer::renderParticlesToSceneResolved(const RenderFrameData& frame) {
    if (!m_pipelineSettings.sceneParticlesEnabled ||
        m_particleSystem == nullptr ||
        m_particleGBufferShader == nullptr) {
        return;
    }

    // Bind SceneComposite so the later volumetric composite pass reads the
    // particle contribution instead of overwriting it from the pre-particle scene.
    m_deferredTargets.bindSceneComposite();

    // Particles render here with alpha blending, depth test (read-only), and
    // sample voxel light from the GBuffer for basic lighting. The subsequent
    // volumetric fog composite will apply atmospheric scattering to particles.
    const glm::mat4& viewProj = m_pipelineSettings.taaEnabled ? frame.jitteredViewProj : frame.viewProj;
    const glm::vec2 screenSize(
        static_cast<float>(std::max(1, m_deferredTargets.width())),
        static_cast<float>(std::max(1, m_deferredTargets.height())));

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_particleSystem->renderToSceneResolved(
        *m_particleGBufferShader,
        m_deferredTargets.voxelLightTexture(),
        m_deferredTargets.depthTexture(),
        frame.view, viewProj,
        screenSize);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Renderer::renderVolumetricFogPass(const RenderFrameData& /*frame*/) {
    // Phase 5c: Delegated to VolumetricPass::execute()
}

void Renderer::renderVolumetricTemporalPass(const RenderFrameData& /*frame*/) {
    // Phase 5c: Delegated to VolumetricPass::execute()
}

void Renderer::compositeVolumetricFogPass() {
    // Phase 5c: Delegated to VolumetricPass::execute()
}

void Renderer::renderReflectionFilterPass(const RenderFrameData& /*frame*/) {
    // Phase 4: Delegated to ReflectionPass::execute()
}

void Renderer::renderReflectionTemporalPass() {
    // Phase 4: Delegated to ReflectionPass::execute()
}

void Renderer::renderTemporalResolvePass(const RenderFrameData& /*frame*/) {
    // Phase 4: Delegated to TemporalResolvePass::execute()
}

void Renderer::renderSsaoFilterPass() {
    // Phase 3: Delegated to SsaoPass::execute()
}

void Renderer::renderSsaoUpsamplePass() {
    // Phase 3: Delegated to SsaoPass::execute()
}

void Renderer::renderSsaoTemporalPass() {
    // Phase 3: Delegated to SsaoPass::execute()
}

void Renderer::renderMotionBlurPass(const RenderFrameData& /*frame*/) {
    // Phase 4: Delegated to MotionBlurPass::execute()
}

void Renderer::renderDofPass(const RenderFrameData& /*frame*/) {
    // Phase 4: Delegated to DepthOfFieldPass::execute()
}

void Renderer::renderDeferredDebugView(const GLint framebuffer, const int width, const int height) {
    // Phase 5: Delegated to DebugPass::execute()
    if (m_deferredPipeline && m_deferredPipeline->debugPass()) {
        const RenderFrameData& debugFrame = m_currentFrameDataValid
            ? m_currentFrameData
            : (m_hasPreviousFrameData ? m_previousFrameData : m_currentFrameData);
        FrameContext ctx = buildFrameContextFromRenderFrameData(debugFrame);
        RenderSettings rs = buildRenderSettingsFromPipelineSettings();
        m_deferredPipeline->debugPass()->execute(ctx, rs, m_deferredTargets,
                                                   framebuffer, width, height);
    }
}

void Renderer::renderSkyCapturePass(const World& /*world*/) {
    // Phase 7a: Delegated to SkyCapturePass::execute()
}

void Renderer::renderFullscreen(Shader& shader) const {
    shader.use();
    glBindVertexArray(m_deferredTargets.fullscreenVao());
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

glm::vec3 Renderer::currentShadowLightDirection(const World& world, bool* moonShadowActive) const {
    const GameplaySkyRenderer::SkyColors skyColors = m_gameplaySkyRenderer.computeSkyColors(world.getDayNightSystem());
    // Use a temporary ShadowRenderer to compute light direction without modifying state.
    shadow::ShadowRenderer temp;
    return temp.computeLightDirection(skyColors, moonShadowActive);
}

void Renderer::captureCurrentFramebuffer() {
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_capturedFramebuffer);
    glGetIntegerv(GL_VIEWPORT, m_capturedViewport);
}

void Renderer::restoreDefaultFbo() {
    m_deferredTargets.bindDefaultLike(m_capturedFramebuffer, m_capturedViewport[2], m_capturedViewport[3]);
}

void Renderer::restoreCapturedFramebufferViewport(const Window& window) {
    const int fallbackWidth = std::max(1, window.getWidth());
    const int fallbackHeight = std::max(1, window.getHeight());
    const int width = m_capturedViewport[2] > 0 ? m_capturedViewport[2] : fallbackWidth;
    const int height = m_capturedViewport[3] > 0 ? m_capturedViewport[3] : fallbackHeight;
    m_deferredTargets.bindDefaultLike(m_capturedFramebuffer, width, height);
}

void Renderer::submitMeshingJobs(const World& world) {
    if (m_terrainStreamingService) {
        m_terrainStreamingService->submitMeshingJobs(world, m_cameraPos);
        return;
    }
    m_terrainCache.submitMeshingJobs(world, m_cameraPos);
    m_meshingInFlight = m_terrainCache.meshingInFlight();
    syncTerrainCacheFrameStats();
}

void Renderer::renderOpaqueChunksAndCollectPasses(const World& world,
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

void Renderer::syncChunkRenderColumns(const World& world) {
    if (m_terrainStreamingService) {
        m_terrainStreamingService->terrainCache().syncChunkRenderColumns(world);
    } else {
        m_terrainCache.syncChunkRenderColumns(world);
    }
    m_chunkRenderColumns.clear();
}

void Renderer::releaseMdiAllocation(const SubChunkGpuKey& key) {
    if (m_terrainStreamingService) {
        m_terrainStreamingService->releaseMdiAllocation(key);
        return;
    }
    m_terrainCache.releaseMdiAllocation(key);
    m_mdiMeshAllocations = m_terrainCache.mdiMeshAllocations();
}

void Renderer::releaseStaleMdiAllocations(const World& world) {
    if (m_terrainStreamingService) {
        m_terrainStreamingService->releaseStaleMdiAllocations(world);
        return;
    }
    m_terrainCache.releaseStaleMdiAllocations(world);
    m_mdiMeshAllocations = m_terrainCache.mdiMeshAllocations();
}

void Renderer::refreshChunkRenderColumnCache(ChunkRenderColumnCache& column) {
    if (m_terrainStreamingService) {
        m_terrainStreamingService->terrainCache().refreshChunkRenderColumnCache(column);
    } else {
        m_terrainCache.refreshChunkRenderColumnCache(column);
    }
}

void Renderer::syncTerrainCacheFrameStats() {
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

void Renderer::syncTerrainRendererFrameStats() {
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

void Renderer::renderCutoutChunks(const std::vector<ChunkRenderEntry>& cutoutEntries) {
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

void Renderer::addTransparentBatch(const GpuMeshRange& range,
                                   const float distanceSq,
                                   const TransparentBatchKind kind) {
    m_terrainCache.addTransparentBatch(range, distanceSq, kind);
}

void Renderer::clearTransparentBatches() {
    m_terrainCache.clearTransparentBatches();
    m_deferredTransparentBatch.clear();
    m_transparentPassPlan.clear();
}

void Renderer::syncTransparentBatches() {
    m_deferredTransparentBatch = m_terrainCache.deferredTransparentBatch();
    m_transparentPassPlan = m_terrainCache.transparentPassPlan();
}


void Renderer::renderTransparentChunks(const std::vector<ChunkRenderEntry>& transparentEntries) {
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
void Renderer::renderTransparentShadowChunks(const std::vector<ChunkRenderEntry>& /*transparentEntries*/) {
    // Phase 7b: Delegated to ShadowPass (inline in execute())
}


void Renderer::endFrame(const Window &window) {
    (void)window;
    if (m_currentFrameDataValid) {
        m_previousFrameData = m_currentFrameData;
        m_hasPreviousFrameData = true;
    }
    recordMeshingHistory();
    m_currentFrameDataValid = false;
    m_deferredFrameActive = false;
    m_waterRenderedBeforeTemporal = false;
}

void Renderer::initOutlineMesh() {
    if (m_outlineVao != 0) {
        return;
    }

    constexpr std::array<float, 72> kOutlineVertices = {
        0,0,0, 1,0,0,  1,0,0, 1,1,0,  1,1,0, 0,1,0,  0,1,0, 0,0,0,
        0,0,1, 1,0,1,  1,0,1, 1,1,1,  1,1,1, 0,1,1,  0,1,1, 0,0,1,
        0,0,0, 0,0,1,  1,0,0, 1,0,1,  1,1,0, 1,1,1,  0,1,0, 0,1,1
    };

    glGenVertexArrays(1, &m_outlineVao);
    glGenBuffers(1, &m_outlineVbo);

    glBindVertexArray(m_outlineVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_outlineVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kOutlineVertices.size() * sizeof(float)),
                 kOutlineVertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void Renderer::initBreakOverlayMesh() {
    if (m_breakOverlayVao != 0 && m_breakOverlayCrossVao != 0) {
        return;
    }

    // position.xyz + uv.xy
    constexpr std::array<float, 180> kBreakOverlayVertices = {
        // front
        0,0,1, 0,0,  1,0,1, 1,0,  1,1,1, 1,1,
        0,0,1, 0,0,  1,1,1, 1,1,  0,1,1, 0,1,
        // back
        1,0,0, 0,0,  0,0,0, 1,0,  0,1,0, 1,1,
        1,0,0, 0,0,  0,1,0, 1,1,  1,1,0, 0,1,
        // left
        0,0,0, 0,0,  0,0,1, 1,0,  0,1,1, 1,1,
        0,0,0, 0,0,  0,1,1, 1,1,  0,1,0, 0,1,
        // right
        1,0,1, 0,0,  1,0,0, 1,0,  1,1,0, 1,1,
        1,0,1, 0,0,  1,1,0, 1,1,  1,1,1, 0,1,
        // top
        0,1,1, 0,0,  1,1,1, 1,0,  1,1,0, 1,1,
        0,1,1, 0,0,  1,1,0, 1,1,  0,1,0, 0,1,
        // bottom
        0,0,0, 0,0,  1,0,0, 1,0,  1,0,1, 1,1,
        0,0,0, 0,0,  1,0,1, 1,1,  0,0,1, 0,1
    };

    constexpr std::array<float, 60> kBreakOverlayCrossVertices = {
        // quad A
        0.1464f,0.0f,0.1464f, 0,0,  0.8536f,0.0f,0.8536f, 1,0,  0.8536f,1.0f,0.8536f, 1,1,
        0.1464f,0.0f,0.1464f, 0,0,  0.8536f,1.0f,0.8536f, 1,1,  0.1464f,1.0f,0.1464f, 0,1,
        // quad B
        0.8536f,0.0f,0.1464f, 0,0,  0.1464f,0.0f,0.8536f, 1,0,  0.1464f,1.0f,0.8536f, 1,1,
        0.8536f,0.0f,0.1464f, 0,0,  0.1464f,1.0f,0.8536f, 1,1,  0.8536f,1.0f,0.1464f, 0,1
    };

    glGenVertexArrays(1, &m_breakOverlayVao);
    glGenBuffers(1, &m_breakOverlayVbo);

    glBindVertexArray(m_breakOverlayVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_breakOverlayVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kBreakOverlayVertices.size() * sizeof(float)),
                 kBreakOverlayVertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));

    m_breakOverlayVertexCount = static_cast<GLsizei>(kBreakOverlayVertices.size() / 5);

    glGenVertexArrays(1, &m_breakOverlayCrossVao);
    glGenBuffers(1, &m_breakOverlayCrossVbo);

    glBindVertexArray(m_breakOverlayCrossVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_breakOverlayCrossVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kBreakOverlayCrossVertices.size() * sizeof(float)),
                 kBreakOverlayCrossVertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));

    m_breakOverlayCrossVertexCount = static_cast<GLsizei>(kBreakOverlayCrossVertices.size() / 5);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void Renderer::renderBlockOutline(const World& world, const BlockTargetRenderData& target) {
    if (m_outlineShader == nullptr || m_outlineVao == 0 || !target.hasTarget) {
        return;
    }

    const glm::ivec3 targetBlock = target.targetBlock;
    const StateID targetState = world.getBlockState(targetBlock.x, targetBlock.y, targetBlock.z);
    const BlockSelectionBox selectionBox = BlockSelection::getBox(targetState);
    const glm::vec3 boxCenter = (selectionBox.min + selectionBox.max) * 0.5f;
    const glm::vec3 boxSize = selectionBox.max - selectionBox.min;

    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3(targetBlock) + boxCenter);
    model = glm::scale(model, boxSize + glm::vec3(0.002f));
    model = glm::translate(model, glm::vec3(-0.5f));

    m_outlineShader->use();
    m_outlineShader->setMat4("viewProj", m_projection * m_view);
    m_outlineShader->setMat4("model", model);
    m_outlineShader->setVec3("lineColor", 0.05f, 0.05f, 0.05f);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    // glLineWidth > 1.0 is deprecated in OpenGL 4.5+ core profile.
    // Use 1.0 to avoid GL_INVALID_VALUE in future drivers.

    glBindVertexArray(m_outlineVao);
    glDrawArrays(GL_LINES, 0, 24);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    ++drawCallCount;
}

void Renderer::renderBlockBreakOverlay(const World& world, const BlockBreakRenderData& blockBreak) {
    if (m_breakOverlayShader == nullptr || m_breakOverlayVao == 0 || !blockBreak.active) {
        return;
    }

    const float breakProgress = blockBreak.progress01;
    if (breakProgress <= 0.0f) {
        return;
    }

    const glm::ivec3 target = blockBreak.blockPos;
    const BlockID targetId = world.getBlock(target.x, target.y, target.z);
    const BlockDef& targetDef = BlockRegistry::get(targetId);
    const bool useCrossOverlay = (targetDef.renderShape == BlockRenderShape::Cross);
    const StateID targetState = world.getBlockState(target.x, target.y, target.z);
    const BlockSelectionBox selectionBox = BlockSelection::getBox(targetState);
    const glm::vec3 boxCenter = (selectionBox.min + selectionBox.max) * 0.5f;
    const glm::vec3 boxSize = selectionBox.max - selectionBox.min;

    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3(target) + boxCenter);
    model = glm::scale(model, boxSize + glm::vec3(0.001f));
    model = glm::translate(model, glm::vec3(-0.5f));

    m_breakOverlayShader->use();
    m_breakOverlayShader->setMat4("viewProj", m_projection * m_view);
    m_breakOverlayShader->setMat4("model", model);
    m_breakOverlayShader->setFloat("breakProgress", breakProgress);
    m_breakOverlayShader->setVec3("blockWorldPos", glm::vec3(target));
    m_breakOverlayShader->setInt("uUseMeshUV", useCrossOverlay ? 1 : 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    glBindVertexArray(useCrossOverlay ? m_breakOverlayCrossVao : m_breakOverlayVao);
    glDrawArrays(GL_TRIANGLES, 0, useCrossOverlay ? m_breakOverlayCrossVertexCount : m_breakOverlayVertexCount);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    ++drawCallCount;
}

void Renderer::recordMeshingHistory() {
#ifdef MECRAFT_DEBUG
    if (m_terrainStreamingService) {
        m_terrainStreamingService->endFrame();
        return;
    }
    if (m_meshingHistoryCount < MESHING_HISTORY_SIZE) {
        m_meshingSubmittedHistory[m_meshingHistoryCount] = static_cast<float>(m_meshingSubmittedThisFrame);
        m_meshingCompletedHistory[m_meshingHistoryCount] = static_cast<float>(m_meshingCompletedThisFrame);
        m_meshingInFlightHistory[m_meshingHistoryCount] = static_cast<float>(m_meshingInFlight.size());
        ++m_meshingHistoryCount;
        return;
    }

    for (size_t i = 1; i < MESHING_HISTORY_SIZE; ++i) {
        m_meshingSubmittedHistory[i - 1] = m_meshingSubmittedHistory[i];
        m_meshingCompletedHistory[i - 1] = m_meshingCompletedHistory[i];
        m_meshingInFlightHistory[i - 1] = m_meshingInFlightHistory[i];
    }

    m_meshingSubmittedHistory[MESHING_HISTORY_SIZE - 1] = static_cast<float>(m_meshingSubmittedThisFrame);
    m_meshingCompletedHistory[MESHING_HISTORY_SIZE - 1] = static_cast<float>(m_meshingCompletedThisFrame);
    m_meshingInFlightHistory[MESHING_HISTORY_SIZE - 1] = static_cast<float>(m_meshingInFlight.size());
#endif
}

void Renderer::drainMeshingResults(const World& world) {
    if (m_terrainStreamingService) {
        m_terrainStreamingService->drainMeshingResults(world);
        return;
    }
    m_terrainCache.drainMeshingResults(world);
    m_meshingInFlight = m_terrainCache.meshingInFlight();
    m_mdiMeshAllocations = m_terrainCache.mdiMeshAllocations();
    syncTerrainCacheFrameStats();
}

void Renderer::updateFrustum(const glm::mat4 &viewProj) {
    m_viewProj = viewProj;

    const glm::vec4 row0(viewProj[0][0], viewProj[1][0], viewProj[2][0], viewProj[3][0]);
    const glm::vec4 row1(viewProj[0][1], viewProj[1][1], viewProj[2][1], viewProj[3][1]);
    const glm::vec4 row2(viewProj[0][2], viewProj[1][2], viewProj[2][2], viewProj[3][2]);
    const glm::vec4 row3(viewProj[0][3], viewProj[1][3], viewProj[2][3], viewProj[3][3]);

    const std::array<glm::vec4, 6> rawPlanes = {
        row3 + row0, // left
        row3 - row0, // right
        row3 + row1, // bottom
        row3 - row1, // top
        row3 + row2, // near
        row3 - row2  // far
    };

    for (size_t i = 0; i < rawPlanes.size(); ++i) {
        const glm::vec3 n(rawPlanes[i].x, rawPlanes[i].y, rawPlanes[i].z);
        const float length = glm::length(n);
        if (length > 0.0f) {
            m_frustumPlanes[i].n = n / length;
            m_frustumPlanes[i].d = rawPlanes[i].w / length;
        } else {
            m_frustumPlanes[i].n = glm::vec3(0.0f);
            m_frustumPlanes[i].d = 0.0f;
        }
    }
}

#ifdef MECRAFT_DEBUG
bool Renderer::isChunkInFrustum(const glm::vec3 &chunkMin, const glm::vec3 &chunkMax) const {
    return isChunkInFrustum(chunkMin, chunkMax, nullptr);
}

void Renderer::recordChunkCull(const FrustumPlane plane, const int count) {
    if (!m_chunkCullingDebugEnabled || count <= 0) {
        return;
    }

    m_chunkCulledThisFrame += count;
    const size_t planeIndex = static_cast<size_t>(plane);
    if (planeIndex < m_chunkCulledByPlaneThisFrame.size()) {
        m_chunkCulledByPlaneThisFrame[planeIndex] += count;
    }
}

void Renderer::initGpuTimers() {
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

void Renderer::shutdownGpuTimers() {
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

void Renderer::beginGpuTimerFrame() {
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

void Renderer::beginGpuTimer(const GpuTimerPass pass) {
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

void Renderer::endGpuTimer(const GpuTimerPass pass) {
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

bool Renderer::isChunkInFrustum(const glm::vec3 &chunkMin, const glm::vec3 &chunkMax, FrustumPlane* culledPlane) const {
#else
bool Renderer::isChunkInFrustum(const glm::vec3 &chunkMin, const glm::vec3 &chunkMax) const {
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

int Renderer::getDrawCallCount() const {
    return drawCallCount;
}

int Renderer::getGlSubmitCount() const {
    if (m_useMultiDrawIndirect) {
        return m_worldRenderBuffer.glSubmitCount();
    }
    return drawCallCount;
}
