#ifndef MECRAFT_TEXTURE_ATLAS_BUILDERS_H
#define MECRAFT_TEXTURE_ATLAS_BUILDERS_H

#include "BlockTextureCatalog.h"
#include "TextureAtlas.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace resource {

struct IndexedTextureAtlas {
    TextureAtlas atlas;
    std::vector<unsigned char> pixels;
    std::unordered_map<std::string, int> indices;
};

[[nodiscard]] IndexedTextureAtlas buildItemTextureAtlas(const std::string& directory,
                                                        int tileSize,
                                                        const BlockTextureCatalog& catalog);

[[nodiscard]] IndexedTextureAtlas buildBlockTextureAtlas(const std::string& directory,
                                                         int tileSize,
                                                         const BlockTextureCatalog& catalog);

[[nodiscard]] IndexedTextureAtlas buildHudIconAtlas(const std::string& directory,
                                                    int iconSize);

} // namespace resource

#endif // MECRAFT_TEXTURE_ATLAS_BUILDERS_H
