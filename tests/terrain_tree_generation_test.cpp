#include <cstdlib>
#include <cmath>
#include <iostream>

#include "../src/world/FluidState.h"
#include "../src/world/TerrainGenerator.h"
#include "../src/world/World.h"

namespace {
constexpr uint32_t kSeed = 20260508;
constexpr int kSeaLevel = 63;

int fail(const char* message) {
    std::cerr << "[terrain_tree_generation_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

int worldToChunkCoord(int world, int chunkSize) {
    return static_cast<int>(std::floor(static_cast<float>(world) / static_cast<float>(chunkSize)));
}

bool isTreeBlock(BlockID id) {
    return id == BlockIds::WOOD ||
           id == BlockIds::BIRCH_LOG ||
           id == BlockIds::OAK_LEAVES ||
           id == BlockIds::BIRCH_LEAVES;
}

bool isLog(BlockID id) {
    return id == BlockIds::WOOD || id == BlockIds::BIRCH_LOG;
}

struct TreeRoot {
    int x = 0;
    int z = 0;
    int surfaceY = 0;
};

bool findTreeRoot(const TerrainGenerator& generator, TreeRoot& outRoot) {
    for (int z = -128; z <= 128; ++z) {
        for (int x = -128; x <= 128; ++x) {
            const int surfaceY = generator.sampleSurfaceY(x, z);
            if (generator.sampleBlock(x, surfaceY, z) != BlockIds::GRASS) {
                continue;
            }
            if (isLog(generator.sampleBlock(x, surfaceY + 1, z))) {
                outRoot = TreeRoot{x, z, surfaceY};
                return true;
            }
        }
    }
    return false;
}

bool findCrossChunkTreeBlock(const TerrainGenerator& generator,
                             TreeRoot& outRoot,
                             int& outX,
                             int& outY,
                             int& outZ,
                             BlockID& outBlock) {
    for (int z = -192; z <= 192; ++z) {
        for (int x = -192; x <= 192; ++x) {
            const int localX = x - worldToChunkCoord(x, Chunk::SIZE_X) * Chunk::SIZE_X;
            const int localZ = z - worldToChunkCoord(z, Chunk::SIZE_Z) * Chunk::SIZE_Z;
            if (localX > 1 && localX < Chunk::SIZE_X - 2 &&
                localZ > 1 && localZ < Chunk::SIZE_Z - 2) {
                continue;
            }

            const int surfaceY = generator.sampleSurfaceY(x, z);
            if (!isLog(generator.sampleBlock(x, surfaceY + 1, z))) {
                continue;
            }

            const int rootChunkX = worldToChunkCoord(x, Chunk::SIZE_X);
            const int rootChunkZ = worldToChunkCoord(z, Chunk::SIZE_Z);
            for (int dz = -2; dz <= 2; ++dz) {
                for (int dx = -2; dx <= 2; ++dx) {
                    const int targetX = x + dx;
                    const int targetZ = z + dz;
                    if (worldToChunkCoord(targetX, Chunk::SIZE_X) == rootChunkX &&
                        worldToChunkCoord(targetZ, Chunk::SIZE_Z) == rootChunkZ) {
                        continue;
                    }

                    for (int y = surfaceY + 1; y <= surfaceY + 8 && y < Chunk::SIZE_Y; ++y) {
                        const BlockID block = generator.sampleBlock(targetX, y, targetZ);
                        if (isTreeBlock(block)) {
                            outRoot = TreeRoot{x, z, surfaceY};
                            outX = targetX;
                            outY = y;
                            outZ = targetZ;
                            outBlock = block;
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

BlockID chunkBlockAt(const TerrainGenerator& generator, int worldX, int y, int worldZ) {
    const int chunkX = worldToChunkCoord(worldX, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(worldZ, Chunk::SIZE_Z);
    Chunk chunk(chunkX, chunkZ);
    generator.generateChunk(chunk);
    const int localX = worldX - chunkX * Chunk::SIZE_X;
    const int localZ = worldZ - chunkZ * Chunk::SIZE_Z;
    return chunk.getBlock(localX, y, localZ);
}

bool findUnsuitableRoot(const TerrainGenerator& generator, int& outX, int& outZ, int& outSurfaceY) {
    for (int z = -128; z <= 128; ++z) {
        for (int x = -128; x <= 128; ++x) {
            const int surfaceY = generator.sampleSurfaceY(x, z);
            const TerrainBiome biome = generator.sampleBiome(x, z);
            const BlockID surface = generator.sampleBlock(x, surfaceY, z);
            if (surfaceY < kSeaLevel ||
                biome == TerrainBiome::Arid ||
                biome == TerrainBiome::HighMountain ||
                FluidState::isWater(surface)) {
                outX = x;
                outZ = z;
                outSurfaceY = surfaceY;
                return true;
            }
        }
    }
    return false;
}

bool findNaturalWaterColumn(const TerrainGenerator& generator, int& outX, int& outZ, int& outSurfaceY) {
    for (int z = -128; z <= 128; ++z) {
        for (int x = -128; x <= 128; ++x) {
            const int surfaceY = generator.sampleSurfaceY(x, z);
            if (surfaceY < kSeaLevel) {
                outX = x;
                outZ = z;
                outSurfaceY = surfaceY;
                return true;
            }
        }
    }
    return false;
}
} // namespace

int main() {
    BlockRegistry::init(nullptr);

    TerrainGenerator generator;
    generator.init(kSeed, kSeaLevel);

    int waterX = 0;
    int waterZ = 0;
    int waterSurfaceY = 0;
    if (!findNaturalWaterColumn(generator, waterX, waterZ, waterSurfaceY)) {
        return fail("expected to find a deterministic natural water column");
    }

    const int waterY = waterSurfaceY + 1;
    const BlockID sampledWater = generator.sampleBlock(waterX, waterY, waterZ);
    if (!FluidState::isWater(sampledWater) || sampledWater == BlockIds::WATER) {
        return fail("sampled natural water should use the canonical fluid state");
    }
    if (chunkBlockAt(generator, waterX, waterY, waterZ) != sampledWater) {
        return fail("generated chunk water should match sampled canonical water state");
    }

    TreeRoot root;
    if (!findTreeRoot(generator, root)) {
        return fail("expected to find at least one deterministic tree root");
    }

    const BlockID rootBlock = generator.sampleBlock(root.x, root.surfaceY + 1, root.z);
    if (!isLog(rootBlock)) {
        return fail("tree root should place a log above grass");
    }
    if (chunkBlockAt(generator, root.x, root.surfaceY + 1, root.z) != rootBlock) {
        return fail("generated chunk should match sampled tree root block");
    }

    TerrainGenerator generator2;
    generator2.init(kSeed, kSeaLevel);
    if (generator2.sampleBlock(root.x, root.surfaceY + 1, root.z) != rootBlock) {
        return fail("same seed should produce the same tree root block");
    }

    TreeRoot crossingRoot;
    int crossX = 0;
    int crossY = 0;
    int crossZ = 0;
    BlockID crossBlock = 0;
    if (!findCrossChunkTreeBlock(generator, crossingRoot, crossX, crossY, crossZ, crossBlock)) {
        return fail("expected to find a tree crossing a chunk boundary");
    }
    if (chunkBlockAt(generator, crossingRoot.x, crossingRoot.surfaceY + 1, crossingRoot.z) !=
        generator.sampleBlock(crossingRoot.x, crossingRoot.surfaceY + 1, crossingRoot.z)) {
        return fail("root-side chunk should match sampled cross-boundary tree");
    }
    if (chunkBlockAt(generator, crossX, crossY, crossZ) != crossBlock) {
        return fail("neighbor-side chunk should include cross-boundary tree block");
    }

    World unloadedWorld;
    unloadedWorld.init(kSeed);
    if (unloadedWorld.sampleGeneratedBlock(root.x, root.surfaceY + 1, root.z) != rootBlock) {
        return fail("unloaded world sampling should include tree generation");
    }

    World loadedWorld;
    loadedWorld.init(kSeed);
    loadedWorld.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        loadedWorld.update(glm::vec3(static_cast<float>(root.x), 0.0f, static_cast<float>(root.z)));
    }
    if (loadedWorld.getBlock(root.x, root.surfaceY + 1, root.z) != rootBlock ||
        loadedWorld.sampleGeneratedBlock(root.x, root.surfaceY + 1, root.z) != rootBlock) {
        return fail("loaded world block should match generated tree sample");
    }

    int unsuitableX = 0;
    int unsuitableZ = 0;
    int unsuitableSurfaceY = 0;
    if (!findUnsuitableRoot(generator, unsuitableX, unsuitableZ, unsuitableSurfaceY)) {
        return fail("expected to find an unsuitable tree root location");
    }
    if (isLog(generator.sampleBlock(unsuitableX, unsuitableSurfaceY + 1, unsuitableZ))) {
        return fail("unsuitable terrain should not host a tree trunk");
    }

    std::cout << "[terrain_tree_generation_test] PASS\n";
    return EXIT_SUCCESS;
}
