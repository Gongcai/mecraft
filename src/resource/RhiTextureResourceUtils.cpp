#include "RhiTextureResourceUtils.h"

#include "renderer/rhi/gl/GlRhiTextureRegistry.h"

namespace resource {

bool registerTextureAtlas(TextureAtlas& atlas) {
    if (atlas.textureID == 0 || atlas.atlasWidth <= 0 || atlas.atlasHeight <= 0) {
        return false;
    }

    atlas.texture = renderer::rhi::gl::registerTexture({
        atlas.textureID,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba8Unorm,
        static_cast<uint32_t>(atlas.atlasWidth),
        static_cast<uint32_t>(atlas.atlasHeight),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled),
        false
    });
    return atlas.texture.isValid();
}

void unregisterTextureAtlas(TextureAtlas& atlas) {
    renderer::rhi::gl::unregisterTextureAndReset(atlas.texture);
}

bool registerTextureArray(TextureArray& textureArray) {
    if (textureArray.textureID == 0 || textureArray.tileSize <= 0 || textureArray.layerCount <= 0) {
        return false;
    }

    textureArray.texture = renderer::rhi::gl::registerTexture({
        textureArray.textureID,
        RhiTextureDimension::Texture2DArray,
        RhiTextureFormat::Rgba8Unorm,
        static_cast<uint32_t>(textureArray.tileSize),
        static_cast<uint32_t>(textureArray.tileSize),
        static_cast<uint32_t>(textureArray.layerCount),
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled),
        false
    });
    return textureArray.texture.isValid();
}

void unregisterTextureArray(TextureArray& textureArray) {
    renderer::rhi::gl::unregisterTextureAndReset(textureArray.texture);
}

} // namespace resource
