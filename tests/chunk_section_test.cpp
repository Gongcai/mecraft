#include <cstdlib>
#include <iostream>
#include <vector>

#include "../src/world/Chunk.h"

namespace {
int fail(const char* message) {
    std::cerr << "[chunk_section_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void clearDirty(Chunk& chunk) {
    chunk.markMeshClean();
}
} // namespace

int main() {
    BlockRegistry::init(nullptr);

    if (Chunk::toSubChunkIndex(0) != 0 ||
        Chunk::toSubChunkIndex(15) != 0 ||
        Chunk::toSubChunkIndex(16) != 1 ||
        Chunk::toSubChunkIndex(255) != 15) {
        return fail("toSubChunkIndex should map 16-block slices correctly");
    }

    if (Chunk::toSubChunkLocalY(0) != 0 ||
        Chunk::toSubChunkLocalY(15) != 15 ||
        Chunk::toSubChunkLocalY(16) != 0 ||
        Chunk::toSubChunkLocalY(31) != 15) {
        return fail("toSubChunkLocalY should preserve in-sub-chunk Y coordinates");
    }

    {
        Chunk chunk(0, 0);
        if (chunk.getSubChunk(5) != nullptr || chunk.getSubChunkMeshRevision(5) != 0) {
            return fail("fresh chunks should not allocate or revision-track missing air sub-chunks");
        }

        chunk.markSubChunkDirty(5);
        if (chunk.getSubChunk(5) != nullptr || chunk.getSubChunkMeshRevision(5) != 0) {
            return fail("markSubChunkDirty should not instantiate missing air sub-chunks");
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

        center.getOrCreateSubChunk(0);
        center.getOrCreateSubChunk(1);
        center.getOrCreateSubChunk(2);
        posX.getOrCreateSubChunk(2);
        negX.getOrCreateSubChunk(2);
        posZ.getOrCreateSubChunk(2);
        negZ.getOrCreateSubChunk(2);

        clearDirty(center);
        clearDirty(posX);
        clearDirty(negX);
        clearDirty(posZ);
        clearDirty(negZ);

        const uint64_t baseRevision = center.getSubChunkMeshRevision(1);
        center.setBlock(4, 20, 4, BlockIds::STONE);
        if (!center.isSubChunkDirty(1) || center.isSubChunkDirty(0) || center.isSubChunkDirty(2)) {
            return fail("interior edits should dirty only the owning sub-chunk");
        }
        if (center.getSubChunkMeshRevision(1) <= baseRevision) {
            return fail("owning sub-chunk revision should advance after edits");
        }
        if (posX.isDirty() || negX.isDirty() || posZ.isDirty() || negZ.isDirty()) {
            return fail("interior edits should not dirty neighboring chunks");
        }

        clearDirty(center);
        clearDirty(posX);
        clearDirty(negX);
        clearDirty(posZ);
        clearDirty(negZ);

        center.setBlock(4, 15, 4, BlockIds::STONE);
        if (!center.isSubChunkDirty(0) || !center.isSubChunkDirty(1) || center.isSubChunkDirty(2)) {
            return fail("top/bottom boundary edits should dirty both touching sub-chunks");
        }

        clearDirty(center);
        clearDirty(posX);
        clearDirty(negX);
        clearDirty(posZ);
        clearDirty(negZ);

        center.setBlock(0, 33, 5, BlockIds::STONE);
        if (!center.isSubChunkDirty(2)) {
            return fail("chunk-border edits should keep the local dirty sub-chunk");
        }
        if (!negX.isSubChunkDirty(2)) {
            return fail("x-border edits should dirty the matching neighbor sub-chunk");
        }
        if (posX.isDirty() || posZ.isDirty() || negZ.isDirty()) {
            return fail("x-border edits should not dirty unrelated neighbors");
        }

        clearDirty(center);
        clearDirty(posX);
        clearDirty(negX);
        clearDirty(posZ);
        clearDirty(negZ);

        center.setBlock(0, 64, 5, BlockIds::STONE);
        if (!center.isSubChunkDirty(4) || !center.isSubChunkDirty(3)) {
            return fail("boundary edits on a sub-chunk floor should dirty both local touching sub-chunks");
        }
        if (!negX.isSubChunkDirty(4) || !negX.isSubChunkDirty(3)) {
            return fail("cross-chunk boundary edits on a sub-chunk floor should dirty the neighbor's touching sub-chunks");
        }
    }

    {
        Chunk chunk(2, 3);
        clearDirty(chunk);
        chunk.setBlock(2, 33, 4, BlockIds::STONE);

        SubChunk* sc = chunk.getSubChunk(2);
        if (!sc) {
            return fail("setBlock should materialize the owning sub-chunk");
        }
        if (!chunk.isSubChunkDirty(2) || !chunk.isDirty()) {
            return fail("setBlock should dirty the owning sub-chunk and column");
        }
        if (sc->getBlock(2, 1, 4) != BlockIds::STONE || chunk.getBlock(2, 33, 4) != BlockIds::STONE) {
            return fail("block reads should round-trip through sub-chunk-backed storage");
        }
    }

    {
        Chunk chunk(0, 0);
        chunk.setBlock(1, 63, 1, BlockIds::STONE);
        chunk.recalcHeightMap(1, 1);
        clearDirty(chunk);

        std::vector<uint8_t> packed(Chunk::BLOCK_COUNT);
        for (int y = 0; y < Chunk::SIZE_Y; ++y) {
            for (int z = 0; z < Chunk::SIZE_Z; ++z) {
                for (int x = 0; x < Chunk::SIZE_X; ++x) {
                    packed[Chunk::toIndex(x, y, z)] = chunk.getPackedLight(x, y, z);
                }
            }
        }

        packed[Chunk::toIndex(1, 64, 1)] = 0xE0;
        if (!chunk.replacePackedLight(packed.data(), packed.size(), nullptr)) {
            return fail("replacePackedLight should accept a full packed-light snapshot");
        }
        if (!chunk.isSubChunkDirty(3)) {
            return fail("light changes in the section above should dirty the lower neighbor mesh section");
        }
        if (!chunk.isSubChunkDirty(4)) {
            return fail("replacePackedLight should still dirty the directly changed light section");
        }
    }

    std::cout << "[chunk_section_test] PASS\n";
    return EXIT_SUCCESS;
}
