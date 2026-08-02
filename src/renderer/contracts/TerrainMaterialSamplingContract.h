#ifndef MECRAFT_TERRAIN_MATERIAL_SAMPLING_CONTRACT_H
#define MECRAFT_TERRAIN_MATERIAL_SAMPLING_CONTRACT_H

#include <cstdint>
#include <optional>

namespace renderer::contracts {

inline constexpr float kTerrainAlphaCutoff = 0.1f;
inline constexpr uint32_t kTerrainTextureLayerCount = 1024u;
inline constexpr uint32_t kTerrainAnimationFrameCountMax = 63u;
inline constexpr uint32_t kTerrainAnimationFramesPerSecondMax = 63u;

/// Describes one validated block-texture animation stored by packed terrain vertices.
struct TerrainTextureAnimation final {
    uint32_t firstLayer = 0u;
    uint32_t frameCount = 1u;
    uint32_t framesPerSecond = 0u;
    bool animated = false;
};

/// Applies the fixed terrain opacity threshold shared by raster and ray-query paths.
/// @param opacity Sampled block-texture alpha in the linear [0, 1] domain.
/// @return True when opacity is finite and greater than or equal to the fixed cutoff.
[[nodiscard]] bool terrainAlphaTestPasses(float opacity);

/// Resolves the current array layer for one validated packed terrain animation.
/// @param animation First layer, frame count, integer playback rate, and animation flag.
/// @param animationTimeSeconds Non-negative finite deterministic render time in seconds.
/// @return Current array layer, or no value when metadata or time violates the packed contract.
[[nodiscard]] std::optional<uint32_t> terrainAnimatedTextureLayer(const TerrainTextureAnimation& animation,
                                                                  float animationTimeSeconds);

} // namespace renderer::contracts

#endif // MECRAFT_TERRAIN_MATERIAL_SAMPLING_CONTRACT_H
