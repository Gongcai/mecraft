//
// Created by Caiwe on 2026/3/21.
//

#include "Renderer.h"

#include "ChunkMesher.h"
#include "shadow/ShadowMatrices.h"
#include "shadow/ShadowCasterCuller.h"
#include "../Paths.h"
#include "../core/Time.h"
#include "../world/BlockSelection.h"
#include "../world/World.h"

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

constexpr Renderer::FrustumPlane kPlaneFromIndex(const size_t index) {
    return static_cast<Renderer::FrustumPlane>(index);
}

struct RenderWeatherFactors {
    float mist = 0.0f;
    float wetness = 0.0f;
    float storm = 0.0f;
    float aerialReduction = 0.55f;
};

RenderWeatherFactors weatherFactorsForPreset(const int preset) {
    switch (std::clamp(preset, 0, 3)) {
    case 1:
        return {0.55f, 0.15f, 0.0f, 0.42f};
    case 2:
        return {0.38f, 0.72f, 0.18f, 0.34f};
    case 3:
        return {0.72f, 1.0f, 0.85f, 0.28f};
    default:
        return {0.0f, 0.0f, 0.0f, 0.55f};
    }
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
    m_waterCompositeShader = resourceMgr.getShader("water_composite");
    if (m_transparentCompositeShader == nullptr) {
        m_transparentCompositeShader = m_chunkForwardShader;
    }
    m_chunkShader = m_chunkForwardShader;
    m_chunkGBufferShader = resourceMgr.getShader("chunk_gbuffer");
    m_shadowDepthShader = resourceMgr.getShader("shadow_depth");
    m_deferredLightingShader = resourceMgr.getShader("deferred_lighting");
    m_sceneCompositeShader = resourceMgr.getShader("scene_composite");
    m_deferredDebugShader = resourceMgr.getShader("deferred_debug");
    m_ssaoShader = resourceMgr.getShader("ssao");
    m_velocityShader = resourceMgr.getShader("velocity_resolve");
    m_volumetricFogShader = resourceMgr.getShader("volumetric_fog");
    m_volumetricCompositeShader = resourceMgr.getShader("volumetric_composite");
    m_reflectionShader = resourceMgr.getShader("reflection_probe");
    m_cloudShader = resourceMgr.getShader("cloud_target");
    m_bloomExtractShader = resourceMgr.getShader("bloom_extract");
    m_bloomBlurShader = resourceMgr.getShader("bloom_blur");
    m_temporalResolveShader = resourceMgr.getShader("temporal_resolve");
    m_reflectionFilterShader = resourceMgr.getShader("reflection_filter");
    m_ssaoFilterShader = resourceMgr.getShader("ssao_filter");
    m_motionBlurShader = resourceMgr.getShader("motion_blur");
    m_dofShader = resourceMgr.getShader("dof");
    if (m_deferredLightingShader != nullptr) {
        const GLint csmLocation = m_deferredLightingShader->getUniformLocation("uCsmShadowMap");
        if (csmLocation >= 0) {
            glProgramUniform1i(m_deferredLightingShader->ID, csmLocation, 15);
        }
    }
    if (m_volumetricFogShader != nullptr) {
        const GLint csmLocation = m_volumetricFogShader->getUniformLocation("uCsmShadowMap");
        if (csmLocation >= 0) {
            glProgramUniform1i(m_volumetricFogShader->ID, csmLocation, 6);
        }
    }
    //m_uiShader = resourceMgr.getShader("ui");
    m_outlineShader = resourceMgr.getShader("outline");
    m_breakOverlayShader = resourceMgr.getShader("break_overlay");
    initOutlineMesh();
    initBreakOverlayMesh();
    m_worldRenderBuffer.init();
    m_deferredTargets.init();
    const std::string atmosphereLutPath = resolveAtmosphereFinalLutPath();
    m_deferredTargets.loadAtmosphereLut(atmosphereLutPath.c_str());
    m_gameplaySkyRenderer.init(resourceMgr);
#ifdef MECRAFT_DEBUG
    initGpuTimers();
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
    m_meshingService.start(&m_threadPool);
}

void Renderer::shutdown() {
#ifdef MECRAFT_DEBUG
    shutdownGpuTimers();
#endif
    m_mdiMeshAllocations.clear();
    m_gameplaySkyRenderer.shutdown();
    m_deferredTargets.shutdown();
    m_worldRenderBuffer.shutdown();
    m_meshingService.shutdown();
    m_threadPool.shutdown();
    m_meshingInFlight.clear();
    m_deferredMeshResults.clear();
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
    m_chunkShader = nullptr;
    m_chunkForwardShader = nullptr;
    m_transparentCompositeShader = nullptr;
    m_waterCompositeShader = nullptr;
    m_chunkGBufferShader = nullptr;
    m_shadowDepthShader = nullptr;
    m_deferredLightingShader = nullptr;
    m_sceneCompositeShader = nullptr;
    m_deferredDebugShader = nullptr;
    m_ssaoShader = nullptr;
    m_velocityShader = nullptr;
    m_volumetricFogShader = nullptr;
    m_volumetricCompositeShader = nullptr;
    m_reflectionShader = nullptr;
    m_cloudShader = nullptr;
    m_bloomExtractShader = nullptr;
    m_bloomBlurShader = nullptr;
    m_temporalResolveShader = nullptr;
    m_reflectionFilterShader = nullptr;
    m_ssaoFilterShader = nullptr;
    m_motionBlurShader = nullptr;
    m_deferredFrameActive = false;
}

void Renderer::render(const World& world, const Camera &camera, const Window &window, const BlockTargetRenderData& target, const BlockBreakRenderData& blockBreak) {
    renderOpaqueAndCutout(world, camera, window);
    renderTransparentAndOverlays(world, target, blockBreak, window);
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
    m_gameplaySkyRenderer.render(camera, window.getAspectRatio(), world.getDayNightSystem());
    m_chunkShader = m_chunkForwardShader;
    m_deferredFrameActive = false;
    renderWorldForward(world, m_currentFrameData);
}

void Renderer::renderTransparentAndOverlays(const World& world, const BlockTargetRenderData& target, const BlockBreakRenderData& blockBreak, const Window& window) {
    renderTransparentCompositePass(world, window);

    if (m_deferredFrameActive &&
        m_deferredTargets.isReady() &&
        (m_pipelineSettings.debugViewMode == 0 || m_deferredDebugShader == nullptr)) {
        const int capturedWidth = m_capturedViewport[2] > 0 ? m_capturedViewport[2] : window.getWidth();
        const int capturedHeight = m_capturedViewport[3] > 0 ? m_capturedViewport[3] : window.getHeight();
        m_deferredTargets.copyFramebufferColorToSceneResolved(m_capturedFramebuffer, capturedWidth, capturedHeight);
        updateDeferredHistoryTargets();
        restoreCapturedFramebufferViewport(window);
    }

    renderBlockBreakOverlay(world, blockBreak);
    renderBlockOutline(world, target);
#ifdef MECRAFT_DEBUG
    beginGpuTimer(GpuTimerPass::Post);
#endif
    endFrame(window);
#ifdef MECRAFT_DEBUG
    endGpuTimer(GpuTimerPass::Post);
#endif
}

void Renderer::renderWaterCompositePass(const World& world, const Window& window) {
    if (!m_pipelineSettings.waterEffectsEnabled ||
        !m_transparentPassPlan.hasWater() ||
        m_waterCompositeShader == nullptr ||
        m_resourceMgr == nullptr) {
        return;
    }

    const bool deferredInputsEnabled = m_deferredFrameActive && m_deferredTargets.isReady();
    const bool compositeInputsEnabled = deferredInputsEnabled && m_pipelineSettings.transparentCompositeEnabled;
    const int capturedWidth = m_capturedViewport[2] > 0 ? m_capturedViewport[2] : window.getWidth();
    const int capturedHeight = m_capturedViewport[3] > 0 ? m_capturedViewport[3] : window.getHeight();

    if (compositeInputsEnabled) {
        m_deferredTargets.copySceneResolvedToTransparentComposite();
        m_deferredTargets.copyDepthToTransparentComposite();
        m_deferredTargets.bindTransparentComposite();
    } else if (m_deferredFrameActive) {
        restoreCapturedFramebufferViewport(window);
    }

    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    const RenderFrameData frame = m_currentFrameDataValid ? m_currentFrameData : buildRenderFrameData(world);

    m_waterCompositeShader->use();
    m_waterCompositeShader->setMat4("view", frame.view);
    m_waterCompositeShader->setMat4("viewProj", frame.viewProj);
    m_waterCompositeShader->setMat4("uInvViewProj", frame.invViewProj);
    m_waterCompositeShader->setMat4("model", glm::mat4(1.0f));
    m_waterCompositeShader->setInt("uUseModel", 0);
    m_waterCompositeShader->setInt("texArray", 0);
    m_waterCompositeShader->setInt("uOpaqueDepthTex", 5);
    m_waterCompositeShader->setInt("uSkyCaptureTex", 6);
    m_waterCompositeShader->setInt("uSceneColorTex", 7);
    m_waterCompositeShader->setInt("uNoiseTex", 8);
    m_waterCompositeShader->setInt("uReflectionTex", 9);
    m_waterCompositeShader->setInt("uAtmosphereLut", 10);
    m_waterCompositeShader->setInt("uSkyCaptureEnabled", m_deferredFrameActive ? 1 : 0);
    m_waterCompositeShader->setInt("uCompositeInputsEnabled", compositeInputsEnabled ? 1 : 0);
    m_waterCompositeShader->setInt("uWaterCompositeEnabled", compositeInputsEnabled ? 1 : 0);
    m_waterCompositeShader->setInt("uDepthSofteningEnabled", deferredInputsEnabled ? 1 : 0);
    m_waterCompositeShader->setFloat("uAnimationTime", frame.animationTime);
    m_waterCompositeShader->setFloat("uTime", frame.shaderTime);
    m_waterCompositeShader->setVec3("uCameraPos", frame.cameraPos);
    // DerivativeMain shaders.properties: uniform.vec3.waterAbsorption = vec3(0.4, 0.14, 0.08)
    m_waterCompositeShader->setVec3("uWaterAbsorption", glm::vec3(0.4f, 0.14f, 0.08f));
    m_waterCompositeShader->setVec3("uSunDirection", frame.skyColors.sunDirection);
    m_waterCompositeShader->setVec3("uMoonDirection", frame.skyColors.moonDirection);
    m_waterCompositeShader->setVec3("uSunLightColor", frame.skyColors.sunLightColor);
    m_waterCompositeShader->setVec3("uMoonLightColor", frame.skyColors.moonLightColor);
    m_waterCompositeShader->setVec3("uSkyAmbientColor", frame.skyColors.skyAmbientColor);
    m_waterCompositeShader->setFloat("uSkyIntensity", frame.skyIntensity);
    m_waterCompositeShader->setFloat("uMoonVisibility", frame.skyColors.moonVisibility);
    m_waterCompositeShader->setFloat("uWeatherWetness", frame.weatherWetness);
    m_waterCompositeShader->setFloat("uWaterWaveHeight", 1.0f);
    m_waterCompositeShader->setFloat("uWaterWaveSpeed", 1.0f);
    m_waterCompositeShader->setFloat("uWaterIOR", 1.33f);
    m_waterCompositeShader->setInt("uIsEyeInWater", 0); // TODO: detect from camera position

    if (m_resourceMgr) {
        const TextureAnimationInfo still = m_resourceMgr->getTextureAnimation("water_still");
        const TextureAnimationInfo flow = m_resourceMgr->getTextureAnimation("water_flow");
        m_waterCompositeShader->setFloat("uWaterStillFirstLayer", static_cast<float>(still.firstLayer));
        m_waterCompositeShader->setFloat("uWaterStillLayerCount", static_cast<float>(std::max(1, still.frameCount)));
        m_waterCompositeShader->setFloat("uWaterFlowFirstLayer", static_cast<float>(flow.firstLayer));
        m_waterCompositeShader->setFloat("uWaterFlowLayerCount", static_cast<float>(std::max(1, flow.frameCount)));
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texArray.textureID);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.depthTexture());
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.skyCaptureTexture());
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.sceneResolvedTexture());
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getTexture2D("shader_noise2d"));
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.reflectionTexture());
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_3D, m_deferredTargets.atmosphereLutTexture());

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

#ifdef MECRAFT_DEBUG
    beginGpuTimer(GpuTimerPass::Water);
#endif
    if (m_useMultiDrawIndirect) {
        // Sort water entries back-to-front
        std::vector<const DrawBatchEntry*> waterEntries;
        for (const auto& entry : m_deferredTransparentBatch) {
            if (entry.kind == TransparentBatchKind::Water) {
                waterEntries.push_back(&entry);
            }
        }
        std::sort(waterEntries.begin(), waterEntries.end(),
            [](const DrawBatchEntry* a, const DrawBatchEntry* b) {
                return a->distanceSq > b->distanceSq;
            });

        glBindVertexArray(m_worldRenderBuffer.transparentVao());
        for (const auto* entry : waterEntries) {
            glDrawArrays(GL_TRIANGLES,
                         static_cast<GLint>(entry->range.firstVertex),
                         static_cast<GLsizei>(entry->range.vertexCount));
            ++drawCallCount;
        }
    } else {
        // Non-MDI: sort and draw water sub-chunks
        struct WaterItem {
            const ChunkRenderEntry* entry = nullptr;
            float distanceSq = 0.0f;
        };
        std::vector<WaterItem> waterItems;
        for (const auto& entry : m_deferredTransparentEntries) {
            if (!entry.chunk) continue;
            const SubChunk* sc = entry.chunk->getSubChunk(entry.scy);
            if (!sc || sc->getMesh().waterVertexCount == 0) continue;
            const glm::ivec3 offset = entry.chunk->getWorldOffset();
            const int yBase = entry.scy * SubChunk::SIZE;
            const glm::vec3 center(
                static_cast<float>(offset.x) + Chunk::SIZE_X * 0.5f,
                static_cast<float>(yBase + offset.y) + SubChunk::SIZE * 0.5f,
                static_cast<float>(offset.z) + Chunk::SIZE_Z * 0.5f);
            const glm::vec3 toCamera = center - m_cameraPos;
            waterItems.push_back({&entry, glm::dot(toCamera, toCamera)});
        }
        std::sort(waterItems.begin(), waterItems.end(),
            [](const WaterItem& a, const WaterItem& b) {
                return a.distanceSq > b.distanceSq;
            });
        for (const auto& item : waterItems) {
            const SubChunk* sc = item.entry->chunk->getSubChunk(item.entry->scy);
            glBindVertexArray(sc->getMesh().transparentVao);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(sc->getMesh().waterVertexCount));
            ++drawCallCount;
        }
    }

#ifdef MECRAFT_DEBUG
    endGpuTimer(GpuTimerPass::Water);
#endif
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    for (int i = 10; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_3D, 0);
    glActiveTexture(GL_TEXTURE0);

    if (compositeInputsEnabled) {
        m_deferredTargets.blitTransparentCompositeTo(m_capturedFramebuffer, capturedWidth, capturedHeight);
        restoreCapturedFramebufferViewport(window);
    }

    for (int unit = 8; unit >= 5; --unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void Renderer::renderTransparentCompositePass(const World& world, const Window& window) {
    // Step 1: Render water with dedicated shader
    renderWaterCompositePass(world, window);

    // Step 2: Render generic transparent
    if (m_deferredFrameActive) {
        restoreCapturedFramebufferViewport(window);
    }

    if (!m_transparentPassPlan.hasGeneric()) {
        if (m_deferredFrameActive) {
            restoreCapturedFramebufferViewport(window);
        }
        return;
    }

    m_chunkShader = m_transparentCompositeShader != nullptr ? m_transparentCompositeShader : m_chunkForwardShader;
    if (m_chunkShader != nullptr && m_resourceMgr != nullptr) {
        const bool deferredInputsEnabled = m_deferredFrameActive && m_deferredTargets.isReady();
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
        // Render generic transparent only -- water was handled in renderWaterCompositePass
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
                    if (entry.kind == TransparentBatchKind::Generic) {
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
            // Non-MDI: filter to generic transparent only
            std::vector<ChunkRenderEntry> genericEntries;
            for (const auto& entry : m_deferredTransparentEntries) {
                if (!entry.chunk) continue;
                const SubChunk* sc = entry.chunk->getSubChunk(entry.scy);
                if (!sc) continue;
                const SubChunkMesh& mesh = sc->getMesh();
                // Non-MDI path: water vertex count is tracked, generic = total - water
                if (mesh.transparentVertexCount > mesh.waterVertexCount) {
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
    m_meshingSubmitBudget = std::max(1, budget);
    m_meshingSubmitBudgetOverridden = true;
}

void Renderer::setRegionChunkSize(const int chunkSize) {
    m_regionChunkSize = std::max(1, chunkSize);
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
    m_pipelineSettings.bloomThreshold = std::clamp(m_pipelineSettings.bloomThreshold, 0.0f, 4.0f);
    m_pipelineSettings.bloomStrength = std::clamp(m_pipelineSettings.bloomStrength, 0.0f, 2.0f);
    m_pipelineSettings.autoExposureMin = std::clamp(m_pipelineSettings.autoExposureMin, 0.001f, 64.0f);
    m_pipelineSettings.autoExposureMax = std::clamp(m_pipelineSettings.autoExposureMax, m_pipelineSettings.autoExposureMin, 64.0f);
    m_pipelineSettings.autoExposureSpeed = std::clamp(m_pipelineSettings.autoExposureSpeed, 0.05f, 12.0f);
    m_pipelineSettings.autoExposureBias = std::clamp(m_pipelineSettings.autoExposureBias, -3.0f, 3.0f);
    m_pipelineSettings.sunRayStrength = std::clamp(m_pipelineSettings.sunRayStrength, 0.0f, 1.0f);
    m_pipelineSettings.sceneCloudCompositeStrength = std::clamp(m_pipelineSettings.sceneCloudCompositeStrength, 0.0f, 1.0f);
    m_pipelineSettings.sceneReflectionCompositeStrength = std::clamp(m_pipelineSettings.sceneReflectionCompositeStrength, 0.0f, 1.0f);
    m_pipelineSettings.debugViewMode = std::clamp(m_pipelineSettings.debugViewMode, 0, 44);
    m_pipelineSettings.weatherPreset = std::clamp(m_pipelineSettings.weatherPreset, 0, 3);
    m_pipelineSettings.tonemapMode = std::clamp(m_pipelineSettings.tonemapMode, 0, 3);
    m_pipelineSettings.debugDisableGreedyMeshing = false;
    ChunkMesher::setDebugDisableGreedyMeshing(m_pipelineSettings.debugDisableGreedyMeshing);
    m_pipelineSettings.colorTemperature = std::clamp(m_pipelineSettings.colorTemperature, 0.0f, 2.0f);
    m_pipelineSettings.vibrance = std::clamp(m_pipelineSettings.vibrance, -1.0f, 1.0f);
    m_pipelineSettings.kappaGradingStrength = std::clamp(m_pipelineSettings.kappaGradingStrength, 0.0f, 1.0f);
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
           m_deferredDebugShader != nullptr;
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
           m_deferredLightingShader != nullptr &&
           m_deferredDebugShader != nullptr &&
           m_ssaoShader != nullptr &&
           m_velocityShader != nullptr &&
           m_reflectionShader != nullptr &&
           m_cloudShader != nullptr;
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
}

int Renderer::getMeshingSubmitBudget() const {
    return m_meshingSubmitBudget;
}

int Renderer::getRegionChunkSize() const {
    return m_regionChunkSize;
}

bool Renderer::isChunkCullingDebugEnabled() const {
    return m_chunkCullingDebugEnabled;
}

Renderer::MeshingFrameStats Renderer::getMeshingFrameStats() const {
    MeshingFrameStats stats;
    stats.submitBudget = m_meshingSubmitBudget;
    stats.submitted = m_meshingSubmittedThisFrame;
    stats.completed = m_meshingCompletedThisFrame;
    stats.inFlight = static_cast<int>(m_meshingInFlight.size());
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

Renderer::CullingFrameStats Renderer::getCullingFrameStats() const {
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

Renderer::GpuFrameStats Renderer::getGpuFrameStats() const {
    return m_gpuFrameStats;
}

Renderer::RenderWorkStats Renderer::getRenderWorkStats() const {
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
    m_gpuTimerEnabled = enabled;
}

bool Renderer::isGpuTimerEnabled() const {
    return m_gpuTimerEnabled;
}

void Renderer::setCutoutDistanceLimitEnabled(const bool enabled) {
    m_cutoutDistanceLimitEnabled = enabled;
}

bool Renderer::isCutoutDistanceLimitEnabled() const {
    return m_cutoutDistanceLimitEnabled;
}

void Renderer::setCutoutRenderDistanceChunks(const float distanceChunks) {
    m_cutoutRenderDistanceChunks = std::clamp(distanceChunks, 1.0f, 32.0f);
}

float Renderer::getCutoutRenderDistanceChunks() const {
    return m_cutoutRenderDistanceChunks;
}

const std::array<float, Renderer::MESHING_HISTORY_SIZE>& Renderer::getMeshingSubmittedHistory() const {
    return m_meshingSubmittedHistory;
}

const std::array<float, Renderer::MESHING_HISTORY_SIZE>& Renderer::getMeshingCompletedHistory() const {
    return m_meshingCompletedHistory;
}

const std::array<float, Renderer::MESHING_HISTORY_SIZE>& Renderer::getMeshingInFlightHistory() const {
    return m_meshingInFlightHistory;
}

size_t Renderer::getMeshingHistoryCount() const {
    return m_meshingHistoryCount;
}
#endif

void Renderer::beginFrame(const Camera &camera, const Window &window) {
    ++m_frameCounter;
    glClearColor(m_fogSettings.color.r, m_fogSettings.color.g, m_fogSettings.color.b, 1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_projection = camera.getProjectionMatrix(window.getAspectRatio());
    m_view = camera.getViewMatrix();
    m_cameraPos = camera.getPosition();
    m_currentFrameDataValid = false;
    updateFrustum(m_projection * m_view);
    drawCallCount = 0;

#ifdef MECRAFT_DEBUG
    m_meshingSubmittedThisFrame = 0;
    m_meshingCompletedThisFrame = 0;
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
    m_deferredTransparentBatch.clear();
    m_transparentPassPlan.clear();

    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    bindChunkRenderState(frame, texArray);
    submitMeshingJobs(world);

    std::vector<ChunkRenderEntry> cutoutEntries;
    cutoutEntries.reserve(world.getActiveChunks().size() * 2);
    m_deferredTransparentEntries.clear();
    m_deferredTransparentEntries.reserve(world.getActiveChunks().size() * 2);
    renderOpaqueChunksAndCollectPasses(world, cutoutEntries, m_deferredTransparentEntries);
    if (m_useMultiDrawIndirect) {
        m_worldRenderBuffer.flushOpaque();
    }
    renderCutoutChunks(cutoutEntries);

    glBindVertexArray(0);
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

    // Temporal foundation
    frame.frameIndex = m_frameCounter;
    frame.deltaTime = static_cast<float>(Time::deltaTime);

    // Halton(2,3) sequence for 8-sample jitter base
    {
        constexpr int kHaltonBase = 8;
        const uint64_t idx = m_frameCounter % static_cast<uint64_t>(kHaltonBase);
        float haltonX = 0.0f, haltonY = 0.0f;
        float f2 = 1.0f, f3 = 1.0f;
        uint64_t n = idx;
        while (n > 0) { f2 /= 2.0f; haltonX += f2 * static_cast<float>(n % 2); n /= 2; }
        n = idx;
        while (n > 0) { f3 /= 3.0f; haltonY += f3 * static_cast<float>(n % 3); n /= 3; }
        const float invW = 1.0f / static_cast<float>(std::max(1, m_deferredTargets.width()));
        const float invH = 1.0f / static_cast<float>(std::max(1, m_deferredTargets.height()));
        frame.jitter.x = (haltonX * 2.0f - 1.0f) * invW;
        frame.jitter.y = (haltonY * 2.0f - 1.0f) * invH;
    }

    if (m_hasPreviousFrameData) {
        frame.previousJitter = m_previousFrameData.jitter;
        frame.previousView = m_previousFrameData.view;
        frame.previousProjection = m_previousFrameData.projection;
        frame.previousViewProj = m_previousFrameData.viewProj;
        frame.previousInvViewProj = m_previousFrameData.invViewProj;
    } else {
        frame.previousJitter = frame.jitter;
        frame.previousView = frame.view;
        frame.previousProjection = frame.projection;
        frame.previousViewProj = frame.viewProj;
        frame.previousInvViewProj = frame.invViewProj;
    }
    frame.skyColors = m_gameplaySkyRenderer.computeSkyColors(world.getDayNightSystem());
    frame.skyIlluminance = m_gameplaySkyRenderer.computeSkyIlluminance(frame.skyColors);
    frame.skyIntensity = world.getDayNightSystem().getSkyIntensity();
    const double gameTime = Time::getGameTime();
    frame.animationTime = static_cast<float>(std::fmod(gameTime, 16.0));
    frame.shaderTime = static_cast<float>(std::fmod(gameTime, 8192.0));

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

    const RenderWeatherFactors weather = weatherFactorsForPreset(m_pipelineSettings.weatherPreset);
    frame.weatherMist = weather.mist;
    frame.weatherWetness = weather.wetness;
    frame.weatherStorm = weather.storm;
    frame.aerialReduction = weather.aerialReduction;
    frame.atmosphere.aerialStrength = m_pipelineSettings.aerialStrength;
    frame.atmosphere.horizonScatterStrength = m_pipelineSettings.horizonScatterStrength;
    frame.atmosphere.sunWarmth = m_pipelineSettings.sunWarmth;
    frame.atmosphere.skyCoolness = m_pipelineSettings.skyCoolness;
    frame.atmosphere.weatherMist = frame.weatherMist;
    frame.atmosphere.weatherWetness = frame.weatherWetness;
    frame.atmosphere.weatherStorm = frame.weatherStorm;
    frame.atmosphere.aerialReduction = frame.aerialReduction;
    frame.volumetric.fogEnabled = m_pipelineSettings.volumetricFogEnabled;
    frame.volumetric.fogStrength = m_pipelineSettings.volumetricFogStrength;
    frame.volumetric.lightStrength = m_pipelineSettings.sunRayStrength;
    frame.cloud.shadowsEnabled = m_pipelineSettings.cloudShadowsEnabled;
    frame.cloud.shadowStrength = m_pipelineSettings.cloudShadowStrength;
    frame.cloud.shadowScale = m_pipelineSettings.cloudShadowScale;
    frame.cloud.shadowSpeed = m_pipelineSettings.cloudShadowSpeed;
    frame.cloud.coverage = std::clamp(0.24f + frame.weatherMist * 0.28f + frame.weatherWetness * 0.18f + frame.weatherStorm * 0.32f, 0.0f, 1.0f);
    frame.cloud.density = 0.85f + frame.weatherWetness * 0.35f + frame.weatherStorm * 0.55f;
    frame.moonShadowActive = frame.skyColors.moonVisibility > frame.skyColors.sunVisibility;
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
    shader.setInt("uHeldBlockLightValue", m_heldBlockLightValue);
    shader.setInt("uHeldBlockLightValue2", 0); // Off-hand slot (unused until dual-wield)
}

void Renderer::bindWeatherUniforms(Shader& shader, const RenderFrameData& frame, const bool bindAerialReduction) const {
    shader.setFloat("uWeatherMist", frame.weatherMist);
    shader.setFloat("uWeatherWetness", frame.weatherWetness);
    shader.setFloat("uWeatherStorm", frame.weatherStorm);
    if (bindAerialReduction) {
        shader.setFloat("uAerialReduction", frame.aerialReduction);
    }
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
    shader.setFloat("uWeatherMist", frame.atmosphere.weatherMist);
    shader.setFloat("uWeatherWetness", frame.atmosphere.weatherWetness);
    shader.setFloat("uWeatherStorm", frame.atmosphere.weatherStorm);
    shader.setFloat("uAerialReduction", frame.atmosphere.aerialReduction);
}

void Renderer::bindVolumetricUniforms(Shader& shader, const RenderFrameData& frame) const {
    shader.setInt("uVolumetricFogEnabled", frame.volumetric.fogEnabled ? 1 : 0);
    shader.setFloat("uVolumetricFogStrength", frame.volumetric.fogStrength);
    shader.setFloat("uVolumetricLightStrength", frame.volumetric.lightStrength);
    shader.setFloat("uVolumetricPhaseG", frame.volumetric.phaseG);
    shader.setFloat("uVolumetricBaseDensity", frame.volumetric.baseDensity);
    shader.setFloat("uVolumetricHeightFalloff", frame.volumetric.heightFalloff);
    shader.setFloat("uVolumetricMaxDistance", frame.volumetric.maxDistance);
}

void Renderer::bindCloudUniforms(Shader& shader, const RenderFrameData& frame) const {
    shader.setInt("uCloudShadowsEnabled", frame.cloud.shadowsEnabled ? 1 : 0);
    shader.setFloat("uCloudShadowStrength", frame.cloud.shadowStrength);
    shader.setFloat("uCloudShadowScale", frame.cloud.shadowScale);
    shader.setFloat("uCloudShadowSpeed", frame.cloud.shadowSpeed);
    shader.setFloat("uCloudCoverage", frame.cloud.coverage);
    shader.setFloat("uCloudDensity", frame.cloud.density);
    shader.setFloat("uCloudHeight", frame.cloud.height);
    shader.setFloat("uCloudThickness", frame.cloud.thickness);
    shader.setFloat("uPlanarCloudCoverage", frame.cloud.planarCoverage);
    shader.setFloat("uPlanarCloudDensity", frame.cloud.planarDensity);
    shader.setFloat("uPlanarCloudAltitude", frame.cloud.planarAltitude);
}

void Renderer::bindShadowFrameUniforms(Shader& shader, const RenderFrameData& frame) const {
    const shadow::ShadowRenderer::BiasSettings bias{
        m_pipelineSettings.shadowConstantBias,
        m_pipelineSettings.shadowSlopeBias,
        m_pipelineSettings.shadowNormalOffset
    };
    m_shadowRenderer.bindShadowUniforms(shader, frame.moonShadowActive, bias);
}

void Renderer::bindSceneCompositeInputs(Shader& shader, const RenderFrameData& frame) const {
    shader.setInt("uSceneLightingTex", 0);
    shader.setInt("uReflectionTex", 1);
    shader.setInt("uCloudTex", 2);
    shader.setInt("uDepthTex", 3);
    shader.setInt("uNormalAoTex", 4);
    shader.setInt("uMaterialTex", 5);
    shader.setInt("uMaterialAuxTex", 6);
    shader.setInt("uVelocityTex", 7);
    shader.setInt("uHistorySceneTex", 8);
    shader.setInt("uHistoryDepthTex", 9);
    shader.setInt("uHistoryReflectionTex", 10);
    shader.setInt("uHistoryCloudTex", 11);
    shader.setInt("uSkyCaptureTex", 12);
    shader.setInt("uAlbedoTex", 13);
    shader.setInt("uAtmosphereLut", 14);
    shader.setMat4("uViewProj", frame.viewProj);
    shader.setMat4("uInvViewProj", frame.invViewProj);
    shader.setMat4("uPreviousViewProj", frame.previousViewProj);
    shader.setMat4("uPreviousInvViewProj", frame.previousInvViewProj);
    shader.setVec2("uJitter", frame.jitter);
    shader.setVec2("uPreviousJitter", frame.previousJitter);
    shader.setInt("uFrameIndex", static_cast<int>(frame.frameIndex & 0x7fffffffULL));
    shader.setFloat("uTime", frame.shaderTime);
    shader.setFloat("uWeatherWetness", frame.weatherWetness);
    shader.setVec3("uCameraPos", frame.cameraPos);
    shader.setVec3("uSunDirection", frame.skyColors.sunDirection);
    shader.setVec3("uMoonDirection", frame.skyColors.moonDirection);
    shader.setFloat("uSkyIntensity", frame.skyIntensity);
    shader.setFloat("uMoonVisibility", frame.skyColors.moonVisibility);
    shader.setFloat("uCloudCompositeStrength", m_pipelineSettings.sceneCloudCompositeStrength);
    shader.setFloat("uReflectionCompositeStrength", m_pipelineSettings.sceneReflectionCompositeStrength);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.sceneLightingTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.reflectionTexture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.cloudTexture());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.depthTexture());
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.normalAoTexture());
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.materialTexture());
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.materialAuxTexture());
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.velocityTexture());
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.historySceneTexturePrev());
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.historyDepthTexturePrev());
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.historyReflectionTexturePrev());
    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.historyCloudTexturePrev());
    glActiveTexture(GL_TEXTURE12);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.skyCaptureTexture());
    glActiveTexture(GL_TEXTURE13);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.albedoTexture());
    glActiveTexture(GL_TEXTURE14);
    glBindTexture(GL_TEXTURE_3D, m_deferredTargets.atmosphereLutTexture());
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
    shader.setInt("uSkyCaptureEnabled", m_deferredFrameActive ? 1 : 0);
    shader.setInt("uCompositeInputsEnabled", 0);
    shader.setInt("uWaterCompositeEnabled", 0);
    shader.setInt("uForceBaseLod", 0);
    shader.setInt("uDepthSofteningEnabled", 0);
    bindFogUniforms(shader, frame);
    shader.setFloat("uAnimationTime", frame.animationTime);
    shader.setInt("uDebugLightMode", m_debugLightMode);
    bindSkyLightingUniforms(shader, frame);
    shader.setInt("uAerialPerspectiveEnabled", m_pipelineSettings.aerialPerspectiveEnabled ? 1 : 0);
    shader.setFloat("uDirectSunStrength", m_pipelineSettings.directSunStrength);
    shader.setFloat("uSkyAmbientStrength", m_pipelineSettings.skyAmbientStrength);
    shader.setFloat("uMinimumAmbient", m_pipelineSettings.minimumAmbient);
    shader.setFloat("uBlockLightStrength", m_pipelineSettings.blockLightStrength);
    shader.setFloat("uFakeBounceStrength", m_pipelineSettings.fakeBounceStrength);
    shader.setFloat("uAlbedoDesaturation", m_pipelineSettings.albedoDesaturation);
    shader.setFloat("uShadowDesaturation", m_pipelineSettings.shadowDesaturation);
    bindAtmosphereUniforms(shader, frame);
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

bool Renderer::renderWorldDeferred(const World& world,
                                   const Camera& camera,
                                   const Window& window,
                                   const RenderFrameData& frame) {
    if (m_resourceMgr == nullptr ||
        m_chunkGBufferShader == nullptr ||
        m_deferredLightingShader == nullptr ||
        m_ssaoShader == nullptr ||
        !m_deferredTargets.init()) {
        return false;
    }

    captureCurrentFramebuffer();
    if (!m_deferredTargets.ensureSize(window.getWidth(), window.getHeight(), m_pipelineSettings.shadowResolution)) {
        restoreCapturedFramebufferViewport(window);
        return false;
    }
    clearDeferredAuxiliaryTargets();
    renderSkyCapturePass(world);

    m_deferredFrameActive = true;
#ifdef MECRAFT_DEBUG
    beginGpuTimer(GpuTimerPass::GBuffer);
#endif
    renderGBufferTerrain(world, frame);
#ifdef MECRAFT_DEBUG
    endGpuTimer(GpuTimerPass::GBuffer);
#endif
    renderVelocityPass(frame);
    if (m_pipelineSettings.shadowsEnabled && m_shadowDepthShader != nullptr) {
#ifdef MECRAFT_DEBUG
        beginGpuTimer(GpuTimerPass::Shadow);
#endif
        renderShadowMap(world, camera, frame);
#ifdef MECRAFT_DEBUG
        endGpuTimer(GpuTimerPass::Shadow);
#endif
    }
    if (m_pipelineSettings.ssaoEnabled) {
#ifdef MECRAFT_DEBUG
        beginGpuTimer(GpuTimerPass::Ssao);
#endif
        renderSsaoPass(camera, window);
        if (m_pipelineSettings.ssaoFilterEnabled && m_ssaoFilterShader != nullptr) {
            renderSsaoFilterPass();
        }
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
    renderDeferredLightingPass(frame);
#ifdef MECRAFT_DEBUG
    endGpuTimer(GpuTimerPass::Lighting);
#endif
    if (m_reflectionShader != nullptr) {
#ifdef MECRAFT_DEBUG
        beginGpuTimer(GpuTimerPass::Reflection);
#endif
        renderReflectionPass(frame);
#ifdef MECRAFT_DEBUG
        endGpuTimer(GpuTimerPass::Reflection);
#endif
    }
    if (m_pipelineSettings.reflectionFilterEnabled &&
        m_reflectionFilterShader != nullptr) {
        renderReflectionFilterPass(frame);
    }
    if (m_cloudShader != nullptr) {
#ifdef MECRAFT_DEBUG
        beginGpuTimer(GpuTimerPass::Cloud);
#endif
        renderCloudPass(frame);
#ifdef MECRAFT_DEBUG
        endGpuTimer(GpuTimerPass::Cloud);
#endif
    }
    renderSceneCompositePass(frame);
    m_deferredTargets.copySceneCompositeToTransparentComposite();
    m_deferredTargets.copySceneCompositeToSceneResolved();
    if (m_pipelineSettings.volumetricFogEnabled &&
        m_pipelineSettings.aerialPerspectiveEnabled &&
        m_pipelineSettings.volumetricFogStrength > 0.001f &&
        m_volumetricFogShader != nullptr &&
        m_volumetricCompositeShader != nullptr) {
#ifdef MECRAFT_DEBUG
        beginGpuTimer(GpuTimerPass::Volumetric);
#endif
        renderVolumetricFogPass(frame);
        compositeVolumetricFogPass();
#ifdef MECRAFT_DEBUG
        endGpuTimer(GpuTimerPass::Volumetric);
#endif
    }
    // TAA resolve: blend current SceneResolved with reprojected history
    if (m_pipelineSettings.taaEnabled &&
        m_temporalResolveShader != nullptr &&
        m_hasPreviousFrameData) {
        renderTemporalResolvePass(frame);
    }
    // Motion blur (applied after TAA, before transparent compositing)
    if (m_pipelineSettings.motionBlurEnabled &&
        m_motionBlurShader != nullptr &&
        m_hasPreviousFrameData) {
        renderMotionBlurPass(frame);
    }
    // Depth of Field (after motion blur, before transparent compositing)
    if (m_pipelineSettings.dofEnabled && m_dofShader != nullptr) {
        renderDofPass(frame);
    }
    m_deferredTargets.copySceneResolvedToTransparentComposite();
    updateDeferredHistoryTargets();
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
    m_deferredTransparentBatch.clear();
    m_transparentPassPlan.clear();

    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    m_chunkShader = m_chunkGBufferShader;
    bindChunkRenderStateForShader(frame, texArray, *m_chunkGBufferShader);
    submitMeshingJobs(world);

    std::vector<ChunkRenderEntry> cutoutEntries;
    cutoutEntries.reserve(world.getActiveChunks().size() * 2);
    m_deferredTransparentEntries.clear();
    m_deferredTransparentEntries.reserve(world.getActiveChunks().size() * 2);
    renderOpaqueChunksAndCollectPasses(world, cutoutEntries, m_deferredTransparentEntries);
    if (m_useMultiDrawIndirect) {
        m_worldRenderBuffer.flushOpaque();
    }
    renderCutoutChunks(cutoutEntries);

    glBindVertexArray(0);
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

void Renderer::renderShadowMap(const World& world, const Camera& camera, const RenderFrameData& frame) {
    if (m_shadowDepthShader == nullptr) {
        return;
    }
    std::vector<DrawBatchEntry> preservedTransparentBatch = m_deferredTransparentBatch;
    const TransparentPassPlan preservedTransparentPlan = m_transparentPassPlan;
    m_deferredTransparentBatch.clear();
    m_transparentPassPlan.clear();

    // Update shadow cascades via ShadowRenderer.
    m_shadowRenderer.computeLightDirection(frame.skyColors);
    shadow::ShadowMatrices::Settings settings;
    settings.shadowDistance = m_pipelineSettings.shadowDistance;
    settings.shadowResolution = m_pipelineSettings.shadowResolution;
    m_shadowRenderer.update(camera, settings, m_deferredTargets.width(), m_deferredTargets.height());

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    m_chunkShader = m_shadowDepthShader;
    m_shadowDepthShader->use();
    m_shadowDepthShader->setInt("uUseModel", 0);
    m_shadowDepthShader->setInt("uForceBaseLod", 1);
    m_shadowDepthShader->setInt("texArray", 0);
    m_shadowDepthShader->setFloat("uAnimationTime", frame.animationTime);
    m_shadowDepthShader->setFloat("uTime", frame.shaderTime);
    m_shadowDepthShader->setInt("uNoiseTex", 1);
    m_shadowDepthShader->setInt("uGrassColormap", 2);
    m_shadowDepthShader->setInt("uFoliageColormap", 3);
    const GLuint noiseTex = m_resourceMgr != nullptr ? m_resourceMgr->getTexture2D("shader_noise2d") : 0;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_resourceMgr->getTextureArray().textureID);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, noiseTex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getGrassColormap());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getFoliageColormap());

    const float shadowDist = std::max(64.0f, m_pipelineSettings.shadowDistance);
    int visibleTotal = 0;
    int culledTotal = 0;
    float maxCasterDistance = 0.0f;
    const char* cullingMode = "CSMBoxCulling";

    for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade) {
        const ShadowCascadeData& cascadeData = m_shadowRenderer.cascade(cascade);
        m_deferredTargets.bindCsmShadowLayer(cascade);
        glClear(GL_DEPTH_BUFFER_BIT);

        m_shadowDepthShader->setMat4("viewProj", cascadeData.viewProj);
        m_shadowDepthShader->setMat4("uShadowModelView", cascadeData.view);
        m_shadowDepthShader->setMat4("uShadowProjection", cascadeData.projection);
        m_shadowDepthShader->setMat4("uShadowProjectionInverse", glm::inverse(cascadeData.projection));

        m_worldRenderBuffer.beginFrame();
        std::vector<ChunkRenderEntry> cutoutEntries;
        cutoutEntries.reserve(world.getActiveChunks().size() * 2);
        std::vector<ChunkRenderEntry> transparentEntries;
        transparentEntries.reserve(world.getActiveChunks().size() * 2);
        m_deferredTransparentBatch.clear();
        m_transparentPassPlan.clear();

        shadow::ShadowCasterCuller shadowCuller;
        shadowCuller.setup(shadowDist, 1.0f, camera.getPosition());
        shadowCuller.resetCounters();
        renderOpaqueChunksAndCollectPasses(world, cutoutEntries, transparentEntries, false,
                                           shadowDist, &shadowCuller);
        visibleTotal += shadowCuller.getVisibleCount();
        culledTotal += shadowCuller.getCulledCount();
        maxCasterDistance = std::max(maxCasterDistance, shadowCuller.getMaxCasterDistance());
        cullingMode = shadowCuller.getCullingMode();

        if (m_useMultiDrawIndirect) {
            m_worldRenderBuffer.flushOpaque();
        }
        renderCutoutChunks(cutoutEntries);
    }

    static int frameCounter = 0;
    if (++frameCounter % 120 == 0) {
        printf("[shadow:csm] cascades=%d submitted=%d culled=%d maxDist=%.1f mode=%s halfPlane=%.1f\n",
               SHADOW_CASCADE_COUNT, visibleTotal, culledTotal, maxCasterDistance, cullingMode, shadowDist);
    }

    // Transitional compatibility for historical debug modes that still inspect
    // the legacy single-map projection: expose cascade 0 there.
    glCopyImageSubData(m_deferredTargets.csmShadowDepthTexture(), GL_TEXTURE_2D_ARRAY,
                       0, 0, 0, 0,
                       m_deferredTargets.shadowDepthTexture(), GL_TEXTURE_2D,
                       0, 0, 0, 0,
                       m_deferredTargets.shadowResolution(),
                       m_deferredTargets.shadowResolution(),
                       1);

    m_worldRenderBuffer.beginFrame();
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    m_deferredTransparentBatch = std::move(preservedTransparentBatch);
    m_transparentPassPlan = preservedTransparentPlan;
}

void Renderer::renderSsaoPass(const Camera& camera, const Window& window) {
    if (m_ssaoShader == nullptr) {
        return;
    }
    m_deferredTargets.bindSsao();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glClear(GL_COLOR_BUFFER_BIT);

    m_ssaoShader->use();
    m_ssaoShader->setInt("uDepthTex", 0);
    m_ssaoShader->setInt("uNormalAoTex", 1);
    m_ssaoShader->setInt("uNoiseTex", 2);
    const glm::mat4 proj = camera.getProjectionMatrix(window.getAspectRatio());
    m_ssaoShader->setMat4("uProjection", proj);
    m_ssaoShader->setMat4("uInvProjection", glm::inverse(proj));
    m_ssaoShader->setFloat("uRadius", m_pipelineSettings.ssaoRadius);
    m_ssaoShader->setFloat("uStrength", m_pipelineSettings.ssaoStrength);
    m_ssaoShader->setVec2("uInvResolution", glm::vec2(1.0f / std::max(1, window.getWidth()), 1.0f / std::max(1, window.getHeight())));
    m_ssaoShader->setInt("uFrameIndex", static_cast<int>(m_frameCounter % 64));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.depthTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.normalAoTexture());
    const GLuint noiseTexture = m_resourceMgr != nullptr ? m_resourceMgr->getTexture2D("shader_noise2d") : 0;
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, noiseTexture);
    renderFullscreen(*m_ssaoShader);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::renderDeferredLightingPass(const RenderFrameData& frame) {
    if (m_deferredLightingShader == nullptr) {
        return;
    }

    m_deferredTargets.bindSceneLighting();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    glActiveTexture(GL_TEXTURE15);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_deferredTargets.csmShadowDepthComparisonTexture());
    m_deferredLightingShader->use();
    m_deferredLightingShader->setInt("uAlbedoTex", 0);
    m_deferredLightingShader->setInt("uNormalAoTex", 1);
    m_deferredLightingShader->setInt("uVoxelLightTex", 2);
    m_deferredLightingShader->setInt("uMaterialTex", 3);
    m_deferredLightingShader->setInt("uMaterialAuxTex", 4);
    m_deferredLightingShader->setInt("uDepthTex", 5);
    m_deferredLightingShader->setInt("uLightmapDay", 6);
    m_deferredLightingShader->setInt("uLightmapNight", 7);
    m_deferredLightingShader->setInt("uShadowMapRaw", 8);
    m_deferredLightingShader->setInt("uSsaoTex", 9);
    m_deferredLightingShader->setInt("uSkyCaptureTex", 10);
    m_deferredLightingShader->setInt("uNoiseTex", 11);
    m_deferredLightingShader->setMat4("uViewProj", frame.viewProj);
    m_deferredLightingShader->setMat4("uInvViewProj", frame.invViewProj);
    m_deferredLightingShader->setMat4("uProjection", frame.projection);
    bindShadowFrameUniforms(*m_deferredLightingShader, frame);
    bindSkyLightingUniforms(*m_deferredLightingShader, frame);
    m_deferredLightingShader->setInt("uAerialPerspectiveEnabled", m_pipelineSettings.aerialPerspectiveEnabled ? 1 : 0);
    m_deferredLightingShader->setFloat("uShadowTintStrength", m_pipelineSettings.shadowTintStrength);
    m_deferredLightingShader->setFloat("uDirectSunStrength", m_pipelineSettings.directSunStrength);
    m_deferredLightingShader->setFloat("uSkyAmbientStrength", m_pipelineSettings.skyAmbientStrength);
    m_deferredLightingShader->setFloat("uMinimumAmbient", m_pipelineSettings.minimumAmbient);
    m_deferredLightingShader->setFloat("uShadowMinLight", m_pipelineSettings.shadowMinLight);
    m_deferredLightingShader->setFloat("uShadowContrast", m_pipelineSettings.shadowContrast);
    m_deferredLightingShader->setFloat("uBlockLightStrength", m_pipelineSettings.blockLightStrength);
    m_deferredLightingShader->setFloat("uFakeBounceStrength", m_pipelineSettings.fakeBounceStrength);
    m_deferredLightingShader->setFloat("uAlbedoDesaturation", m_pipelineSettings.albedoDesaturation);
    m_deferredLightingShader->setFloat("uShadowDesaturation", m_pipelineSettings.shadowDesaturation);
    bindAtmosphereUniforms(*m_deferredLightingShader, frame);
    m_deferredLightingShader->setInt("uShadowsEnabled", m_pipelineSettings.shadowsEnabled ? 1 : 0);
    m_deferredLightingShader->setInt("uSoftShadowsEnabled", m_pipelineSettings.softShadowsEnabled ? 1 : 0);
    m_deferredLightingShader->setInt("uPcssShadowsEnabled", m_pipelineSettings.pcssShadowsEnabled ? 1 : 0);
    m_deferredLightingShader->setInt("uContactShadowsEnabled", m_pipelineSettings.contactShadowsEnabled ? 1 : 0);
    bindCloudUniforms(*m_deferredLightingShader, frame);
    m_deferredLightingShader->setFloat("uShadowSoftness", m_pipelineSettings.shadowSoftness);
    m_deferredLightingShader->setFloat("uShadowPcssStrength", m_pipelineSettings.shadowPcssStrength);
    m_deferredLightingShader->setFloat("uShadowNormalOffset", m_pipelineSettings.shadowNormalOffset);
    m_deferredLightingShader->setFloat("uContactShadowStrength", m_pipelineSettings.contactShadowStrength);
    m_deferredLightingShader->setFloat("uTime", frame.shaderTime);
    m_deferredLightingShader->setInt("uSsaoEnabled", m_pipelineSettings.ssaoEnabled ? 1 : 0);
    m_deferredLightingShader->setInt("uIsEyeInWater", m_eyeInWater ? 1 : 0);
    m_deferredLightingShader->setInt("uShadowColorTex", 12);
    m_deferredLightingShader->setInt("uShadowNormalTex", 13);
    m_deferredLightingShader->setInt("uAtmosphereLut", 14);
    m_deferredLightingShader->setInt("uCsmShadowMap", 15);
    bindFogUniforms(*m_deferredLightingShader, frame);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.albedoTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.normalAoTexture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.voxelLightTexture());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.materialTexture());
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.materialAuxTexture());
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.depthTexture());
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getLightmapDay());
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getLightmapNight());
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.shadowDepthTexture());
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, m_pipelineSettings.ssaoFilterEnabled && m_ssaoFilterShader != nullptr
        ? m_deferredTargets.ssaoFilteredTexture() : m_deferredTargets.ssaoTexture());
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.skyCaptureTexture());
    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getTexture2D("shader_noise2d"));
    glActiveTexture(GL_TEXTURE12);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.shadowColorTexture());
    glActiveTexture(GL_TEXTURE13);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.shadowNormalTexture());
    glActiveTexture(GL_TEXTURE14);
    glBindTexture(GL_TEXTURE_3D, m_deferredTargets.atmosphereLutTexture());
    glActiveTexture(GL_TEXTURE15);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_deferredTargets.csmShadowDepthComparisonTexture());
    m_deferredLightingShader->setInt("uCsmShadowMap", 15);
    glActiveTexture(GL_TEXTURE16);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_deferredTargets.csmShadowDepthTexture());
    m_deferredLightingShader->setInt("uCsmShadowDepthRaw", 16);
    renderFullscreen(*m_deferredLightingShader);

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE16);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glActiveTexture(GL_TEXTURE15);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    for (int i = 14; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE14);
    glBindTexture(GL_TEXTURE_3D, 0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderSceneCompositePass(const RenderFrameData& frame) {
    if (m_sceneCompositeShader == nullptr) {
        m_deferredTargets.copySceneLightingToSceneComposite();
        return;
    }

    m_deferredTargets.bindSceneComposite();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_sceneCompositeShader->use();
    bindSceneCompositeInputs(*m_sceneCompositeShader, frame);
    renderFullscreen(*m_sceneCompositeShader);

    for (int unit = 13; unit >= 0; --unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE14);
    glBindTexture(GL_TEXTURE_3D, 0);
    glActiveTexture(GL_TEXTURE0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
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

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderVelocityPass(const RenderFrameData& frame) {
    if (m_velocityShader == nullptr) {
        return;
    }

    m_deferredTargets.bindVelocity();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_velocityShader->use();
    m_velocityShader->setInt("uDepthTex", 0);
    m_velocityShader->setMat4("uInvViewProj", frame.invViewProj);
    m_velocityShader->setMat4("uPreviousViewProj", frame.previousViewProj);
    m_velocityShader->setVec2("uJitter", frame.jitter);
    m_velocityShader->setVec2("uPreviousJitter", frame.previousJitter);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.depthTexture());
    renderFullscreen(*m_velocityShader);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::updateDeferredHistoryTargets() {
    if (!m_deferredTargets.isReady()) {
        return;
    }

    m_deferredTargets.copySceneResolvedToHistory();
    m_deferredTargets.copyDepthToHistory();
    m_deferredTargets.copyReflectionToHistory();
    m_deferredTargets.copyCloudToHistory();
    m_deferredTargets.swapHistory();
}

void Renderer::renderReflectionPass(const RenderFrameData& frame) {
    if (m_reflectionShader == nullptr) {
        return;
    }

    m_deferredTargets.bindReflection();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_reflectionShader->use();
    m_reflectionShader->setInt("uSceneLightingTex", 0);
    m_reflectionShader->setInt("uDepthTex", 1);
    m_reflectionShader->setInt("uNormalAoTex", 2);
    m_reflectionShader->setInt("uMaterialTex", 3);
    m_reflectionShader->setInt("uMaterialAuxTex", 4);
    m_reflectionShader->setInt("uSkyCaptureTex", 5);
    m_reflectionShader->setInt("uAtmosphereLut", 6);
    m_reflectionShader->setMat4("uViewProj", frame.viewProj);
    m_reflectionShader->setMat4("uInvViewProj", frame.invViewProj);
    m_reflectionShader->setVec3("uCameraPos", frame.cameraPos);
    m_reflectionShader->setVec3("uSunDirection", frame.skyColors.sunDirection);
    m_reflectionShader->setVec3("uMoonDirection", frame.skyColors.moonDirection);
    m_reflectionShader->setFloat("uSkyIntensity", frame.skyIntensity);
    m_reflectionShader->setFloat("uMoonVisibility", frame.skyColors.moonVisibility);
    m_reflectionShader->setFloat("uWeatherWetness", frame.weatherWetness);
    m_reflectionShader->setFloat("uTime", frame.shaderTime);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.sceneLightingTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.depthTexture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.normalAoTexture());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.materialTexture());
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.materialAuxTexture());
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.skyCaptureTexture());
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_3D, m_deferredTargets.atmosphereLutTexture());
    renderFullscreen(*m_reflectionShader);

    for (int unit = 6; unit >= 0; --unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_3D, 0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderCloudPass(const RenderFrameData& frame) {
    if (m_cloudShader == nullptr) {
        return;
    }

    m_deferredTargets.bindCloud();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_cloudShader->use();
    m_cloudShader->setInt("uDepthTex", 0);
    m_cloudShader->setInt("uSkyCaptureTex", 1);
    m_cloudShader->setInt("uNoiseTex", 2);
    m_cloudShader->setInt("uAtmosphereLut", 3);
    m_cloudShader->setMat4("uInvViewProj", frame.invViewProj);
    bindSkyLightingUniforms(*m_cloudShader, frame);
    bindAtmosphereUniforms(*m_cloudShader, frame);
    bindCloudUniforms(*m_cloudShader, frame);
    m_cloudShader->setFloat("uTime", frame.shaderTime);
    const GLuint noiseTexture = m_resourceMgr != nullptr ? m_resourceMgr->getTexture2D("shader_noise2d") : 0;
    m_cloudShader->setBool("uNoiseEnabled", noiseTexture != 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.depthTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.skyCaptureTexture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, noiseTexture);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_3D, m_deferredTargets.atmosphereLutTexture());
    renderFullscreen(*m_cloudShader);

    for (int i = 3; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_3D, 0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderVolumetricFogPass(const RenderFrameData& frame) {
    m_deferredTargets.bindHalfRes();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_deferredTargets.csmShadowDepthComparisonTexture());
    m_volumetricFogShader->use();
    m_volumetricFogShader->setInt("uDepthTex", 0);
    m_volumetricFogShader->setInt("uSkyCaptureTex", 1);
    m_volumetricFogShader->setInt("uNoiseTex", 2);
    m_volumetricFogShader->setInt("uShadowMapRaw", 3);
    m_volumetricFogShader->setInt("uShadowColorTex", 4);
    m_volumetricFogShader->setInt("uAtmosphereLut", 5);
    m_volumetricFogShader->setInt("uCsmShadowMap", 6);
    m_volumetricFogShader->setMat4("uInvViewProj", frame.invViewProj);
    bindShadowFrameUniforms(*m_volumetricFogShader, frame);
    bindSkyLightingUniforms(*m_volumetricFogShader, frame);
    bindAtmosphereUniforms(*m_volumetricFogShader, frame);
    bindVolumetricUniforms(*m_volumetricFogShader, frame);
    bindCloudUniforms(*m_volumetricFogShader, frame);
    m_volumetricFogShader->setInt("uShadowsEnabled", m_pipelineSettings.shadowsEnabled ? 1 : 0);
    m_volumetricFogShader->setFloat("uTime", frame.shaderTime);
    const GLuint noiseTexture = m_resourceMgr != nullptr ? m_resourceMgr->getTexture2D("shader_noise2d") : 0;
    m_volumetricFogShader->setBool("uNoiseEnabled", noiseTexture != 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.depthTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.skyCaptureTexture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, noiseTexture);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.shadowDepthTexture());
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.shadowColorTexture());
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_3D, m_deferredTargets.atmosphereLutTexture());
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_deferredTargets.csmShadowDepthComparisonTexture());
    m_volumetricFogShader->setInt("uCsmShadowMap", 6);
    renderFullscreen(*m_volumetricFogShader);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    for (int i = 5; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_3D, 0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::compositeVolumetricFogPass() {
    m_deferredTargets.bindSceneResolved();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    m_volumetricCompositeShader->use();
    m_volumetricCompositeShader->setInt("uSceneTex", 0);
    m_volumetricCompositeShader->setInt("uVolumetricTex", 1);
    m_volumetricCompositeShader->setInt("uDepthTex", 2);
    m_volumetricCompositeShader->setVec2(
        "uInvFullResolution",
        glm::vec2(1.0f / static_cast<float>(std::max(1, m_deferredTargets.width())),
                  1.0f / static_cast<float>(std::max(1, m_deferredTargets.height()))));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.sceneCompositeTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.halfResTexture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.depthTexture());
    renderFullscreen(*m_volumetricCompositeShader);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderReflectionFilterPass(const RenderFrameData& frame) {
    if (m_reflectionFilterShader == nullptr) {
        return;
    }

    m_deferredTargets.bindReflection();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    m_reflectionFilterShader->use();
    m_reflectionFilterShader->setInt("uReflectionTex", 0);
    m_reflectionFilterShader->setInt("uDepthTex", 1);
    m_reflectionFilterShader->setInt("uNormalAoTex", 2);
    m_reflectionFilterShader->setInt("uMaterialTex", 3);
    m_reflectionFilterShader->setVec2("uScreenSize",
        glm::vec2(static_cast<float>(std::max(1, m_deferredTargets.width())),
                   static_cast<float>(std::max(1, m_deferredTargets.height()))));
    m_reflectionFilterShader->setFloat("uFilterStrength", m_pipelineSettings.reflectionFilterStrength);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.reflectionTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.depthTexture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.normalAoTexture());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.materialTexture());
    renderFullscreen(*m_reflectionFilterShader);

    for (int i = 3; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderTemporalResolvePass(const RenderFrameData& frame) {
    if (m_temporalResolveShader == nullptr) {
        return;
    }

    // Copy current SceneResolved to history[current] so we can read it while writing SceneResolved.
    // history[current] = this frame's unresolved color, history[prev] = last frame's resolved color.
    m_deferredTargets.copySceneResolvedToHistory();

    m_deferredTargets.bindSceneResolved();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    m_temporalResolveShader->use();
    m_temporalResolveShader->setInt("uCurrentTex", 0);
    m_temporalResolveShader->setInt("uHistoryTex", 1);
    m_temporalResolveShader->setInt("uVelocityTex", 2);
    m_temporalResolveShader->setInt("uDepthTex", 3);
    m_temporalResolveShader->setInt("uHistoryDepthTex", 4);

    m_temporalResolveShader->setMat4("uInvViewProj", frame.invViewProj);
    m_temporalResolveShader->setMat4("uPreviousViewProj", frame.previousViewProj);
    m_temporalResolveShader->setVec2("uScreenSize",
        glm::vec2(static_cast<float>(std::max(1, m_deferredTargets.width())),
                   static_cast<float>(std::max(1, m_deferredTargets.height()))));
    m_temporalResolveShader->setVec2("uJitter", frame.jitter);
    m_temporalResolveShader->setVec2("uPreviousJitter", frame.previousJitter);
    m_temporalResolveShader->setInt("uFrameIndex", static_cast<int>(frame.frameIndex));
    m_temporalResolveShader->setFloat("uBlendMin", m_pipelineSettings.taaBlendMin);
    m_temporalResolveShader->setFloat("uBlendMax", m_pipelineSettings.taaBlendMax);

    // uCurrentTex = this frame's unresolved color (now stored in history[current])
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.historySceneTexture());
    // uHistoryTex = previous frame's resolved color
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.historySceneTexturePrev());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.velocityTexture());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.depthTexture());
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.historyDepthTexturePrev());

    renderFullscreen(*m_temporalResolveShader);

    for (int i = 4; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderSsaoFilterPass() {
    if (m_ssaoFilterShader == nullptr) {
        return;
    }

    m_deferredTargets.bindSsaoFiltered();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    m_ssaoFilterShader->use();
    m_ssaoFilterShader->setInt("uSsaoTex", 0);
    m_ssaoFilterShader->setInt("uDepthTex", 1);
    m_ssaoFilterShader->setInt("uNormalAoTex", 2);
    m_ssaoFilterShader->setVec2("uScreenSize",
        glm::vec2(static_cast<float>(std::max(1, m_deferredTargets.width())),
                   static_cast<float>(std::max(1, m_deferredTargets.height()))));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.ssaoTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.depthTexture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.normalAoTexture());
    renderFullscreen(*m_ssaoFilterShader);

    for (int i = 2; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderMotionBlurPass(const RenderFrameData& frame) {
    if (m_motionBlurShader == nullptr) {
        return;
    }

    // Save current SceneResolved to history[current] so we can read it while writing SceneResolved
    m_deferredTargets.copySceneResolvedToHistory();

    m_deferredTargets.bindSceneResolved();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    m_motionBlurShader->use();
    m_motionBlurShader->setInt("uSceneTex", 0);
    m_motionBlurShader->setInt("uVelocityTex", 1);
    m_motionBlurShader->setInt("uDepthTex", 2);
    m_motionBlurShader->setFloat("uStrength", m_pipelineSettings.motionBlurStrength);
    m_motionBlurShader->setInt("uSamples", m_pipelineSettings.motionBlurSamples);
    m_motionBlurShader->setVec2("uScreenSize",
        glm::vec2(static_cast<float>(std::max(1, m_deferredTargets.width())),
                   static_cast<float>(std::max(1, m_deferredTargets.height()))));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.historySceneTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.velocityTexture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.depthTexture());
    renderFullscreen(*m_motionBlurShader);

    for (int i = 2; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderDofPass(const RenderFrameData& frame) {
    if (m_dofShader == nullptr) {
        return;
    }

    m_deferredTargets.copySceneResolvedToHistory();
    m_deferredTargets.bindSceneResolved();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    m_dofShader->use();
    m_dofShader->setInt("uSceneTex", 0);
    m_dofShader->setInt("uDepthTex", 1);
    m_dofShader->setInt("uNoiseTex", 2);
    m_dofShader->setMat4("uProjection", frame.projection);
    m_dofShader->setMat4("uInvProjection", glm::inverse(frame.projection));
    m_dofShader->setFloat("uFocusDistance", m_pipelineSettings.dofFocusDistance);
    m_dofShader->setFloat("uAperture", m_pipelineSettings.dofAperture);
    m_dofShader->setFloat("uDofIntensity", m_pipelineSettings.dofIntensity);
    m_dofShader->setFloat("uDofAnamorphic", 1.0f);
    m_dofShader->setVec2("uScreenSize",
        glm::vec2(static_cast<float>(std::max(1, m_deferredTargets.width())),
                   static_cast<float>(std::max(1, m_deferredTargets.height()))));

    const GLuint noiseTex = m_resourceMgr != nullptr ? m_resourceMgr->getTexture2D("shader_noise2d") : 0;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.historySceneTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.depthTexture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, noiseTex);
    renderFullscreen(*m_dofShader);

    for (int i = 2; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderDeferredDebugView(const GLint framebuffer, const int width, const int height) {
    if (m_deferredDebugShader == nullptr) {
        return;
    }

    m_deferredTargets.bindDefaultLike(framebuffer, width, height);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    m_deferredDebugShader->use();
    m_deferredDebugShader->setInt("uAlbedoTex", 0);
    m_deferredDebugShader->setInt("uNormalAoTex", 1);
    m_deferredDebugShader->setInt("uVoxelLightTex", 2);
    m_deferredDebugShader->setInt("uMaterialTex", 3);
    m_deferredDebugShader->setInt("uDepthTex", 4);
    m_deferredDebugShader->setInt("uShadowMapRaw", 5);
    m_deferredDebugShader->setInt("uSsaoTex", 6);
    m_deferredDebugShader->setInt("uSceneLightingTex", 7);
    m_deferredDebugShader->setInt("uTransparentCompositeTex", 8);
    m_deferredDebugShader->setInt("uTransparentCompositeDepthTex", 9);
    m_deferredDebugShader->setInt("uVolumetricTex", 10);
    m_deferredDebugShader->setInt("uSkyCaptureTex", 11);
    m_deferredDebugShader->setInt("uVelocityTex", 12);
    m_deferredDebugShader->setInt("uHistorySceneTex", 13);
    m_deferredDebugShader->setInt("uHistoryDepthTex", 13);
    m_deferredDebugShader->setInt("uNoiseTex", 13);
    m_deferredDebugShader->setInt("uReflectionTex", 14);
    m_deferredDebugShader->setInt("uCloudTex", 15);
    m_deferredDebugShader->setInt("uSceneCompositeTex", 15);
    m_deferredDebugShader->setInt("uSceneResolvedTex", 15);
    m_deferredDebugShader->setInt("uMaterialAuxTex", 13);
    m_deferredDebugShader->setInt("uShadowColorTex", 14);
    m_deferredDebugShader->setInt("uShadowNormalTex", 15);
    m_deferredDebugShader->setInt("uHistoryReflectionTex", 14);
    m_deferredDebugShader->setInt("uHistoryCloudTex", 15);
    m_deferredDebugShader->setInt("uCsmShadowDepthTex", 16);
    const RenderFrameData* debugFrame = m_currentFrameDataValid
        ? &m_currentFrameData
        : (m_hasPreviousFrameData ? &m_previousFrameData : nullptr);
    m_deferredDebugShader->setMat4("uShadowModelView", m_shadowRenderer.modelView());
    m_deferredDebugShader->setMat4("uShadowProjection", m_shadowRenderer.projection());
    m_deferredDebugShader->setMat4("uShadowProjectionInverse", m_shadowRenderer.projectionInverse());
    m_deferredDebugShader->setFloat("uShadowExtent", m_shadowRenderer.shadowExtent());
    m_deferredDebugShader->setFloat("uShadowTexelWorldSize", m_shadowRenderer.texelWorldSize());
    m_deferredDebugShader->setFloat("uShadowMapSize", static_cast<float>(m_pipelineSettings.shadowResolution));
    m_deferredDebugShader->setFloat("uShadowDistance", std::max(64.0f, m_pipelineSettings.shadowDistance));
    m_deferredDebugShader->setFloat("uShadowConstantBias", m_pipelineSettings.shadowConstantBias);
    m_deferredDebugShader->setFloat("uShadowSlopeBias", m_pipelineSettings.shadowSlopeBias);
    m_deferredDebugShader->setFloat("uShadowNormalOffset", m_pipelineSettings.shadowNormalOffset);
    if (debugFrame != nullptr) {
        const shadow::ShadowRenderer::BiasSettings bias{
            m_pipelineSettings.shadowConstantBias,
            m_pipelineSettings.shadowSlopeBias,
            m_pipelineSettings.shadowNormalOffset
        };
        m_shadowRenderer.bindShadowUniforms(*m_deferredDebugShader, debugFrame->moonShadowActive, bias);
    } else {
        m_deferredDebugShader->setInt("uCsmCascadeCount", SHADOW_CASCADE_COUNT);
        for (int i = 0; i < SHADOW_CASCADE_COUNT; ++i) {
            const std::string prefix = "uCsmCascades[" + std::to_string(i) + "]";
            m_deferredDebugShader->setMat4(prefix + ".viewProj", m_shadowRenderer.cascade(i).viewProj);
            m_deferredDebugShader->setFloat(prefix + ".splitNear", m_shadowRenderer.cascade(i).splitNear);
            m_deferredDebugShader->setFloat(prefix + ".splitFar", m_shadowRenderer.cascade(i).splitFar);
            m_deferredDebugShader->setFloat(prefix + ".texelWorldSize", m_shadowRenderer.cascade(i).texelWorldSize);
        }
    }
    m_deferredDebugShader->setInt("uDebugViewMode", m_pipelineSettings.debugViewMode);
    m_deferredDebugShader->setMat4("uInvViewProj", debugFrame != nullptr ? debugFrame->invViewProj : glm::mat4(1.0f));
    m_deferredDebugShader->setVec3("uCameraPos", debugFrame != nullptr ? debugFrame->cameraPos : m_cameraPos);
    m_deferredDebugShader->setVec3("uSunDirection", debugFrame != nullptr ? debugFrame->skyColors.sunDirection : glm::vec3(0.0f, 1.0f, 0.0f));
    m_deferredDebugShader->setVec3("uMoonDirection", debugFrame != nullptr ? debugFrame->skyColors.moonDirection : glm::vec3(0.0f, 1.0f, 0.0f));
    m_deferredDebugShader->setVec3("uShadowLightDirection", m_shadowRenderer.lightDirection());
    m_deferredDebugShader->setInt("uShadowLightMode", (debugFrame != nullptr && debugFrame->moonShadowActive) ? 1 : 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.albedoTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.normalAoTexture());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.voxelLightTexture());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.materialTexture());
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.depthTexture());
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.shadowDepthTexture());
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.ssaoTexture());
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.sceneLightingTexture());
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.transparentCompositeTexture());
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.transparentCompositeDepthTexture());
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.halfResTexture());
    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.skyCaptureTexture());
    glActiveTexture(GL_TEXTURE12);
    glBindTexture(GL_TEXTURE_2D, m_deferredTargets.velocityTexture());
    glActiveTexture(GL_TEXTURE13);
    const bool materialAuxDebug =
        m_pipelineSettings.debugViewMode == 26 || m_pipelineSettings.debugViewMode == 27;
    const bool historyDepthDebug = m_pipelineSettings.debugViewMode == 19;
    const bool shadowCompareDebug =
        m_pipelineSettings.debugViewMode == 21 ||
        m_pipelineSettings.debugViewMode == 22 ||
        m_pipelineSettings.debugViewMode == 34 ||
        m_pipelineSettings.debugViewMode == 35;
    glBindTexture(GL_TEXTURE_2D,
                  shadowCompareDebug ? m_resourceMgr->getTexture2D("shader_noise2d")
                                     : (materialAuxDebug ? m_deferredTargets.materialAuxTexture()
                                                         : (historyDepthDebug ? m_deferredTargets.historyDepthTexturePrev()
                                                                              : m_deferredTargets.historySceneTexturePrev())));
    glActiveTexture(GL_TEXTURE14);
    const bool shadowCasterDebug = m_pipelineSettings.debugViewMode == 35;
    const bool reflectionHistoryDebug = m_pipelineSettings.debugViewMode == 28;
    glBindTexture(GL_TEXTURE_2D,
                  shadowCasterDebug ? m_deferredTargets.shadowColorTexture()
                                    : (reflectionHistoryDebug ? m_deferredTargets.historyReflectionTexturePrev()
                                                              : m_deferredTargets.reflectionTexture()));
    glActiveTexture(GL_TEXTURE15);
    const bool cloudHistoryDebug = m_pipelineSettings.debugViewMode == 29;
    const bool sceneCompositeDebug = m_pipelineSettings.debugViewMode == 11;
    const bool sceneResolvedDebug = m_pipelineSettings.debugViewMode == 31;
    glBindTexture(GL_TEXTURE_2D,
                  shadowCasterDebug ? m_deferredTargets.shadowNormalTexture()
                                    : (sceneResolvedDebug ? m_deferredTargets.sceneResolvedTexture()
                                                          : (sceneCompositeDebug ? m_deferredTargets.sceneCompositeTexture()
                                                                                 : (cloudHistoryDebug ? m_deferredTargets.historyCloudTexturePrev()
                                                                                                      : m_deferredTargets.cloudTexture()))));
    glActiveTexture(GL_TEXTURE16);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_deferredTargets.csmShadowDepthTexture());
    renderFullscreen(*m_deferredDebugShader);

    glActiveTexture(GL_TEXTURE16);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    for (int unit = 15; unit >= 0; --unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderSkyCapturePass(const World& world) {
    const float cameraAltitude = m_cameraPos.y;
    const GLuint atmosphereLut = m_deferredTargets.atmosphereLutTexture();
    const int moonPhase = world.getDayNightSystem().getMoonPhaseIndex();
    // DerivativeMain MoonFlux: vec3(abs(moonPhase - 4.0) * 0.25 + 0.2).
    const float moonPhaseFlux = static_cast<float>(std::abs(moonPhase - 4)) * 0.25f + 0.2f;

    // Raw sky radiance (rows 0..257)
    m_gameplaySkyRenderer.renderSkyCapture(world.getDayNightSystem(),
                                           m_deferredTargets.skyCaptureFramebuffer(),
                                           m_deferredTargets.skyCaptureWidth(),
                                           m_deferredTargets.skyCaptureHeight(),
                                           cameraAltitude, atmosphereLut, moonPhaseFlux);

    // Cloudy sky radiance (rows 258..513) — same atmosphere pass, composited with cloud data
    m_gameplaySkyRenderer.renderCloudySkyCapture(world.getDayNightSystem(),
                                                  m_deferredTargets.skyCaptureFramebuffer(),
                                                  m_deferredTargets.skyCaptureWidth(),
                                                  m_deferredTargets.skyCaptureHeight(),
                                                  cameraAltitude, atmosphereLut, moonPhaseFlux);

    // Compute illuminance metadata and cloud dynamic weather
    auto illum = m_gameplaySkyRenderer.computeSkyIlluminance(m_gameplaySkyRenderer.computeSkyColors(world.getDayNightSystem()));
    // DerivativeMain worldTime: 24000 ticks/day, our timeOfDay is in seconds with 1200s/day.
    const int worldDay = world.getDayNightSystem().getElapsedDays();
    const int worldTime = static_cast<int>(world.getDayNightSystem().getTimeOfDay() * 20.0f);
    illum.cloudDynamicWeather = GameplaySkyRenderer::computeCloudDynamicWeather(worldDay, worldTime);

    m_gameplaySkyRenderer.writeSkyCacheMetadata(illum,
                                                 m_deferredTargets.skyCaptureFramebuffer(),
                                                 m_deferredTargets.skyCaptureWidth(),
                                                 cameraAltitude, atmosphereLut, moonPhaseFlux);
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

void Renderer::restoreCapturedFramebufferViewport(const Window& window) {
    const int fallbackWidth = std::max(1, window.getWidth());
    const int fallbackHeight = std::max(1, window.getHeight());
    const int width = m_capturedViewport[2] > 0 ? m_capturedViewport[2] : fallbackWidth;
    const int height = m_capturedViewport[3] > 0 ? m_capturedViewport[3] : fallbackHeight;
    m_deferredTargets.bindDefaultLike(m_capturedFramebuffer, width, height);
}

void Renderer::submitMeshingJobs(const World& world) {
    std::vector<MeshingCandidate> candidates;
    const auto& activeChunks = world.getActiveChunks();

    auto findSharedByPtr = [&](const Chunk* raw) -> std::shared_ptr<Chunk> {
        if (!raw) return nullptr;
        const int64_t key = World::chunkKey(raw->m_chunkX, raw->m_chunkZ);
        auto it = activeChunks.find(key);
        return (it != activeChunks.end() && it->second.get() == raw) ? it->second : nullptr;
    };

    // Build sub-chunk key for in-flight tracking: pack chunkKey + scy
    auto subChunkFlightKey = [](int64_t chunkKey, int scy) -> int64_t {
        return (chunkKey & 0x00FFFFFFFFFFFFFFLL) | (static_cast<int64_t>(scy) << 56);
    };

    for (const auto& pair : activeChunks) {
        const int64_t chunkKey = pair.first;
        Chunk& chunk = *pair.second;

        // Check each sub-chunk individually
        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            // Skip if not dirty
            if (!chunk.isSubChunkDirty(scy)) continue;

            // Skip Air sub-chunks / fully occluded solid sub-chunks
            if (ChunkMesher::shouldSkipSubChunk(chunk, scy)) continue;

            // Skip if already in flight
            const int64_t flightKey = subChunkFlightKey(chunkKey, scy);
            if (m_meshingInFlight.find(flightKey) != m_meshingInFlight.end()) continue;

            const glm::ivec3 offset = chunk.getWorldOffset();
            const float centerX = static_cast<float>(offset.x) + Chunk::SIZE_X * 0.5f;
            const float centerZ = static_cast<float>(offset.z) + Chunk::SIZE_Z * 0.5f;
            const float dx = centerX - m_cameraPos.x;
            const float dz = centerZ - m_cameraPos.z;

            MeshingCandidate candidate;
            candidate.chunkKey = chunkKey;
            candidate.chunk = &chunk;
            candidate.scy = scy;
            candidate.distanceSq = dx * dx + dz * dz;
            candidate.chunkRef = pair.second;
            candidate.neighborPosX = findSharedByPtr(chunk.neighbors[0]);
            candidate.neighborNegX = findSharedByPtr(chunk.neighbors[1]);
            candidate.neighborPosZ = findSharedByPtr(chunk.neighbors[2]);
            candidate.neighborNegZ = findSharedByPtr(chunk.neighbors[3]);
            candidates.push_back(std::move(candidate));
        }
    }

    const int availableInFlightSlots = std::max(0, m_meshingMaxInFlight - static_cast<int>(m_meshingInFlight.size()));
    const int submitCount = std::min({m_meshingSubmitBudget, availableInFlightSlots, static_cast<int>(candidates.size())});
    if (submitCount <= 0) {
        return;
    }

    const auto candidateLess = [](const MeshingCandidate& lhs, const MeshingCandidate& rhs) {
        if (lhs.distanceSq != rhs.distanceSq) {
            return lhs.distanceSq < rhs.distanceSq;
        }
        if (lhs.chunkKey != rhs.chunkKey) {
            return lhs.chunkKey < rhs.chunkKey;
        }
        return lhs.scy < rhs.scy;
    };
    std::partial_sort(candidates.begin(),
                      candidates.begin() + submitCount,
                      candidates.end(),
                      candidateLess);

    for (int index = 0; index < submitCount; ++index) {
        MeshingCandidate& candidate = candidates[static_cast<size_t>(index)];
        if (candidate.chunk == nullptr) {
            continue;
        }

        SubChunkMeshingJob job;
        job.chunkKey = candidate.chunkKey;
        job.scy = candidate.scy;
        job.revision = candidate.chunk->getSubChunkMeshRevision(candidate.scy);
        job.chunk = std::move(candidate.chunkRef);
        job.neighborPosX = std::move(candidate.neighborPosX);
        job.neighborNegX = std::move(candidate.neighborNegX);
        job.neighborPosZ = std::move(candidate.neighborPosZ);
        job.neighborNegZ = std::move(candidate.neighborNegZ);
        job.world = &world;

        const int priority = static_cast<int>(candidate.distanceSq);
        m_meshingService.submit(std::move(job), priority);

        const int64_t flightKey = subChunkFlightKey(candidate.chunkKey, candidate.scy);
        m_meshingInFlight.insert(flightKey);
#ifdef MECRAFT_DEBUG
        ++m_meshingSubmittedThisFrame;
#endif
    }
}

void Renderer::renderOpaqueChunksAndCollectPasses(const World& world,
                                                  std::vector<ChunkRenderEntry>& cutoutEntries,
                                                  std::vector<ChunkRenderEntry>& transparentEntries,
                                                  const bool frustumCull,
                                                  const float maxCameraDistance,
                                                  shadow::ShadowCasterCuller* shadowCuller) {
    syncChunkRenderColumns(world);
    if (m_chunkRenderColumns.empty()) {
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
    while (regionBegin < m_chunkRenderColumns.size()) {
        size_t regionEnd = regionBegin + 1;
        const ChunkRenderColumnCache& regionFirst = m_chunkRenderColumns[regionBegin];
        while (regionEnd < m_chunkRenderColumns.size()) {
            const ChunkRenderColumnCache& candidate = m_chunkRenderColumns[regionEnd];
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
            ChunkRenderColumnCache& column = m_chunkRenderColumns[i];
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
            ChunkRenderColumnCache& column = m_chunkRenderColumns[i];
            if (column.chunk == nullptr || !column.columnHasBounds) {
                continue;
            }

            const int columnCandidateCount = (column.aggregatedPresent ? 1 : 0) + column.transparentCount;

            if (!boundsWithinCameraDistance(column.columnBoundsMin, column.columnBoundsMax)) {
                continue;
            }

#ifdef MECRAFT_DEBUG
            ++m_columnTestsThisFrame;
            FrustumPlane culledPlane = FrustumPlane::Count;
            if (frustumCull && !isChunkInFrustum(column.columnBoundsMin, column.columnBoundsMax,
                                  m_chunkCullingDebugEnabled ? &culledPlane : nullptr)) {
                if (m_chunkCullingDebugEnabled) {
                    recordChunkCull(culledPlane, columnCandidateCount);
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
                        mesh.transparentRange.vertexCount == 0) {
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
    const uint64_t activeChunkRevision = world.getActiveChunkRevision();
    const int regionChunkSize = std::max(1, m_regionChunkSize);
    if (m_chunkRenderColumnsRevision == activeChunkRevision &&
        m_chunkRenderColumnsRegionSize == regionChunkSize) {
        return;
    }

    const auto& activeChunks = world.getActiveChunks();
    m_chunkRenderColumns.clear();
    m_chunkRenderColumns.reserve(activeChunks.size());

    for (const auto& pair : activeChunks) {
        if (!pair.second) {
            continue;
        }

        ChunkRenderColumnCache column;
        column.chunk = pair.second.get();
        column.chunkKey = pair.first;
        column.chunkX = column.chunk->m_chunkX;
        column.chunkZ = column.chunk->m_chunkZ;
        column.regionX = floorDiv(column.chunkX, regionChunkSize);
        column.regionZ = floorDiv(column.chunkZ, regionChunkSize);
        column.worldOffset = glm::vec3(column.chunk->getWorldOffset());
        m_chunkRenderColumns.push_back(column);
    }

    std::sort(m_chunkRenderColumns.begin(), m_chunkRenderColumns.end(),
              [](const ChunkRenderColumnCache& a, const ChunkRenderColumnCache& b) {
                  if (a.regionX != b.regionX) {
                      return a.regionX < b.regionX;
                  }
                  if (a.regionZ != b.regionZ) {
                      return a.regionZ < b.regionZ;
                  }
                  if (a.chunkX != b.chunkX) {
                      return a.chunkX < b.chunkX;
                  }
                  return a.chunkZ < b.chunkZ;
              });

    m_chunkRenderColumnsRevision = activeChunkRevision;
    m_chunkRenderColumnsRegionSize = regionChunkSize;
}

void Renderer::releaseMdiAllocation(const SubChunkGpuKey& key) {
    const auto it = m_mdiMeshAllocations.find(key);
    if (it == m_mdiMeshAllocations.end()) {
        return;
    }
    m_worldRenderBuffer.free(it->second.mesh);
    m_mdiMeshAllocations.erase(it);
}

void Renderer::releaseStaleMdiAllocations(const World& world) {
    if (m_mdiMeshAllocations.empty()) {
        return;
    }

    const auto& activeChunks = world.getActiveChunks();
    for (auto it = m_mdiMeshAllocations.begin(); it != m_mdiMeshAllocations.end(); ) {
        const auto chunkIt = activeChunks.find(it->first.chunkKey);
        bool release = (chunkIt == activeChunks.end() || !chunkIt->second);
        if (!release) {
            const SubChunk* sc = chunkIt->second->getSubChunk(it->first.scy);
            if (sc == nullptr || !sc->getMesh().inGlobalPool) {
                release = true;
            } else {
                const SubChunkMesh& current = sc->getMesh();
                release =
                    current.opaqueRange.generation != it->second.mesh.opaque.generation ||
                    current.cutoutRange.generation != it->second.mesh.cutout.generation ||
                    current.cutoutDistanceRange.generation != it->second.mesh.cutoutDistance.generation ||
                    current.transparentRange.generation != it->second.mesh.transparent.generation;
            }
        }

        if (release) {
            m_worldRenderBuffer.free(it->second.mesh);
            it = m_mdiMeshAllocations.erase(it);
        } else {
            ++it;
        }
    }
}

void Renderer::refreshChunkRenderColumnCache(ChunkRenderColumnCache& column) {
    if (column.chunk == nullptr) {
        return;
    }

    column.chunk->ensureColumnMeshBuilt();

    bool needsRefresh = !column.stateValid;
    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        const uint64_t revision = column.chunk->getSubChunkMeshRevision(scy);
        if (!column.stateValid || column.subChunkMeshRevisions[scy] != revision) {
            column.subChunkMeshRevisions[scy] = revision;
            needsRefresh = true;
        }
    }

    if (!needsRefresh) {
        return;
    }

    const SubChunkMesh& columnMesh = column.chunk->getColumnMesh();
    column.aggregatedHasOpaque = columnMesh.vertexCount > 0;
    column.aggregatedHasCutout =
        columnMesh.cutoutVertexCount > 0 ||
        columnMesh.cutoutDistanceVertexCount > 0;
    column.aggregatedPresent = column.aggregatedHasOpaque || column.aggregatedHasCutout;

    const bool columnBoundsPresent = m_useMultiDrawIndirect
        ? columnMesh.hasBounds
        : column.aggregatedPresent;
    if (columnBoundsPresent) {
        column.aggregatedBoundsMin = columnMesh.hasBounds
            ? columnMesh.boundsMin
            : column.worldOffset;
        column.aggregatedBoundsMax = columnMesh.hasBounds
            ? columnMesh.boundsMax
            : column.worldOffset + glm::vec3(Chunk::SIZE_X, Chunk::SIZE_Y, Chunk::SIZE_Z);
    } else {
        column.aggregatedBoundsMin = glm::vec3(0.0f);
        column.aggregatedBoundsMax = glm::vec3(0.0f);
    }

    bool columnHasBounds = false;
    glm::vec3 columnMin(0.0f);
    glm::vec3 columnMax(0.0f);
    if (columnBoundsPresent) {
        expandBounds(columnMin, columnMax, columnHasBounds,
                     column.aggregatedBoundsMin, column.aggregatedBoundsMax);
    }

    column.transparentCount = 0;
    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        const SubChunk* sc = column.chunk->getSubChunk(scy);
        if (!sc) {
            continue;
        }

        const SubChunkMesh& mesh = sc->getMesh();
        if (mesh.transparentVertexCount == 0 && mesh.waterVertexCount == 0) {
            continue;
        }

        const int yBase = scy * SubChunk::SIZE;
        TransparentSubChunkCache& transparent = column.transparentSubChunks[scy];
        if (m_useMultiDrawIndirect) {
            transparent.boundsMin = mesh.hasBounds
                ? mesh.boundsMin
                : column.worldOffset + glm::vec3(0.0f, static_cast<float>(yBase), 0.0f);
            transparent.boundsMax = mesh.hasBounds
                ? mesh.boundsMax
                : column.worldOffset + glm::vec3(Chunk::SIZE_X, static_cast<float>(yBase + SubChunk::SIZE), Chunk::SIZE_Z);
        } else {
            transparent.boundsMin = mesh.hasBounds
                ? mesh.boundsMin + column.worldOffset
                : column.worldOffset + glm::vec3(0.0f, static_cast<float>(yBase), 0.0f);
            transparent.boundsMax = mesh.hasBounds
                ? mesh.boundsMax + column.worldOffset
                : column.worldOffset + glm::vec3(Chunk::SIZE_X, static_cast<float>(yBase + SubChunk::SIZE), Chunk::SIZE_Z);
        }

        column.transparentScys[column.transparentCount++] = scy;
        expandBounds(columnMin, columnMax, columnHasBounds,
                     transparent.boundsMin, transparent.boundsMax);
    }

    column.columnHasBounds = columnHasBounds;
    column.columnBoundsMin = columnHasBounds ? columnMin : glm::vec3(0.0f);
    column.columnBoundsMax = columnHasBounds ? columnMax : glm::vec3(0.0f);
    column.stateValid = true;
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
    if (range.vertexCount == 0) {
        return;
    }

    m_deferredTransparentBatch.push_back({range, distanceSq, kind});
    if (kind == TransparentBatchKind::Water) {
        ++m_transparentPassPlan.waterCommands;
        m_transparentPassPlan.waterVertices += range.vertexCount;
    } else {
        ++m_transparentPassPlan.genericCommands;
        m_transparentPassPlan.genericVertices += range.vertexCount;
    }
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
            if (entry.kind == TransparentBatchKind::Water) {
                m_worldRenderBuffer.addWater(entry.range);
            } else {
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


void Renderer::endFrame(const Window &window) {
    (void)window;
    if (m_currentFrameDataValid) {
        m_previousFrameData = m_currentFrameData;
        m_hasPreviousFrameData = true;
    }
    recordMeshingHistory();
    m_currentFrameDataValid = false;
    m_deferredFrameActive = false;
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
    glLineWidth(2.0f);

    glBindVertexArray(m_outlineVao);
    glDrawArrays(GL_LINES, 0, 24);
    glBindVertexArray(0);

    glLineWidth(1.0f);
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
    // Phase 1: Drain all completed results from the service into the deferred buffer.
    // This avoids interleaving tryPopCompleted with budget checks, and lets us
    // process results in order with strict vertex/time budgets.
    {
        SubChunkMeshingResult result;
        while (m_meshingService.tryPopCompleted(result)) {
            m_deferredMeshResults.push_back(std::move(result));
        }
    }

    if (m_deferredMeshResults.empty()) {
        return;
    }

    const auto drainStartTime = std::chrono::steady_clock::now();
    int uploadedCount = 0;

    auto subChunkFlightKey = [](int64_t chunkKey, int scy) -> int64_t {
        return (chunkKey & 0x00FFFFFFFFFFFFFFLL) | (static_cast<int64_t>(scy) << 56);
    };

    // Phase 2: Process from deferred buffer respecting budgets.
    // Over-budget results stay in the buffer for the next frame.
    size_t processIdx = 0;
    while (processIdx < m_deferredMeshResults.size()) {
        const double elapsedMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - drainStartTime).count();
        if (elapsedMs >= m_meshingDrainTimeBudgetMs) {
            break;
        }

        SubChunkMeshingResult& result = m_deferredMeshResults[processIdx];

        // Compute vertex count for budget check BEFORE uploading
        const int currentVertices =
            static_cast<int>(result.meshData.opaqueVertices.size()) +
            static_cast<int>(result.meshData.cutoutVertices.size()) +
            static_cast<int>(result.meshData.cutoutDistanceVertices.size()) +
            static_cast<int>(result.meshData.transparentVertices.size()) +
            static_cast<int>(result.meshData.waterVertices.size());

        // Hard vertex budget: if this result would push us over, allow at most
        // one over-budget upload then stop for this frame.
        const bool overBudget = m_meshUploadVerticesThisFrame + currentVertices > m_meshingDrainVertexBudget;
        if (overBudget && uploadedCount > 0) {
            break;  // Already uploaded something; defer the rest
        }

        // Count result as processed (whether we upload or discard it)
        ++processIdx;
        ++uploadedCount;

#ifdef MECRAFT_DEBUG
        ++m_meshingCompletedThisFrame;
#endif

        const int64_t flightKey = subChunkFlightKey(result.chunkKey, result.scy);
        m_meshingInFlight.erase(flightKey);

        const auto& activeChunks = world.getActiveChunks();
        const auto it = activeChunks.find(result.chunkKey);
        if (it == activeChunks.end() || !it->second) {
            continue;
        }

        Chunk& chunk = *it->second;
        if (chunk.getSubChunkMeshRevision(result.scy) != result.revision) {
            continue;
        }

#ifdef MECRAFT_DEBUG
        m_meshingBuildMsThisFrame += result.meshData.buildTimeMs;
        m_lastMeshingBuildMs = result.meshData.buildTimeMs;
        m_lastOpaqueFacesBeforeGreedy = result.meshData.opaqueFaceCountBeforeGreedy;
        m_lastOpaqueFacesAfterGreedy = result.meshData.opaqueFaceCountAfterGreedy;
        m_lastTransparentFacesBeforeGreedy = result.meshData.transparentFaceCountBeforeGreedy;
        m_lastTransparentFacesAfterGreedy = result.meshData.transparentFaceCountAfterGreedy;
        m_lastOpaqueVertexCount = result.meshData.opaqueVertexCount;
#endif

        // Upload per-sub-chunk mesh and refresh column-level aggregate for opaque/cutout.
        SubChunkMesh mesh;

        const glm::ivec3 worldOff = chunk.getWorldOffset();
        const float txOff = static_cast<float>(worldOff.x);
        const float tyOff = static_cast<float>(worldOff.y);
        const float tzOff = static_cast<float>(worldOff.z);
        const float scyYOff = static_cast<float>(result.scy * SubChunk::SIZE);

        auto bakeWorldOffset = [&](std::vector<BlockVertex>& verts) {
            for (BlockVertex& v : verts) {
                v.x += txOff;
                v.y += tyOff + scyYOff;
                v.z += tzOff;
            }
        };

        if (m_useMultiDrawIndirect) {
            // MDI path: bake world offset and upload to global buffer pool.
            const bool hasOpaqueOrCutout =
                !result.meshData.opaqueVertices.empty() ||
                !result.meshData.cutoutVertices.empty() ||
                !result.meshData.cutoutDistanceVertices.empty();
            std::vector<BlockVertex> opaqueVerts = std::move(result.meshData.opaqueVertices);
            std::vector<BlockVertex> cutoutVerts = std::move(result.meshData.cutoutVertices);
            std::vector<BlockVertex> cutoutDistanceVerts = std::move(result.meshData.cutoutDistanceVertices);
            std::vector<BlockVertex> transparentVerts = std::move(result.meshData.transparentVertices);
            std::vector<BlockVertex> waterVerts = std::move(result.meshData.waterVertices);
            bakeWorldOffset(opaqueVerts);
            bakeWorldOffset(cutoutVerts);
            bakeWorldOffset(cutoutDistanceVerts);
            bakeWorldOffset(transparentVerts);
            bakeWorldOffset(waterVerts);

            const glm::vec3 boundsWorldOffset(txOff, tyOff, tzOff);
            WorldGpuMesh gpu = m_worldRenderBuffer.uploadSubChunk(
                opaqueVerts, cutoutVerts, cutoutDistanceVerts, transparentVerts, waterVerts,
                result.meshData.hasBounds,
                result.meshData.hasBounds ? result.meshData.boundsMin + boundsWorldOffset : glm::vec3(0.0f),
                result.meshData.hasBounds ? result.meshData.boundsMax + boundsWorldOffset : glm::vec3(0.0f));
            if ((!opaqueVerts.empty() && gpu.opaque.vertexCount == 0) ||
                (!cutoutVerts.empty() && gpu.cutout.vertexCount == 0) ||
                (!cutoutDistanceVerts.empty() && gpu.cutoutDistance.vertexCount == 0) ||
                (!transparentVerts.empty() && gpu.transparent.vertexCount == 0) ||
                (!waterVerts.empty() && gpu.water.vertexCount == 0)) {
                continue;
            }

            mesh.opaqueRange = gpu.opaque;
            mesh.cutoutRange = gpu.cutout;
            mesh.cutoutDistanceRange = gpu.cutoutDistance;
            mesh.transparentRange = gpu.transparent;
            mesh.waterRange = gpu.water;
            mesh.vertexCount = gpu.opaque.vertexCount;
            mesh.cutoutVertexCount = gpu.cutout.vertexCount;
            mesh.cutoutDistanceVertexCount = gpu.cutoutDistance.vertexCount;
            mesh.transparentVertexCount = gpu.transparent.vertexCount;
            mesh.waterVertexCount = gpu.water.vertexCount;
            mesh.hasBounds = result.meshData.hasBounds;
            mesh.boundsMin = gpu.boundsMin;
            mesh.boundsMax = gpu.boundsMax;
            mesh.inGlobalPool = true;

            const SubChunkGpuKey gpuKey{result.chunkKey, result.scy};
            releaseMdiAllocation(gpuKey);
            m_mdiMeshAllocations[gpuKey] = MdiMeshAllocation{gpu};
            chunk.setSubChunkMesh(result.scy, mesh);

            // MDI mode only needs column bounds for hierarchical frustum culling.
            chunk.updateColumnAggregateBoundsOnly(result.scy, result.meshData, hasOpaqueOrCutout);
        } else {
            // Old path: per-mesh VAOs.
            mesh.upload(result.meshData.opaqueVertices);
            mesh.uploadCutout(result.meshData.cutoutVertices);
            mesh.uploadCutoutDistance(result.meshData.cutoutDistanceVertices);

            std::vector<BlockVertex> transparentVerts = result.meshData.transparentVertices;
            transparentVerts.insert(transparentVerts.end(), result.meshData.waterVertices.begin(), result.meshData.waterVertices.end());
            bakeWorldOffset(transparentVerts);
            mesh.uploadTransparent(transparentVerts);

            mesh.hasBounds = result.meshData.hasBounds;
            mesh.boundsMin = result.meshData.boundsMin;
            mesh.boundsMax = result.meshData.boundsMax;
            chunk.setSubChunkMesh(result.scy, mesh);
            chunk.updateColumnAggregateData(result.scy, result.meshData);
        }

        m_meshUploadVerticesThisFrame += currentVertices;
        m_meshUploadBytesThisFrame += static_cast<size_t>(currentVertices) * sizeof(BlockVertex);

        if (overBudget) {
            break;  // Allow one over-budget upload, then stop
        }
    }

    // Record upload time
    m_worldBufferUploadMsThisFrame = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - drainStartTime).count();

    // Record pool expand count
    m_worldBufferExpandCountThisFrame =
        m_worldRenderBuffer.opaqueExpandCount() +
        m_worldRenderBuffer.cutoutExpandCount() +
        m_worldRenderBuffer.transparentExpandCount();

    // Remove processed results, keep deferred ones
    if (processIdx > 0) {
        m_deferredMeshResults.erase(
            m_deferredMeshResults.begin(),
            m_deferredMeshResults.begin() + static_cast<ptrdiff_t>(processIdx));
    }

    m_meshUploadDeferredCount = m_deferredMeshResults.size();
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
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled || !m_gpuTimerCanIssueThisFrame || m_gpuTimerActive) {
        return;
    }

    const size_t passIndex = static_cast<size_t>(pass);
    glBeginQuery(GL_TIME_ELAPSED, m_gpuTimerQueries[m_gpuTimerWriteIndex][passIndex]);
    m_gpuTimerActive = true;
    m_activeGpuTimerPass = pass;
}

void Renderer::endGpuTimer(const GpuTimerPass pass) {
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
