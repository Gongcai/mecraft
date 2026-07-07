#ifndef MECRAFT_RHI_TEXTURE_RESOURCE_UTILS_H
#define MECRAFT_RHI_TEXTURE_RESOURCE_UTILS_H

#include "TextureAtlas.h"

#include <cstdint>

namespace resource {

[[nodiscard]] bool registerTextureAtlas(TextureAtlas& atlas, uint32_t textureId);
void unregisterTextureAtlas(TextureAtlas& atlas);

[[nodiscard]] bool registerTextureArray(TextureArray& textureArray, uint32_t textureId);
void unregisterTextureArray(TextureArray& textureArray);

} // namespace resource

#endif // MECRAFT_RHI_TEXTURE_RESOURCE_UTILS_H
