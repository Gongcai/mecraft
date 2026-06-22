#include "TerrainGenerator.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

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
constexpr uint32_t kOreSaltCopper = 0x6c8e9cf5U;
constexpr uint32_t kOreSaltRedstone = 0xa2f9836bU;
constexpr uint32_t kOreSaltLapis = 0x4f1bbcdcU;
constexpr uint32_t kOreSaltEmerald = 0x9d72e4a5U;
constexpr uint32_t kDecorSaltDensity = 0x4a3c2f1dU;
constexpr uint32_t kDecorSaltFlower = 0xc13f7e59U;
constexpr uint32_t kDecorSaltVariant = 0x8b44f2d7U;
constexpr uint32_t kTreeSaltDensity = 0x7b9d3f25U;
constexpr uint32_t kTreeSaltSpecies = 0x2f4c8a91U;
constexpr uint32_t kTreeSaltHeight = 0x5e6b1c37U;
constexpr uint32_t kSurfaceSaltSoil = 0x2f6b5a13U;
constexpr uint32_t kSurfaceSaltDirtPatch = 0x9b05688cU;
constexpr uint32_t kSurfaceSaltSediment = 0x63a7cb91U;
constexpr uint32_t kSurfaceSaltRedSand = 0xbc4f8123U;
constexpr uint32_t kDeepLayerSalt = 0x77e5f2a9U;
constexpr uint32_t kRockSaltVariant = 0x3b9aca07U;
constexpr uint32_t kRockSaltTuff = 0xf00dcafeU;
constexpr uint32_t kRockSaltCalcite = 0x10293847U;

constexpr uint32_t kOreCutoffDiamond = static_cast<uint32_t>(0.0045 * 4294967295.0);
constexpr uint32_t kOreCutoffGold = static_cast<uint32_t>(0.0080 * 4294967295.0);
constexpr uint32_t kOreCutoffIron = static_cast<uint32_t>(0.0160 * 4294967295.0);
constexpr uint32_t kOreCutoffCoal = static_cast<uint32_t>(0.0240 * 4294967295.0);

constexpr int kTreeLeafRadius = 2;
constexpr int kTreeScanRadius = kTreeLeafRadius;

uint32_t probabilityToCutoff(double probability) {
    const double clamped = std::clamp(probability, 0.0, 1.0);
    return static_cast<uint32_t>(clamped * 4294967295.0);
}

struct OreRule {
    BlockID targetBlock = 0;
    BlockID outputBlock = 0;
    int minY = 0;
    int maxY = 0;
    uint32_t salt = 0;
    uint32_t cutoff = 0;
};

struct WorldGenBlocks {
    BlockID air = 0;
    BlockID dirt = 0;
    BlockID grass = 0;
    BlockID stone = 0;
    BlockID sand = 0;
    BlockID redSand = 0;
    BlockID sandstone = 0;
    BlockID redSandstone = 0;
    BlockID gravel = 0;
    BlockID clay = 0;
    BlockID snowBlock = 0;
    BlockID water = 0;
    BlockID bedrock = 0;
    BlockID deepslate = 0;
    BlockID granite = 0;
    BlockID diorite = 0;
    BlockID andesite = 0;
    BlockID tuff = 0;
    BlockID calcite = 0;
    BlockID dripstoneBlock = 0;

    BlockID coalOre = 0;
    BlockID ironOre = 0;
    BlockID goldOre = 0;
    BlockID diamondOre = 0;
    BlockID copperOre = 0;
    BlockID redstoneOre = 0;
    BlockID lapisOre = 0;
    BlockID emeraldOre = 0;
    BlockID deepslateCoalOre = 0;
    BlockID deepslateIronOre = 0;
    BlockID deepslateGoldOre = 0;
    BlockID deepslateDiamondOre = 0;
    BlockID deepslateCopperOre = 0;
    BlockID deepslateRedstoneOre = 0;
    BlockID deepslateLapisOre = 0;
    BlockID deepslateEmeraldOre = 0;

    BlockID oakLog = 0;
    BlockID oakLeaves = 0;
    BlockID birchLog = 0;
    BlockID birchLeaves = 0;
    BlockID spruceLog = 0;
    BlockID spruceLeaves = 0;
    BlockID jungleLog = 0;
    BlockID jungleLeaves = 0;
    BlockID acaciaLog = 0;
    BlockID acaciaLeaves = 0;
    BlockID darkOakLog = 0;
    BlockID darkOakLeaves = 0;
    BlockID cherryLog = 0;
    BlockID cherryLeaves = 0;
    BlockID paleOakLog = 0;
    BlockID paleOakLeaves = 0;

    BlockID tallGrass = 0;
    BlockID shortGrass = 0;
    BlockID fern = 0;
    BlockID rose = 0;
    BlockID poppy = 0;
    BlockID dandelion = 0;
    BlockID deadBush = 0;
    BlockID brownMushroom = 0;
    BlockID redMushroom = 0;

    std::array<OreRule, 8> stoneOreRules{};
    std::array<OreRule, 8> deepslateOreRules{};
};

struct SurfaceProfile {
    BlockID topBlock = 0;
    BlockID fillBlock = 0;
    BlockID foundationBlock = 0;
    int coverDepth = 0;
    int foundationDepth = 0;
};

struct TerrainColumnSample {
    int surfaceY = 0;
    double moisture = 0.0;
    double ruggedness = 0.0;
    TerrainBiome biome = TerrainBiome::Temperate;
    SurfaceProfile surface{};
};

BlockID requireBlockId(const char* path) {
    BlockID id = 0;
    if (!BlockRegistry::tryGetId(NamespacedId("minecraft", path), id)) {
        throw std::runtime_error(std::string("Missing required world generation block: minecraft:") + path);
    }
    return id;
}

WorldGenBlocks makeWorldGenBlocks() {
    WorldGenBlocks blocks;
    blocks.air = requireBlockId("air");
    blocks.dirt = requireBlockId("dirt");
    blocks.grass = requireBlockId("grass_block");
    blocks.stone = requireBlockId("stone");
    blocks.sand = requireBlockId("sand");
    blocks.redSand = requireBlockId("red_sand");
    blocks.sandstone = requireBlockId("sandstone");
    blocks.redSandstone = requireBlockId("red_sandstone");
    blocks.gravel = requireBlockId("gravel");
    blocks.clay = requireBlockId("clay");
    blocks.snowBlock = requireBlockId("snow_block");
    blocks.water = requireBlockId("water");
    blocks.bedrock = requireBlockId("bedrock");
    blocks.deepslate = requireBlockId("deepslate");
    blocks.granite = requireBlockId("granite");
    blocks.diorite = requireBlockId("diorite");
    blocks.andesite = requireBlockId("andesite");
    blocks.tuff = requireBlockId("tuff");
    blocks.calcite = requireBlockId("calcite");
    blocks.dripstoneBlock = requireBlockId("dripstone_block");

    blocks.coalOre = requireBlockId("coal_ore");
    blocks.ironOre = requireBlockId("iron_ore");
    blocks.goldOre = requireBlockId("gold_ore");
    blocks.diamondOre = requireBlockId("diamond_ore");
    blocks.copperOre = requireBlockId("copper_ore");
    blocks.redstoneOre = requireBlockId("redstone_ore");
    blocks.lapisOre = requireBlockId("lapis_ore");
    blocks.emeraldOre = requireBlockId("emerald_ore");
    blocks.deepslateCoalOre = requireBlockId("deepslate_coal_ore");
    blocks.deepslateIronOre = requireBlockId("deepslate_iron_ore");
    blocks.deepslateGoldOre = requireBlockId("deepslate_gold_ore");
    blocks.deepslateDiamondOre = requireBlockId("deepslate_diamond_ore");
    blocks.deepslateCopperOre = requireBlockId("deepslate_copper_ore");
    blocks.deepslateRedstoneOre = requireBlockId("deepslate_redstone_ore");
    blocks.deepslateLapisOre = requireBlockId("deepslate_lapis_ore");
    blocks.deepslateEmeraldOre = requireBlockId("deepslate_emerald_ore");

    blocks.oakLog = requireBlockId("oak_log");
    blocks.oakLeaves = requireBlockId("oak_leaves");
    blocks.birchLog = requireBlockId("birch_log");
    blocks.birchLeaves = requireBlockId("birch_leaves");
    blocks.spruceLog = requireBlockId("spruce_log");
    blocks.spruceLeaves = requireBlockId("spruce_leaves");
    blocks.jungleLog = requireBlockId("jungle_log");
    blocks.jungleLeaves = requireBlockId("jungle_leaves");
    blocks.acaciaLog = requireBlockId("acacia_log");
    blocks.acaciaLeaves = requireBlockId("acacia_leaves");
    blocks.darkOakLog = requireBlockId("dark_oak_log");
    blocks.darkOakLeaves = requireBlockId("dark_oak_leaves");
    blocks.cherryLog = requireBlockId("cherry_log");
    blocks.cherryLeaves = requireBlockId("cherry_leaves");
    blocks.paleOakLog = requireBlockId("pale_oak_log");
    blocks.paleOakLeaves = requireBlockId("pale_oak_leaves");

    blocks.tallGrass = requireBlockId("tall_grass");
    blocks.shortGrass = requireBlockId("short_grass");
    blocks.fern = requireBlockId("fern");
    blocks.rose = requireBlockId("rose");
    blocks.poppy = requireBlockId("poppy");
    blocks.dandelion = requireBlockId("dandelion");
    blocks.deadBush = requireBlockId("dead_bush");
    blocks.brownMushroom = requireBlockId("brown_mushroom");
    blocks.redMushroom = requireBlockId("red_mushroom");

    blocks.stoneOreRules = {{
        {blocks.stone, blocks.diamondOre, 1, 16, kOreSaltDiamond, kOreCutoffDiamond},
        {blocks.stone, blocks.emeraldOre, 24, 128, kOreSaltEmerald, probabilityToCutoff(0.0025)},
        {blocks.stone, blocks.redstoneOre, 1, 24, kOreSaltRedstone, probabilityToCutoff(0.0100)},
        {blocks.stone, blocks.lapisOre, 8, 48, kOreSaltLapis, probabilityToCutoff(0.0060)},
        {blocks.stone, blocks.goldOre, 1, 32, kOreSaltGold, kOreCutoffGold},
        {blocks.stone, blocks.copperOre, 40, 96, kOreSaltCopper, probabilityToCutoff(0.0140)},
        {blocks.stone, blocks.ironOre, 1, 72, kOreSaltIron, kOreCutoffIron},
        {blocks.stone, blocks.coalOre, 48, 128, kOreSaltCoal, kOreCutoffCoal},
    }};
    blocks.deepslateOreRules = {{
        {blocks.deepslate, blocks.deepslateDiamondOre, 1, 20, kOreSaltDiamond, probabilityToCutoff(0.0050)},
        {blocks.deepslate, blocks.deepslateEmeraldOre, 16, 64, kOreSaltEmerald, probabilityToCutoff(0.0020)},
        {blocks.deepslate, blocks.deepslateRedstoneOre, 1, 28, kOreSaltRedstone, probabilityToCutoff(0.0120)},
        {blocks.deepslate, blocks.deepslateLapisOre, 8, 40, kOreSaltLapis, probabilityToCutoff(0.0065)},
        {blocks.deepslate, blocks.deepslateGoldOre, 1, 36, kOreSaltGold, probabilityToCutoff(0.0085)},
        {blocks.deepslate, blocks.deepslateCopperOre, 16, 56, kOreSaltCopper, probabilityToCutoff(0.0100)},
        {blocks.deepslate, blocks.deepslateIronOre, 1, 56, kOreSaltIron, probabilityToCutoff(0.0150)},
        {blocks.deepslate, blocks.deepslateCoalOre, 24, 64, kOreSaltCoal, probabilityToCutoff(0.0140)},
    }};
    return blocks;
}

const WorldGenBlocks& worldGenBlocks() {
    static const WorldGenBlocks blocks = makeWorldGenBlocks();
    return blocks;
}

BlockID naturalWaterState() {
    return FluidState::makeWater(0, false);
}

MECRAFT_FORCEINLINE double hashToUnit(uint32_t value) {
    constexpr double kInvUint32Max = 1.0 / 4294967295.0;
    return static_cast<double>(value) * kInvUint32Max;
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

int floorDiv(const int value, const int divisor) {
    const int quotient = value / divisor;
    const int remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}

uint32_t hashRockCell(const int worldX, const int y, const int worldZ, const uint32_t seed) {
    uint32_t h = seed;
    h ^= hash32(static_cast<uint32_t>(floorDiv(worldX, 18)) * 0x27d4eb2dU);
    h ^= hash32(static_cast<uint32_t>(floorDiv(y, 12)) * 0x85ebca6bU);
    h ^= hash32(static_cast<uint32_t>(floorDiv(worldZ, 18)) * 0xc2b2ae35U);
    return hash32(h);
}

SurfaceProfile sampleSurfaceProfile(const int worldX,
                                    const int worldZ,
                                    const uint32_t seed,
                                    const int seaLevel,
                                    const int surfaceY,
                                    const double moisture,
                                    const TerrainBiome biome,
                                    const double ruggedness,
                                    const WorldGenBlocks& blocks) {
    SurfaceProfile profile{blocks.grass, blocks.dirt, 0, 3, 0};
    const bool belowSeaLevel = surfaceY < seaLevel;

    if (belowSeaLevel) {
        const double sedimentNoise = fbm2D(static_cast<double>(worldX), static_cast<double>(worldZ),
                                           34.0, 2, seed ^ kSurfaceSaltSediment);
        if (sedimentNoise > 0.73) {
            profile.topBlock = blocks.gravel;
            profile.fillBlock = blocks.gravel;
            profile.foundationBlock = 0;
            profile.foundationDepth = 0;
        } else if (sedimentNoise < 0.18 && moisture > 0.50) {
            profile.topBlock = blocks.clay;
            profile.fillBlock = blocks.clay;
            profile.foundationBlock = 0;
            profile.foundationDepth = 0;
        } else {
            profile.topBlock = blocks.sand;
            profile.fillBlock = blocks.sand;
            profile.foundationBlock = blocks.sandstone;
            profile.foundationDepth = 2;
        }
        profile.coverDepth = 4;
        return profile;
    }

    if (biome == TerrainBiome::Arid || moisture < 0.34) {
        const double redSandNoise = fbm2D(static_cast<double>(worldX), static_cast<double>(worldZ),
                                          180.0, 2, seed ^ kSurfaceSaltRedSand);
        const bool useRedSand = moisture < 0.26 || redSandNoise > 0.66;
        profile.topBlock = useRedSand ? blocks.redSand : blocks.sand;
        profile.fillBlock = profile.topBlock;
        profile.foundationBlock = useRedSand ? blocks.redSandstone : blocks.sandstone;
        profile.coverDepth = 4;
        profile.foundationDepth = 3;
        return profile;
    }

    if (biome == TerrainBiome::HighMountain) {
        const bool snowCap = surfaceY >= seaLevel + 42 ||
                             (surfaceY >= seaLevel + 30 && moisture > 0.48);
        if (snowCap) {
            profile.topBlock = blocks.snowBlock;
            profile.fillBlock = blocks.stone;
            profile.coverDepth = 1;
            return profile;
        }

        const double dirtPatchNoise = fbm2D(static_cast<double>(worldX), static_cast<double>(worldZ),
                                            24.0, 2, seed ^ kSurfaceSaltDirtPatch);
        const bool dirtPatch = dirtPatchNoise > 0.62 && moisture > 0.40;
        profile.topBlock = blocks.grass;
        profile.fillBlock = dirtPatch ? blocks.dirt : blocks.stone;
        profile.coverDepth = dirtPatch ? 2 : 1;
        return profile;
    }

    if (biome == TerrainBiome::Mountain) {
        const bool snowCap = surfaceY >= seaLevel + 48 && moisture > 0.40;
        if (snowCap) {
            profile.topBlock = blocks.snowBlock;
            profile.fillBlock = blocks.stone;
            profile.coverDepth = 1;
            return profile;
        }

        const double gravelNoise = fbm2D(static_cast<double>(worldX), static_cast<double>(worldZ),
                                         30.0, 2, seed ^ kSurfaceSaltSediment);
        const bool gravelPatch = ruggedness > 0.70 && moisture < 0.56 && gravelNoise > 0.58;
        const bool rockyTop = ruggedness > 0.62 && moisture < 0.55;
        profile.topBlock = gravelPatch ? blocks.gravel : blocks.grass;
        profile.fillBlock = (rockyTop || gravelPatch) ? blocks.stone : blocks.dirt;
        profile.coverDepth = (rockyTop || gravelPatch) ? 2 : 3;
        return profile;
    }

    const double soilNoise = fbm2D(static_cast<double>(worldX), static_cast<double>(worldZ),
                                   18.0, 2, seed ^ kSurfaceSaltSoil);
    if (soilNoise > 0.74) {
        profile.coverDepth = 4;
    } else if (soilNoise < 0.30) {
        profile.coverDepth = 2;
    }
    return profile;
}

TerrainColumnSample sampleTerrainColumn(const int worldX,
                                        const int worldZ,
                                        const uint32_t seed,
                                        const int seaLevel,
                                        const WorldGenBlocks& blocks) {
    TerrainColumnSample column;
    sampleSurfaceAndMoistureScalar(worldX, worldZ, seed, seaLevel,
                                   column.surfaceY, column.moisture, column.biome, column.ruggedness);
    column.surface = sampleSurfaceProfile(worldX, worldZ, seed, seaLevel,
                                          column.surfaceY, column.moisture,
                                          column.biome, column.ruggedness, blocks);
    return column;
}

BlockID sampleStoneLayerBlock(const int worldX,
                              const int y,
                              const int worldZ,
                              const uint32_t seed,
                              const WorldGenBlocks& blocks) {
    const uint32_t columnHash = hashColumn(worldX, worldZ, seed) ^ kDeepLayerSalt;
    const int deepslateTop = 20 + static_cast<int>(hash32(columnHash) % 10U);
    const int stoneFloor = 42 + static_cast<int>(hash32(columnHash ^ 0xb5297a4dU) % 9U);
    if (y <= deepslateTop) {
        return blocks.deepslate;
    }
    if (y >= stoneFloor) {
        return blocks.stone;
    }

    const double deepslateChance =
        static_cast<double>(stoneFloor - y) / static_cast<double>(stoneFloor - deepslateTop);
    const uint32_t mixHash = hash32(columnHash ^ hash32(static_cast<uint32_t>(y) * kOreYMul));
    return mixHash < probabilityToCutoff(deepslateChance) ? blocks.deepslate : blocks.stone;
}

BlockID sampleRockBlock(const int worldX,
                        const int y,
                        const int worldZ,
                        const uint32_t seed,
                        const WorldGenBlocks& blocks) {
    const BlockID layerBlock = sampleStoneLayerBlock(worldX, y, worldZ, seed, blocks);
    const uint32_t cellHash = hashRockCell(worldX, y, worldZ, seed);

    if (layerBlock == blocks.deepslate) {
        if (y <= 64 && hash32(cellHash ^ kRockSaltTuff) < probabilityToCutoff(0.12)) {
            return blocks.tuff;
        }
        return blocks.deepslate;
    }

    if (y < 80 && hash32(cellHash ^ kRockSaltCalcite) < probabilityToCutoff(0.035)) {
        return (hash32(cellHash ^ 0x51ed270bU) & 1U) == 0U ? blocks.calcite : blocks.dripstoneBlock;
    }

    const uint32_t variantRoll = hash32(cellHash ^ kRockSaltVariant);
    if (variantRoll < probabilityToCutoff(0.16)) {
        switch (hash32(cellHash ^ 0x94d049bbU) % 3U) {
            case 0U:
                return blocks.granite;
            case 1U:
                return blocks.diorite;
            default:
                return blocks.andesite;
        }
    }
    return blocks.stone;
}

BlockID sampleTerrainSolidBlock(const int worldX,
                                const int y,
                                const int worldZ,
                                const uint32_t seed,
                                const TerrainColumnSample& column,
                                const WorldGenBlocks& blocks) {
    if (y == 0) {
        return blocks.bedrock;
    }

    BlockID id = sampleRockBlock(worldX, y, worldZ, seed, blocks);
    if (y == column.surfaceY) {
        id = column.surface.topBlock;
    } else if (y >= column.surfaceY - column.surface.coverDepth) {
        id = column.surface.fillBlock;
    } else if (column.surface.foundationDepth > 0 &&
               y >= column.surfaceY - column.surface.coverDepth - column.surface.foundationDepth) {
        id = column.surface.foundationBlock;
    }
    return id;
}

bool isCaveCarvableBlock(const BlockID id, const WorldGenBlocks& blocks) {
    return id == blocks.stone ||
           id == blocks.deepslate ||
           id == blocks.granite ||
           id == blocks.diorite ||
           id == blocks.andesite ||
           id == blocks.tuff ||
           id == blocks.calcite ||
           id == blocks.dripstoneBlock;
}

BlockID sampleOreBlockFromHash(const int y,
                               const BlockID baseBlock,
                               const uint32_t oreColumnSeed,
                               const WorldGenBlocks& blocks) {
    const std::array<OreRule, 8>* rules = nullptr;
    if (baseBlock == blocks.stone) {
        rules = &blocks.stoneOreRules;
    } else if (baseBlock == blocks.deepslate) {
        rules = &blocks.deepslateOreRules;
    } else {
        return baseBlock;
    }

    if (y > 128) {
        return baseBlock;
    }

    const uint32_t oreHash = oreColumnSeed ^ hash32(static_cast<uint32_t>(y) * kOreYMul);
    for (const OreRule& rule : *rules) {
        if (baseBlock == rule.targetBlock &&
            y >= rule.minY &&
            y <= rule.maxY &&
            hash32(oreHash ^ rule.salt) < rule.cutoff) {
            return rule.outputBlock;
        }
    }
    return baseBlock;
}

struct TreeSpeciesChoice {
    BlockID log = 0;
    BlockID leaves = 0;
    int minHeight = 0;
    int heightSpan = 0;
    uint32_t weight = 0;
};

bool surfaceCanHostTree(const TerrainBiome biome, const double moisture, const int seaLevel, const int surfaceY) {
    if (surfaceY < seaLevel) {
        return false;
    }

    switch (biome) {
        case TerrainBiome::Temperate:
            return moisture >= 0.38;
        case TerrainBiome::Mountain:
            return moisture >= 0.40;
        case TerrainBiome::Arid:
            return moisture >= 0.28;
        case TerrainBiome::HighMountain:
        default:
            return false;
    }
}

bool selectTreeSpecies(const TerrainBiome biome,
                       const double moisture,
                       const double ruggedness,
                       const uint32_t columnHash,
                       const WorldGenBlocks& blocks,
                       TreeSpeciesChoice& outSpecies) {
    std::array<TreeSpeciesChoice, 8> choices{};
    int count = 0;
    uint32_t totalWeight = 0;

    const auto addChoice = [&](const BlockID log,
                               const BlockID leaves,
                               const int minHeight,
                               const int heightSpan,
                               const uint32_t weight) {
        choices[static_cast<size_t>(count)] = TreeSpeciesChoice{log, leaves, minHeight, heightSpan, weight};
        ++count;
        totalWeight += weight;
    };

    if (biome == TerrainBiome::Arid) {
        addChoice(blocks.acaciaLog, blocks.acaciaLeaves, 4, 3, 100);
    } else if (biome == TerrainBiome::Mountain) {
        addChoice(blocks.spruceLog, blocks.spruceLeaves, 5, 4, 70);
        addChoice(blocks.birchLog, blocks.birchLeaves, 4, 3, 25);
        if (ruggedness < 0.58) {
            addChoice(blocks.oakLog, blocks.oakLeaves, 4, 3, 20);
        }
    } else if (biome == TerrainBiome::Temperate) {
        addChoice(blocks.oakLog, blocks.oakLeaves, 4, 3, 42);
        addChoice(blocks.birchLog, blocks.birchLeaves, 4, 3, 32);
        addChoice(blocks.cherryLog, blocks.cherryLeaves, 4, 3, 10);
        addChoice(blocks.paleOakLog, blocks.paleOakLeaves, 4, 3, 8);
        if (moisture > 0.58) {
            addChoice(blocks.darkOakLog, blocks.darkOakLeaves, 5, 3, 16);
        }
        if (moisture > 0.66) {
            addChoice(blocks.jungleLog, blocks.jungleLeaves, 6, 4, 14);
        }
    }

    if (count == 0 || totalWeight == 0) {
        return false;
    }

    uint32_t roll = hash32(columnHash ^ kTreeSaltSpecies) % totalWeight;
    for (int i = 0; i < count; ++i) {
        const TreeSpeciesChoice& choice = choices[static_cast<size_t>(i)];
        if (roll < choice.weight) {
            outSpecies = choice;
            return true;
        }
        roll -= choice.weight;
    }

    outSpecies = choices[static_cast<size_t>(count - 1)];
    return true;
}

TreeCandidate sampleTreeCandidate(int worldX,
                                  int worldZ,
                                  uint32_t seed,
                                  int seaLevel,
                                  const WorldGenBlocks& blocks) {
    int surfaceY = 0;
    double moisture = 0.0;
    double ruggedness = 0.0;
    TerrainBiome biome = TerrainBiome::Temperate;
    sampleSurfaceAndMoistureScalar(worldX, worldZ, seed, seaLevel,
                                   surfaceY, moisture, biome, ruggedness);

    if (!surfaceCanHostTree(biome, moisture, seaLevel, surfaceY)) {
        return {};
    }

    double density = 0.0;
    if (biome == TerrainBiome::Temperate) {
        density = 0.012 + moisture * 0.018;
    } else if (biome == TerrainBiome::Mountain) {
        density = 0.004 + moisture * 0.008;
    } else {
        density = 0.003 + moisture * 0.006;
    }

    const uint32_t h = hashColumn(worldX, worldZ, seed);
    if (hash32(h ^ kTreeSaltDensity) > probabilityToCutoff(density)) {
        return {};
    }

    TreeSpeciesChoice species;
    if (!selectTreeSpecies(biome, moisture, ruggedness, h, blocks, species)) {
        return {};
    }

    const int height = species.minHeight + static_cast<int>(hash32(h ^ kTreeSaltHeight) %
                                                            static_cast<uint32_t>(species.heightSpan));
    if (surfaceY + height + 1 >= Chunk::SIZE_Y) {
        return {};
    }

    TreeCandidate candidate;
    candidate.valid = true;
    candidate.worldX = worldX;
    candidate.worldZ = worldZ;
    candidate.surfaceY = surfaceY;
    candidate.height = height;
    candidate.log = species.log;
    candidate.leaves = species.leaves;
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

BlockID sampleTreeBlock(int worldX,
                        int y,
                        int worldZ,
                        uint32_t seed,
                        int seaLevel,
                        const WorldGenBlocks& blocks) {
    BlockID firstLeaves = 0;
    for (int anchorX = worldX - kTreeScanRadius; anchorX <= worldX + kTreeScanRadius; ++anchorX) {
        for (int anchorZ = worldZ - kTreeScanRadius; anchorZ <= worldZ + kTreeScanRadius; ++anchorZ) {
            const TreeCandidate tree = sampleTreeCandidate(anchorX, anchorZ, seed, seaLevel, blocks);
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

bool isTreeLeavesBlock(const BlockID id, const WorldGenBlocks& blocks) {
    return id == blocks.oakLeaves ||
           id == blocks.birchLeaves ||
           id == blocks.spruceLeaves ||
           id == blocks.jungleLeaves ||
           id == blocks.acaciaLeaves ||
           id == blocks.darkOakLeaves ||
           id == blocks.cherryLeaves ||
           id == blocks.paleOakLeaves;
}

bool isReplaceableDecoration(const BlockID id, const WorldGenBlocks& blocks) {
    return id == blocks.tallGrass ||
           id == blocks.shortGrass ||
           id == blocks.fern ||
           id == blocks.rose ||
           id == blocks.poppy ||
           id == blocks.dandelion ||
           id == blocks.deadBush ||
           id == blocks.brownMushroom ||
           id == blocks.redMushroom;
}

bool canTreeLogReplace(const BlockID id, const WorldGenBlocks& blocks) {
    return id == 0 ||
           isReplaceableDecoration(id, blocks) ||
           isTreeLeavesBlock(id, blocks);
}

bool canTreeLeavesReplace(const BlockID id, const WorldGenBlocks& blocks) {
    return id == 0 ||
           isReplaceableDecoration(id, blocks);
}

BlockID sampleVegetationBlock(int worldX,
                              int worldZ,
                              uint32_t seed,
                              TerrainBiome biome,
                              double moisture,
                              int seaLevel,
                              int surfaceY,
                              BlockID surfaceBlock,
                              const WorldGenBlocks& blocks) {
    if (surfaceY < seaLevel) {
        return 0;
    }

    const uint32_t h = hashColumn(worldX, worldZ, seed);

    if (surfaceBlock == blocks.sand || surfaceBlock == blocks.redSand) {
        const double dryDensity = surfaceBlock == blocks.redSand ? 0.055 : 0.035;
        return hash32(h ^ kDecorSaltDensity) < probabilityToCutoff(dryDensity) ? blocks.deadBush : 0;
    }

    if (surfaceBlock != blocks.grass || moisture < 0.36) {
        return 0;
    }

    double density = 0.0;
    if (biome == TerrainBiome::Temperate) {
        density = 0.20 + moisture * 0.25;
    } else if (biome == TerrainBiome::Mountain) {
        density = 0.05 + moisture * 0.10;
    } else {
        return 0;
    }

    if (hash32(h ^ kDecorSaltDensity) > probabilityToCutoff(density)) {
        return 0;
    }

    const uint32_t variant = hash32(h ^ kDecorSaltVariant);
    const double flowerChance = biome == TerrainBiome::Temperate
                                    ? (0.06 + moisture * 0.06)
                                    : (0.02 + moisture * 0.02);
    if (hash32(h ^ kDecorSaltFlower) < probabilityToCutoff(flowerChance)) {
        switch (variant % 3U) {
            case 0U:
                return blocks.rose;
            case 1U:
                return blocks.poppy;
            default:
                return blocks.dandelion;
        }
    }

    if (moisture > 0.70 && (variant & 15U) == 0U) {
        return (variant & 16U) == 0U ? blocks.brownMushroom : blocks.redMushroom;
    }
    if (moisture > 0.62 && (variant & 3U) == 0U) {
        return blocks.fern;
    }
    return (variant & 1U) == 0U ? blocks.shortGrass : blocks.tallGrass;
}

} // namespace

void TerrainGenerator::init(uint32_t seed, int seaLevel) {
    (void)worldGenBlocks();
    m_seed = seed;
    m_seaLevel = std::clamp(seaLevel, 16, Chunk::SIZE_Y - 32);
}

BlockID TerrainGenerator::sampleBlock(const int worldX, const int y, const int worldZ) const {
    if (y < 0 || y >= Chunk::SIZE_Y) {
        return 0;
    }

    const WorldGenBlocks& blocks = worldGenBlocks();
    const TerrainColumnSample column = sampleTerrainColumn(worldX, worldZ, m_seed, m_seaLevel, blocks);

    BlockID id = 0;
    if (y <= column.surfaceY) {
        id = sampleTerrainSolidBlock(worldX, y, worldZ, m_seed, column, blocks);

        if (isCaveCarvableBlock(id, blocks) && shouldCarveCave(worldX, y, worldZ, column.surfaceY)) {
            id = 0;
        }

        if (id == blocks.stone || id == blocks.deepslate) {
            id = sampleOreBlock(worldX, y, worldZ, id);
        }
    } else if (y <= m_seaLevel) {
        id = naturalWaterState();
    }

    if (id == 0) {
        const BlockID treeBlock = sampleTreeBlock(worldX, y, worldZ, m_seed, m_seaLevel, blocks);
        if (treeBlock != 0) {
            return treeBlock;
        }
    }

    const int vegetationY = column.surfaceY + 1;
    if (id == 0 &&
        y == vegetationY) {
        id = sampleVegetationBlock(worldX, worldZ, m_seed,
                                   column.biome, column.moisture,
                                   m_seaLevel, column.surfaceY,
                                   column.surface.topBlock, blocks);
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
    const WorldGenBlocks& blocks = worldGenBlocks();
    uint32_t oreColumnSeed = m_seed;
    oreColumnSeed ^= hash32(static_cast<uint32_t>(worldX) * kOreXMul);
    oreColumnSeed ^= hash32(static_cast<uint32_t>(worldZ) * kOreZMul);
    return sampleOreBlockFromHash(y, baseBlock, oreColumnSeed, blocks);
}

void TerrainGenerator::generateChunk(Chunk& chunk) const {
    const glm::ivec3 offset = chunk.getWorldOffset();
    const WorldGenBlocks& blocks = worldGenBlocks();
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

#if defined(MECRAFT_HAS_AVX2)
            if (remaining >= 4) {
                laneCount = 4;
                sampleSurfaceAndMoisture4(offset.x + x, offset.x + x + 1, offset.x + x + 2, offset.x + x + 3,
                                          offset.z + z, m_seed, m_seaLevel,
                                          sampledSurface, sampledMoisture,
                                          sampledSurfaceKind, sampledRuggedness);
            } else
#endif
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
                TerrainColumnSample column;
                column.surfaceY = surfaceY;
                column.moisture = moisture;
                column.ruggedness = ruggedness;
                column.biome = surfaceKind;
                column.surface = sampleSurfaceProfile(worldX, worldZ, m_seed, m_seaLevel,
                                                      surfaceY, moisture, surfaceKind,
                                                      ruggedness, blocks);

                const int columnTop = std::max(surfaceY, m_seaLevel);
                if (surfaceY - 5 >= 10) {
                    buildCaveMaskColumn(worldX, worldZ, surfaceY, m_seed, caveMask);
                }
                int highestOpaqueY = 0;
                for (int y = 0; y <= columnTop; ++y) {
                    BlockID id = 0;
                    if (y <= surfaceY) {
                        id = sampleTerrainSolidBlock(worldX, y, worldZ, m_seed, column, blocks);

                        if (isCaveCarvableBlock(id, blocks) && y >= 10 && y <= surfaceY - 5 && caveMask[y] != 0) {
                            id = 0;
                        }

                        if (id == blocks.stone || id == blocks.deepslate) {
                            id = sampleOreBlockFromHash(y, id, oreColumnSeed, blocks);
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

                    if (blockAbove == 0) {
                        const BlockID vegetation = sampleVegetationBlock(worldX, worldZ, m_seed,
                                                                         surfaceKind, moisture,
                                                                         m_seaLevel, surfaceY,
                                                                         surfaceBlock, blocks);
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
            const TreeCandidate tree = sampleTreeCandidate(anchorX, anchorZ, m_seed, m_seaLevel, blocks);
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
                        const std::size_t index = SubChunk::toIndex(localX, Chunk::toSubChunkLocalY(y), localZ);
                        const BlockID current = generatedBlocks[scy][index];
                        const bool canReplace = treeBlock == tree.log
                                                    ? canTreeLogReplace(current, blocks)
                                                    : canTreeLeavesReplace(current, blocks);
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

