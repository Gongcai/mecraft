#include <cstdlib>
#include <iostream>

#include "../src/world/chunk/Chunk.h"

namespace {
[[noreturn]] void fail(const char* message) {
    std::cerr << "[chunk_initial_lightmap_test] FAIL: " << message << '\n';
    std::cerr.flush();
    std::_Exit(EXIT_FAILURE);
}

BlockStateId stateForBlockName(const char* name) {
    return BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName(name));
}

void testWaterColumnsArePreseeded() {
    Chunk chunk(0, 0);

    for (int y = 70; y <= 72; ++y) {
        chunk.setBlockFast(3, y, 3, stateForBlockName("minecraft:water"));
    }

    chunk.seedInitialLightMap();

    if (chunk.getSunlight(3, 72, 3) != 13) {
        fail("top water voxel should start with attenuated sunlight");
    }
    if (chunk.getSunlight(3, 71, 3) != 11) {
        fail("middle water voxel should keep vertical attenuation");
    }
    if (chunk.getSunlight(3, 70, 3) != 9) {
        fail("lower water voxel should keep vertical attenuation");
    }
}

void testLightSourcesSeedOwnVoxel() {
    Chunk chunk(0, 0);
    chunk.setBlockFast(4, 80, 4, stateForBlockName("minecraft:torch"));

    chunk.seedInitialLightMap();

    if (chunk.getBlockLight(4, 80, 4) == 0) {
        fail("light source voxel should seed block light immediately");
    }
}

void testGlowLichenSeedsPropagatedBlockLight() {
    Chunk chunk(0, 0);
    chunk.setBlockFast(4, 80, 4, stateForBlockName("minecraft:glow_lichen"));

    chunk.seedInitialLightMap();

    if (chunk.getBlockLight(4, 80, 4) != 7) {
        fail("glow lichen should seed its configured block-light level");
    }
}
} // namespace

int main() {
    BlockRegistry::init(nullptr);

    testWaterColumnsArePreseeded();
    testLightSourcesSeedOwnVoxel();
    testGlowLichenSeedsPropagatedBlockLight();

    std::cout << "[chunk_initial_lightmap_test] PASS\n";
    return EXIT_SUCCESS;
}
