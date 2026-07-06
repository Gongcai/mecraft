#ifndef MECRAFT_RHI_TEXTURE_RESOURCE_UTILS_H
#define MECRAFT_RHI_TEXTURE_RESOURCE_UTILS_H

#include "TextureAtlas.h"

namespace resource {

[[nodiscard]] bool registerTextureAtlas(TextureAtlas& atlas);
void unregisterTextureAtlas(TextureAtlas& atlas);

[[nodiscard]] bool registerTextureArray(TextureArray& textureArray);
void unregisterTextureArray(TextureArray& textureArray);

} // namespace resource

#endif // MECRAFT_RHI_TEXTURE_RESOURCE_UTILS_H
