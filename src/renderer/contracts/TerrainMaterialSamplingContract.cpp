#include "renderer/contracts/TerrainMaterialSamplingContract.h"

#include <cmath>

namespace renderer::contracts {

bool terrainAlphaTestPasses(const float opacity) {
    return std::isfinite(opacity) && opacity >= kTerrainAlphaCutoff;
}

std::optional<uint32_t> terrainAnimatedTextureLayer(const TerrainTextureAnimation& animation,
                                                    const float animationTimeSeconds) {
    if (animation.firstLayer >= kTerrainTextureLayerCount || animation.frameCount == 0u ||
        animation.frameCount > kTerrainAnimationFrameCountMax ||
        animation.framesPerSecond > kTerrainAnimationFramesPerSecondMax ||
        animation.frameCount > kTerrainTextureLayerCount - animation.firstLayer ||
        !std::isfinite(animationTimeSeconds) || animationTimeSeconds < 0.0f) {
        return std::nullopt;
    }
    if (!animation.animated) {
        return animation.firstLayer;
    }
    if (animation.frameCount <= 1u || animation.framesPerSecond == 0u) {
        return std::nullopt;
    }

    const float frameSequence = std::floor(animationTimeSeconds * static_cast<float>(animation.framesPerSecond));
    if (!std::isfinite(frameSequence)) {
        return std::nullopt;
    }
    const float frame = std::fmod(frameSequence, static_cast<float>(animation.frameCount));
    return animation.firstLayer + static_cast<uint32_t>(frame);
}

} // namespace renderer::contracts
