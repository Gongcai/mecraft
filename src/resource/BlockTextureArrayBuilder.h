#ifndef MECRAFT_BLOCK_TEXTURE_ARRAY_BUILDER_H
#define MECRAFT_BLOCK_TEXTURE_ARRAY_BUILDER_H

#include "BlockTextureCatalog.h"
#include "TextureAtlas.h"

#include <string>
#include <unordered_map>

namespace resource {

struct BlockTextureArray {
    TextureArray textureArray;
    std::unordered_map<std::string, int> layers;
    std::unordered_map<int, int> layerToAtlasTile;
};

[[nodiscard]] BlockTextureArray buildBlockTextureArray(const std::string& directory,
                                                       int tileSize,
                                                       BlockTextureCatalog& catalog);

} // namespace resource

#endif // MECRAFT_BLOCK_TEXTURE_ARRAY_BUILDER_H
