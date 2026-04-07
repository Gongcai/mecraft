#include "TerrainGenerator.h"

#include <algorithm>
#include <cmath>

#if defined(__SSE2__) || defined(_M_X64) || defined(__AVX2__)
#include <immintrin.h>
#endif

#if defined(__AVX2__)
#define MECRAFT_HAS_AVX2 1
#endif

#if defined(__SSE2__) || defined(_M_X64) || defined(MECRAFT_HAS_AVX2)
#define MECRAFT_HAS_SSE2 1
#endif

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

#if defined(MECRAFT_HAS_SSE2)
__m128d smoothStep2(__m128d t) {
    const __m128d two = _mm_set1_pd(2.0);
    const __m128d three = _mm_set1_pd(3.0);
    return _mm_mul_pd(_mm_mul_pd(t, t), _mm_sub_pd(three, _mm_mul_pd(two, t)));
}

__m128d valueNoise2D2(double x0, double x1, double z, double cellSize, uint32_t seed) {
    const __m128d vx = _mm_set_pd(x1, x0);
    const __m128d vCell = _mm_set1_pd(cellSize);
    const __m128d scaledX = _mm_div_pd(vx, vCell);

    double sx[2];
    _mm_storeu_pd(sx, scaledX);
    int xBase[2];
    double fracX[2];
    for (int i = 0; i < 2; ++i) {
        const double fx = std::floor(sx[i]);
        xBase[i] = static_cast<int>(fx);
        fracX[i] = sx[i] - fx;
    }

    const double scaledZ = z / cellSize;
    const double fz = std::floor(scaledZ);
    const int z0 = static_cast<int>(fz);
    const int z1 = z0 + 1;

    const __m128d tx = smoothStep2(_mm_set_pd(fracX[1], fracX[0]));
    const __m128d tz = _mm_set1_pd(smoothStep(scaledZ - fz));

    const double n00a = lattice2D(xBase[0], z0, seed);
    const double n10a = lattice2D(xBase[0] + 1, z0, seed);
    const double n01a = lattice2D(xBase[0], z1, seed);
    const double n11a = lattice2D(xBase[0] + 1, z1, seed);

    const double n00b = lattice2D(xBase[1], z0, seed);
    const double n10b = lattice2D(xBase[1] + 1, z0, seed);
    const double n01b = lattice2D(xBase[1], z1, seed);
    const double n11b = lattice2D(xBase[1] + 1, z1, seed);

    const __m128d n00 = _mm_set_pd(n00b, n00a);
    const __m128d n10 = _mm_set_pd(n10b, n10a);
    const __m128d n01 = _mm_set_pd(n01b, n01a);
    const __m128d n11 = _mm_set_pd(n11b, n11a);

    const __m128d nx0 = _mm_add_pd(n00, _mm_mul_pd(_mm_sub_pd(n10, n00), tx));
    const __m128d nx1 = _mm_add_pd(n01, _mm_mul_pd(_mm_sub_pd(n11, n01), tx));
    return _mm_add_pd(nx0, _mm_mul_pd(_mm_sub_pd(nx1, nx0), tz));
}

__m128d fbm2D2(double x0, double x1, double z, double firstCell, int octaves, uint32_t seed) {
    __m128d sum = _mm_setzero_pd();
    double amplitude = 1.0;
    double weight = 0.0;
    double cell = firstCell;

    for (int i = 0; i < octaves; ++i) {
        const __m128d octave = valueNoise2D2(x0, x1, z, cell, seed + static_cast<uint32_t>(i) * 1013U);
        sum = _mm_add_pd(sum, _mm_mul_pd(octave, _mm_set1_pd(amplitude)));
        weight += amplitude;
        amplitude *= 0.5;
        cell *= 0.5;
    }

    if (weight <= 0.0) {
        return _mm_setzero_pd();
    }
    return _mm_div_pd(sum, _mm_set1_pd(weight));
}
#endif

#if defined(MECRAFT_HAS_AVX2)
__m128i hash32_4(__m128i x) {
    x = _mm_xor_si128(x, _mm_srli_epi32(x, 16));
    x = _mm_mullo_epi32(x, _mm_set1_epi32(0x7feb352dU));
    x = _mm_xor_si128(x, _mm_srli_epi32(x, 15));
    x = _mm_mullo_epi32(x, _mm_set1_epi32(0x846ca68bU));
    x = _mm_xor_si128(x, _mm_srli_epi32(x, 16));
    return x;
}

__m256d hashToUnit4(__m128i value) {
    const __m256d signedD = _mm256_cvtepi32_pd(value);
    const __m256d wasNegative = _mm256_cmp_pd(signedD, _mm256_setzero_pd(), _CMP_LT_OQ);
    const __m256d unsignedD = _mm256_add_pd(signedD, _mm256_and_pd(wasNegative, _mm256_set1_pd(4294967296.0)));
    return _mm256_mul_pd(unsignedD, _mm256_set1_pd(1.0 / 4294967295.0));
}

__m256d lattice2D4(__m128i x, int z, uint32_t seed) {
    __m128i h = _mm_set1_epi32(static_cast<int>(seed));
    const __m128i zv = _mm_set1_epi32(z);

    h = _mm_xor_si128(h, hash32_4(_mm_mullo_epi32(x, _mm_set1_epi32(0x27d4eb2dU))));
    h = _mm_xor_si128(h, hash32_4(_mm_mullo_epi32(zv, _mm_set1_epi32(0x165667b1U))));

    return hashToUnit4(hash32_4(h));
}

__m256d smoothStep4(__m256d t) {
    const __m256d two = _mm256_set1_pd(2.0);
    const __m256d three = _mm256_set1_pd(3.0);
    return _mm256_mul_pd(_mm256_mul_pd(t, t), _mm256_sub_pd(three, _mm256_mul_pd(two, t)));
}

__m256d valueNoise2D4(double x0, double x1, double x2, double x3, double z, double cellSize, uint32_t seed) {
    const __m256d vx = _mm256_set_pd(x3, x2, x1, x0);
    const __m256d scaledX = _mm256_div_pd(vx, _mm256_set1_pd(cellSize));
    const __m256d floorX = _mm256_floor_pd(scaledX);
    const __m128i xBase = _mm256_cvttpd_epi32(floorX);
    const __m256d fracX = _mm256_sub_pd(scaledX, floorX);

    const double scaledZ = z / cellSize;
    const double fz = std::floor(scaledZ);
    const int z0 = static_cast<int>(fz);
    const int z1 = z0 + 1;

    const __m256d tx = smoothStep4(fracX);
    const __m256d tz = _mm256_set1_pd(smoothStep(scaledZ - fz));

    const __m128i x1i = _mm_add_epi32(xBase, _mm_set1_epi32(1));
    const __m256d n00v = lattice2D4(xBase, z0, seed);
    const __m256d n10v = lattice2D4(x1i, z0, seed);
    const __m256d n01v = lattice2D4(xBase, z1, seed);
    const __m256d n11v = lattice2D4(x1i, z1, seed);

    const __m256d nx0 = _mm256_add_pd(n00v, _mm256_mul_pd(_mm256_sub_pd(n10v, n00v), tx));
    const __m256d nx1 = _mm256_add_pd(n01v, _mm256_mul_pd(_mm256_sub_pd(n11v, n01v), tx));
    return _mm256_add_pd(nx0, _mm256_mul_pd(_mm256_sub_pd(nx1, nx0), tz));
}

__m256d fbm2D4(double x0, double x1, double x2, double x3, double z, double firstCell, int octaves, uint32_t seed) {
    __m256d sum = _mm256_setzero_pd();
    double amplitude = 1.0;
    double weight = 0.0;
    double cell = firstCell;

    for (int i = 0; i < octaves; ++i) {
        const __m256d octave = valueNoise2D4(x0, x1, x2, x3, z, cell, seed + static_cast<uint32_t>(i) * 1013U);
        sum = _mm256_add_pd(sum, _mm256_mul_pd(octave, _mm256_set1_pd(amplitude)));
        weight += amplitude;
        amplitude *= 0.5;
        cell *= 0.5;
    }

    if (weight <= 0.0) {
        return _mm256_setzero_pd();
    }
    return _mm256_div_pd(sum, _mm256_set1_pd(weight));
}
#endif

void sampleSurfaceAndMoistureScalar(int worldX, int worldZ, uint32_t seed, int seaLevel,
                                    int& outSurfaceY, double& outMoisture) {
    const auto x = static_cast<double>(worldX);
    const auto z = static_cast<double>(worldZ);

    const double continental = fbm2D(x, z, 320.0, 4, seed ^ 0x6a09e667U);
    const double detail = fbm2D(x, z, 72.0, 3, seed ^ 0xbb67ae85U);
    const double rough = fbm2D(x, z, 36.0, 2, seed ^ 0x3c6ef372U);
    const double mountainMask = std::max(0.0, (continental - 0.52) * 2.2);

    double height = static_cast<double>(seaLevel);
    height += (continental - 0.5) * 34.0;
    height += (detail - 0.5) * 14.0;
    height += std::max(0.0, rough - 0.56) * 22.0;
    height += mountainMask * 12.0;

    outMoisture = fbm2D(x, z, 420.0, 3, seed ^ 0xa54ff53aU);
    if (outMoisture < 0.35) {
        height -= 2.0;
    }

    const int rounded = static_cast<int>(std::lround(height));
    outSurfaceY = std::clamp(rounded, 8, Chunk::SIZE_Y - 8);
}

#if defined(MECRAFT_HAS_AVX2)
void sampleSurfaceAndMoisture4(int worldX0, int worldX1, int worldX2, int worldX3, int worldZ, uint32_t seed,
                               int seaLevel, int outSurfaceY[4], double outMoisture[4]) {
    const auto z = static_cast<double>(worldZ);
    const auto x0 = static_cast<double>(worldX0);
    const auto x1 = static_cast<double>(worldX1);
    const auto x2 = static_cast<double>(worldX2);
    const auto x3 = static_cast<double>(worldX3);

    const __m256d continental = fbm2D4(x0, x1, x2, x3, z, 320.0, 4, seed ^ 0x6a09e667U);
    const __m256d detail = fbm2D4(x0, x1, x2, x3, z, 72.0, 3, seed ^ 0xbb67ae85U);
    const __m256d rough = fbm2D4(x0, x1, x2, x3, z, 36.0, 2, seed ^ 0x3c6ef372U);
    const __m256d moisture = fbm2D4(x0, x1, x2, x3, z, 420.0, 3, seed ^ 0xa54ff53aU);

    const __m256d mountainMask = _mm256_max_pd(_mm256_setzero_pd(),
                                               _mm256_mul_pd(_mm256_sub_pd(continental, _mm256_set1_pd(0.52)),
                                                             _mm256_set1_pd(2.2)));

    __m256d height = _mm256_set1_pd(static_cast<double>(seaLevel));
    height = _mm256_add_pd(height, _mm256_mul_pd(_mm256_sub_pd(continental, _mm256_set1_pd(0.5)),
                                                  _mm256_set1_pd(34.0)));
    height = _mm256_add_pd(height, _mm256_mul_pd(_mm256_sub_pd(detail, _mm256_set1_pd(0.5)), _mm256_set1_pd(14.0)));
    height = _mm256_add_pd(height,
                           _mm256_mul_pd(_mm256_max_pd(_mm256_setzero_pd(), _mm256_sub_pd(rough, _mm256_set1_pd(0.56))),
                                         _mm256_set1_pd(22.0)));
    height = _mm256_add_pd(height, _mm256_mul_pd(mountainMask, _mm256_set1_pd(12.0)));

    double moistureArr[4];
    double heightArr[4];
    _mm256_storeu_pd(moistureArr, moisture);
    _mm256_storeu_pd(heightArr, height);

    for (int i = 0; i < 4; ++i) {
        outMoisture[i] = moistureArr[i];
        double h = heightArr[i];
        if (outMoisture[i] < 0.35) {
            h -= 2.0;
        }
        const int rounded = static_cast<int>(std::lround(h));
        outSurfaceY[i] = std::clamp(rounded, 8, Chunk::SIZE_Y - 8);
    }
}
#endif

void sampleSurfaceAndMoisture2(int worldX0, int worldX1, int worldZ, uint32_t seed, int seaLevel,
                               int outSurfaceY[2], double outMoisture[2]) {
#if defined(MECRAFT_HAS_SSE2)
    const auto z = static_cast<double>(worldZ);
    const auto x0 = static_cast<double>(worldX0);
    const auto x1 = static_cast<double>(worldX1);

    const __m128d continental = fbm2D2(x0, x1, z, 320.0, 4, seed ^ 0x6a09e667U);
    const __m128d detail = fbm2D2(x0, x1, z, 72.0, 3, seed ^ 0xbb67ae85U);
    const __m128d rough = fbm2D2(x0, x1, z, 36.0, 2, seed ^ 0x3c6ef372U);
    const __m128d moisture = fbm2D2(x0, x1, z, 420.0, 3, seed ^ 0xa54ff53aU);

    const __m128d mountainMask = _mm_max_pd(_mm_setzero_pd(),
                                            _mm_mul_pd(_mm_sub_pd(continental, _mm_set1_pd(0.52)),
                                                       _mm_set1_pd(2.2)));

    __m128d height = _mm_set1_pd(static_cast<double>(seaLevel));
    height = _mm_add_pd(height, _mm_mul_pd(_mm_sub_pd(continental, _mm_set1_pd(0.5)), _mm_set1_pd(34.0)));
    height = _mm_add_pd(height, _mm_mul_pd(_mm_sub_pd(detail, _mm_set1_pd(0.5)), _mm_set1_pd(14.0)));
    height = _mm_add_pd(height,
                        _mm_mul_pd(_mm_max_pd(_mm_setzero_pd(), _mm_sub_pd(rough, _mm_set1_pd(0.56))),
                                   _mm_set1_pd(22.0)));
    height = _mm_add_pd(height, _mm_mul_pd(mountainMask, _mm_set1_pd(12.0)));

    double moistureArr[2];
    double heightArr[2];
    _mm_storeu_pd(moistureArr, moisture);
    _mm_storeu_pd(heightArr, height);

    for (int i = 0; i < 2; ++i) {
        outMoisture[i] = moistureArr[i];
        double h = heightArr[i];
        if (outMoisture[i] < 0.35) {
            h -= 2.0;
        }
        const int rounded = static_cast<int>(std::lround(h));
        outSurfaceY[i] = std::clamp(rounded, 8, Chunk::SIZE_Y - 8);
    }
#else
    sampleSurfaceAndMoistureScalar(worldX0, worldZ, seed, seaLevel, outSurfaceY[0], outMoisture[0]);
    sampleSurfaceAndMoistureScalar(worldX1, worldZ, seed, seaLevel, outSurfaceY[1], outMoisture[1]);
#endif
}

} // namespace

void TerrainGenerator::init(uint32_t seed, int seaLevel) {
    m_seed = seed;
    m_seaLevel = std::clamp(seaLevel, 16, Chunk::SIZE_Y - 32);
}

int TerrainGenerator::sampleSurfaceY(int worldX, int worldZ) const {
    int surfaceY = 0;
    sampleSurfaceYBatch(worldX, worldZ, 1, &surfaceY);
    return surfaceY;
}

void TerrainGenerator::sampleSurfaceYBatch(int startWorldX, int worldZ, int count, int* outSurfaceY) const {
    if (count <= 0 || outSurfaceY == nullptr) {
        return;
    }

    // Batch API stays scalar for now because current SIMD variant is slower on benchmark hardware.
    for (int i = 0; i < count; ++i) {
        double moisture = 0.0;
        sampleSurfaceAndMoistureScalar(startWorldX + i, worldZ, m_seed, m_seaLevel, outSurfaceY[i], moisture);
    }
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
    // Use early returns to avoid redundant condition checks and hash computations.
    if (y <= 16) {
        if (hashedChance(m_seed, worldX, y, worldZ, 0x89abcdefU) < 0.0045) {
            return BlockType::DIAMOND_ORE;
        }
    } else if (y <= 32) {
        if (hashedChance(m_seed, worldX, y, worldZ, 0x13572468U) < 0.0080) {
            return BlockType::GOLD_ORE;
        }
    } else if (y <= 64) {
        if (hashedChance(m_seed, worldX, y, worldZ, 0xfedcba98U) < 0.0160) {
            return BlockType::IRON_ORE;
        }
    } else if (y <= 128) {
        if (hashedChance(m_seed, worldX, y, worldZ, 0x2468ace0U) < 0.0240) {
            return BlockType::COAL_ORE;
        }
    }

    return baseBlock;
}

void TerrainGenerator::generateChunk(Chunk& chunk) const {
    const glm::ivec3 offset = chunk.getWorldOffset();

    for (int z = 0; z < Chunk::SIZE_Z; ++z) {
        for (int x = 0; x < Chunk::SIZE_X;) {
            int laneCount = 1;
            int sampledSurface[4] = {};
            double sampledMoisture[4] = {};
            const int remaining = Chunk::SIZE_X - x;

#if defined(MECRAFT_HAS_AVX2)
            if (remaining >= 4) {
                laneCount = 4;
                sampleSurfaceAndMoisture4(offset.x + x, offset.x + x + 1, offset.x + x + 2, offset.x + x + 3,
                                          offset.z + z, m_seed, m_seaLevel, sampledSurface, sampledMoisture);
            } else
#endif
            if (remaining >= 2) {
                laneCount = 2;
                sampleSurfaceAndMoisture2(offset.x + x, offset.x + x + 1, offset.z + z, m_seed, m_seaLevel,
                                          sampledSurface, sampledMoisture);
            } else {
                laneCount = 1;
                sampleSurfaceAndMoistureScalar(offset.x + x, offset.z + z, m_seed, m_seaLevel,
                                               sampledSurface[0], sampledMoisture[0]);
            }

            for (int lane = 0; lane < laneCount; ++lane) {
                const int localX = x + lane;
                const int worldX = offset.x + localX;
                const int worldZ = offset.z + z;
                const int surfaceY = sampledSurface[lane];
                const double moisture = sampledMoisture[lane];

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
                        chunk.setBlock(localX, y, z, id);
                    }
                }

                bool skylightVisible = true;
                for (int y = Chunk::SIZE_Y - 1; y >= 0; --y) {
                    const BlockID id = chunk.getBlock(localX, y, z);
                    chunk.setSunlight(localX, y, z, skylightVisible ? 15 : 0);
                    if (BlockRegistry::get(id).isSolid) {
                        skylightVisible = false;
                    }
                }
            }

            x += laneCount;
        }
    }
}

