#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>

#include <glm/vec3.hpp>

#include "../src/renderer/mesh/ChunkMesher.h"

namespace {
int fail(const char* message) {
    std::cerr << "[chunk_aggregation_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

BlockStateId stateForBlockName(const char* name) {
    return BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName(name));
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
        for (auto& vertex : scMeshData.opaqueVertices) {
            vertex.y += yOffset;
        }
        for (auto& vertex : scMeshData.cutoutVertices) {
            vertex.y += yOffset;
        }
        for (auto& vertex : scMeshData.cutoutDistanceVertices) {
            vertex.y += yOffset;
        }
        for (auto& vertex : scMeshData.transparentVertices) {
            vertex.y += yOffset;
        }
        for (auto& vertex : scMeshData.waterVertices) {
            vertex.y += yOffset;
        }

        merged.opaqueVertices.insert(merged.opaqueVertices.end(), scMeshData.opaqueVertices.begin(),
                                     scMeshData.opaqueVertices.end());
        merged.cutoutVertices.insert(merged.cutoutVertices.end(), scMeshData.cutoutVertices.begin(),
                                     scMeshData.cutoutVertices.end());
        merged.cutoutDistanceVertices.insert(merged.cutoutDistanceVertices.end(),
                                             scMeshData.cutoutDistanceVertices.begin(),
                                             scMeshData.cutoutDistanceVertices.end());
        merged.transparentVertices.insert(merged.transparentVertices.end(), scMeshData.transparentVertices.begin(),
                                          scMeshData.transparentVertices.end());
        merged.waterVertices.insert(merged.waterVertices.end(), scMeshData.waterVertices.begin(),
                                    scMeshData.waterVertices.end());
        merged.opaqueFaceCountBeforeGreedy += scMeshData.opaqueFaceCountBeforeGreedy;
        merged.opaqueFaceCountAfterGreedy += scMeshData.opaqueFaceCountAfterGreedy;
        merged.transparentFaceCountBeforeGreedy += scMeshData.transparentFaceCountBeforeGreedy;
        merged.transparentFaceCountAfterGreedy += scMeshData.transparentFaceCountAfterGreedy;
        if (scMeshData.hasBounds) {
            expandBounds(merged, scMeshData.boundsMin + glm::vec3(0.0f, yOffset, 0.0f),
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

float minVertexY(const std::vector<BlockVertex>& first, const std::vector<BlockVertex>& second) {
    return std::min(minVertexY(first), minVertexY(second));
}

float maxVertexY(const std::vector<BlockVertex>& vertices) {
    float maxY = std::numeric_limits<float>::lowest();
    for (const BlockVertex& vertex : vertices) {
        maxY = std::max(maxY, vertex.y);
    }
    return maxY;
}

float maxVertexY(const std::vector<BlockVertex>& first, const std::vector<BlockVertex>& second) {
    return std::max(maxVertexY(first), maxVertexY(second));
}
} // namespace

int main() {
    BlockRegistry::init(nullptr);

    Chunk chunk(0, 0);
    chunk.setBlock(0, 1, 0, stateForBlockName("minecraft:stone"));
    chunk.setBlock(0, 18, 0, stateForBlockName("minecraft:tall_grass"));
    chunk.setBlock(0, 33, 0, stateForBlockName("minecraft:water"));

    const ChunkMeshData aggregated = buildMeshDataFor(chunk);
    if (aggregated.opaqueFaceCountBeforeGreedy != 6 || aggregated.opaqueFaceCountAfterGreedy != 6) {
        return fail("single opaque cube should aggregate as six faces");
    }
    if (aggregated.transparentFaceCountBeforeGreedy != 6 || aggregated.transparentFaceCountAfterGreedy != 6) {
        return fail("single transparent cube should aggregate as six faces");
    }
    const bool hasCutoutPass = !aggregated.cutoutVertices.empty() || !aggregated.cutoutDistanceVertices.empty();
    if (aggregated.opaqueVertices.empty() || !hasCutoutPass || aggregated.waterVertices.empty()) {
        return fail("aggregation should preserve all render passes across sub-chunks");
    }

    glm::vec3 emittedBoundsMin(std::numeric_limits<float>::max());
    glm::vec3 emittedBoundsMax(std::numeric_limits<float>::lowest());
    const auto expandEmittedBounds = [&](const std::vector<BlockVertex>& vertices) {
        for (const BlockVertex& vertex : vertices) {
            emittedBoundsMin.x = std::min(emittedBoundsMin.x, vertex.x);
            emittedBoundsMin.y = std::min(emittedBoundsMin.y, vertex.y);
            emittedBoundsMin.z = std::min(emittedBoundsMin.z, vertex.z);
            emittedBoundsMax.x = std::max(emittedBoundsMax.x, vertex.x);
            emittedBoundsMax.y = std::max(emittedBoundsMax.y, vertex.y);
            emittedBoundsMax.z = std::max(emittedBoundsMax.z, vertex.z);
        }
    };
    expandEmittedBounds(aggregated.opaqueVertices);
    expandEmittedBounds(aggregated.cutoutVertices);
    expandEmittedBounds(aggregated.cutoutDistanceVertices);
    expandEmittedBounds(aggregated.transparentVertices);
    expandEmittedBounds(aggregated.waterVertices);
    if (!aggregated.hasBounds || aggregated.boundsMin != emittedBoundsMin || aggregated.boundsMax != emittedBoundsMax) {
        return fail("aggregated bounds should expand across all populated sub-chunks");
    }

    const float opaqueMinY = minVertexY(aggregated.opaqueVertices);
    const float opaqueMaxY = maxVertexY(aggregated.opaqueVertices);
    const float cutoutMinY = minVertexY(aggregated.cutoutVertices, aggregated.cutoutDistanceVertices);
    const float cutoutMaxY = maxVertexY(aggregated.cutoutVertices, aggregated.cutoutDistanceVertices);
    const float transparentMinY = minVertexY(aggregated.waterVertices);
    const float transparentMaxY = maxVertexY(aggregated.waterVertices);

    constexpr float kSubChunkHeight = static_cast<float>(SubChunk::SIZE);
    if (opaqueMinY < 0.0f || opaqueMaxY >= kSubChunkHeight) {
        return fail("opaque vertices should remain offset into the owning low sub-chunk slice");
    }
    if (cutoutMinY < kSubChunkHeight || cutoutMaxY >= 2.0f * kSubChunkHeight) {
        return fail("cutout vertices should remain offset into the owning middle sub-chunk slice");
    }
    if (transparentMinY < 2.0f * kSubChunkHeight || transparentMaxY >= 3.0f * kSubChunkHeight) {
        return fail("transparent vertices should remain offset into the owning high sub-chunk slice");
    }
    if (!aggregated.transparentVertices.empty()) {
        return fail("water aggregation should stay in the dedicated water pass");
    }
    const bool hasAnimatedWaterVertex =
        std::any_of(aggregated.waterVertices.begin(), aggregated.waterVertices.end(), [](const BlockVertex& vertex) {
            return vertex.animated > 0.5f && vertex.animationFrameCount >= 32.0f && vertex.animationFps > 0.0f;
        });
    if (!hasAnimatedWaterVertex) {
        return fail("aggregated transparent water vertices should preserve animation metadata");
    }

    std::cout << "[chunk_aggregation_test] PASS\n";
    return EXIT_SUCCESS;
}
