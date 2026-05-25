#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include "../src/renderer/mesh/ChunkMesher.h"
#include "../src/renderer/mesh/MeshBuilderRegistry.h"
#include "../src/world/block/BlockStateRegistry.h"
#include "../src/world/fluid/FluidFlow.h"
#include "../src/world/fluid/FluidState.h"
#include "../src/world/block/PropIndices.h"
#include "../src/world/World.h"

namespace {
int fail(const char* message) {
    std::cerr << "[chunk_mesher_test] FAIL: " << message << '\n';
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
        for (auto& vertex : scMeshData.cutoutDistanceVertices) { vertex.y += yOffset; }
        for (auto& vertex : scMeshData.transparentVertices) { vertex.y += yOffset; }
        for (auto& vertex : scMeshData.waterVertices) { vertex.y += yOffset; }

        merged.opaqueVertices.insert(merged.opaqueVertices.end(),
                                     scMeshData.opaqueVertices.begin(), scMeshData.opaqueVertices.end());
        merged.cutoutVertices.insert(merged.cutoutVertices.end(),
                                     scMeshData.cutoutVertices.begin(), scMeshData.cutoutVertices.end());
        merged.cutoutDistanceVertices.insert(merged.cutoutDistanceVertices.end(),
                                             scMeshData.cutoutDistanceVertices.begin(),
                                             scMeshData.cutoutDistanceVertices.end());
        merged.transparentVertices.insert(merged.transparentVertices.end(),
                                          scMeshData.transparentVertices.begin(), scMeshData.transparentVertices.end());
        merged.waterVertices.insert(merged.waterVertices.end(),
                                    scMeshData.waterVertices.begin(), scMeshData.waterVertices.end());
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

bool approxEqual(const float lhs, const float rhs, const float epsilon = 0.001f) {
    return std::fabs(lhs - rhs) <= epsilon;
}

std::size_t snapshotHaloIndex(const int x, const int y, const int z) {
    return static_cast<std::size_t>(x + 1) +
           static_cast<std::size_t>(z + 1) * SC_HALO_SIZE +
           static_cast<std::size_t>(y + 1) * SC_HALO_SIZE * SC_HALO_SIZE;
}

std::size_t snapshotBorderIndex(const int y, const int xz) {
    return static_cast<std::size_t>(y) + static_cast<std::size_t>(xz) * SubChunk::SIZE;
}

std::vector<const BlockVertex*> collectTopFaceVertices(const ChunkMeshData& meshData,
                                                       const float minX,
                                                       const float maxX,
                                                       const float y,
                                                       const float minZ,
                                                       const float maxZ) {
    std::vector<const BlockVertex*> matches;
    for (const BlockVertex& vertex : meshData.opaqueVertices) {
        if (!approxEqual(vertex.normal, 0.0f) ||
            !approxEqual(vertex.y, y) ||
            vertex.x < minX - 0.001f || vertex.x > maxX + 0.001f ||
            vertex.z < minZ - 0.001f || vertex.z > maxZ + 0.001f) {
            continue;
        }
        matches.push_back(&vertex);
    }
    return matches;
}

std::vector<const BlockVertex*> collectFaceVertices(const std::vector<BlockVertex>& vertices,
                                                    const float face,
                                                    const float minX,
                                                    const float maxX,
                                                    const float minY,
                                                    const float maxY,
                                                    const float minZ,
                                                    const float maxZ) {
    std::vector<const BlockVertex*> matches;
    for (const BlockVertex& vertex : vertices) {
        if (!approxEqual(vertex.normal, face) ||
            vertex.x < minX - 0.001f || vertex.x > maxX + 0.001f ||
            vertex.y < minY - 0.001f || vertex.y > maxY + 0.001f ||
            vertex.z < minZ - 0.001f || vertex.z > maxZ + 0.001f) {
            continue;
        }
        matches.push_back(&vertex);
    }
    return matches;
}

void fillSubChunk(Chunk& chunk, const int scy, const BlockID blockId) {
    const int yBase = scy * SubChunk::SIZE;
    for (int y = 0; y < SubChunk::SIZE; ++y) {
        for (int z = 0; z < SubChunk::SIZE; ++z) {
            for (int x = 0; x < SubChunk::SIZE; ++x) {
                chunk.setBlockFast(x, yBase + y, z, blockId);
            }
        }
    }

    SubChunk* sc = chunk.getSubChunk(scy);
    if (sc) {
        sc->inferType();
    }
}
} // namespace

int main() {
    BlockRegistry::init(nullptr);

    {
        if (MeshBuilderRegistry::getShapeTag("cube") != MeshBuilderRegistry::CUBE_TAG) {
            return fail("cube shape should resolve to the builtin cube tag");
        }
        if (MeshBuilderRegistry::getShapeTag("cross") != MeshBuilderRegistry::CROSS_TAG) {
            return fail("cross shape should resolve to the builtin cross tag");
        }
        if (MeshBuilderRegistry::getShapeTag("torch") == MeshBuilderRegistry::INVALID_TAG) {
            return fail("torch shape alias should be registered");
        }
        if (MeshBuilderRegistry::getShapeTag("water") == MeshBuilderRegistry::INVALID_TAG) {
            return fail("water shape alias should be registered");
        }
        if (MeshBuilderRegistry::getBuilder(MeshBuilderRegistry::CROSS_TAG) == nullptr) {
            return fail("cross shape should resolve to a mesh builder");
        }
        const BlockDef& torchDef = BlockRegistry::get(BlockIds::TORCH);
        if (torchDef.renderShapeTag != MeshBuilderRegistry::getShapeTag("torch")) {
            return fail("torch block should resolve renderShapeTag through the mesh builder registry");
        }
        const BlockDef& waterDef = BlockRegistry::get(BlockIds::WATER);
        if (waterDef.renderShapeTag != MeshBuilderRegistry::getShapeTag("water")) {
            return fail("water block should resolve renderShapeTag through the mesh builder registry");
        }
    }

    {
        World world;
        world.init(20260525);
        constexpr int sampleY = 1;
        const BlockID generatedNeighbor = world.sampleGeneratedBlock(Chunk::SIZE_X, sampleY, 0);
        const BlockID generatedDiagonal = world.sampleGeneratedBlock(Chunk::SIZE_X, sampleY, Chunk::SIZE_Z);
        if (generatedNeighbor == BlockIds::AIR || generatedDiagonal == BlockIds::AIR) {
            return fail("snapshot fallback test seed should generate solid missing neighbor samples");
        }

        Chunk center(0, 0);
        center.setBlock(0, sampleY, 0, BlockIds::STONE);
        const SubChunkMeshingSnapshotPtr snapshot = ChunkMesher::captureSubChunkSnapshot(
            center, 0, nullptr, nullptr, nullptr, nullptr, &world);
        if (!snapshot) {
            return fail("snapshot fallback test should capture a sub-chunk");
        }
        if (snapshot->posXBorder[snapshotBorderIndex(sampleY, 0)] != BlockIds::AIR) {
            return fail("missing +X neighbor border should be air instead of generated terrain");
        }
        if (snapshot->haloBlocks[snapshotHaloIndex(Chunk::SIZE_X, sampleY, 0)] != BlockIds::AIR) {
            return fail("missing +X neighbor halo should be air instead of generated terrain");
        }
        if (snapshot->haloBlocks[snapshotHaloIndex(Chunk::SIZE_X, sampleY, Chunk::SIZE_Z)] != BlockIds::AIR) {
            return fail("missing diagonal neighbor halo should be air instead of generated terrain");
        }

        Chunk posX(1, 0);
        posX.setBlock(0, sampleY, 0, BlockIds::DIRT);
        const SubChunkMeshingSnapshotPtr snapshotWithNeighbor = ChunkMesher::captureSubChunkSnapshot(
            center, 0, &posX, nullptr, nullptr, nullptr, &world);
        if (!snapshotWithNeighbor) {
            return fail("snapshot fallback test should capture with a held neighbor");
        }
        if (snapshotWithNeighbor->posXBorder[snapshotBorderIndex(sampleY, 0)] != BlockIds::DIRT ||
            snapshotWithNeighbor->haloBlocks[snapshotHaloIndex(Chunk::SIZE_X, sampleY, 0)] != BlockIds::DIRT) {
            return fail("held +X neighbor should still be sampled from the job snapshot");
        }
    }

    {
        Chunk chunk(0, 0);
        if (!ChunkMesher::shouldSkipSubChunk(chunk, 0)) {
            return fail("empty sub-chunks should be skipped");
        }

        chunk.setBlock(1, 1, 1, BlockIds::STONE);
        SubChunk* sc = chunk.getSubChunk(0);
        if (!sc) {
            return fail("editing a block should create the owning sub-chunk");
        }
        if (sc->getType() != SubChunkType::Normal) {
            return fail("partially filled sub-chunks should remain Normal");
        }
        if (ChunkMesher::shouldSkipSubChunk(chunk, 0)) {
            return fail("partially filled sub-chunks should not be skipped");
        }

        chunk.setBlock(2, 1, 1, BlockIds::DIRT);
        if (sc->getType() != SubChunkType::Normal) {
            return fail("mixed edited sub-chunks should remain Normal");
        }

        chunk.setBlock(1, 1, 1, BlockIds::AIR);
        chunk.setBlock(2, 1, 1, BlockIds::AIR);
        if (chunk.getSubChunk(0) != nullptr) {
            return fail("cleared sub-chunks should recycle back to implicit air storage");
        }
        if (!ChunkMesher::shouldSkipSubChunk(chunk, 0)) {
            return fail("air-only sub-chunks should be skipped after runtime edits");
        }
    }

    {
        Chunk center(0, 0);
        Chunk posX(1, 0);
        Chunk negX(-1, 0);
        Chunk posZ(0, 1);
        Chunk negZ(0, -1);
        center.neighbors[0] = &posX;
        center.neighbors[1] = &negX;
        center.neighbors[2] = &posZ;
        center.neighbors[3] = &negZ;
        posX.neighbors[1] = &center;
        negX.neighbors[0] = &center;
        posZ.neighbors[3] = &center;
        negZ.neighbors[2] = &center;

        if (center.getSubChunk(5) != nullptr) {
            return fail("fresh chunk should not eagerly allocate unrelated sub-chunks");
        }
        center.markSubChunkDirty(5);
        if (center.getSubChunk(5) != nullptr) {
            return fail("markSubChunkDirty should not instantiate missing air sub-chunks");
        }

        SubChunk* base = center.getOrCreateSubChunk(2);
        SubChunk* above = center.getOrCreateSubChunk(3);
        SubChunk* east = posX.getOrCreateSubChunk(2);
        SubChunk* south = negZ.getOrCreateSubChunk(2);

        if (base->neighbors[2] != above || above->neighbors[3] != base) {
            return fail("vertical sub-chunk neighbor pointers should be linked on creation");
        }
        if (base->neighbors[0] != east || east->neighbors[1] != base) {
            return fail("horizontal +X sub-chunk neighbor pointers should be linked on creation");
        }
        if (base->neighbors[5] != south || south->neighbors[4] != base) {
            return fail("horizontal -Z sub-chunk neighbor pointers should be linked on creation");
        }
    }

    {
        Chunk center(0, 0);
        Chunk posX(1, 0);
        Chunk negX(-1, 0);
        Chunk posZ(0, 1);
        Chunk negZ(0, -1);
        center.neighbors[0] = &posX;
        center.neighbors[1] = &negX;
        center.neighbors[2] = &posZ;
        center.neighbors[3] = &negZ;
        posX.neighbors[1] = &center;
        negX.neighbors[0] = &center;
        posZ.neighbors[3] = &center;
        negZ.neighbors[2] = &center;

        fillSubChunk(center, 0, BlockIds::STONE);
        fillSubChunk(center, 1, BlockIds::STONE);
        fillSubChunk(center, 2, BlockIds::STONE);
        fillSubChunk(posX, 1, BlockIds::STONE);
        fillSubChunk(negX, 1, BlockIds::STONE);
        fillSubChunk(posZ, 1, BlockIds::STONE);
        fillSubChunk(negZ, 1, BlockIds::STONE);

        const SubChunk* centerSolid = center.getSubChunk(1);
        if (!centerSolid || centerSolid->getType() != SubChunkType::Solid) {
            return fail("fully filled opaque cube sub-chunks should infer Solid");
        }
        if (!ChunkMesher::shouldSkipSubChunk(center, 1)) {
            return fail("fully occluded semantic-solid sub-chunks should be skipped");
        }

        posX.setBlock(0, 16, 0, BlockIds::AIR);
        if (ChunkMesher::shouldSkipSubChunk(center, 1)) {
            return fail("solid sub-chunks with any exposed border should not be skipped");
        }
    }

    {
        Chunk waterChunk(0, 0);
        fillSubChunk(waterChunk, 1, BlockIds::WATER);
        const SubChunk* water = waterChunk.getSubChunk(1);
        if (!water || water->getType() != SubChunkType::Normal) {
            return fail("uniform transparent sub-chunks should remain Normal");
        }
        if (ChunkMesher::shouldSkipSubChunk(waterChunk, 1)) {
            return fail("uniform transparent sub-chunks should still mesh when exposed");
        }
    }

    {
        Chunk center(0, 0);
        Chunk posX(1, 0);
        Chunk negX(-1, 0);
        Chunk posZ(0, 1);
        Chunk negZ(0, -1);
        Chunk above(0, 0);
        Chunk below(0, 0);

        center.neighbors[0] = &posX;
        center.neighbors[1] = &negX;
        center.neighbors[2] = &posZ;
        center.neighbors[3] = &negZ;
        posX.neighbors[1] = &center;
        negX.neighbors[0] = &center;
        posZ.neighbors[3] = &center;
        negZ.neighbors[2] = &center;

        fillSubChunk(center, 0, BlockIds::STONE);
        fillSubChunk(center, 1, BlockIds::STONE);
        fillSubChunk(center, 2, BlockIds::STONE);
        fillSubChunk(posX, 1, BlockIds::STONE);
        fillSubChunk(negX, 1, BlockIds::STONE);
        fillSubChunk(posZ, 1, BlockIds::STONE);
        fillSubChunk(negZ, 1, BlockIds::STONE);
        fillSubChunk(above, 1, BlockIds::STONE);
        fillSubChunk(below, 1, BlockIds::STONE);

        SubChunk* centerSubChunk = center.getSubChunk(1);
        if (!centerSubChunk) {
            return fail("center solid sub-chunk should exist");
        }
        centerSubChunk->neighbors[2] = above.getSubChunk(1);
        centerSubChunk->neighbors[3] = below.getSubChunk(1);
        if (above.getSubChunk(1)) {
            above.getSubChunk(1)->neighbors[3] = centerSubChunk;
        }
        if (below.getSubChunk(1)) {
            below.getSubChunk(1)->neighbors[2] = centerSubChunk;
        }

        SubChunkMesh staleMesh;
        staleMesh.vertexCount = 6;
        staleMesh.hasBounds = true;
        center.setSubChunkMesh(1, staleMesh);
        ChunkMeshData staleData;
        staleData.hasBounds = true;
        staleData.boundsMin = glm::vec3(0.0f, 16.0f, 0.0f);
        staleData.boundsMax = glm::vec3(1.0f, 17.0f, 1.0f);
        staleData.opaqueVertices.push_back({});
        center.updateColumnAggregateData(1, staleData, true);
        center.markSubChunkDirty(1);

        if (!ChunkMesher::shouldSkipSubChunk(center, 1)) {
            return fail("fully occluded dirty solid sub-chunk should become skippable");
        }

        ChunkMeshData emptyData;
        center.setSubChunkMesh(1, SubChunkMesh{});
        center.updateColumnAggregateData(1, emptyData, true);
        const SubChunkMesh& clearedMesh = center.getSubChunkMesh(1);
        if (center.isSubChunkDirty(1) ||
            clearedMesh.vertexCount != 0 ||
            clearedMesh.hasBounds ||
            center.getColumnMesh().hasBounds) {
            return fail("skipped dirty sub-chunks should clear stale mesh and aggregate bounds");
        }
    }

    {
        Chunk chunk(0, 0);
        chunk.setBlock(0, 15, 0, BlockIds::STONE);
        chunk.recalcHeightMap(0, 0);

        if (chunk.getSubChunk(1) != nullptr) {
            return fail("top air sub-chunks should stay unallocated before any non-default light writes");
        }
        if (chunk.getSunlight(0, 16, 0) != 15) {
            return fail("missing sky-exposed sub-chunks should report implicit full sunlight");
        }
        if (chunk.getPackedLight(0, 16, 0) != 0xF0) {
            return fail("packed light reads should preserve implicit sunlight for missing sub-chunks");
        }

        chunk.setSunlight(0, 16, 0, 15);
        if (chunk.getSubChunk(1) != nullptr) {
            return fail("writing implicit sunlight should not instantiate a sky-only sub-chunk");
        }

        const SubChunkMeshingSnapshotPtr snapshot = ChunkMesher::captureSubChunkSnapshot(chunk, 0);
        if (!snapshot) {
            return fail("sub-chunk snapshot capture should succeed for populated sections");
        }
        if (snapshot->posYLightBorder[0] != 0xF0) {
            return fail("vertical border capture should preserve implicit sky light above missing air sections");
        }

        chunk.setSunlight(0, 16, 0, 14);
        SubChunk* lightOnly = chunk.getSubChunk(1);
        if (!lightOnly || chunk.getSunlight(0, 16, 0) != 14) {
            return fail("non-default sky light writes should materialize and store the affected sub-chunk");
        }
        if (lightOnly->getSunlight(1, 0, 0) != 15) {
            return fail("newly materialized light-only sub-chunks should seed implicit sunlight for untouched voxels");
        }

        chunk.setSunlight(0, 16, 0, 15);
        if (chunk.getSubChunk(1) != nullptr) {
            return fail("light-only sub-chunks should be recycled once sunlight returns to the implicit default");
        }

        chunk.setBlockLight(0, 16, 0, 7);
        lightOnly = chunk.getSubChunk(1);
        if (!lightOnly || lightOnly->getSunlight(1, 0, 0) != 15 || chunk.getBlockLight(0, 16, 0) != 7) {
            return fail("block-light-only sub-chunks should preserve implicit sunlight when materialized");
        }

        chunk.setBlockLight(0, 16, 0, 0);
        if (chunk.getSubChunk(1) != nullptr) {
            return fail("block-light-only sub-chunks should be recycled once lighting returns to implicit defaults");
        }
    }

    {
        Chunk chunk(0, 0);
        chunk.setBlock(0, 1, 0, BlockIds::STONE);
        chunk.setBlock(0, 33, 0, BlockIds::STONE);
        chunk.recalcHeightMap(0, 0);

        SubChunk* bottom = chunk.getSubChunk(0);
        SubChunk* top = chunk.getSubChunk(2);
        if (!bottom || !top || bottom->neighbors[2] != nullptr || top->neighbors[3] != nullptr) {
            return fail("non-adjacent solid sub-chunks should not be linked before a middle section exists");
        }

        chunk.setBlockLight(0, 16, 0, 5);
        SubChunk* middle = chunk.getSubChunk(1);
        if (!middle || bottom->neighbors[2] != middle || middle->neighbors[3] != bottom || top->neighbors[3] != middle || middle->neighbors[2] != top) {
            return fail("materialized light-only sub-chunks should wire vertical neighbors while alive");
        }

        chunk.setBlockLight(0, 16, 0, 0);
        if (chunk.getSubChunk(1) != nullptr || bottom->neighbors[2] != nullptr || top->neighbors[3] != nullptr) {
            return fail("recycling a light-only sub-chunk should clear reciprocal vertical neighbor links");
        }
    }

    {
        Chunk left(0, 0);
        Chunk right(1, 0);
        left.neighbors[0] = &right;
        right.neighbors[1] = &left;

        left.setBlock(Chunk::SIZE_X - 1, 63, 8, BlockIds::STONE);
        right.setBlock(0, 64, 8, BlockIds::STONE);

        const ChunkMeshData meshData = buildMeshDataFor(left);
        const auto faceVertices = collectTopFaceVertices(meshData, 15.0f, 16.0f, 64.0f, 8.0f, 9.0f);
        if (faceVertices.size() != 6) {
            return fail("expected one top face worth of vertices for boundary AO regression case");
        }

        const bool hasOccludedCorner = std::any_of(faceVertices.begin(), faceVertices.end(),
                                                   [](const BlockVertex* vertex) {
                                                       return vertex && vertex->ao < 3.0f - 0.001f;
                                                   });
        if (!hasOccludedCorner) {
            return fail("top-face AO should account for neighbor blocks across chunk and sub-chunk edges");
        }
    }

    {
        Chunk left(0, 0);
        Chunk right(1, 0);
        left.neighbors[0] = &right;
        right.neighbors[1] = &left;

        left.setBlock(Chunk::SIZE_X - 1, 63, 8, BlockIds::STONE);
        right.setBlockLight(0, 64, 8, 12);

        const ChunkMeshData meshData = buildMeshDataFor(left);
        const auto faceVertices = collectTopFaceVertices(meshData, 15.0f, 16.0f, 64.0f, 8.0f, 9.0f);
        if (faceVertices.size() != 6) {
            return fail("expected one top face worth of vertices for boundary block-light regression case");
        }

        const bool receivedNeighborLight = std::any_of(faceVertices.begin(), faceVertices.end(),
                                                       [](const BlockVertex* vertex) {
                                                           return vertex && vertex->blockLight > 0.001f;
                                                       });
        if (!receivedNeighborLight) {
            return fail("top-face block light should sample across chunk and sub-chunk halo positions");
        }
    }

    {
        Chunk chunk(0, 0);
        for (int x = 0; x < 4; ++x) {
            chunk.setBlock(x, 32, 0, BlockIds::DIRT);
        }

        const ChunkMeshData meshData = buildMeshDataFor(chunk);

        if (meshData.opaqueFaceCountBeforeGreedy != 18) {
            return fail("unexpected raw opaque face count for 4-block strip");
        }
        if (meshData.opaqueFaceCountAfterGreedy >= meshData.opaqueFaceCountBeforeGreedy) {
            return fail("greedy meshing should reduce opaque face count for a flat strip");
        }
        if (meshData.opaqueVertices.size() != static_cast<size_t>(meshData.opaqueFaceCountAfterGreedy) * 6) {
            return fail("opaque vertex count should stay aligned to merged quad count");
        }
        if (meshData.transparentFaceCountBeforeGreedy != 0 || meshData.transparentFaceCountAfterGreedy != 0) {
            return fail("opaque strip should not contribute transparent greedy stats");
        }
        if (!meshData.transparentVertices.empty() ||
            !meshData.cutoutVertices.empty() ||
            !meshData.cutoutDistanceVertices.empty()) {
            return fail("opaque strip should not emit transparent or cutout geometry");
        }
    }

    {
        Chunk chunk(0, 0);
        for (int x = 0; x < 4; ++x) {
            chunk.setBlock(x, 32, 0, FluidState::makeWater(0, false));
        }

        const ChunkMeshData meshData = buildMeshDataFor(chunk);

        if (meshData.opaqueFaceCountBeforeGreedy != 0 || meshData.opaqueFaceCountAfterGreedy != 0) {
            return fail("transparent blocks should not enter the opaque greedy path");
        }
        if (meshData.transparentFaceCountBeforeGreedy != 18) {
            return fail("unexpected raw transparent face count for 4-block water strip");
        }
        if (meshData.transparentFaceCountAfterGreedy == 0 ||
            meshData.transparentFaceCountAfterGreedy > meshData.transparentFaceCountBeforeGreedy) {
            return fail("water meshing should report a sane emitted quad count");
        }
        if (meshData.waterVertices.size() != static_cast<size_t>(meshData.transparentFaceCountAfterGreedy) * 6) {
            return fail("water vertex count should stay aligned to merged quad count");
        }
        if (!meshData.transparentVertices.empty()) {
            return fail("water strip should not emit generic transparent geometry");
        }
        const bool hasAnimatedWaterVertex = std::any_of(meshData.waterVertices.begin(),
                                                        meshData.waterVertices.end(),
                                                        [](const BlockVertex& vertex) {
                                                            return vertex.animated > 0.5f &&
                                                                   vertex.animationFrameCount >= 32.0f &&
                                                                   vertex.animationFps > 0.0f;
                                                        });
        if (!hasAnimatedWaterVertex) {
            return fail("water vertices should carry animation sampling metadata");
        }
        if (!meshData.cutoutVertices.empty() || !meshData.cutoutDistanceVertices.empty()) {
            return fail("water strip should not emit cutout geometry");
        }
    }

    {
        Chunk chunk(0, 0);
        chunk.setBlock(0, 32, 0, FluidState::makeWater(0, false));
        chunk.setBlock(1, 32, 0, BlockIds::GLASS);

        const ChunkMeshData meshData = buildMeshDataFor(chunk);

        if (meshData.transparentFaceCountBeforeGreedy != 12) {
            return fail("different transparent cube materials should keep both interface faces");
        }
        if (meshData.transparentFaceCountAfterGreedy != meshData.transparentFaceCountBeforeGreedy) {
            return fail("different transparent cube materials must not merge together");
        }
    }

    {
        Chunk leftChunk(0, 0);
        Chunk rightChunk(1, 0);
        leftChunk.neighbors[0] = &rightChunk;
        rightChunk.neighbors[1] = &leftChunk;

        leftChunk.setBlock(Chunk::SIZE_X - 1, 32, 0, FluidState::makeWater(0, false));
        rightChunk.setBlock(0, 32, 0, FluidState::makeWater(0, false));

        const ChunkMeshData meshData = buildMeshDataFor(leftChunk);

        if (meshData.transparentFaceCountBeforeGreedy != 5 || meshData.transparentFaceCountAfterGreedy != 5) {
            return fail("same-water faces across chunk borders should be culled");
        }
        if (meshData.waterVertices.size() != 30) {
            return fail("cross-chunk water culling should leave exactly five faces for one boundary block");
        }
    }

    {
        Chunk chunk(0, 0);
        chunk.setBlock(Chunk::SIZE_X - 1, 32, Chunk::SIZE_Z - 1, FluidState::makeWater(0, false));

        const ChunkMeshData meshData = buildMeshDataFor(chunk);
        const auto topFaceVertices = collectFaceVertices(
            meshData.waterVertices,
            0.0f,
            15.0f, 16.0f,
            32.0f, 33.0f,
            15.0f, 16.0f);
        if (topFaceVertices.size() != 6) {
            return fail("expected one top face worth of vertices for water chunk-corner light regression case");
        }

        const bool hasDarkCorner = std::any_of(topFaceVertices.begin(), topFaceVertices.end(),
                                               [](const BlockVertex* vertex) {
                                                   return vertex != nullptr && vertex->sunlight < 250;
                                               });
        if (hasDarkCorner) {
            return fail("water top faces at chunk corners should not average missing halo light as darkness");
        }
    }

    {
        Chunk chunk(0, 0);
        chunk.setBlock(0, 32, 0, FluidState::makeWater(0, false));
        chunk.setBlock(1, 32, 0, BlockIds::TALL_GRASS);

        const ChunkMeshData meshData = buildMeshDataFor(chunk);

        if (meshData.transparentFaceCountBeforeGreedy != 6 || meshData.transparentFaceCountAfterGreedy != 6) {
            return fail("isolated water block should still contribute six transparent faces");
        }
        if (meshData.waterVertices.empty()) {
            return fail("water should still emit transparent geometry when mixed with cutout blocks");
        }
        if (meshData.cutoutVertices.empty() && meshData.cutoutDistanceVertices.empty()) {
            return fail("tall grass should stay in the cutout pass");
        }
    }

    {
        Chunk chunk(0, 0);
        SubChunk* sc = chunk.getOrCreateSubChunk(2);
        if (!sc) {
            return fail("creating a fluid-layer regression cell should allocate a sub-chunk");
        }
        sc->setFluidLayer(0, 0, 0, FluidState::makeWater(0, false));

        if (ChunkMesher::shouldSkipSubChunk(chunk, 2)) {
            return fail("fluid-layer-only sub-chunks should still be meshed");
        }

        const ChunkMeshData meshData = buildMeshDataFor(chunk);
        const auto topFaceVertices = collectFaceVertices(
            meshData.waterVertices,
            0.0f,
            0.0f, 1.0f,
            33.0f, 33.0f,
            0.0f, 1.0f);
        if (topFaceVertices.size() != 6) {
            return fail("fluid layer water should keep rendering its top face after the host block is removed");
        }
    }

    {
        Chunk chunk(0, 0);
        chunk.setBlock(0, 32, 0, FluidState::makeWater(3, false));
        chunk.setBlock(1, 32, 0, FluidState::makeWater(0, false));

        const ChunkMeshData meshData = buildMeshDataFor(chunk);
        float topMinY = 9999.0f;
        float topMaxY = -9999.0f;
        for (const BlockVertex& vertex : meshData.waterVertices) {
            if (!approxEqual(vertex.normal, 0.0f)) {
                continue;
            }
            topMinY = std::min(topMinY, vertex.y);
            topMaxY = std::max(topMaxY, vertex.y);
        }

        if (topMinY < 32.0f || topMaxY >= 32.95f) {
            return fail("level-3 water should render below full block height");
        }
        if (!(topMinY + 0.01f < topMaxY)) {
            return fail("neighboring water heights should produce a sloped top surface");
        }
    }

    {
        Chunk eastFlowChunk(0, 0);
        eastFlowChunk.setBlock(0, 32, 0, FluidState::makeWater(0, false));
        eastFlowChunk.setBlock(1, 32, 0, FluidState::makeWater(3, false));
        const ChunkMeshData meshData = buildMeshDataFor(eastFlowChunk);
        const auto topFaceVertices = collectFaceVertices(
            meshData.waterVertices,
            0.0f,
            0.0f, 1.0f,
            32.0f, 33.0f,
            0.0f, 1.0f);
        if (topFaceVertices.size() < 6) {
            return fail("expected water top-face vertices for flow-direction UV test");
        }
        const bool hasFlowAnimatedTopVertex = std::any_of(topFaceVertices.begin(), topFaceVertices.end(),
                                                          [](const BlockVertex* vertex) {
                                                              return vertex != nullptr &&
                                                                     vertex->animated > 0.5f &&
                                                                     vertex->animationFrameCount >= 32.0f &&
                                                                     vertex->animationFps >= 8.0f;
                                                          });
        if (!hasFlowAnimatedTopVertex) {
            return fail("flowing water top face should use flow animation metadata after meshing");
        }

        const bool hasEastFlowUv = std::any_of(topFaceVertices.begin(), topFaceVertices.end(),
                                               [](const BlockVertex* vertex) {
                                                   return vertex != nullptr &&
                                                          approxEqual(vertex->x, 1.0f) &&
                                                          approxEqual(vertex->z, 1.0f) &&
                                                          approxEqual(vertex->u, 0.0f) &&
                                                          approxEqual(vertex->v, 1.0f);
                                               });
        if (!hasEastFlowUv) {
            return fail("east-flowing water top face should rotate UVs along the flow direction");
        }
    }

    {
        Chunk fallingChunk(0, 0);
        fallingChunk.setBlock(0, 32, 0, FluidState::makeWater(0, true));
        const ChunkMeshData meshData = buildMeshDataFor(fallingChunk);
        const auto frontFaceVertices = collectFaceVertices(
            meshData.waterVertices,
            2.0f,
            0.0f, 1.0f,
            32.0f, 33.0f,
            1.0f, 1.0f);
        if (frontFaceVertices.size() < 6) {
            return fail("expected falling water front-face vertices for flow-direction UV test");
        }

        float minFallingY = frontFaceVertices[0]->y;
        float maxFallingY = frontFaceVertices[0]->y;
        for (const BlockVertex* vertex : frontFaceVertices) {
            if (vertex == nullptr) {
                continue;
            }
            minFallingY = std::min(minFallingY, vertex->y);
            maxFallingY = std::max(maxFallingY, vertex->y);
        }

        const bool hasBottomLowV = std::any_of(frontFaceVertices.begin(), frontFaceVertices.end(),
                                               [minFallingY](const BlockVertex* vertex) {
                                                   return vertex != nullptr &&
                                                          approxEqual(vertex->y, minFallingY) &&
                                                          approxEqual(vertex->v, 0.0f);
                                               });
        const bool hasTopHighV = std::any_of(frontFaceVertices.begin(), frontFaceVertices.end(),
                                             [maxFallingY](const BlockVertex* vertex) {
                                                 return vertex != nullptr &&
                                                        approxEqual(vertex->y, maxFallingY) &&
                                                        approxEqual(vertex->v, 1.0f);
                                             });
        if (!hasBottomLowV || !hasTopHighV) {
            return fail("falling water front face should keep its vertical UV direction");
        }

        const bool hasFlowAnimation = std::any_of(frontFaceVertices.begin(), frontFaceVertices.end(),
                                                  [](const BlockVertex* vertex) {
                                                      return vertex != nullptr &&
                                                             vertex->animated > 0.5f &&
                                                             vertex->animationFps >= 8.0f;
                                                  });
        if (!hasFlowAnimation) {
            return fail("falling water side face should use flow animation metadata");
        }

        const auto backFaceVertices = collectFaceVertices(
            meshData.waterVertices,
            3.0f,
            0.0f, 1.0f,
            32.0f, 33.0f,
            0.0f, 0.0f);
        if (backFaceVertices.size() < 6) {
            return fail("expected falling water back-face vertices for flow-direction UV test");
        }

        float minBackY = backFaceVertices[0]->y;
        float maxBackY = backFaceVertices[0]->y;
        for (const BlockVertex* vertex : backFaceVertices) {
            if (vertex == nullptr) {
                continue;
            }
            minBackY = std::min(minBackY, vertex->y);
            maxBackY = std::max(maxBackY, vertex->y);
        }

        const bool hasBackBottomHighV = std::any_of(backFaceVertices.begin(), backFaceVertices.end(),
                                                    [minBackY](const BlockVertex* vertex) {
                                                        return vertex != nullptr &&
                                                               approxEqual(vertex->y, minBackY) &&
                                                               approxEqual(vertex->v, 1.0f);
                                                    });
        const bool hasBackTopLowV = std::any_of(backFaceVertices.begin(), backFaceVertices.end(),
                                                [maxBackY](const BlockVertex* vertex) {
                                                    return vertex != nullptr &&
                                                           approxEqual(vertex->y, maxBackY) &&
                                                           approxEqual(vertex->v, 0.0f);
                                                });
        if (!hasBackBottomHighV || !hasBackTopLowV) {
            return fail("falling water back face should flip vertical UVs to match flow direction");
        }

        const auto rightFaceVertices = collectFaceVertices(
            meshData.waterVertices,
            5.0f,
            1.0f, 1.0f,
            32.0f, 33.0f,
            0.0f, 1.0f);
        if (rightFaceVertices.size() < 6) {
            return fail("expected falling water right-face vertices for flow-direction UV test");
        }

        float minRightY = rightFaceVertices[0]->y;
        float maxRightY = rightFaceVertices[0]->y;
        for (const BlockVertex* vertex : rightFaceVertices) {
            if (vertex == nullptr) {
                continue;
            }
            minRightY = std::min(minRightY, vertex->y);
            maxRightY = std::max(maxRightY, vertex->y);
        }

        const bool hasRightBottomHighV = std::any_of(rightFaceVertices.begin(), rightFaceVertices.end(),
                                                     [minRightY](const BlockVertex* vertex) {
                                                         return vertex != nullptr &&
                                                                approxEqual(vertex->y, minRightY) &&
                                                                approxEqual(vertex->v, 1.0f);
                                                     });
        const bool hasRightTopLowV = std::any_of(rightFaceVertices.begin(), rightFaceVertices.end(),
                                                 [maxRightY](const BlockVertex* vertex) {
                                                     return vertex != nullptr &&
                                                            approxEqual(vertex->y, maxRightY) &&
                                                            approxEqual(vertex->v, 0.0f);
                                                 });
        if (!hasRightBottomHighV || !hasRightTopLowV) {
            return fail("falling water side faces should flip vertical UVs to match flow direction");
        }
    }

    {
        Chunk chunk(0, 0);
        const StateID birchLogX = BlockStateRegistry::getState(
            BlockIds::BIRCH_LOG,
            std::vector<std::pair<uint16_t, uint16_t>>{
                {PropIndices::AXIS, PropIndices::AXIS_X}
            });
        chunk.setBlock(0, 32, 0, birchLogX);

        const ChunkMeshData meshData = buildMeshDataFor(chunk);
        const auto frontFaceVertices = collectFaceVertices(
            meshData.opaqueVertices,
            2.0f,
            0.0f, 1.0f,
            32.0f, 32.0f,
            1.0f, 1.0f);

        if (frontFaceVertices.size() != 3) {
            return fail("expected one front-face triangle worth of bottom-edge vertices for rotated log UV test");
        }

        const bool hasSharedU = approxEqual(frontFaceVertices[0]->u, frontFaceVertices[1]->u) ||
                                approxEqual(frontFaceVertices[1]->u, frontFaceVertices[2]->u) ||
                                approxEqual(frontFaceVertices[0]->u, frontFaceVertices[2]->u);
        const bool hasDifferentV = !approxEqual(frontFaceVertices[0]->v, frontFaceVertices[1]->v) ||
                                   !approxEqual(frontFaceVertices[1]->v, frontFaceVertices[2]->v) ||
                                   !approxEqual(frontFaceVertices[0]->v, frontFaceVertices[2]->v);
        if (!hasSharedU || !hasDifferentV) {
            return fail("x-axis log bark should rotate front-face UVs so texture direction follows the log axis");
        }
    }

    {
        constexpr float kTorchHeight = 10.0f / 16.0f;
        constexpr float kTorchHalfWidth = 1.0f / 16.0f;
        constexpr float kTorchTopU0 = 7.0f / 16.0f;
        constexpr float kTorchTopU1 = 9.0f / 16.0f;
        constexpr float kTorchTopV0 = 1.0f - 8.0f / 16.0f;
        constexpr float kTorchTopV1 = 1.0f - 6.0f / 16.0f;

        Chunk chunk(0, 0);
        chunk.setBlock(0, 32, 0, BlockStateRegistry::getDefaultState(BlockIds::TORCH));

        const ChunkMeshData meshData = buildMeshDataFor(chunk);
        if (meshData.cutoutVertices.size() != 36) {
            return fail("floor torch should emit the rebuilt torch template in the cutout pass");
        }

        float minY = meshData.cutoutVertices.front().y;
        float maxY = meshData.cutoutVertices.front().y;
        for (const BlockVertex& vertex : meshData.cutoutVertices) {
            if (vertex.normal < 0.0f || vertex.normal > 5.0f) {
                return fail("torch template should use regular face normals instead of cross markers");
            }
            minY = std::min(minY, vertex.y);
            maxY = std::max(maxY, vertex.y);
        }
        if (!approxEqual(minY, 32.0f) || !approxEqual(maxY, 33.0f)) {
            return fail("floor torch planes should span the full texture height after rebuild");
        }

        float minU = meshData.cutoutVertices.front().u;
        float maxU = meshData.cutoutVertices.front().u;
        float minV = meshData.cutoutVertices.front().v;
        float maxV = meshData.cutoutVertices.front().v;
        for (const BlockVertex& vertex : meshData.cutoutVertices) {
            minU = std::min(minU, vertex.u);
            maxU = std::max(maxU, vertex.u);
            minV = std::min(minV, vertex.v);
            maxV = std::max(maxV, vertex.v);
        }
        if (!approxEqual(minU, 0.0f) || !approxEqual(maxU, 1.0f) ||
            !approxEqual(minV, 0.0f) || !approxEqual(maxV, 1.0f)) {
            return fail("torch planes should sample the full torch texture after rebuild");
        }

        const auto topFaceVertices = collectFaceVertices(meshData.cutoutVertices,
                                                         0.0f,
                                                         0.5f - kTorchHalfWidth,
                                                         0.5f + kTorchHalfWidth,
                                                         32.0f + kTorchHeight,
                                                         32.0f + kTorchHeight,
                                                         0.5f - kTorchHalfWidth,
                                                         0.5f + kTorchHalfWidth);
        if (topFaceVertices.size() != 6) {
            return fail("floor torch should keep a 2x2 core top face");
        }
        float topMinV = topFaceVertices.front()->v;
        float topMaxV = topFaceVertices.front()->v;
        float topMinU = topFaceVertices.front()->u;
        float topMaxU = topFaceVertices.front()->u;
        for (const BlockVertex* vertex : topFaceVertices) {
            topMinV = std::min(topMinV, vertex->v);
            topMaxV = std::max(topMaxV, vertex->v);
            topMinU = std::min(topMinU, vertex->u);
            topMaxU = std::max(topMaxU, vertex->u);
        }
        if (!approxEqual(topMinU, kTorchTopU0) || !approxEqual(topMaxU, kTorchTopU1) ||
            !approxEqual(topMinV, kTorchTopV0) || !approxEqual(topMaxV, kTorchTopV1)) {
            return fail("torch core top face should sample the flame cap source texels");
        }
    }

    {
        Chunk chunk(0, 0);
        const StateID northTorch = BlockStateRegistry::getState(
            BlockIds::TORCH,
            std::vector<std::pair<uint16_t, uint16_t>>{
                {PropIndices::FACING, PropIndices::FACING_NORTH}
            });
        chunk.setBlock(0, 32, 0, northTorch);

        const ChunkMeshData meshData = buildMeshDataFor(chunk);
        if (meshData.cutoutVertices.size() != 36) {
            return fail("wall torch should emit the rebuilt torch template in the cutout pass");
        }

        float minZ = meshData.cutoutVertices.front().z;
        float maxZ = meshData.cutoutVertices.front().z;
        for (const BlockVertex& vertex : meshData.cutoutVertices) {
            minZ = std::min(minZ, vertex.z);
            maxZ = std::max(maxZ, vertex.z);
        }
        if (minZ > 0.1f || maxZ < 0.45f) {
            return fail("north wall torch should lean out from the north wall after rebuild");
        }
    }

    std::cout << "[chunk_mesher_test] PASS\n";
    return EXIT_SUCCESS;
}
