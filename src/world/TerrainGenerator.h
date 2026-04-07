#ifndef MECRAFT_TERRAINGENERATOR_H
#define MECRAFT_TERRAINGENERATOR_H

#include <cstdint>

#include "Chunk.h"

class TerrainGenerator {
public:
    void init(uint32_t seed, int seaLevel);
    void generateChunk(Chunk& chunk) const;
    [[nodiscard]] int sampleSurfaceY(int worldX, int worldZ) const;
    void sampleSurfaceYBatch(int startWorldX, int worldZ, int count, int* outSurfaceY) const;

private:
    [[nodiscard]] double sampleMoisture(int worldX, int worldZ) const;
    [[nodiscard]] bool shouldCarveCave(int worldX, int y, int worldZ, int surfaceY) const;
    [[nodiscard]] BlockID sampleOreBlock(int worldX, int y, int worldZ, BlockID baseBlock) const;

    uint32_t m_seed = 0;
    int m_seaLevel = 63;
};

#endif // MECRAFT_TERRAINGENERATOR_H

