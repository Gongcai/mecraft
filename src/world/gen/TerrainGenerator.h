#ifndef MECRAFT_TERRAINGENERATOR_H
#define MECRAFT_TERRAINGENERATOR_H

#include <array>
#include <cstdint>

#include "../chunk/Chunk.h"
#include "WorldGenerationMode.h"

enum class TerrainBiome : uint8_t {
    Temperate = 0,
    Arid = 1,
    Mountain = 2,
    HighMountain = 3,
};

struct TerrainLayerRule {
    int thickness = 0;
    BlockID blockId = RUNTIME_ID_NULL;
};

struct TerrainGenerationProfile {
    WorldGenerationMode mode = WorldGenerationMode::Default;
    int seaLevel = 63;
    double heightScale = 1.0;
    std::array<TerrainLayerRule, 3> flatLayers{};
    uint8_t flatLayerCount = 0;
    int flatSurfaceY = 0;
};

[[nodiscard]] TerrainGenerationProfile makeTerrainGenerationProfile(WorldGenerationMode mode, int seaLevel);

class TerrainGeneratorPerfAccess;

class TerrainGenerator {
public:
    void init(uint32_t seed, int seaLevel, WorldGenerationMode mode = WorldGenerationMode::Default);
    void init(uint32_t seed, TerrainGenerationProfile profile);
    [[nodiscard]] WorldGenerationMode mode() const { return m_profile.mode; }
    void generateChunk(Chunk& chunk) const;
    [[nodiscard]] BlockStateId sampleBlock(int worldX, int y, int worldZ) const;
    [[nodiscard]] int sampleSurfaceY(int worldX, int worldZ) const;
    [[nodiscard]] TerrainBiome sampleBiome(int worldX, int worldZ) const;
    void sampleSurfaceYBatch(int startWorldX, int worldZ, int count, int* outSurfaceY) const;

private:
    friend class TerrainGeneratorPerfAccess;

    [[nodiscard]] double sampleMoisture(int worldX, int worldZ) const;
    [[nodiscard]] bool shouldCarveCave(int worldX, int y, int worldZ, int surfaceY) const;
    [[nodiscard]] BlockID sampleOreBlock(int worldX, int y, int worldZ, BlockID baseBlock) const;

    uint32_t m_seed = 0;
    TerrainGenerationProfile m_profile{};
};

#endif // MECRAFT_TERRAINGENERATOR_H
