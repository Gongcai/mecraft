#include "TerrainRenderer.h"
#include "TerrainRenderCache.h"
#include "WorldRenderBuffer.h"
#include "../shadow/ShadowCasterCuller.h"
#include "../../world/IWorldView.h"
#include "../../world/chunk/Chunk.h"
#include <algorithm>
#include <cmath>

namespace {

bool isCascadeAabbVisible(const glm::vec3& boundsMin,
                          const glm::vec3& boundsMax,
                          const CascadeAabbCuller& culler) {
    const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
    const glm::vec3 extents = (boundsMax - boundsMin) * 0.5f;
    const glm::mat4& m = culler.viewProj;

    const glm::vec4 clipCenter = m * glm::vec4(center, 1.0f);
    const float invW = 1.0f / clipCenter.w;

    // Orthographic shadow cascades are affine, so projecting center/extents gives the exact AABB in clip space.
    const glm::vec3 clipExtents(glm::dot(culler.absClipExtentX, extents),
                                glm::dot(culler.absClipExtentY, extents),
                                glm::dot(culler.absClipExtentZ, extents));

    const glm::vec3 ndcCenter(clipCenter.x * invW, clipCenter.y * invW, clipCenter.z * invW);
    const glm::vec3 ndcExtents = clipExtents * std::abs(invW);
    const glm::vec3 minNdc = ndcCenter - ndcExtents;
    const glm::vec3 maxNdc = ndcCenter + ndcExtents;

    const float xyPad = culler.xyPaddingNdc;
    const float zPad = culler.zPaddingNdc;
    bool visible = !(maxNdc.x < -1.0f - xyPad || minNdc.x > 1.0f + xyPad ||
                     maxNdc.y < -1.0f - xyPad || minNdc.y > 1.0f + xyPad);
    if (visible && culler.useZCulling) {
        visible = !(maxNdc.z < -1.0f - zPad || minNdc.z > 1.0f + zPad);
    }
    return visible;
}

unsigned int buildCascadeVisibilityMask(const glm::vec3& boundsMin,
                                        const glm::vec3& boundsMax,
                                        const std::array<CascadeAabbCuller, 4>& cascadeCullers,
                                        const unsigned int candidateMask = 0xFu) {
    unsigned int mask = 0;
    for (int c = 0; c < 4; ++c) {
        if ((candidateMask & (1u << c)) == 0) {
            continue;
        }
        if (isCascadeAabbVisible(boundsMin, boundsMax, cascadeCullers[c])) {
            mask |= 1u << c;
        }
    }
    return mask;
}

} // namespace

void TerrainRenderer::init() {
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
// Opaque chunk traversal with hierarchical frustum culling
// ============================================================================

void TerrainRenderer::renderOpaqueChunksAndCollectPasses(
    const IWorldView& worldView,
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

    const bool distanceCull = maxCameraDistance > 0.0f || shadowCuller != nullptr;
    const float maxCameraDistanceSq = maxCameraDistance * maxCameraDistance;

    // A shadow culler uses Iris BoxCuller AABB cube semantics.
    // Main-view traversal uses the original XZ clamped distance check.
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
        [[maybe_unused]] int regionCandidateCount = 0;
        for (size_t i = regionBegin; i < regionEnd; ++i) {
            ChunkRenderColumnCache& column = chunkRenderColumns[i];
            m_terrainCache->refreshChunkRenderColumnCache(column);
            if (!column.columnHasBounds) {
                continue;
            }
            expandBounds(regionMin, regionMax, regionHasBounds, column.columnBoundsMin, column.columnBoundsMax);
            regionCandidateCount += column.renderableCount;
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

            [[maybe_unused]] const int columnCandidateCount = column.renderableCount;

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

            const glm::ivec3 offset = column.chunk->getWorldOffset();
            const float cutoutLimitBlocks = m_cutoutRenderDistanceChunks * static_cast<float>(Chunk::SIZE_X);
            const float cutoutLimitSq = cutoutLimitBlocks * cutoutLimitBlocks;
            for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
                const SubChunk* sc = column.chunk->getSubChunk(scy);
                if (!sc) continue;
                const SubChunkMesh& mesh = sc->getMesh();
                if (!mesh.inGlobalPool) continue;
                if (mesh.opaqueRange.vertexCount == 0 && mesh.cutoutRange.vertexCount == 0 &&
                    mesh.cutoutDistanceRange.vertexCount == 0 && mesh.transparentRange.vertexCount == 0 &&
                    mesh.waterRange.vertexCount == 0) {
                    continue;
                }

#ifdef MECRAFT_DEBUG
                ++m_mdiSubChunkTestsThisFrame;
                ++m_chunkTestsThisFrame;
#endif
                const int yBase = scy * SubChunk::SIZE;
                const glm::vec3 derivedMin(static_cast<float>(offset.x), static_cast<float>(offset.y + yBase),
                                           static_cast<float>(offset.z));
                const glm::vec3 derivedMax(static_cast<float>(offset.x + Chunk::SIZE_X),
                                           static_cast<float>(offset.y + yBase + SubChunk::SIZE),
                                           static_cast<float>(offset.z + Chunk::SIZE_Z));
                const glm::vec3 boundsMin = mesh.hasBounds ? mesh.boundsMin : derivedMin;
                const glm::vec3 boundsMax = mesh.hasBounds ? mesh.boundsMax : derivedMax;
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
                    const glm::vec3 sectionCenter(static_cast<float>(offset.x) + Chunk::SIZE_X * 0.5f,
                                                  static_cast<float>(offset.y + yBase) + SubChunk::SIZE * 0.5f,
                                                  static_cast<float>(offset.z) + Chunk::SIZE_Z * 0.5f);
                    const glm::vec2 toCameraXZ(sectionCenter.x - m_cameraPos.x, sectionCenter.z - m_cameraPos.z);
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
                    const glm::vec3 sectionCenter(static_cast<float>(offset.x) + Chunk::SIZE_X * 0.5f,
                                                  static_cast<float>(offset.y + yBase) + SubChunk::SIZE * 0.5f,
                                                  static_cast<float>(offset.z) + Chunk::SIZE_Z * 0.5f);
                    const glm::vec3 toCamera = sectionCenter - m_cameraPos;
                    m_terrainCache->addTransparentBatch(mesh.transparentRange, glm::dot(toCamera, toCamera),
                                                        TransparentBatchKind::Generic);
                }
                if (mesh.waterRange.vertexCount > 0) {
                    const glm::vec3 sectionCenter(static_cast<float>(offset.x) + Chunk::SIZE_X * 0.5f,
                                                  static_cast<float>(offset.y + yBase) + SubChunk::SIZE * 0.5f,
                                                  static_cast<float>(offset.z) + Chunk::SIZE_Z * 0.5f);
                    const glm::vec3 toCamera = sectionCenter - m_cameraPos;
                    m_terrainCache->addTransparentBatch(mesh.waterRange, glm::dot(toCamera, toCamera),
                                                        TransparentBatchKind::Water);
                }
            }
        }

        regionBegin = regionEnd;
    }
}

void TerrainRenderer::collectShadowChunks(
    const IWorldView& worldView,
    const glm::vec3& cameraPos,
    float maxShadowDistance,
    shadow::ShadowCasterCuller* shadowCuller,
    const std::array<CascadeAabbCuller, 4>& cascadeCullers,
    std::array<std::vector<GpuMeshRange>, 4>& outOpaqueRanges,
    std::array<std::vector<GpuMeshRange>, 4>& outCutoutRanges,
    std::array<std::vector<GpuMeshRange>, 4>& outTransparentRanges
) {
    resetDebugCounters();
    m_terrainCache->syncChunkRenderColumns(worldView);
    std::vector<ChunkRenderColumnCache>& chunkRenderColumns = m_terrainCache->chunkRenderColumns();
    if (chunkRenderColumns.empty()) {
        return;
    }

    const bool distanceCull = maxShadowDistance > 0.0f || shadowCuller != nullptr;
    const float maxCameraDistanceSq = maxShadowDistance * maxShadowDistance;

    auto boundsWithinCameraDistance = [&](const glm::vec3& boundsMin, const glm::vec3& boundsMax) {
        if (!distanceCull) {
            return true;
        }
        if (shadowCuller) {
            const bool visible = shadowCuller->isAabbVisible(boundsMin, boundsMax);
            if (visible) {
                const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
                const float dist = glm::length(center - cameraPos);
                shadowCuller->recordVisible(dist);
            } else {
                shadowCuller->recordCulled();
            }
            return visible;
        }
        const float clampedX = std::clamp(cameraPos.x, boundsMin.x, boundsMax.x);
        const float clampedZ = std::clamp(cameraPos.z, boundsMin.z, boundsMax.z);
        const float dx = clampedX - cameraPos.x;
        const float dz = clampedZ - cameraPos.z;
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
        for (size_t i = regionBegin; i < regionEnd; ++i) {
            ChunkRenderColumnCache& column = chunkRenderColumns[i];
            m_terrainCache->refreshChunkRenderColumnCache(column);
            if (!column.columnHasBounds) {
                continue;
            }
            expandBounds(regionMin, regionMax, regionHasBounds, column.columnBoundsMin, column.columnBoundsMax);
        }

        if (!regionHasBounds) {
            regionBegin = regionEnd;
            continue;
        }

        if (!boundsWithinCameraDistance(regionMin, regionMax)) {
            regionBegin = regionEnd;
            continue;
        }
        const unsigned int regionCascadeMask = buildCascadeVisibilityMask(regionMin, regionMax, cascadeCullers);
        if (regionCascadeMask == 0) {
            regionBegin = regionEnd;
            continue;
        }

        for (size_t i = regionBegin; i < regionEnd; ++i) {
            ChunkRenderColumnCache& column = chunkRenderColumns[i];
            if (column.chunk == nullptr || !column.columnHasBounds) {
                continue;
            }

            if (!boundsWithinCameraDistance(column.columnBoundsMin, column.columnBoundsMax)) {
                continue;
            }
            const unsigned int columnCascadeMask = buildCascadeVisibilityMask(
                column.columnBoundsMin,
                column.columnBoundsMax,
                cascadeCullers,
                regionCascadeMask);
            if (columnCascadeMask == 0) {
                continue;
            }

            const glm::vec3 offset = column.worldOffset;
            const float cutoutLimitBlocks = m_cutoutRenderDistanceChunks * static_cast<float>(Chunk::SIZE_X);
            const float cutoutLimitSq = cutoutLimitBlocks * cutoutLimitBlocks;
            for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
                const SubChunk* sc = column.chunk->getSubChunk(scy);
                if (!sc) continue;
                const SubChunkMesh& mesh = sc->getMesh();
                if (!mesh.inGlobalPool) continue;
                if (mesh.opaqueRange.vertexCount == 0 && mesh.cutoutRange.vertexCount == 0 &&
                    mesh.cutoutDistanceRange.vertexCount == 0 && mesh.transparentRange.vertexCount == 0 &&
                    mesh.waterRange.vertexCount == 0) {
                    continue;
                }

                const int yBase = scy * SubChunk::SIZE;
                const glm::vec3 derivedMin(offset.x, offset.y + static_cast<float>(yBase), offset.z);
                const glm::vec3 derivedMax(offset.x + static_cast<float>(Chunk::SIZE_X),
                                           offset.y + static_cast<float>(yBase + SubChunk::SIZE),
                                           offset.z + static_cast<float>(Chunk::SIZE_Z));
                const glm::vec3 boundsMin = mesh.hasBounds ? mesh.boundsMin : derivedMin;
                const glm::vec3 boundsMax = mesh.hasBounds ? mesh.boundsMax : derivedMax;
                if (!boundsWithinCameraDistance(boundsMin, boundsMax)) {
                    continue;
                }
                const unsigned int cascadeMask =
                    buildCascadeVisibilityMask(boundsMin, boundsMax, cascadeCullers, columnCascadeMask);
                if (cascadeMask == 0) {
                    continue;
                }

                bool cutoutDistanceVisible = false;
                if (mesh.cutoutDistanceRange.vertexCount > 0) {
                    const glm::vec3 sectionCenter(offset.x + static_cast<float>(Chunk::SIZE_X) * 0.5f,
                                                  offset.y + static_cast<float>(yBase) +
                                                      static_cast<float>(SubChunk::SIZE) * 0.5f,
                                                  offset.z + static_cast<float>(Chunk::SIZE_Z) * 0.5f);
                    const glm::vec2 toCameraXZ(sectionCenter.x - cameraPos.x, sectionCenter.z - cameraPos.z);
                    const float distanceSq = glm::dot(toCameraXZ, toCameraXZ);
                    cutoutDistanceVisible = !m_cutoutDistanceLimitEnabled || distanceSq <= cutoutLimitSq;
                }

                // Bin to cascades from the visibility mask computed for this
                // sub-chunk.
                for (int c = 0; c < 4; ++c) {
                    if ((columnCascadeMask & (1u << c)) == 0) {
                        continue;
                    }
                    if ((cascadeMask & (1u << c)) != 0) {
                        cascadeCullers[c].visibleCount++;
                        if (mesh.opaqueRange.vertexCount > 0) {
                            outOpaqueRanges[c].push_back(mesh.opaqueRange);
                        }
                        if (mesh.cutoutRange.vertexCount > 0) {
                            outCutoutRanges[c].push_back(mesh.cutoutRange);
                        }
                        if (cutoutDistanceVisible) {
                            outCutoutRanges[c].push_back(mesh.cutoutDistanceRange);
                        }
                        if (mesh.transparentRange.vertexCount > 0) {
                            outTransparentRanges[c].push_back(mesh.transparentRange);
                        }
                        if (mesh.waterRange.vertexCount > 0) {
                            outTransparentRanges[c].push_back(mesh.waterRange);
                        }
                    } else {
                        cascadeCullers[c].culledCount++;
                    }
                }
            }
        }

        regionBegin = regionEnd;
    }
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
