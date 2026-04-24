#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>

#include <glm/vec3.hpp>

#include "../src/renderer/ChunkMesher.h"

namespace {
int fail(const char* message) {
    std::cerr << "[chunk_aggregation_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void expandBounds(ChunkMeshData& merged, const glm::vec3& candidateMin, const glm::vec3& candidateMax) {
    if (!merged.hasBounds) {
        merged.hasBounds = true;
        merged.boundsMin = candidateMin;
        merged.boundsMax = candidateMax;
        return;
    }

    merged.boundsMin.x = std::min(merged.boundsMin.x, candidateMin.x);
    merged.boundsMin.y = std::min(merged.boundsMin.y, candidateMin.y);
    merged.boundsMin.z = std::min(merged.boundsMin.z, candidateMin.z);
    merged.boundsMax.x = std::max(merged.boundsMax.x, candidateMax.x);
    merged.boundsMax.y = std::max(merged.boundsMax.y, candidateMax.y);
    merged.boundsMax.z = std::max(merged.boundsMax.z, candidateMax.z);
}

ChunkMeshData buildMeshDataFor(const Chunk& chunk) {
    ChunkMeshData merged;
    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        if (ChunkMesher::shouldSkipSubChunk(chunk, scy)) {
            continue;
        }

        const SubChunkMeshingSnapshotPtr snapshot = ChunkMesher::captureSubChunkSnapshot(chunk, scy);
        if (!snapshot) {
            continue;
        }

        ChunkMeshData scMeshData = ChunkMesher::buildSubChunkMeshData(*snapshot);
        const float yOffset = static_cast<float>(scy * SubChunk::SIZE);
        for (auto& vertex : scMeshData.opaqueVertices) { vertex.y += yOffset; }
        for (auto& vertex : scMeshData.cutoutVertices) { vertex.y += yOffset; }
        for (auto& vertex : scMeshData.transparentVertices) { vertex.y += yOffset; }

        merged.opaqueVertices.insert(merged.opaqueVertices.end(),
                                     scMeshData.opaqueVertices.begin(), scMeshData.opaqueVertices.end());
        merged.cutoutVertices.insert(merged.cutoutVertices.end(),
                                     scMeshData.cutoutVertices.begin(), scMeshData.cutoutVertices.end());
        merged.transparentVertices.insert(merged.transparentVertices.end(),
                                          scMeshData.transparentVertices.begin(), scMeshData.transparentVertices.end());
        merged.opaqueFaceCountBeforeGreedy += scMeshData.opaqueFaceCountBeforeGreedy;
        merged.opaqueFaceCountAfterGreedy += scMeshData.opaqueFaceCountAfterGreedy;
        merged.transparentFaceCountBeforeGreedy += scMeshData.transparentFaceCountBeforeGreedy;
        merged.transparentFaceCountAfterGreedy += scMeshData.transparentFaceCountAfterGreedy;
        if (scMeshData.hasBounds) {
            expandBounds(merged,
                         scMeshData.boundsMin + glm::vec3(0.0f, yOffset, 0.0f),
                         scMeshData.boundsMax + glm::vec3(0.0f, yOffset, 0.0f));
        }
    }

    merged.opaqueVertexCount = static_cast<uint32_t>(merged.opaqueVertices.size());
    return merged;
}

float minVertexY(const std::vector<BlockVertex>& vertices) {
    float minY = std::numeric_limits<float>::max();
    for (const BlockVertex& vertex : vertices) {
        minY = std::min(minY, vertex.y);
    }
    return minY;
}

float maxVertexY(const std::vector<BlockVertex>& vertices) {
    float maxY = std::numeric_limits<float>::lowest();
    for (const BlockVertex& vertex : vertices) {
        maxY = std::max(maxY, vertex.y);
    }
    return maxY;
}
} // namespace

int main() {
    BlockRegistry::init(nullptr);

    Chunk chunk(0, 0);
    chunk.setBlock(0, 1, 0, BlockIds::STONE);
    chunk.setBlock(0, 18, 0, BlockIds::TALL_GRASS);
    chunk.setBlock(0, 33, 0, BlockIds::WATER);

    const ChunkMeshData aggregated = buildMeshDataFor(chunk);
    if (aggregated.opaqueFaceCountBeforeGreedy != 6 || aggregated.opaqueFaceCountAfterGreedy != 6) {
        return fail("single opaque cube should aggregate as six faces");
    }
    if (aggregated.transparentFaceCountBeforeGreedy != 6 || aggregated.transparentFaceCountAfterGreedy != 6) {
        return fail("single transparent cube should aggregate as six faces");
    }
    if (aggregated.opaqueVertices.empty() || aggregated.cutoutVertices.empty() || aggregated.transparentVertices.empty()) {
        return fail("aggregation should preserve all render passes across sub-chunks");
    }
    if (!aggregated.hasBounds ||
        aggregated.boundsMin != glm::vec3(0.0f, 1.0f, 0.0f) ||
        aggregated.boundsMax != glm::vec3(1.0f, 34.0f, 1.0f)) {
        return fail("aggregated bounds should expand across all populated sub-chunks");
    }

    const float opaqueMinY = minVertexY(aggregated.opaqueVertices);
    const float opaqueMaxY = maxVertexY(aggregated.opaqueVertices);
    const float cutoutMinY = minVertexY(aggregated.cutoutVertices);
    const float cutoutMaxY = maxVertexY(aggregated.cutoutVertices);
    const float transparentMinY = minVertexY(aggregated.transparentVertices);
    const float transparentMaxY = maxVertexY(aggregated.transparentVertices);

    if (opaqueMinY < 1.0f || opaqueMaxY > 2.0f) {
        return fail("opaque vertices should remain offset into the owning low sub-chunk slice");
    }
    if (cutoutMinY < 18.0f || cutoutMaxY > 19.0f) {
        return fail("cutout vertices should remain offset into the owning middle sub-chunk slice");
    }
    if (transparentMinY < 33.0f || transparentMaxY > 34.0f) {
        return fail("transparent vertices should remain offset into the owning high sub-chunk slice");
    }
    const bool hasAnimatedWaterVertex = std::any_of(aggregated.transparentVertices.begin(),
                                                    aggregated.transparentVertices.end(),
                                                    [](const BlockVertex& vertex) {
                                                        return vertex.animated > 0.5f &&
                                                               vertex.animationFrameCount >= 32.0f &&
                                                               vertex.animationFps > 0.0f;
                                                    });
    if (!hasAnimatedWaterVertex) {
        return fail("aggregated transparent water vertices should preserve animation metadata");
    }

    std::cout << "[chunk_aggregation_test] PASS\n";
    return EXIT_SUCCESS;
}
