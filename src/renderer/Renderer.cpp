//
// Created by Caiwe on 2026/3/21.
//

#include "Renderer.h"

#include "ChunkMesher.h"
#include "../core/Time.h"
#include "../world/World.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <utility>
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

constexpr float kWindStrength = 0.06f;
constexpr float kWindSpeed = 1.8f;
constexpr float kWindSpatialFreq = 0.22f;
// Current world block animations loop on 32-frame strips at 6 fps and 8 fps.
// 16 seconds is a common multiple of both animation periods, so wrapping here
// preserves seamless looping while keeping the shader time uniform in a stable range.
constexpr double kWorldAnimationLoopSeconds = 16.0;

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
}

Renderer::~Renderer() {
    shutdown();
}

void Renderer::init(ResourceMgr &resourceMgr) {
    m_resourceMgr = &resourceMgr;
    m_chunkShader = resourceMgr.getShader("chunk_lit");
    if (m_chunkShader == nullptr) {
        m_chunkShader = resourceMgr.getShader("chunk_lit");
    }
    //m_uiShader = resourceMgr.getShader("ui");
    m_outlineShader = resourceMgr.getShader("outline");
    m_breakOverlayShader = resourceMgr.getShader("break_overlay");
    initOutlineMesh();
    initBreakOverlayMesh();
    m_threadPool.start();
    if (!m_meshingSubmitBudgetOverridden) {
        const int workerCount = std::max(1, m_threadPool.numWorkers());
        m_meshingSubmitBudget = 2 + std::max(0, workerCount - 1);
        m_meshingMaxInFlight = std::max(4, workerCount * 2);
#ifdef NDEBUG
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
    m_meshingService.shutdown();
    m_threadPool.shutdown();
    m_meshingInFlight.clear();
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

void Renderer::render(const World& world, const Camera &camera, const Window &window, const BlockTargetRenderData& target, const BlockBreakRenderData& blockBreak) {
    renderOpaqueAndCutout(world, camera, window);
    renderTransparentAndOverlays(world, target, blockBreak, window);
}

void Renderer::renderOpaqueAndCutout(const World& world, const Camera& camera, const Window& window) {
    beginFrame(camera, window);
    renderWorld(world);
}

void Renderer::renderTransparentAndOverlays(const World& world, const BlockTargetRenderData& target, const BlockBreakRenderData& blockBreak, const Window& window) {
    if (m_chunkShader != nullptr && m_resourceMgr != nullptr) {
        const TextureArray& texArray = m_resourceMgr->getTextureArray();
        bindChunkRenderState(world, texArray);
        renderTransparentChunks(m_deferredTransparentEntries);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }

    renderBlockBreakOverlay(world, blockBreak);
    renderBlockOutline(target);
    endFrame(window);
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

void Renderer::setFogAutoStartOffsetChunks(const float offsetChunks) {
    m_fogSettings.autoStartOffsetChunks = std::clamp(offsetChunks, -1.5f, 1.5f);
}

void Renderer::setFogAutoFadeWidthChunks(const float fadeWidthChunks) {
    m_fogSettings.autoFadeWidthChunks = std::clamp(fadeWidthChunks, 0.25f, 4.0f);
}

Renderer::FogSettings Renderer::getFogSettings() const {
    return m_fogSettings;
}

void Renderer::setDebugLightMode(const int mode) {
    m_debugLightMode = std::clamp(mode, 0, 3);
}

int Renderer::getDebugLightMode() const {
    return m_debugLightMode;
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

#ifndef NDEBUG
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
    glClearColor(m_fogSettings.color.r, m_fogSettings.color.g, m_fogSettings.color.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_projection = camera.getProjectionMatrix(window.getAspectRatio());
    m_view = camera.getViewMatrix();
    m_cameraPos = camera.getPosition();
    updateFrustum(m_projection * m_view);
    drawCallCount = 0;

#ifndef NDEBUG
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
#endif
}

void Renderer::renderWorld(const World& world) {
    if (m_chunkShader == nullptr || m_resourceMgr == nullptr) {
        m_deferredTransparentEntries.clear();
        return;
    }

    drainMeshingResults(world);

    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    bindChunkRenderState(world, texArray);
    submitMeshingJobs(world);

    std::vector<ChunkRenderEntry> cutoutEntries;
    cutoutEntries.reserve(world.getActiveChunks().size() * 2);
    m_deferredTransparentEntries.clear();
    m_deferredTransparentEntries.reserve(world.getActiveChunks().size() * 2);
    renderOpaqueChunksAndCollectPasses(world, cutoutEntries, m_deferredTransparentEntries);
    renderCutoutChunks(cutoutEntries);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void Renderer::bindChunkRenderState(const World& world, const TextureArray& texArray) const {

    float fogStart = m_fogSettings.startDistance;
    float fogEnd = m_fogSettings.endDistance;
    if (m_fogSettings.autoDistanceByRenderDistance) {
        const float chunkSize = static_cast<float>(Chunk::SIZE_X);
        const float renderDistanceChunks = static_cast<float>(std::max(1, world.getRenderDistance()));
        fogStart = std::max(0.0f, (renderDistanceChunks + m_fogSettings.autoStartOffsetChunks) * chunkSize);
        fogEnd = fogStart + m_fogSettings.autoFadeWidthChunks * chunkSize;
    }
    fogEnd = std::max(fogEnd, fogStart + 0.1f);

    m_chunkShader->use();
    m_chunkShader->setMat4("view", m_view);
    m_chunkShader->setMat4("viewProj", m_projection * m_view);
    m_chunkShader->setInt("texArray", 0);
    m_chunkShader->setInt("uLightmapDay", 1);
    m_chunkShader->setInt("uLightmapNight", 2);
    m_chunkShader->setVec3("uGrassTintColor", glm::vec3(0.50f, 0.78f, 0.34f));
    m_chunkShader->setInt("uForceBaseLod", 0);
    m_chunkShader->setInt("uFogEnabled", m_fogSettings.enabled ? 1 : 0);
    m_chunkShader->setInt("uFogMode", static_cast<int>(m_fogSettings.mode));
    m_chunkShader->setVec3("uFogColor", m_fogSettings.color);
    m_chunkShader->setFloat("uFogStart", fogStart);
    m_chunkShader->setFloat("uFogEnd", fogEnd);
    m_chunkShader->setFloat("uFogDensity", m_fogSettings.density);
    m_chunkShader->setFloat("uWindTime", static_cast<float>(Time::getGameTime()));
    m_chunkShader->setFloat("uAnimationTime", static_cast<float>(std::fmod(Time::getGameTime(), kWorldAnimationLoopSeconds)));
    m_chunkShader->setFloat("uWindStrength", kWindStrength);
    m_chunkShader->setFloat("uWindSpeed", kWindSpeed);
    m_chunkShader->setFloat("uWindSpatialFreq", kWindSpatialFreq);
    m_chunkShader->setInt("uDebugLightMode", m_debugLightMode);
    m_chunkShader->setFloat("uSkyIntensity", world.getDayNightSystem().getSkyIntensity());

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texArray.textureID);

    // Bind lightmap textures
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getLightmapDay());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getLightmapNight());
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
#ifndef NDEBUG
        ++m_meshingSubmittedThisFrame;
#endif
    }
}

void Renderer::renderOpaqueChunksAndCollectPasses(const World& world,
                                                  std::vector<ChunkRenderEntry>& cutoutEntries,
                                                  std::vector<ChunkRenderEntry>& transparentEntries) {
    syncChunkRenderColumns(world);
    if (m_chunkRenderColumns.empty()) {
        return;
    }

    const int modelLoc = m_chunkShader->getUniformLocation("model");

    GLuint lastOpaqueVao = 0;

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

#ifndef NDEBUG
        ++m_regionTestsThisFrame;
        FrustumPlane culledPlane = FrustumPlane::Count;
        if (!isChunkInFrustum(regionMin, regionMax, m_chunkCullingDebugEnabled ? &culledPlane : nullptr)) {
            if (m_chunkCullingDebugEnabled) {
                recordChunkCull(culledPlane, regionCandidateCount);
            }
            regionBegin = regionEnd;
            continue;
        }
        ++m_regionPassedThisFrame;
#else
        if (!isChunkInFrustum(regionMin, regionMax)) {
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

#ifndef NDEBUG
            ++m_columnTestsThisFrame;
            FrustumPlane culledPlane = FrustumPlane::Count;
            if (!isChunkInFrustum(column.columnBoundsMin, column.columnBoundsMax,
                                  m_chunkCullingDebugEnabled ? &culledPlane : nullptr)) {
                if (m_chunkCullingDebugEnabled) {
                    recordChunkCull(culledPlane, columnCandidateCount);
                }
                continue;
            }
            ++m_columnPassedThisFrame;
#else
            if (!isChunkInFrustum(column.columnBoundsMin, column.columnBoundsMax)) {
                continue;
            }
#endif

            if (column.aggregatedPresent) {
#ifndef NDEBUG
                ++m_chunkTestsThisFrame;
                if (!isChunkInFrustum(column.aggregatedBoundsMin, column.aggregatedBoundsMax,
                                      m_chunkCullingDebugEnabled ? &culledPlane : nullptr)) {
                    if (m_chunkCullingDebugEnabled) {
                        recordChunkCull(culledPlane, 1);
                    }
                } else {
                    ++m_chunkPassedThisFrame;
#else
                if (isChunkInFrustum(column.aggregatedBoundsMin, column.aggregatedBoundsMax)) {
#endif
                    const SubChunkMesh& mesh = column.chunk->getColumnMesh();
                    glm::mat4 model(1.0f);
                    model = glm::translate(model, column.worldOffset);
                    m_chunkShader->setMat4(modelLoc, model);

                    if (column.aggregatedHasOpaque && mesh.vertexCount > 0) {
                        if (lastOpaqueVao != mesh.vao) {
                            glBindVertexArray(mesh.vao);
                            lastOpaqueVao = mesh.vao;
                        }
                        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.vertexCount));
                        ++drawCallCount;
                    }

                    if (column.aggregatedHasCutout && mesh.cutoutVertexCount > 0) {
                        cutoutEntries.push_back({column.chunk, -1, true});
                    }
                }
            }

            for (int transparentIndex = 0; transparentIndex < column.transparentCount; ++transparentIndex) {
                const int scy = column.transparentScys[transparentIndex];
                const TransparentSubChunkCache& transparent = column.transparentSubChunks[scy];

#ifndef NDEBUG
                ++m_chunkTestsThisFrame;
                if (!isChunkInFrustum(transparent.boundsMin, transparent.boundsMax,
                                      m_chunkCullingDebugEnabled ? &culledPlane : nullptr)) {
                    if (m_chunkCullingDebugEnabled) {
                        recordChunkCull(culledPlane, 1);
                    }
                    continue;
                }
                ++m_chunkPassedThisFrame;
#else
                if (!isChunkInFrustum(transparent.boundsMin, transparent.boundsMax)) {
                    continue;
                }
#endif

                transparentEntries.push_back({column.chunk, scy, false});
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
    column.aggregatedHasCutout = columnMesh.cutoutVertexCount > 0;
    column.aggregatedPresent = column.aggregatedHasOpaque || column.aggregatedHasCutout;

    if (column.aggregatedPresent) {
        column.aggregatedBoundsMin = columnMesh.hasBounds
            ? columnMesh.boundsMin + column.worldOffset
            : column.worldOffset;
        column.aggregatedBoundsMax = columnMesh.hasBounds
            ? columnMesh.boundsMax + column.worldOffset
            : column.worldOffset + glm::vec3(Chunk::SIZE_X, Chunk::SIZE_Y, Chunk::SIZE_Z);
    } else {
        column.aggregatedBoundsMin = glm::vec3(0.0f);
        column.aggregatedBoundsMax = glm::vec3(0.0f);
    }

    bool columnHasBounds = false;
    glm::vec3 columnMin(0.0f);
    glm::vec3 columnMax(0.0f);
    if (column.aggregatedPresent) {
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
        if (mesh.transparentVertexCount == 0) {
            continue;
        }

        const int yBase = scy * SubChunk::SIZE;
        TransparentSubChunkCache& transparent = column.transparentSubChunks[scy];
        transparent.boundsMin = mesh.hasBounds
            ? mesh.boundsMin + column.worldOffset
            : column.worldOffset + glm::vec3(0.0f, static_cast<float>(yBase), 0.0f);
        transparent.boundsMax = mesh.hasBounds
            ? mesh.boundsMax + column.worldOffset
            : column.worldOffset + glm::vec3(Chunk::SIZE_X, static_cast<float>(yBase + SubChunk::SIZE), Chunk::SIZE_Z);

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
    if (cutoutEntries.empty()) {
        return;
    }

    const int modelLoc = m_chunkShader->getUniformLocation("model");

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    m_chunkShader->setInt("uForceBaseLod", 1);

    for (const ChunkRenderEntry& entry : cutoutEntries) {
        if (entry.chunk == nullptr) continue;

        const SubChunkMesh* mesh = nullptr;
        int yBase = 0;
        if (entry.aggregated) {
            mesh = &entry.chunk->getColumnMesh();
        } else {
            const SubChunk* sc = entry.chunk->getSubChunk(entry.scy);
            if (!sc) continue;
            mesh = &sc->getMesh();
            yBase = entry.scy * SubChunk::SIZE;
        }
        if (mesh->cutoutVertexCount == 0) continue;

        glm::mat4 model(1.0f);
        const glm::ivec3 offset = entry.chunk->getWorldOffset();
        model = glm::translate(model, glm::vec3(offset.x, offset.y + yBase, offset.z));
        m_chunkShader->setMat4(modelLoc, model);

        glBindVertexArray(mesh->cutoutVao);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->cutoutVertexCount));
        ++drawCallCount;
    }

    m_chunkShader->setInt("uForceBaseLod", 0);
}


void Renderer::renderTransparentChunks(const std::vector<ChunkRenderEntry>& transparentEntries) {
    if (transparentEntries.empty()) {
        return;
    }

    const int modelLoc = m_chunkShader->getUniformLocation("model");

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

    for (const TransparentSubChunkItem& item : items) {
        const ChunkRenderEntry& entry = *item.entry;
        const SubChunk* sc = entry.chunk->getSubChunk(entry.scy);
        if (!sc) continue;
        const SubChunkMesh& mesh = sc->getMesh();
        if (mesh.transparentVertexCount == 0) continue;

        glm::mat4 model(1.0f);
        const glm::ivec3 offset = entry.chunk->getWorldOffset();
        const int yBase = entry.scy * SubChunk::SIZE;
        model = glm::translate(model, glm::vec3(offset.x, offset.y + yBase, offset.z));
        m_chunkShader->setMat4(modelLoc, model);

        glBindVertexArray(mesh.transparentVao);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.transparentVertexCount));
        ++drawCallCount;
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    m_chunkShader->setInt("uForceBaseLod", 0);
}


void Renderer::endFrame(const Window &window) {
    recordMeshingHistory();
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

void Renderer::renderBlockOutline(const BlockTargetRenderData& target) {
    if (m_outlineShader == nullptr || m_outlineVao == 0 || !target.hasTarget) {
        return;
    }

    const glm::ivec3 targetBlock = target.targetBlock;

    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3(targetBlock) + glm::vec3(0.5f));
    model = glm::scale(model, glm::vec3(1.002f));
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

    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3(target) + glm::vec3(0.5f));
    model = glm::scale(model, glm::vec3(1.001f));
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
#ifndef NDEBUG
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
    const auto drainStartTime = std::chrono::steady_clock::now();
    int drainedCount = 0;

    auto subChunkFlightKey = [](int64_t chunkKey, int scy) -> int64_t {
        return (chunkKey & 0x00FFFFFFFFFFFFFFLL) | (static_cast<int64_t>(scy) << 56);
    };

    SubChunkMeshingResult result;
    while (drainedCount < m_meshingDrainBudget) {
        const double elapsedMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - drainStartTime).count();
        if (elapsedMs >= m_meshingDrainTimeBudgetMs) {
            break;
        }
        if (!m_meshingService.tryPopCompleted(result)) {
            break;
        }

#ifndef NDEBUG
        ++m_meshingCompletedThisFrame;
#endif
        ++drainedCount;
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

#ifndef NDEBUG
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
        mesh.upload(result.meshData.opaqueVertices);
        mesh.uploadCutout(result.meshData.cutoutVertices);
        mesh.uploadTransparent(result.meshData.transparentVertices);
        mesh.hasBounds = result.meshData.hasBounds;
        mesh.boundsMin = result.meshData.boundsMin;
        mesh.boundsMax = result.meshData.boundsMax;
        chunk.setSubChunkMesh(result.scy, mesh);
        chunk.updateColumnAggregateData(result.scy, result.meshData);

    }
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

#ifndef NDEBUG
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
#ifndef NDEBUG
            if (culledPlane != nullptr) {
                *culledPlane = kPlaneFromIndex(static_cast<size_t>(&plane - m_frustumPlanes.data()));
            }
#endif
            return false;
        }
    }

#ifndef NDEBUG
    if (culledPlane != nullptr) {
        *culledPlane = FrustumPlane::Count;
    }
#endif

    return true;
}

int Renderer::getDrawCallCount() const {
    return drawCallCount;
}
