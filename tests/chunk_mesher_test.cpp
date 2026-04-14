#include <cstdlib>
#include <iostream>

#include "../src/renderer/ChunkMesher.h"

namespace {
int fail(const char* message) {
    std::cerr << "[chunk_mesher_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

ChunkMeshData buildMeshDataFor(const Chunk& chunk) {
    const ChunkMeshingSnapshot snapshot = ChunkMesher::captureSnapshot(chunk);
    return ChunkMesher::buildMeshData(snapshot);
}
}

int main() {
    BlockRegistry::init(nullptr);

    {
        Chunk chunk(0, 0);
        for (int x = 0; x < 4; ++x) {
            chunk.setBlock(x, 32, 0, BlockType::DIRT);
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
        if (!meshData.transparentVertices.empty() || !meshData.cutoutVertices.empty()) {
            return fail("opaque strip should not emit transparent or cutout geometry");
        }
    }

    {
        Chunk chunk(0, 0);
        for (int x = 0; x < 4; ++x) {
            chunk.setBlock(x, 32, 0, BlockType::WATER);
        }

        const ChunkMeshData meshData = buildMeshDataFor(chunk);

        if (meshData.opaqueFaceCountBeforeGreedy != 0 || meshData.opaqueFaceCountAfterGreedy != 0) {
            return fail("transparent blocks should not enter the opaque greedy path");
        }
        if (meshData.transparentFaceCountBeforeGreedy != 18) {
            return fail("unexpected raw transparent face count for 4-block water strip");
        }
        if (meshData.transparentFaceCountAfterGreedy >= meshData.transparentFaceCountBeforeGreedy) {
            return fail("transparent greedy meshing should reduce water face count");
        }
        if (meshData.transparentVertices.size() != static_cast<size_t>(meshData.transparentFaceCountAfterGreedy) * 6) {
            return fail("transparent vertex count should stay aligned to merged quad count");
        }
        if (!meshData.cutoutVertices.empty()) {
            return fail("water strip should not emit cutout geometry");
        }
    }

    {
        Chunk chunk(0, 0);
        chunk.setBlock(0, 32, 0, BlockType::WATER);
        chunk.setBlock(1, 32, 0, BlockType::GLASS);

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

        leftChunk.setBlock(Chunk::SIZE_X - 1, 32, 0, BlockType::WATER);
        rightChunk.setBlock(0, 32, 0, BlockType::WATER);

        const ChunkMeshData meshData = buildMeshDataFor(leftChunk);

        if (meshData.transparentFaceCountBeforeGreedy != 5 || meshData.transparentFaceCountAfterGreedy != 5) {
            return fail("same-water faces across chunk borders should be culled");
        }
        if (meshData.transparentVertices.size() != 30) {
            return fail("cross-chunk water culling should leave exactly five faces for one boundary block");
        }
    }

    {
        Chunk chunk(0, 0);
        chunk.setBlock(0, 32, 0, BlockType::WATER);
        chunk.setBlock(1, 32, 0, BlockType::TALL_GRASS);

        const ChunkMeshData meshData = buildMeshDataFor(chunk);

        if (meshData.transparentFaceCountBeforeGreedy != 6 || meshData.transparentFaceCountAfterGreedy != 6) {
            return fail("isolated water block should still contribute six transparent faces");
        }
        if (meshData.transparentVertices.empty()) {
            return fail("water should still emit transparent geometry when mixed with cutout blocks");
        }
        if (meshData.cutoutVertices.empty()) {
            return fail("tall grass should stay in the cutout pass");
        }
    }

    std::cout << "[chunk_mesher_test] PASS\n";
    return EXIT_SUCCESS;
}
