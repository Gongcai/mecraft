#include "TerrainRenderer.h"
#include "TerrainRenderCache.h"
#include "WorldRenderBuffer.h"
#include "../core/Shader.h"
#include "../gl/GlStateGuard.h"
#include "../shadow/ShadowCasterCuller.h"
#include "../targets/DeferredRenderTargets.h"
#include "../../world/IWorldView.h"
#include "../../world/chunk/Chunk.h"
#include "../../resource/ResourceMgr.h"
#include <algorithm>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

void TerrainRenderer::init(ResourceMgr& /*resourceMgr*/) {
    // No owned resources to initialize; dependencies are injected via setters.
}

void TerrainRenderer::shutdown() {
    m_worldRenderBuffer = nullptr;
    m_terrainCache = nullptr;
}

// ============================================================================
// Frustum management
// ============================================================================

void TerrainRenderer::updateFrustum(const glm::mat4& viewProj) {
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
            m_frustumPlanes[i].normal = n / length;
            m_frustumPlanes[i].distance = rawPlanes[i].w / length;
        } else {
            m_frustumPlanes[i].normal = glm::vec3(0.0f);
            m_frustumPlanes[i].distance = 0.0f;
        }
    }
}

bool TerrainRenderer::isChunkInFrustum(const glm::vec3& chunkMin, const glm::vec3& chunkMax,
                                        FrustumPlane* culledPlane) const {
    for (const Plane& plane : m_frustumPlanes) {
        const glm::vec3 positive(
            plane.normal.x >= 0.0f ? chunkMax.x : chunkMin.x,
            plane.normal.y >= 0.0f ? chunkMax.y : chunkMin.y,
            plane.normal.z >= 0.0f ? chunkMax.z : chunkMin.z
        );

        if (glm::dot(plane.normal, positive) + plane.distance < 0.0f) {
            if (culledPlane != nullptr) {
                *culledPlane = kPlaneFromIndex(static_cast<size_t>(&plane - m_frustumPlanes.data()));
            }
            return false;
        }
    }

    if (culledPlane != nullptr) {
        *culledPlane = FrustumPlane::Count;
    }

    return true;
}

void TerrainRenderer::recordChunkCull(const FrustumPlane plane, const int count) {
    if (!m_chunkCullingDebugEnabled || count <= 0) {
        return;
    }

    m_chunkCulledThisFrame += count;
    const size_t planeIndex = static_cast<size_t>(plane);
    if (planeIndex < m_chunkCulledByPlaneThisFrame.size()) {
        m_chunkCulledByPlaneThisFrame[planeIndex] += count;
    }
}

void TerrainRenderer::expandBounds(glm::vec3& minBounds, glm::vec3& maxBounds, bool& hasBounds,
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

// ============================================================================
// Debug counters
// ============================================================================

void TerrainRenderer::resetDebugCounters() {
    m_drawCallCount = 0;
    m_regionTestsThisFrame = 0;
    m_regionPassedThisFrame = 0;
    m_columnTestsThisFrame = 0;
    m_columnPassedThisFrame = 0;
    m_chunkTestsThisFrame = 0;
    m_chunkPassedThisFrame = 0;
    m_chunkCulledThisFrame = 0;
    m_cutoutCandidatesThisFrame = 0;
    m_cutoutSkippedByDistanceThisFrame = 0;
    m_mdiSubChunkTestsThisFrame = 0;
    m_mdiSubChunksCulledThisFrame = 0;
    m_chunkCulledByPlaneThisFrame.fill(0);
}

// ============================================================================
// Inline shader binding helpers (extracted from Renderer)
// ============================================================================

void TerrainRenderer::bindFogUniforms(Shader& shader, const TerrainFogData& fog) {
    shader.setInt("uFogEnabled", fog.enabled ? 1 : 0);
    shader.setInt("uFogMode", fog.mode);
    shader.setVec3("uFogColor", fog.color);
    shader.setFloat("uFogStart", fog.start);
    shader.setFloat("uFogEnd", fog.end);
    shader.setFloat("uFogDensity", fog.density);
}

void TerrainRenderer::bindSkyLightingUniforms(Shader& shader, const TerrainSkyLightingData& sky,
                                               const glm::vec3& cameraPos, int heldBlockLightValue) {
    shader.setVec3("uCameraPos", cameraPos);
    shader.setVec3("uSunDirection", sky.sunDirection);
    shader.setVec3("uMoonDirection", sky.moonDirection);
    shader.setVec3("uSunLightColor", sky.sunLightColor);
    shader.setVec3("uMoonLightColor", sky.moonLightColor);
    shader.setVec3("uSkyAmbientColor", sky.skyAmbientColor);
    shader.setVec3("uShadowTintColor", sky.shadowTintColor);
    shader.setVec3("uHorizonScatterColor", sky.horizonScatterColor);
    shader.setFloat("uSkyIntensity", sky.skyIntensity);
    shader.setFloat("uMoonVisibility", sky.moonVisibility);
    shader.setVec3("uDirectIlluminance", sky.directIlluminance);
    shader.setVec3("uSkyIlluminance", sky.skyIlluminance);
    shader.setVec3("uSunIlluminance", sky.sunIlluminance);
    shader.setVec3("uMoonIlluminance", sky.moonIlluminance);
    shader.setVec3("uCloudDynamicWeather", sky.cloudDynamicWeather);
    shader.setInt("uHeldBlockLightValue", heldBlockLightValue);
    shader.setInt("uHeldBlockLightValue2", 0); // Off-hand slot (unused until dual-wield)
}

void TerrainRenderer::bindAtmosphereUniforms(Shader& shader, const TerrainAtmosphereData& atm) {
    shader.setFloat("uAerialStrength", atm.aerialStrength);
    shader.setFloat("uHorizonScatterStrength", atm.horizonScatterStrength);
    shader.setFloat("uSunWarmth", atm.sunWarmth);
    shader.setFloat("uSkyCoolness", atm.skyCoolness);
    shader.setFloat("uWeatherWetness", atm.weatherWetness);
    shader.setFloat("uWeatherStorm", atm.weatherStorm);
    shader.setFloat("uAerialReduction", atm.aerialReduction);
    shader.setFloat("uLightningFlash", atm.lightningFlash);
    shader.setFloat("uSurfaceWetness", atm.surfaceWetness);
    shader.setFloat("uSkyWetness", atm.skyWetness);
    shader.setFloat("uFogWetness", atm.fogWetness);
    shader.setFloat("uCloudWetness", atm.cloudWetness);
    shader.setFloat("uPrecipitation", atm.precipitation);
    shader.setFloat("uDirectWeatherOcclusion", atm.directWeatherOcclusion);
    shader.setInt("uDirectWeatherOcclusionOverride", atm.directWeatherOcclusionOverride);
}

void TerrainRenderer::bindWaterEffectUniforms(Shader& shader, bool enabled, ResourceMgr* resourceMgr) {
    shader.setInt("uWaterEffectsEnabled", enabled ? 1 : 0);
    if (resourceMgr == nullptr) {
        shader.setFloat("uWaterStillFirstLayer", 0.0f);
        shader.setFloat("uWaterStillLayerCount", 0.0f);
        shader.setFloat("uWaterFlowFirstLayer", 0.0f);
        shader.setFloat("uWaterFlowLayerCount", 0.0f);
        return;
    }

    const TextureAnimationInfo still = resourceMgr->getTextureAnimation("water_still");
    const TextureAnimationInfo flow = resourceMgr->getTextureAnimation("water_flow");
    shader.setFloat("uWaterStillFirstLayer", static_cast<float>(still.firstLayer));
    shader.setFloat("uWaterStillLayerCount", static_cast<float>(std::max(1, still.frameCount)));
    shader.setFloat("uWaterFlowFirstLayer", static_cast<float>(flow.firstLayer));
    shader.setFloat("uWaterFlowLayerCount", static_cast<float>(std::max(1, flow.frameCount)));
}

// ============================================================================
// Chunk render state binding
// ============================================================================

void TerrainRenderer::bindChunkRenderState(const TerrainFrameData& frame, const TextureArray& texArray,
                                            Shader& shader, bool deferredFrameActive, int debugLightMode,
                                            bool /*eyeInWater*/, int heldBlockLightValue,
                                            DeferredRenderTargets& targets, ResourceMgr* resourceMgr,
                                            bool volumetricFogShadersReady,
                                            const TerrainRenderSettings& settings) {
    shader.use();
    shader.setMat4("view", frame.view);
    shader.setMat4("viewProj", frame.viewProj);
    shader.setInt("uUseModel", 0);
    shader.setInt("uVertexFormat", 1);
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
    shader.setInt("uSkyCaptureEnabled", deferredFrameActive ? 1 : 0);
    shader.setInt("uCompositeInputsEnabled", 0);
    shader.setInt("uWaterCompositeEnabled", 0);
    shader.setInt("uForceBaseLod", 0);
    shader.setInt("uDepthSofteningEnabled", 0);
    bindFogUniforms(shader, frame.fog);
    shader.setFloat("uAnimationTime", frame.animationTime);
    shader.setFloat("uShaderTime", frame.shaderTime);
    shader.setFloat("uSurfaceWetness", frame.surfaceWetness);
    shader.setInt("uRainWetSurfacesEnabled", settings.rainWetSurfacesEnabled ? 1 : 0);
    shader.setInt("uRainSurfaceRipplesEnabled", settings.rainSurfaceRipplesEnabled ? 1 : 0);
    shader.setInt("uDebugLightMode", debugLightMode);
    bindSkyLightingUniforms(shader, frame.skyLighting, frame.cameraPos, heldBlockLightValue);
    shader.setInt("uAerialPerspectiveEnabled", settings.aerialPerspectiveEnabled ? 1 : 0);
    shader.setInt("uVolumetricLightEnabled", settings.volumetricLightEnabled ? 1 : 0);
    // Aerial perspective mutual exclusion: skip when volumetric fog OR light is active.
    // Matches deferred_lighting.fs condition for consistent behavior across deferred/forward paths.
    const bool volFogActive = (settings.volumetricLightEnabled ||
                               (settings.volumetricFogEnabled &&
                                settings.volumetricFogStrength > 0.001f)) &&
                              volumetricFogShadersReady;
    shader.setInt("uVolumetricFogActive", volFogActive ? 1 : 0);
    shader.setFloat("uDirectSunStrength", settings.directSunStrength);
    shader.setFloat("uSkyAmbientStrength", settings.skyAmbientStrength);
    shader.setFloat("uWeatherSkylightScale", settings.weatherSkylightScale);
    shader.setFloat("uMinimumAmbient", settings.minimumAmbient);
    shader.setFloat("uBlockLightStrength", settings.blockLightStrength);
    shader.setFloat("uFakeBounceStrength", settings.fakeBounceStrength);
    shader.setFloat("uAlbedoDesaturation", settings.albedoDesaturation);
    shader.setFloat("uShadowDesaturation", settings.shadowDesaturation);
    bindAtmosphereUniforms(shader, frame.atmosphere);
    shader.setInt("uAtmosphereLut", 14);
    shader.setVec3("uWaterAbsorption", glm::vec3(1.0f));
    bindWaterEffectUniforms(shader, false, resourceMgr);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texArray.textureID);

    // Bind lightmap textures
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, resourceMgr->getLightmapDay());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, resourceMgr->getLightmapNight());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, resourceMgr->getGrassColormap());
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, resourceMgr->getFoliageColormap());
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, deferredFrameActive ? targets.skyCaptureTexture() : 0);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, resourceMgr != nullptr ? resourceMgr->getTexture2D("shader_noise2d") : 0);
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, resourceMgr != nullptr ? resourceMgr->getTexture2D("shader_ripple_normal") : 0);
    glActiveTexture(GL_TEXTURE14);
    glBindTexture(GL_TEXTURE_3D, targets.atmosphereLutTexture());
}

void TerrainRenderer::bindBasicForwardState(const TerrainFrameData& frame, const TextureArray& texArray,
                                             Shader& shader, bool /*eyeInWater*/, int /*heldBlockLightValue*/,
                                             ResourceMgr* resourceMgr, const TerrainRenderSettings& /*settings*/) {
    shader.use();
    shader.setMat4("view", frame.view);
    shader.setMat4("viewProj", frame.viewProj);
    shader.setInt("uUseModel", 0);
    shader.setInt("uVertexFormat", 1);

    // Texture unit assignments: texArray, lightmap day/night, biome colormap
    shader.setInt("texArray", 0);
    shader.setInt("uLightmapDay", 1);
    shader.setInt("uLightmapNight", 2);
    shader.setInt("uGrassColormap", 3);
    shader.setInt("uFoliageColormap", 4);

    // Control
    shader.setInt("uForceBaseLod", 0);
    shader.setInt("uDebugLightMode", 0);

    // Fog
    bindFogUniforms(shader, frame.fog);

    // Animation + day/night interpolation
    shader.setFloat("uAnimationTime", frame.animationTime);
    shader.setFloat("uSkyIntensity", frame.skyLighting.skyIntensity);

    // Bind textures: units 0-4 only
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texArray.textureID);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, resourceMgr != nullptr ? resourceMgr->getLightmapDay() : 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, resourceMgr != nullptr ? resourceMgr->getLightmapNight() : 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, resourceMgr != nullptr ? resourceMgr->getGrassColormap() : 0);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, resourceMgr != nullptr ? resourceMgr->getFoliageColormap() : 0);
}

// ============================================================================
// Opaque chunk traversal with hierarchical frustum culling
// ============================================================================

void TerrainRenderer::renderOpaqueChunksAndCollectPasses(const IWorldView& worldView,
                                                          std::vector<ChunkRenderEntry>& cutoutEntries,
                                                          std::vector<ChunkRenderEntry>& transparentEntries,
                                                          const bool frustumCull,
                                                          const float maxCameraDistance,
                                                          shadow::ShadowCasterCuller* shadowCuller,
                                                          AabbVisibilityFn extraAabbCuller,
                                                          void* extraAabbCullerUserData) {
    resetDebugCounters();
    m_terrainCache->syncChunkRenderColumns(worldView);
    std::vector<ChunkRenderColumnCache>& chunkRenderColumns = m_terrainCache->chunkRenderColumns();
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

    auto boundsVisibleToExtraCuller = [&](const glm::vec3& boundsMin, const glm::vec3& boundsMax) {
        return extraAabbCuller == nullptr ||
               extraAabbCuller(boundsMin, boundsMax, extraAabbCullerUserData);
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
            m_terrainCache->refreshChunkRenderColumnCache(column);
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
        if (!boundsVisibleToExtraCuller(regionMin, regionMax)) {
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
            if (!boundsVisibleToExtraCuller(column.columnBoundsMin, column.columnBoundsMax)) {
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
                    if (!boundsVisibleToExtraCuller(boundsMin, boundsMax)) {
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
                        m_worldRenderBuffer->addOpaque(mesh.opaqueRange);
                    }
                    if (mesh.cutoutRange.vertexCount > 0) {
                        m_worldRenderBuffer->addCutout(mesh.cutoutRange);
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
                            m_worldRenderBuffer->addCutout(mesh.cutoutDistanceRange);
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
                        m_terrainCache->addTransparentBatch(mesh.transparentRange, glm::dot(toCamera, toCamera), TransparentBatchKind::Generic);
                    }
                    if (mesh.waterRange.vertexCount > 0) {
                        const glm::vec3 sectionCenter(
                            static_cast<float>(offset.x) + Chunk::SIZE_X * 0.5f,
                            static_cast<float>(offset.y + yBase) + SubChunk::SIZE * 0.5f,
                            static_cast<float>(offset.z) + Chunk::SIZE_Z * 0.5f);
                        const glm::vec3 toCamera = sectionCenter - m_cameraPos;
                        m_terrainCache->addTransparentBatch(mesh.waterRange, glm::dot(toCamera, toCamera), TransparentBatchKind::Water);
                    }
                }
            } else {
                // Old path: draw from column aggregate.
                if (column.aggregatedPresent) {
                    if (boundsVisibleToExtraCuller(column.aggregatedBoundsMin, column.aggregatedBoundsMax)) {
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
                            ++m_drawCallCount;
                        }

                        if (column.aggregatedHasCutout &&
                            (mesh.cutoutVertexCount > 0 || mesh.cutoutDistanceVertexCount > 0)) {
                            cutoutEntries.push_back({column.chunk, -1, true});
                        }
                    }
                    }
                }

                for (int transparentIndex = 0; transparentIndex < column.transparentCount; ++transparentIndex) {
                    const int scy = column.transparentScys[transparentIndex];
                    const TransparentSubChunkCache& transparent = column.transparentSubChunks[scy];

                    if (!boundsVisibleToExtraCuller(transparent.boundsMin, transparent.boundsMax)) {
                        continue;
                    }

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

// ============================================================================
// Cutout chunk rendering
// ============================================================================

void TerrainRenderer::renderCutoutChunks(const std::vector<ChunkRenderEntry>& cutoutEntries,
                                          Shader& chunkShader) {
    renderer::gl::ScopedCullFaceDisable cullFaceGuard;

    if (m_useMultiDrawIndirect) {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        chunkShader.setInt("uForceBaseLod", 1);
        m_worldRenderBuffer->flushCutout();
        chunkShader.setInt("uForceBaseLod", 0);
        return;
    }

    if (cutoutEntries.empty()) {
        return;
    }

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    chunkShader.setInt("uForceBaseLod", 1);

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
            ++m_drawCallCount;
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
                ++m_drawCallCount;
            }
        }
    }
    chunkShader.setInt("uForceBaseLod", 0);
}

// ============================================================================
// Transparent batch management
// ============================================================================

void TerrainRenderer::syncTransparentBatches() {
    // Transparent batches are accumulated in TerrainRenderCache during traversal.
    // This method is a no-op in the extracted design — callers read directly from cache.
    // Kept for API symmetry with Renderer.
}

void TerrainRenderer::clearTransparentBatches() {
    m_terrainCache->clearTransparentBatches();
}

const std::vector<DrawBatchEntry>& TerrainRenderer::transparentBatches() const {
    return m_terrainCache->deferredTransparentBatch();
}

const TransparentPassPlan& TerrainRenderer::transparentPassPlan() const {
    return m_terrainCache->transparentPassPlan();
}

const std::vector<ChunkRenderColumnCache>& TerrainRenderer::chunkRenderColumns() const {
    return m_terrainCache->chunkRenderColumns();
}
