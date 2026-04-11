//
// Created by Caiwe on 2026/3/21.
//

#include "Renderer.h"

#include "ChunkMesher.h"
#include "../world/World.h"
#include "../player/Player.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
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

struct TransparentDrawItem {
    Chunk* chunk = nullptr;
    float distanceSq = 0.0f;
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
}

Renderer::~Renderer() {
    shutdown();
}

void Renderer::init(ResourceMgr &resourceMgr) {
    m_resourceMgr = &resourceMgr;
    m_chunkShader = resourceMgr.getShader("chunk");
    //m_uiShader = resourceMgr.getShader("ui");
    m_outlineShader = resourceMgr.getShader("outline");
    m_breakOverlayShader = resourceMgr.getShader("break_overlay");
    initOutlineMesh();
    initBreakOverlayMesh();
    m_meshingService.start();
}

void Renderer::shutdown() {
    m_meshingService.shutdown();
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
}

void Renderer::render(const World& world, const Camera &camera, const Window &window, const Player& player) {
    beginFrame(camera, window);
    renderWorld(world);
    renderBlockBreakOverlay(player);
    renderBlockOutline(player);
    endFrame(window);
}

void Renderer::setMeshingSubmitBudget(const int budget) {
    m_meshingSubmitBudget = std::max(1, budget);
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
        return;
    }

    drainMeshingResults(world);

    const TextureAtlas& atlas = m_resourceMgr->getAtlas();
    bindChunkRenderState(world, atlas);
    submitMeshingJobs(world, atlas);

    std::vector<Chunk*> transparentChunks;
    transparentChunks.reserve(world.getActiveChunks().size());
    renderOpaqueChunksAndCollectTransparent(world, transparentChunks);
    renderTransparentChunks(transparentChunks);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::bindChunkRenderState(const World& world, const TextureAtlas& atlas) const {

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
    m_chunkShader->setMat4("viewProj", m_projection * m_view);
    m_chunkShader->setInt("texAtlas", 0);
    m_chunkShader->setInt("uForceBaseLod", 0);
    m_chunkShader->setInt("uFogEnabled", m_fogSettings.enabled ? 1 : 0);
    m_chunkShader->setInt("uFogMode", static_cast<int>(m_fogSettings.mode));
    m_chunkShader->setVec3("uFogColor", m_fogSettings.color);
    m_chunkShader->setFloat("uFogStart", fogStart);
    m_chunkShader->setFloat("uFogEnd", fogEnd);
    m_chunkShader->setFloat("uFogDensity", m_fogSettings.density);
    m_chunkShader->setVec3("uCameraPos", m_cameraPos);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas.textureID);
}

void Renderer::submitMeshingJobs(const World& world, const TextureAtlas& atlas) {
    int submittedThisPass = 0;
    const auto& activeChunks = world.getActiveChunks();
    for (const auto& pair : activeChunks) {
        const int64_t chunkKey = pair.first;
        Chunk& chunk = *pair.second;

        if (chunk.isDirty() &&
            submittedThisPass < m_meshingSubmitBudget &&
            m_meshingInFlight.find(chunkKey) == m_meshingInFlight.end()) {
            ChunkMeshingJob job;
            job.chunkKey = chunkKey;
            job.revision = chunk.getMeshRevision();
            job.snapshot = ChunkMesher::captureSnapshot(chunk);
            job.atlas = &atlas;
            m_meshingService.submit(job);
            m_meshingInFlight.insert(chunkKey);
            ++submittedThisPass;
#ifndef NDEBUG
            ++m_meshingSubmittedThisFrame;
#endif
        }
    }
}

void Renderer::renderOpaqueChunksAndCollectTransparent(const World& world, std::vector<Chunk*>& transparentChunks) {
    const auto& activeChunks = world.getActiveChunks();
    const int regionChunkSize = std::max(1, m_regionChunkSize);
    const int modelLoc = m_chunkShader->getUniformLocation("model");

    m_chunkRenderEntries.clear();
    m_chunkRenderEntries.reserve(activeChunks.size());

    for (const auto& pair : activeChunks) {
        Chunk& chunk = *pair.second;
        const ChunkMesh& mesh = chunk.getMesh();
        if (mesh.vertexCount == 0 && mesh.transparentVertexCount == 0) {
            continue;
        }

        const glm::ivec3 offset = chunk.getWorldOffset();
        const glm::vec3 chunkMin = mesh.hasBounds
            ? mesh.boundsMin + glm::vec3(offset)
            : glm::vec3(offset);
        const glm::vec3 chunkMax = mesh.hasBounds
            ? mesh.boundsMax + glm::vec3(offset)
            : glm::vec3(offset) + glm::vec3(static_cast<float>(Chunk::SIZE_X), static_cast<float>(Chunk::SIZE_Y), static_cast<float>(Chunk::SIZE_Z));

        ChunkRenderEntry entry;
        entry.chunk = &chunk;
        entry.regionX = floorDiv(chunk.m_chunkX, regionChunkSize);
        entry.regionZ = floorDiv(chunk.m_chunkZ, regionChunkSize);
        entry.chunkX = chunk.m_chunkX;
        entry.chunkZ = chunk.m_chunkZ;
        entry.boundsMin = chunkMin;
        entry.boundsMax = chunkMax;
        m_chunkRenderEntries.push_back(entry);
    }

    if (m_chunkRenderEntries.empty()) {
        return;
    }

    std::sort(m_chunkRenderEntries.begin(), m_chunkRenderEntries.end(),
              [](const ChunkRenderEntry& a, const ChunkRenderEntry& b) {
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

    GLuint lastOpaqueVao = 0;

    size_t regionBegin = 0;
    while (regionBegin < m_chunkRenderEntries.size()) {
        size_t regionEnd = regionBegin + 1;
        const ChunkRenderEntry& regionFirst = m_chunkRenderEntries[regionBegin];
        while (regionEnd < m_chunkRenderEntries.size()) {
            const ChunkRenderEntry& candidate = m_chunkRenderEntries[regionEnd];
            if (candidate.regionX != regionFirst.regionX || candidate.regionZ != regionFirst.regionZ) {
                break;
            }
            ++regionEnd;
        }

        bool regionHasBounds = false;
        glm::vec3 regionMin(0.0f);
        glm::vec3 regionMax(0.0f);
        for (size_t i = regionBegin; i < regionEnd; ++i) {
            const ChunkRenderEntry& entry = m_chunkRenderEntries[i];
            expandBounds(regionMin, regionMax, regionHasBounds, entry.boundsMin, entry.boundsMax);
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
                recordChunkCull(culledPlane, static_cast<int>(regionEnd - regionBegin));
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

        size_t columnBegin = regionBegin;
        while (columnBegin < regionEnd) {
            size_t columnEnd = columnBegin + 1;
            const ChunkRenderEntry& columnFirst = m_chunkRenderEntries[columnBegin];
            while (columnEnd < regionEnd) {
                const ChunkRenderEntry& candidate = m_chunkRenderEntries[columnEnd];
                if (candidate.chunkX != columnFirst.chunkX || candidate.chunkZ != columnFirst.chunkZ) {
                    break;
                }
                ++columnEnd;
            }

            bool columnHasBounds = false;
            glm::vec3 columnMin(0.0f);
            glm::vec3 columnMax(0.0f);
            for (size_t i = columnBegin; i < columnEnd; ++i) {
                const ChunkRenderEntry& entry = m_chunkRenderEntries[i];
                expandBounds(columnMin, columnMax, columnHasBounds, entry.boundsMin, entry.boundsMax);
            }

            if (!columnHasBounds) {
                columnBegin = columnEnd;
                continue;
            }

#ifndef NDEBUG
            ++m_columnTestsThisFrame;
            if (!isChunkInFrustum(columnMin, columnMax, m_chunkCullingDebugEnabled ? &culledPlane : nullptr)) {
                if (m_chunkCullingDebugEnabled) {
                    recordChunkCull(culledPlane, static_cast<int>(columnEnd - columnBegin));
                }
                columnBegin = columnEnd;
                continue;
            }
            ++m_columnPassedThisFrame;
#else
            if (!isChunkInFrustum(columnMin, columnMax)) {
                columnBegin = columnEnd;
                continue;
            }
#endif

            for (size_t i = columnBegin; i < columnEnd; ++i) {
                const ChunkRenderEntry& entry = m_chunkRenderEntries[i];
                if (entry.chunk == nullptr) {
                    continue;
                }

                const ChunkMesh& mesh = entry.chunk->getMesh();

#ifndef NDEBUG
                ++m_chunkTestsThisFrame;
                if (!isChunkInFrustum(entry.boundsMin, entry.boundsMax, m_chunkCullingDebugEnabled ? &culledPlane : nullptr)) {
                    if (m_chunkCullingDebugEnabled) {
                        recordChunkCull(culledPlane, 1);
                    }
                    continue;
                }
                ++m_chunkPassedThisFrame;
#else
                if (!isChunkInFrustum(entry.boundsMin, entry.boundsMax)) {
                    continue;
                }
#endif

                glm::mat4 model(1.0f);
                const glm::ivec3 offset = entry.chunk->getWorldOffset();
                model = glm::translate(model, glm::vec3(offset.x, offset.y, offset.z));
                m_chunkShader->setMat4(modelLoc, model);

                if (mesh.vertexCount > 0) {
                    if (lastOpaqueVao != mesh.vao) {
                        glBindVertexArray(mesh.vao);
                        lastOpaqueVao = mesh.vao;
                    }
                    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.vertexCount));
                    ++drawCallCount;
                }

                if (mesh.transparentVertexCount > 0) {
                    transparentChunks.push_back(entry.chunk);
                }
            }

            columnBegin = columnEnd;
        }

        regionBegin = regionEnd;
    }
}

void Renderer::renderTransparentChunks(const std::vector<Chunk*>& transparentChunks) {
    if (transparentChunks.empty()) {
        return;
    }

    const int modelLoc = m_chunkShader->getUniformLocation("model");

    std::vector<TransparentDrawItem> transparentItems;
    transparentItems.reserve(transparentChunks.size());

    for (Chunk* chunk : transparentChunks) {
        if (chunk == nullptr) {
            continue;
        }

        const glm::ivec3 offset = chunk->getWorldOffset();
        const glm::vec3 chunkCenter = glm::vec3(static_cast<float>(offset.x) + Chunk::SIZE_X * 0.5f,
                                                static_cast<float>(offset.y) + Chunk::SIZE_Y * 0.5f,
                                                static_cast<float>(offset.z) + Chunk::SIZE_Z * 0.5f);
        const glm::vec3 toCamera = chunkCenter - m_cameraPos;
        transparentItems.push_back({chunk, glm::dot(toCamera, toCamera)});
    }

    std::sort(transparentItems.begin(), transparentItems.end(),
              [](const TransparentDrawItem& a, const TransparentDrawItem& b) {
                  return a.distanceSq > b.distanceSq;
              });

    // Implicit derivatives on atlas UV seams can produce line artifacts on transparent water.
    // Force base LOD sampling for transparent pass only.
    m_chunkShader->setInt("uForceBaseLod", 1);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    for (const TransparentDrawItem& item : transparentItems) {
        if (item.chunk == nullptr) {
            continue;
        }

        const ChunkMesh& mesh = item.chunk->getMesh();
        if (mesh.transparentVertexCount == 0) {
            continue;
        }

        glm::mat4 model(1.0f);
        const glm::ivec3 offset = item.chunk->getWorldOffset();
        model = glm::translate(model, glm::vec3(offset.x, offset.y, offset.z));
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
    //recordMeshingHistory();
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
    if (m_breakOverlayVao != 0) {
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

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void Renderer::renderBlockOutline(const Player& player) {
    if (m_outlineShader == nullptr || m_outlineVao == 0 || !player.hasTargetBlock()) {
        return;
    }

    const glm::ivec3 target = player.getTargetBlock();

    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3(target) + glm::vec3(0.5f));
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

void Renderer::renderBlockBreakOverlay(const Player& player) {
    if (m_breakOverlayShader == nullptr || m_breakOverlayVao == 0 || !player.hasBlockBreakProgress()) {
        return;
    }

    const float breakProgress = player.getBlockBreakProgress();
    if (breakProgress <= 0.0f) {
        return;
    }

    const glm::ivec3 target = player.getBreakTargetBlock();
    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3(target) + glm::vec3(0.5f));
    model = glm::scale(model, glm::vec3(1.001f));
    model = glm::translate(model, glm::vec3(-0.5f));

    m_breakOverlayShader->use();
    m_breakOverlayShader->setMat4("viewProj", m_projection * m_view);
    m_breakOverlayShader->setMat4("model", model);
    m_breakOverlayShader->setFloat("breakProgress", breakProgress);
    m_breakOverlayShader->setVec3("blockWorldPos", glm::vec3(target));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    glBindVertexArray(m_breakOverlayVao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
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
    ChunkMeshingResult result;
    while (m_meshingService.tryPopCompleted(result)) {
#ifndef NDEBUG
        ++m_meshingCompletedThisFrame;
#endif
        m_meshingInFlight.erase(result.chunkKey);

        const auto& activeChunks = world.getActiveChunks();
        const auto it = activeChunks.find(result.chunkKey);
        if (it == activeChunks.end() || !it->second) {
            continue;
        }

        Chunk& chunk = *it->second;
        if (chunk.getMeshRevision() != result.revision) {
            continue;
        }

        ChunkMesh mesh;
        mesh.upload(result.meshData.opaqueVertices);
        mesh.uploadTransparent(result.meshData.transparentVertices);
        mesh.hasBounds = result.meshData.hasBounds;
        mesh.boundsMin = result.meshData.boundsMin;
        mesh.boundsMax = result.meshData.boundsMax;
        chunk.setMesh(mesh);
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
