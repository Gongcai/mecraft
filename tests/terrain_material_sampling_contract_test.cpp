#include "renderer/contracts/TerrainMaterialSamplingContract.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>

namespace {
[[nodiscard]] bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] bool validateShaderMirror() {
    const std::string path = std::string(MECRAFT_TEST_SOURCE_DIR) + "/assets/shaders/terrain_material_sampling.glsl";
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    const std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return source.find("const float TERRAIN_ALPHA_CUTOFF = 0.1;") != std::string::npos &&
           source.find("const uint TERRAIN_MATERIAL_TEXTURE_LAYER_CAPACITY = 1024u;") != std::string::npos &&
           source.find("bool terrainAlphaTestPasses(float opacity)") != std::string::npos &&
           source.find("return !isnan(opacity) && !isinf(opacity) && opacity >= TERRAIN_ALPHA_CUTOFF;") !=
               std::string::npos &&
           source.find("float terrainAnimatedTextureLayer(") != std::string::npos &&
           source.find("float frame = mod(floor(animationTimeSeconds * framesPerSecond), frameCount);") !=
               std::string::npos &&
           source.find("return firstLayer + (animated > 0.5 ? frame : 0.0);") != std::string::npos;
}
} // namespace

int main() {
    using namespace renderer::contracts;

    bool valid = true;
    valid = requireTrue(!terrainAlphaTestPasses(std::nextafter(kTerrainAlphaCutoff, 0.0f)) &&
                            terrainAlphaTestPasses(kTerrainAlphaCutoff) &&
                            terrainAlphaTestPasses(std::nextafter(kTerrainAlphaCutoff, 1.0f)) &&
                            !terrainAlphaTestPasses(std::numeric_limits<float>::quiet_NaN()) &&
                            !terrainAlphaTestPasses(std::numeric_limits<float>::infinity()) &&
                            !terrainAlphaTestPasses(-std::numeric_limits<float>::infinity()),
                        "Terrain alpha testing must preserve its finite inclusive cutoff") &&
            valid;

    const TerrainTextureAnimation staticTexture{17u, 4u, 0u, false};
    const std::optional<uint32_t> staticLayer = terrainAnimatedTextureLayer(staticTexture, 42.0f);
    valid = requireTrue(staticLayer.has_value() && *staticLayer == 17u,
                        "Static terrain textures must retain their first array layer") &&
            valid;

    const TerrainTextureAnimation animation{100u, 3u, 4u, true};
    valid = requireTrue(terrainAnimatedTextureLayer(animation, 0.0f) == std::optional<uint32_t>(100u) &&
                            terrainAnimatedTextureLayer(animation, 0.25f) == std::optional<uint32_t>(101u) &&
                            terrainAnimatedTextureLayer(animation, 0.50f) == std::optional<uint32_t>(102u) &&
                            terrainAnimatedTextureLayer(animation, 0.75f) == std::optional<uint32_t>(100u),
                        "Terrain texture animation must advance and wrap deterministically") &&
            valid;

    const TerrainTextureAnimation maximumAnimation{kTerrainTextureLayerCount - kTerrainAnimationFrameCountMax,
                                                   kTerrainAnimationFrameCountMax, kTerrainAnimationFramesPerSecondMax,
                                                   true};
    valid = requireTrue(terrainAnimatedTextureLayer(maximumAnimation, 0.0f) ==
                            std::optional<uint32_t>(kTerrainTextureLayerCount - kTerrainAnimationFrameCountMax),
                        "Terrain animation must accept the complete packed metadata range") &&
            valid;

    valid =
        requireTrue(
            !terrainAnimatedTextureLayer({kTerrainTextureLayerCount, 1u, 0u, false}, 0.0f).has_value() &&
                !terrainAnimatedTextureLayer({1023u, 2u, 4u, true}, 0.0f).has_value() &&
                !terrainAnimatedTextureLayer({0u, kTerrainAnimationFrameCountMax + 1u, 4u, true}, 0.0f).has_value() &&
                !terrainAnimatedTextureLayer({0u, 2u, kTerrainAnimationFramesPerSecondMax + 1u, true}, 0.0f)
                     .has_value() &&
                !terrainAnimatedTextureLayer({0u, 1u, 4u, true}, 0.0f).has_value() &&
                !terrainAnimatedTextureLayer({0u, 2u, 0u, true}, 0.0f).has_value() &&
                !terrainAnimatedTextureLayer(animation, -0.1f).has_value() &&
                !terrainAnimatedTextureLayer(animation, std::numeric_limits<float>::infinity()).has_value() &&
                !terrainAnimatedTextureLayer(animation, std::numeric_limits<float>::max()).has_value(),
            "Terrain animation must reject invalid packed metadata and render time") &&
        valid;
    valid = requireTrue(validateShaderMirror(), "Terrain GLSL sampling contract must mirror the CPU boundary") && valid;
    return valid ? 0 : 1;
}
