#include "TerrainGenerator.h"

#include <algorithm>
#include <cmath>

#include "../fluid/FluidState.h"

#if defined(__SSE2__) || defined(_M_X64) || defined(__AVX2__)
#include <immintrin.h>
#endif

#if defined(__AVX2__)
#define MECRAFT_HAS_AVX2 1
#endif

#if defined(__SSE2__) || defined(_M_X64) || defined(MECRAFT_HAS_AVX2)
#define MECRAFT_HAS_SSE2 1
#endif

#if defined(_MSC_VER)
#define MECRAFT_FORCEINLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define MECRAFT_FORCEINLINE inline __attribute__((always_inline))
#else
#define MECRAFT_FORCEINLINE inline
#endif

namespace {

double saturate(double v) {
    return std::clamp(v, 0.0, 1.0);
}

double smoothStep(double t) {
    return t * t * (3.0 - 2.0 * t);
}

double smoothRange(double v, double lo, double hi) {
    if (hi <= lo) {
        return v >= hi ? 1.0 : 0.0;
    }
    return smoothStep(saturate((v - lo) / (hi - lo)));
}

MECRAFT_FORCEINLINE uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

constexpr uint32_t kOreXMul = 0x9e3779b9U;
constexpr uint32_t kOreYMul = 0x7f4a7c15U;
constexpr uint32_t kOreZMul = 0x94d049bbU;

constexpr uint32_t kOreSaltDiamond = 0x89abcdefU;
constexpr uint32_t kOreSaltGold = 0x13572468U;
constexpr uint32_t kOreSaltIron = 0xfedcba98U;
constexpr uint32_t kOreSaltCoal = 0x2468ace0U;
constexpr uint32_t kDecorSaltDensity = 0x4a3c2f1dU;
constexpr uint32_t kDecorSaltFlower = 0xc13f7e59U;
constexpr uint32_t kTreeSaltDensity = 0x7b9d3f25U;
constexpr uint32_t kTreeSaltSpecies = 0x2f4c8a91U;
constexpr uint32_t kTreeSaltHeight = 0x5e6b1c37U;

constexpr uint32_t kOreCutoffDiamond = static_cast<uint32_t>(0.0045 * 4294967295.0);
constexpr uint32_t kOreCutoffGold = static_cast<uint32_t>(0.0080 * 4294967295.0);
constexpr uint32_t kOreCutoffIron = static_cast<uint32_t>(0.0160 * 4294967295.0);
constexpr uint32_t kOreCutoffCoal = static_cast<uint32_t>(0.0240 * 4294967295.0);

constexpr int kTreeLeafRadius = 2;
constexpr int kTreeScanRadius = kTreeLeafRadius;

BlockID naturalWaterState() {
    if (BlockIds::WATER == BlockIds::AIR) {
        return BlockIds::WATER;
    }
    return FluidState::makeWater(0, false);
}

MECRAFT_FORCEINLINE double hashToUnit(uint32_t value) {
    constexpr double kInvUint32Max = 1.0 / 4294967295.0;
    return static_cast<double>(value) * kInvUint32Max;
}

uint32_t probabilityToCutoff(double probability) {
    const double clamped = std::clamp(probability, 0.0, 1.0);
    return static_cast<uint32_t>(clamped * 4294967295.0);
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

double sampleCaveNoise(int worldX, int y, int worldZ, uint32_t seed) {
    return fbm3D(static_cast<double>(worldX), static_cast<double>(y), static_cast<double>(worldZ),
                 44.0, 3, seed ^ 0x510e527fU);
}

bool shouldCarveCaveFromNoise(double cave, int y, int surfaceY) {
    const double depthFactor = static_cast<double>(surfaceY - y) / static_cast<double>(Chunk::SIZE_Y);
    const double threshold = 0.77 - depthFactor * 0.18;
    return cave > threshold;
}

double interpolateNoiseXZSlice(int x0, int x1, int y, int z0, int z1, double tx, double tz, uint32_t seed) {
    const double n00 = lattice3D(x0, y, z0, seed);
    const double n10 = lattice3D(x1, y, z0, seed);
    const double n01 = lattice3D(x0, y, z1, seed);
    const double n11 = lattice3D(x1, y, z1, seed);

    const double nx0 = n00 + (n10 - n00) * tx;
    const double nx1 = n01 + (n11 - n01) * tx;
    return nx0 + (nx1 - nx0) * tz;
}

void buildCaveMaskColumn(int worldX,
                         int worldZ,
                         int surfaceY,
                         uint32_t seed,
                         std::array<uint8_t, Chunk::SIZE_Y>& outMask) {
    constexpr double kFirstCell = 44.0;
    constexpr int kOctaves = 3;
    const int caveStartY = 10;
    const int caveEndY = surfaceY - 5;
    if (caveEndY < caveStartY) {
        return;
    }

    std::array<double, Chunk::SIZE_Y> caveNoise{};
    double amplitude = 1.0;
    double weight = 0.0;
    double cell = kFirstCell;

    for (int octave = 0; octave < kOctaves; ++octave) {
        const uint32_t octaveSeed = (seed ^ 0x510e527fU) + static_cast<uint32_t>(octave) * 2053U;
        const double invCell = 1.0 / cell;
        const double sx = static_cast<double>(worldX) * invCell;
        const double sz = static_cast<double>(worldZ) * invCell;

        const int x0 = static_cast<int>(std::floor(sx));
        const int z0 = static_cast<int>(std::floor(sz));
        const int x1 = x0 + 1;
        const int z1 = z0 + 1;

        const double tx = smoothStep(sx - static_cast<double>(x0));
        const double tz = smoothStep(sz - static_cast<double>(z0));

        bool hasCachedSlice = false;
        int cachedYCell = 0;
        double slice0 = 0.0;
        double slice1 = 0.0;

        for (int y = caveStartY; y <= caveEndY; ++y) {
            const double sy = static_cast<double>(y) * invCell;
            const int y0 = static_cast<int>(std::floor(sy));

            if (!hasCachedSlice || y0 != cachedYCell) {
                cachedYCell = y0;
                slice0 = interpolateNoiseXZSlice(x0, x1, y0, z0, z1, tx, tz, octaveSeed);
                slice1 = interpolateNoiseXZSlice(x0, x1, y0 + 1, z0, z1, tx, tz, octaveSeed);
                hasCachedSlice = true;
            }

            const double ty = smoothStep(sy - static_cast<double>(y0));
            caveNoise[y] += (slice0 + (slice1 - slice0) * ty) * amplitude;
        }

        weight += amplitude;
        amplitude *= 0.5;
        cell *= 0.5;
    }

    if (weight <= 0.0) {
        return;
    }

    const double invWeight = 1.0 / weight;
    for (int y = caveStartY; y <= caveEndY; ++y) {
        outMask[y] = static_cast<uint8_t>(shouldCarveCaveFromNoise(caveNoise[y] * invWeight, y, surfaceY));
    }
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

void finalizeSurfaceSample(double continental,
                           double detail,
                           double rough,
                           double ridgeBase,
                           double mountainNoise,
                           double moisture,
                           int seaLevel,
                           int& outSurfaceY,
                           double& outMoisture,
                           TerrainBiome& outSurfaceKind,
                           double& outRuggedness);

void sampleSurfaceAndMoistureScalar(int worldX, int worldZ, uint32_t seed, int seaLevel,
                                    int& outSurfaceY, double& outMoisture,
                                    TerrainBiome& outSurfaceKind, double& outRuggedness) {
    const auto x = static_cast<double>(worldX);
    const auto z = static_cast<double>(worldZ);

    const double continental = fbm2D(x, z, 320.0, 4, seed ^ 0x6a09e667U);
    const double detail = fbm2D(x, z, 64.0, 4, seed ^ 0xbb67ae85U);
    const double rough = fbm2D(x, z, 28.0, 3, seed ^ 0x3c6ef372U);
    const double ridgeBase = fbm2D(x, z, 96.0, 4, seed ^ 0x510e527fU);
    const double mountainNoise = fbm2D(x, z, 220.0, 3, seed ^ 0x1f83d9abU);
    const double moisture = fbm2D(x, z, 420.0, 3, seed ^ 0xa54ff53aU);
    finalizeSurfaceSample(continental, detail, rough, ridgeBase, mountainNoise, moisture,
                          seaLevel, outSurfaceY, outMoisture, outSurfaceKind, outRuggedness);
}

void finalizeSurfaceSample(double continental,
                           double detail,
                           double rough,
                           double ridgeBase,
                           double mountainNoise,
                           double moisture,
                           int seaLevel,
                           int& outSurfaceY,
                           double& outMoisture,
                           TerrainBiome& outSurfaceKind,
                           double& outRuggedness) {
    const double ridge = 1.0 - std::abs(ridgeBase * 2.0 - 1.0);
    const double mountainMask = smoothRange(continental, 0.50, 0.66);
    const double highMountainMask = mountainMask * smoothRange(ridge, 0.58, 0.88) * smoothRange(mountainNoise, 0.45, 0.75);

    double height = static_cast<double>(seaLevel);
    height += (continental - 0.5) * 42.0;
    height += (detail - 0.5) * 20.0;
    height += (rough - 0.5) * 18.0;
    height += std::max(0.0, rough - 0.52) * 20.0;
    height += mountainMask * 22.0;
    height += ridge * mountainMask * 10.0;
    height += highMountainMask * 34.0;

    outMoisture = moisture;
    if (outMoisture < 0.32) {
        height -= 3.0;
    }

    outRuggedness = saturate(0.45 * rough + 0.55 * ridge);

    if (highMountainMask > 0.42) {
        outSurfaceKind = TerrainBiome::HighMountain;
    } else if (mountainMask > 0.48) {
        outSurfaceKind = TerrainBiome::Mountain;
    } else if (outMoisture < 0.34) {
        outSurfaceKind = TerrainBiome::Arid;
    } else {
        outSurfaceKind = TerrainBiome::Temperate;
    }

    const int rounded = static_cast<int>(std::lround(height));
    outSurfaceY = std::clamp(rounded, 8, Chunk::SIZE_Y - 8);
}

#if defined(MECRAFT_HAS_AVX2)
void sampleSurfaceAndMoisture4(int worldX0, int worldX1, int worldX2, int worldX3, int worldZ, uint32_t seed,
                               int seaLevel, int outSurfaceY[4], double outMoisture[4],
                               TerrainBiome outSurfaceKind[4], double outRuggedness[4]) {
    const double z = static_cast<double>(worldZ);
    const double x0 = static_cast<double>(worldX0);
    const double x1 = static_cast<double>(worldX1);
    const double x2 = static_cast<double>(worldX2);
    const double x3 = static_cast<double>(worldX3);

    alignas(32) double continental[4];
    alignas(32) double detail[4];
    alignas(32) double rough[4];
    alignas(32) double ridgeBase[4];
    alignas(32) double mountainNoise[4];
    alignas(32) double moisture[4];

    _mm256_storeu_pd(continental, fbm2D4(x0, x1, x2, x3, z, 320.0, 4, seed ^ 0x6a09e667U));
    _mm256_storeu_pd(detail, fbm2D4(x0, x1, x2, x3, z, 64.0, 4, seed ^ 0xbb67ae85U));
    _mm256_storeu_pd(rough, fbm2D4(x0, x1, x2, x3, z, 28.0, 3, seed ^ 0x3c6ef372U));
    _mm256_storeu_pd(ridgeBase, fbm2D4(x0, x1, x2, x3, z, 96.0, 4, seed ^ 0x510e527fU));
    _mm256_storeu_pd(mountainNoise, fbm2D4(x0, x1, x2, x3, z, 220.0, 3, seed ^ 0x1f83d9abU));
    _mm256_storeu_pd(moisture, fbm2D4(x0, x1, x2, x3, z, 420.0, 3, seed ^ 0xa54ff53aU));

    for (int i = 0; i < 4; ++i) {
        finalizeSurfaceSample(continental[i], detail[i], rough[i], ridgeBase[i], mountainNoise[i], moisture[i],
                              seaLevel, outSurfaceY[i], outMoisture[i], outSurfaceKind[i], outRuggedness[i]);
    }
}
#endif

void sampleSurfaceAndMoisture2(int worldX0, int worldX1, int worldZ, uint32_t seed, int seaLevel,
                               int outSurfaceY[2], double outMoisture[2],
                               TerrainBiome outSurfaceKind[2], double outRuggedness[2]) {
#if defined(MECRAFT_HAS_SSE2)
    const double z = static_cast<double>(worldZ);
    const double x0 = static_cast<double>(worldX0);
    const double x1 = static_cast<double>(worldX1);

    double continental[2];
    double detail[2];
    double rough[2];
    double ridgeBase[2];
    double mountainNoise[2];
    double moisture[2];

    _mm_storeu_pd(continental, fbm2D2(x0, x1, z, 320.0, 4, seed ^ 0x6a09e667U));
    _mm_storeu_pd(detail, fbm2D2(x0, x1, z, 64.0, 4, seed ^ 0xbb67ae85U));
    _mm_storeu_pd(rough, fbm2D2(x0, x1, z, 28.0, 3, seed ^ 0x3c6ef372U));
    _mm_storeu_pd(ridgeBase, fbm2D2(x0, x1, z, 96.0, 4, seed ^ 0x510e527fU));
    _mm_storeu_pd(mountainNoise, fbm2D2(x0, x1, z, 220.0, 3, seed ^ 0x1f83d9abU));
    _mm_storeu_pd(moisture, fbm2D2(x0, x1, z, 420.0, 3, seed ^ 0xa54ff53aU));

    for (int i = 0; i < 2; ++i) {
        finalizeSurfaceSample(continental[i], detail[i], rough[i], ridgeBase[i], mountainNoise[i], moisture[i],
                              seaLevel, outSurfaceY[i], outMoisture[i], outSurfaceKind[i], outRuggedness[i]);
    }
#else
    sampleSurfaceAndMoistureScalar(worldX0, worldZ, seed, seaLevel,
                                   outSurfaceY[0], outMoisture[0], outSurfaceKind[0], outRuggedness[0]);
    sampleSurfaceAndMoistureScalar(worldX1, worldZ, seed, seaLevel,
                                   outSurfaceY[1], outMoisture[1], outSurfaceKind[1], outRuggedness[1]);
#endif
}

struct TreeCandidate {
    bool valid = false;
    int worldX = 0;
    int worldZ = 0;
    int surfaceY = 0;
    int height = 0;
    BlockID log = 0;
    BlockID leaves = 0;
};

uint32_t hashColumn(int worldX, int worldZ, uint32_t seed) {
    uint32_t h = seed;
    h ^= hash32(static_cast<uint32_t>(worldX) * kOreXMul);
    h ^= hash32(static_cast<uint32_t>(worldZ) * kOreZMul);
    return h;
}

bool surfaceCanHostTree(TerrainBiome biome, double moisture, int seaLevel, int surfaceY) {
    if (surfaceY < seaLevel || moisture < 0.38) {
        return false;
    }

    switch (biome) {
        case TerrainBiome::Temperate:
        case TerrainBiome::Mountain:
            return true;
        case TerrainBiome::Arid:
        case TerrainBiome::HighMountain:
        default:
            return false;
    }
}

TreeCandidate sampleTreeCandidate(int worldX, int worldZ, uint32_t seed, int seaLevel) {
    int surfaceY = 0;
    double moisture = 0.0;
    double ruggedness = 0.0;
    TerrainBiome biome = TerrainBiome::Temperate;
    sampleSurfaceAndMoistureScalar(worldX, worldZ, seed, seaLevel,
                                   surfaceY, moisture, biome, ruggedness);

    if (!surfaceCanHostTree(biome, moisture, seaLevel, surfaceY)) {
        return {};
    }

    const double density = biome == TerrainBiome::Temperate
                               ? (0.012 + moisture * 0.018)
                               : (0.004 + moisture * 0.008);
    const uint32_t h = hashColumn(worldX, worldZ, seed);
    if (hash32(h ^ kTreeSaltDensity) > probabilityToCutoff(density)) {
        return {};
    }

    const int height = 4 + static_cast<int>(hash32(h ^ kTreeSaltHeight) % 3U);
    if (surfaceY + height + 1 >= Chunk::SIZE_Y) {
        return {};
    }

    const bool birch = hash32(h ^ kTreeSaltSpecies) < probabilityToCutoff(0.42);
    TreeCandidate candidate;
    candidate.valid = true;
    candidate.worldX = worldX;
    candidate.worldZ = worldZ;
    candidate.surfaceY = surfaceY;
    candidate.height = height;
    candidate.log = birch ? BlockIds::BIRCH_LOG : BlockIds::WOOD;
    candidate.leaves = birch ? BlockIds::BIRCH_LEAVES : BlockIds::OAK_LEAVES;
    return candidate;
}

BlockID sampleTreeBlockFromCandidate(const TreeCandidate& tree, int worldX, int y, int worldZ) {
    if (!tree.valid) {
        return 0;
    }

    const int dx = worldX - tree.worldX;
    const int dz = worldZ - tree.worldZ;
    const int absDx = std::abs(dx);
    const int absDz = std::abs(dz);
    if (absDx > kTreeLeafRadius || absDz > kTreeLeafRadius) {
        return 0;
    }

    const int trunkMinY = tree.surfaceY + 1;
    const int trunkMaxY = tree.surfaceY + tree.height;
    if (dx == 0 && dz == 0 && y >= trunkMinY && y <= trunkMaxY) {
        return tree.log;
    }

    const int leafY = y - trunkMaxY;
    if (leafY < -2 || leafY > 1) {
        return 0;
    }

    const int radius = leafY >= 0 ? 1 : 2;
    if (absDx > radius || absDz > radius) {
        return 0;
    }
    if (radius == 2 && absDx == 2 && absDz == 2) {
        return 0;
    }
    return tree.leaves;
}

BlockID sampleTreeBlock(int worldX, int y, int worldZ, uint32_t seed, int seaLevel) {
    BlockID firstLeaves = 0;
    for (int anchorX = worldX - kTreeScanRadius; anchorX <= worldX + kTreeScanRadius; ++anchorX) {
        for (int anchorZ = worldZ - kTreeScanRadius; anchorZ <= worldZ + kTreeScanRadius; ++anchorZ) {
            const TreeCandidate tree = sampleTreeCandidate(anchorX, anchorZ, seed, seaLevel);
            const BlockID block = sampleTreeBlockFromCandidate(tree, worldX, y, worldZ);
            if (block == tree.log && block != 0) {
                return block;
            }
            if (firstLeaves == 0 && block != 0) {
                firstLeaves = block;
            }
        }
    }
    return firstLeaves;
}

bool canTreeLogReplace(BlockID id) {
    return id == 0 ||
           id == BlockIds::TALL_GRASS ||
           id == BlockIds::ROSE ||
           id == BlockIds::OAK_LEAVES ||
           id == BlockIds::BIRCH_LEAVES;
}

bool canTreeLeavesReplace(BlockID id) {
    return id == 0 ||
           id == BlockIds::TALL_GRASS ||
           id == BlockIds::ROSE;
}

BlockID sampleVegetationBlock(int worldX,
                              int worldZ,
                              uint32_t seed,
                              TerrainBiome biome,
                              double moisture,
                              int seaLevel,
                              int surfaceY) {
    if (surfaceY < seaLevel || moisture < 0.36) {
        return 0;
    }

    double density = 0.0;
    switch (biome) {
        case TerrainBiome::Temperate:
            density = 0.20 + moisture * 0.25;
            break;
        case TerrainBiome::Mountain:
            density = 0.05 + moisture * 0.10;
            break;
        case TerrainBiome::Arid:
        case TerrainBiome::HighMountain:
        default:
            return 0;
    }

    const uint32_t h = hashColumn(worldX, worldZ, seed);

    if (hash32(h ^ kDecorSaltDensity) > probabilityToCutoff(density)) {
        return 0;
    }

    const double flowerChance = (biome == TerrainBiome::Temperate)
                                    ? (0.06 + moisture * 0.06)
                                    : (0.02 + moisture * 0.02);
    if (hash32(h ^ kDecorSaltFlower) < probabilityToCutoff(flowerChance)) {
        return BlockIds::ROSE;
    }
    return BlockIds::TALL_GRASS;
}

} // namespace

void TerrainGenerator::init(uint32_t seed, int seaLevel) {
    m_seed = seed;
    m_seaLevel = std::clamp(seaLevel, 16, Chunk::SIZE_Y - 32);
}

BlockID TerrainGenerator::sampleBlock(const int worldX, const int y, const int worldZ) const {
    if (y < 0 || y >= Chunk::SIZE_Y) {
        return 0;
    }

    int surfaceY = 0;
    double moisture = 0.0;
    double ruggedness = 0.0;
    TerrainBiome surfaceKind = TerrainBiome::Temperate;
    sampleSurfaceAndMoistureScalar(worldX, worldZ, m_seed, m_seaLevel, surfaceY, moisture, surfaceKind, ruggedness);

    const bool belowSeaLevel = surfaceY < m_seaLevel;
    BlockID topBlock = BlockIds::GRASS;
    BlockID fillBlock = BlockIds::DIRT;
    int coverDepth = 3;

    if (belowSeaLevel || surfaceKind == TerrainBiome::Arid || moisture < 0.34) {
        topBlock = BlockIds::SAND;
        fillBlock = BlockIds::SAND;
        coverDepth = 4;
    } else if (surfaceKind == TerrainBiome::HighMountain) {
        const double dirtPatchNoise = fbm2D(static_cast<double>(worldX), static_cast<double>(worldZ),
                                            24.0, 2, m_seed ^ 0x9b05688cU);
        const bool dirtPatch = dirtPatchNoise > 0.62 && moisture > 0.40;
        topBlock = BlockIds::GRASS;
        fillBlock = dirtPatch ? BlockIds::DIRT : BlockIds::STONE;
        coverDepth = dirtPatch ? 2 : 1;
    } else if (surfaceKind == TerrainBiome::Mountain) {
        const bool rockyTop = ruggedness > 0.62 && moisture < 0.55;
        topBlock = BlockIds::GRASS;
        fillBlock = rockyTop ? BlockIds::STONE : BlockIds::DIRT;
        coverDepth = rockyTop ? 2 : 3;
    } else {
        const double soilNoise = fbm2D(static_cast<double>(worldX), static_cast<double>(worldZ),
                                       18.0, 2, m_seed ^ 0x2f6b5a13U);
        if (soilNoise > 0.74) {
            coverDepth = 4;
        } else if (soilNoise < 0.30) {
            coverDepth = 2;
        }
    }

    BlockID id = 0;
    if (y == 0) {
        id = BlockIds::BEDROCK;
    } else if (y <= surfaceY) {
        id = BlockIds::STONE;
        if (y == surfaceY) {
            id = topBlock;
        } else if (y >= surfaceY - coverDepth) {
            id = fillBlock;
        }

        if (id != 0 && shouldCarveCave(worldX, y, worldZ, surfaceY)) {
            id = 0;
        }

        if (id == BlockIds::STONE && y <= 128) {
            id = sampleOreBlock(worldX, y, worldZ, id);
        }
    } else if (y <= m_seaLevel) {
        id = naturalWaterState();
    }

    if (id == 0) {
        const BlockID treeBlock = sampleTreeBlock(worldX, y, worldZ, m_seed, m_seaLevel);
        if (treeBlock != 0) {
            return treeBlock;
        }
    }

    const int vegetationY = surfaceY + 1;
    if (id == 0 &&
        y == vegetationY &&
        topBlock == BlockIds::GRASS) {
        id = sampleVegetationBlock(worldX, worldZ, m_seed, surfaceKind, moisture, m_seaLevel, surfaceY);
    }

    return id;
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

    int i = 0;
#if defined(MECRAFT_HAS_AVX2)
    for (; i + 3 < count; i += 4) {
        double moisture[4] = {};
        double ruggedness[4] = {};
        TerrainBiome kind[4] = {};
        sampleSurfaceAndMoisture4(startWorldX + i, startWorldX + i + 1, startWorldX + i + 2, startWorldX + i + 3,
                                  worldZ, m_seed, m_seaLevel, outSurfaceY + i, moisture, kind, ruggedness);
    }
#endif
#if defined(MECRAFT_HAS_SSE2)
    for (; i + 1 < count; i += 2) {
        double moisture[2] = {};
        double ruggedness[2] = {};
        TerrainBiome kind[2] = {};
        sampleSurfaceAndMoisture2(startWorldX + i, startWorldX + i + 1, worldZ, m_seed, m_seaLevel,
                                  outSurfaceY + i, moisture, kind, ruggedness);
    }
#endif
    for (; i < count; ++i) {
        double moisture = 0.0;
        double ruggedness = 0.0;
        TerrainBiome kind = TerrainBiome::Temperate;
        sampleSurfaceAndMoistureScalar(startWorldX + i, worldZ, m_seed, m_seaLevel,
                                       outSurfaceY[i], moisture, kind, ruggedness);
    }
}

TerrainBiome TerrainGenerator::sampleBiome(int worldX, int worldZ) const {
    int surfaceY = 0;
    double moisture = 0.0;
    double ruggedness = 0.0;
    TerrainBiome kind = TerrainBiome::Temperate;
    sampleSurfaceAndMoistureScalar(worldX, worldZ, m_seed, m_seaLevel, surfaceY, moisture, kind, ruggedness);
    return kind;
}

double TerrainGenerator::sampleMoisture(int worldX, int worldZ) const {
    return fbm2D(static_cast<double>(worldX), static_cast<double>(worldZ), 420.0, 3, m_seed ^ 0xa54ff53aU);
}

bool TerrainGenerator::shouldCarveCave(int worldX, int y, int worldZ, int surfaceY) const {
    if (y > surfaceY - 5 || y < 10) {
        return false;
    }

    return shouldCarveCaveFromNoise(sampleCaveNoise(worldX, y, worldZ, m_seed), y, surfaceY);
}

BlockID TerrainGenerator::sampleOreBlock(int worldX, int y, int worldZ, BlockID baseBlock) const {
    if (baseBlock != BlockIds::STONE || y > 128) {
        return baseBlock;
    }

    uint32_t h = m_seed;
    h ^= hash32(static_cast<uint32_t>(worldX) * kOreXMul);
    h ^= hash32(static_cast<uint32_t>(y) * kOreYMul);
    h ^= hash32(static_cast<uint32_t>(worldZ) * kOreZMul);

    if (y <= 16) {
        return hash32(h ^ kOreSaltDiamond) < kOreCutoffDiamond ? BlockIds::DIAMOND_ORE : baseBlock;
    }
    if (y <= 32) {
        return hash32(h ^ kOreSaltGold) < kOreCutoffGold ? BlockIds::GOLD_ORE : baseBlock;
    }
    if (y <= 64) {
        return hash32(h ^ kOreSaltIron) < kOreCutoffIron ? BlockIds::IRON_ORE : baseBlock;
    }
    return hash32(h ^ kOreSaltCoal) < kOreCutoffCoal ? BlockIds::COAL_ORE : baseBlock;
}

void TerrainGenerator::generateChunk(Chunk& chunk) const {
    const glm::ivec3 offset = chunk.getWorldOffset();
    const BlockID waterState = naturalWaterState();
    std::array<std::array<BlockID, SubChunk::BLOCK_COUNT>, Chunk::NUM_SUB_CHUNKS> generatedBlocks{};
    std::array<bool, Chunk::NUM_SUB_CHUNKS> hasGeneratedBlocks{};

    for (int z = 0; z < Chunk::SIZE_Z; ++z) {
        for (int x = 0; x < Chunk::SIZE_X;) {
            int laneCount = 1;
            int sampledSurface[4] = {};
            double sampledMoisture[4] = {};
            double sampledRuggedness[4] = {};
            TerrainBiome sampledSurfaceKind[4] = {};
            const int remaining = Chunk::SIZE_X - x;

            if (remaining >= 2) {
                laneCount = 2;
                sampleSurfaceAndMoisture2(offset.x + x, offset.x + x + 1, offset.z + z, m_seed, m_seaLevel,
                                          sampledSurface, sampledMoisture, sampledSurfaceKind, sampledRuggedness);
            } else {
                laneCount = 1;
                sampleSurfaceAndMoistureScalar(offset.x + x, offset.z + z, m_seed, m_seaLevel,
                                               sampledSurface[0], sampledMoisture[0],
                                               sampledSurfaceKind[0], sampledRuggedness[0]);
            }

            for (int lane = 0; lane < laneCount; ++lane) {
                const int localX = x + lane;
                const int worldX = offset.x + localX;
                const int worldZ = offset.z + z;
                uint32_t oreColumnSeed = m_seed;
                oreColumnSeed ^= hash32(static_cast<uint32_t>(worldX) * kOreXMul);
                oreColumnSeed ^= hash32(static_cast<uint32_t>(worldZ) * kOreZMul);
                const int surfaceY = sampledSurface[lane];
                const double moisture = sampledMoisture[lane];
                const TerrainBiome surfaceKind = sampledSurfaceKind[lane];
                const double ruggedness = sampledRuggedness[lane];
                std::array<uint8_t, Chunk::SIZE_Y> caveMask{};

                const bool belowSeaLevel = surfaceY < m_seaLevel;
                BlockID topBlock = BlockIds::GRASS;
                BlockID fillBlock = BlockIds::DIRT;
                int coverDepth = 3;

                if (belowSeaLevel || surfaceKind == TerrainBiome::Arid || moisture < 0.34) {
                    topBlock = BlockIds::SAND;
                    fillBlock = BlockIds::SAND;
                    coverDepth = 4;
                } else if (surfaceKind == TerrainBiome::HighMountain) {
                    const double dirtPatchNoise = fbm2D(static_cast<double>(worldX), static_cast<double>(worldZ),
                                                        24.0, 2, m_seed ^ 0x9b05688cU);
                    const bool dirtPatch = dirtPatchNoise > 0.62 && moisture > 0.40;
                    topBlock = BlockIds::GRASS;
                    fillBlock = dirtPatch ? BlockIds::DIRT : BlockIds::STONE;
                    coverDepth = dirtPatch ? 2 : 1;
                } else if (surfaceKind == TerrainBiome::Mountain) {
                    const bool rockyTop = ruggedness > 0.62 && moisture < 0.55;
                    topBlock = BlockIds::GRASS;
                    fillBlock = rockyTop ? BlockIds::STONE : BlockIds::DIRT;
                    coverDepth = rockyTop ? 2 : 3;
                } else {
                    const double soilNoise = fbm2D(static_cast<double>(worldX), static_cast<double>(worldZ),
                                                   18.0, 2, m_seed ^ 0x2f6b5a13U);
                    if (soilNoise > 0.74) {
                        coverDepth = 4;
                    } else if (soilNoise < 0.30) {
                        coverDepth = 2;
                    }
                }

                const int columnTop = std::max(surfaceY, m_seaLevel);
                if (surfaceY - 5 >= 10) {
                    buildCaveMaskColumn(worldX, worldZ, surfaceY, m_seed, caveMask);
                }
                int highestOpaqueY = 0;
                for (int y = 0; y <= columnTop; ++y) {
                    BlockID id = 0;
                    if (y == 0) {
                        id = BlockIds::BEDROCK;
                    } else if (y <= surfaceY) {
                        id = BlockIds::STONE;
                        if (y == surfaceY) {
                            id = topBlock;
                        } else if (y >= surfaceY - coverDepth) {
                            id = fillBlock;
                        }

                        if (id == BlockIds::STONE && y >= 10 && y <= surfaceY - 5 && caveMask[y] != 0) {
                            id = 0;
                        }

                        if (id == BlockIds::STONE && y <= 128) {
                            const uint32_t oreHash = oreColumnSeed ^ hash32(static_cast<uint32_t>(y) * kOreYMul);
                            if (y <= 16) {
                                if (hash32(oreHash ^ kOreSaltDiamond) < kOreCutoffDiamond) {
                                    id = BlockIds::DIAMOND_ORE;
                                }
                            } else if (y <= 32) {
                                if (hash32(oreHash ^ kOreSaltGold) < kOreCutoffGold) {
                                    id = BlockIds::GOLD_ORE;
                                }
                            } else if (y <= 64) {
                                if (hash32(oreHash ^ kOreSaltIron) < kOreCutoffIron) {
                                    id = BlockIds::IRON_ORE;
                                }
                            } else {
                                if (hash32(oreHash ^ kOreSaltCoal) < kOreCutoffCoal) {
                                    id = BlockIds::COAL_ORE;
                                }
                            }
                        }
                    } else if (y <= m_seaLevel) {
                        id = waterState;
                    }

                    if (id != 0) {
                        const int scy = Chunk::toSubChunkIndex(y);
                        generatedBlocks[scy][SubChunk::toIndex(localX, Chunk::toSubChunkLocalY(y), z)] = id;
                        hasGeneratedBlocks[scy] = true;
                        if (BlockRegistry::getOpacityFast(id) >= 15) {
                            highestOpaqueY = y;
                        }
                    }
                }

                const int vegetationY = surfaceY + 1;
                if (vegetationY < Chunk::SIZE_Y) {
                    const int surfaceScy = Chunk::toSubChunkIndex(surfaceY);
                    const int vegetationScy = Chunk::toSubChunkIndex(vegetationY);
                    const BlockID surfaceBlock =
                        generatedBlocks[surfaceScy][SubChunk::toIndex(localX, Chunk::toSubChunkLocalY(surfaceY), z)];
                    const BlockID blockAbove =
                        generatedBlocks[vegetationScy][SubChunk::toIndex(localX, Chunk::toSubChunkLocalY(vegetationY), z)];

                    if (surfaceBlock == BlockIds::GRASS && blockAbove == 0) {
                        const BlockID vegetation = sampleVegetationBlock(worldX, worldZ, m_seed,
                                                                         surfaceKind, moisture,
                                                                         m_seaLevel, surfaceY);
                        if (vegetation != 0) {
                            generatedBlocks[vegetationScy][SubChunk::toIndex(localX, Chunk::toSubChunkLocalY(vegetationY), z)] = vegetation;
                            hasGeneratedBlocks[vegetationScy] = true;
                            if (BlockRegistry::getOpacityFast(vegetation) >= 15) {
                                highestOpaqueY = std::max(highestOpaqueY, vegetationY);
                            }
                        }
                    }
                }
                chunk.setHeightMap(localX, z, highestOpaqueY);

                // Sky light initialization is deferred to LightService::onChunkLoaded
            }

            x += laneCount;
        }
    }

    for (int anchorX = offset.x - kTreeScanRadius; anchorX < offset.x + Chunk::SIZE_X + kTreeScanRadius; ++anchorX) {
        for (int anchorZ = offset.z - kTreeScanRadius; anchorZ < offset.z + Chunk::SIZE_Z + kTreeScanRadius; ++anchorZ) {
            const TreeCandidate tree = sampleTreeCandidate(anchorX, anchorZ, m_seed, m_seaLevel);
            if (!tree.valid) {
                continue;
            }

            for (int dz = -kTreeLeafRadius; dz <= kTreeLeafRadius; ++dz) {
                const int worldZ = tree.worldZ + dz;
                const int localZ = worldZ - offset.z;
                if (localZ < 0 || localZ >= Chunk::SIZE_Z) {
                    continue;
                }

                for (int dx = -kTreeLeafRadius; dx <= kTreeLeafRadius; ++dx) {
                    const int worldX = tree.worldX + dx;
                    const int localX = worldX - offset.x;
                    if (localX < 0 || localX >= Chunk::SIZE_X) {
                        continue;
                    }

                    const int minY = tree.surfaceY + 1;
                    const int maxY = tree.surfaceY + tree.height + 1;
                    for (int y = minY; y <= maxY && y < Chunk::SIZE_Y; ++y) {
                        const BlockID treeBlock = sampleTreeBlockFromCandidate(tree, worldX, y, worldZ);
                        if (treeBlock == 0) {
                            continue;
                        }

                        const int scy = Chunk::toSubChunkIndex(y);
                        const int index = SubChunk::toIndex(localX, Chunk::toSubChunkLocalY(y), localZ);
                        const BlockID current = generatedBlocks[scy][index];
                        const bool canReplace = treeBlock == tree.log
                                                    ? canTreeLogReplace(current)
                                                    : canTreeLeavesReplace(current);
                        if (!canReplace) {
                            continue;
                        }

                        generatedBlocks[scy][index] = treeBlock;
                        hasGeneratedBlocks[scy] = true;
                        if (BlockRegistry::getOpacityFast(treeBlock) >= 15) {
                            chunk.setHeightMap(localX, localZ,
                                               std::max(chunk.getHeightMap(localX, localZ), y));
                        }
                    }
                }
            }
        }
    }

    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        if (!hasGeneratedBlocks[scy]) {
            continue;
        }

        if (SubChunk* sc = chunk.getOrCreateSubChunk(scy)) {
            sc->initializeFromBlocks(generatedBlocks[scy]);
        }
    }
}

