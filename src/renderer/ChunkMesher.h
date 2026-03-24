#ifndef MECRAFT_CHUNKMESHER_H
#define MECRAFT_CHUNKMESHER_H

#include <vector>

#include <glm/vec3.hpp>

#include "../resource/ResourceMgr.h"
#include "../world/Chunk.h"

class ChunkMesher {
public:
    static void generateMesh(Chunk& chunk, const TextureAtlas& atlas);

private:
    static bool shouldRenderFace(const Chunk& chunk,
                                 int nx,
                                 int ny,
                                 int nz,
                                 BlockID currentId);

    static void addFace(std::vector<BlockVertex>& vertices,
                        const glm::vec3& pos,
                        int face,
                        const BlockDef& def,
                        const TextureAtlas& atlas,
                        uint8_t sunlight,
                        uint8_t blockLight,
                        const float aoValues[4]);

    static uint8_t calcAO(bool side1, bool side2, bool corner);
};

#endif // MECRAFT_CHUNKMESHER_H


