#include "TerrainGenerator.h"

#include <algorithm>
#include <cmath>

namespace {

double smoothStep(double t) {
    return t * t * (3.0 - 2.0 * t);
}

uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

double hashToUnit(uint32_t value) {
    return static_cast<double>(value) / static_cast<double>(0xFFFFFFFFU);
}

double hashedChance(uint32_t seed, int x, int y, int z, uint32_t salt) {
    uint32_t h = seed ^ salt;
    h ^= hash32(static_cast<uint32_t>(x) * 0x9e3779b9U);
    h ^= hash32(static_cast<uint32_t>(y) * 0x7f4a7c15U);
    h ^= hash32(static_cast<uint32_t>(z) * 0x94d049bbU);
    return hashToUnit(hash32(h));
}

double lattice2D(int x, int z, uint32_t seed) {
    uint32_t h = seed;
    h ^= hash32(static_cast<uint32_t>(x) * 0x27d4eb2dU);
    h ^= hash32(static_cast<uint32_t>(z) * 0x165667b1U);
    return hashToUnit(hash32(h));
}

double lattice3D(int x, int y, int z, uint32_t seed) {
    uint32_t h = seed;
    h ^= hash32(static_cast<uint32_t>(x) * 0x27d4eb2dU);
    h ^= hash32(static_cast<uint32_t>(y) * 0x85ebca6bU);
    h ^= hash32(static_cast<uint32_t>(z) * 0xc2b2ae35U);
    return hashToUnit(hash32(h));
}

double valueNoise2D(double x, double z, double cellSize, uint32_t seed) {
    const double scaledX = x / cellSize;
    const double scaledZ = z / cellSize;

    const int x0 = static_cast<int>(std::floor(scaledX));
    const int z0 = static_cast<int>(std::floor(scaledZ));
    const int x1 = x0 + 1;
    const int z1 = z0 + 1;

    const double tx = smoothStep(scaledX - static_cast<double>(x0));
    const double tz = smoothStep(scaledZ - static_cast<double>(z0));

    const double n00 = lattice2D(x0, z0, seed);
    const double n10 = lattice2D(x1, z0, seed);
    const double n01 = lattice2D(x0, z1, seed);
    const double n11 = lattice2D(x1, z1, seed);

    const double nx0 = n00 + (n10 - n00) * tx;
    const double nx1 = n01 + (n11 - n01) * tx;
    return nx0 + (nx1 - nx0) * tz;
}

double valueNoise3D(double x, double y, double z, double cellSize, uint32_t seed) {
    const double sx = x / cellSize;
    const double sy = y / cellSize;
    const double sz = z / cellSize;

    const int x0 = static_cast<int>(std::floor(sx));
    const int y0 = static_cast<int>(std::floor(sy));
    const int z0 = static_cast<int>(std::floor(sz));

    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const int z1 = z0 + 1;

    const double tx = smoothStep(sx - static_cast<double>(x0));
    const double ty = smoothStep(sy - static_cast<double>(y0));
    const double tz = smoothStep(sz - static_cast<double>(z0));

    const double n000 = lattice3D(x0, y0, z0, seed);
    const double n100 = lattice3D(x1, y0, z0, seed);
    const double n010 = lattice3D(x0, y1, z0, seed);
    const double n110 = lattice3D(x1, y1, z0, seed);
    const double n001 = lattice3D(x0, y0, z1, seed);
    const double n101 = lattice3D(x1, y0, z1, seed);
    const double n011 = lattice3D(x0, y1, z1, seed);
    const double n111 = lattice3D(x1, y1, z1, seed);

    const double nx00 = n000 + (n100 - n000) * tx;
    const double nx10 = n010 + (n110 - n010) * tx;
    const double nx01 = n001 + (n101 - n001) * tx;
    const double nx11 = n011 + (n111 - n011) * tx;

    const double nxy0 = nx00 + (nx10 - nx00) * ty;
    const double nxy1 = nx01 + (nx11 - nx01) * ty;
    return nxy0 + (nxy1 - nxy0) * tz;
}

double fbm2D(double x, double z, double firstCell, int octaves, uint32_t seed) {
    double amplitude = 1.0;
    double sum = 0.0;
    double weight = 0.0;
    double cell = firstCell;

    for (int i = 0; i < octaves; ++i) {
        sum += valueNoise2D(x, z, cell, seed + static_cast<uint32_t>(i) * 1013U) * amplitude;
        weight += amplitude;
        amplitude *= 0.5;
        cell *= 0.5;
    }

    return weight > 0.0 ? (sum / weight) : 0.0;
}

double fbm3D(double x, double y, double z, double firstCell, int octaves, uint32_t seed) {
    double amplitude = 1.0;
    double sum = 0.0;
    double weight = 0.0;
    double cell = firstCell;

    for (int i = 0; i < octaves; ++i) {
        sum += valueNoise3D(x, y, z, cell, seed + static_cast<uint32_t>(i) * 2053U) * amplitude;
        weight += amplitude;
        amplitude *= 0.5;
        cell *= 0.5;
    }

    return weight > 0.0 ? (sum / weight) : 0.0;
}

} // namespace

void TerrainGenerator::init(uint32_t seed, int seaLevel) {
    m_seed = seed;
    m_seaLevel = std::clamp(seaLevel, 16, Chunk::SIZE_Y - 32);
}

int TerrainGenerator::sampleSurfaceY(int worldX, int worldZ) const {
    const double x = static_cast<double>(worldX);
    const double z = static_cast<double>(worldZ);

    const double continental = fbm2D(x, z, 320.0, 4, m_seed ^ 0x6a09e667U);
    const double detail = fbm2D(x, z, 72.0, 3, m_seed ^ 0xbb67ae85U);
    const double rough = fbm2D(x, z, 36.0, 2, m_seed ^ 0x3c6ef372U);
    const double mountainMask = std::max(0.0, (continental - 0.52) * 2.2);

    double height = static_cast<double>(m_seaLevel);
    height += (continental - 0.5) * 34.0;
    height += (detail - 0.5) * 14.0;
    height += std::max(0.0, rough - 0.56) * 22.0;
    height += mountainMask * 12.0;

    const double moisture = sampleMoisture(worldX, worldZ);
    if (moisture < 0.35) {
        height -= 2.0;
    }

    const int clamped = static_cast<int>(std::lround(height));
    return std::clamp(clamped, 8, Chunk::SIZE_Y - 8);
}

double TerrainGenerator::sampleMoisture(int worldX, int worldZ) const {
    return fbm2D(static_cast<double>(worldX), static_cast<double>(worldZ), 420.0, 3, m_seed ^ 0xa54ff53aU);
}

bool TerrainGenerator::shouldCarveCave(int worldX, int y, int worldZ, int surfaceY) const {
    if (y > surfaceY - 5 || y < 10) {
        return false;
    }

    const double cave = fbm3D(static_cast<double>(worldX), static_cast<double>(y), static_cast<double>(worldZ),
                              44.0, 3, m_seed ^ 0x510e527fU);
    const double depthFactor = static_cast<double>(surfaceY - y) / static_cast<double>(Chunk::SIZE_Y);
    const double threshold = 0.77 - depthFactor * 0.18;
    return cave > threshold;
}

BlockID TerrainGenerator::sampleOreBlock(int worldX, int y, int worldZ, BlockID baseBlock) const {
    if (baseBlock != BlockType::STONE) {
        return baseBlock;
    }

    // Keep ore distribution simple and deterministic by depth bands.
    if (y <= 16 && hashedChance(m_seed, worldX, y, worldZ, 0x89abcdefU) < 0.0045) {
        return BlockType::DIAMOND_ORE;
    }
    if (y <= 32 && hashedChance(m_seed, worldX, y, worldZ, 0x13572468U) < 0.0080) {
        return BlockType::GOLD_ORE;
    }
    if (y <= 64 && hashedChance(m_seed, worldX, y, worldZ, 0xfedcba98U) < 0.0160) {
        return BlockType::IRON_ORE;
    }
    if (y <= 128 && hashedChance(m_seed, worldX, y, worldZ, 0x2468ace0U) < 0.0240) {
        return BlockType::COAL_ORE;
    }

    return baseBlock;
}

void TerrainGenerator::generateChunk(Chunk& chunk) const {
    const glm::ivec3 offset = chunk.getWorldOffset();

    for (int x = 0; x < Chunk::SIZE_X; ++x) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            const int worldX = offset.x + x;
            const int worldZ = offset.z + z;
            const int surfaceY = sampleSurfaceY(worldX, worldZ);
            const double moisture = sampleMoisture(worldX, worldZ);

            const bool belowSeaLevel = surfaceY < m_seaLevel;
            const BlockID topBlock = (belowSeaLevel || moisture < 0.35) ? BlockType::SAND : BlockType::GRASS;
            const BlockID fillBlock = (belowSeaLevel || moisture < 0.35) ? BlockType::SAND : BlockType::DIRT;

            const int columnTop = std::max(surfaceY, m_seaLevel);
            for (int y = 0; y <= columnTop; ++y) {
                BlockID id = BlockType::AIR;
                if (y <= surfaceY) {
                    id = BlockType::STONE;
                    if (y == surfaceY) {
                        id = topBlock;
                    } else if (y >= surfaceY - 3) {
                        id = fillBlock;
                    }

                    if (id != BlockType::AIR && shouldCarveCave(worldX, y, worldZ, surfaceY)) {
                        id = BlockType::AIR;
                    }

                    id = sampleOreBlock(worldX, y, worldZ, id);
                } else if (y <= m_seaLevel) {
                    id = BlockType::WATER;
                }

                if (id != BlockType::AIR) {
                    chunk.setBlock(x, y, z, id);
                }
            }

            bool skylightVisible = true;
            for (int y = Chunk::SIZE_Y - 1; y >= 0; --y) {
                const BlockID id = chunk.getBlock(x, y, z);
                chunk.setSunlight(x, y, z, skylightVisible ? 15 : 0);
                if (BlockRegistry::get(id).isSolid) {
                    skylightVisible = false;
                }
            }
        }
    }
}

