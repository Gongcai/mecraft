#ifndef MECRAFT_TEXTURE_ATLAS_BUILDERS_H
#define MECRAFT_TEXTURE_ATLAS_BUILDERS_H

#include "BlockTextureCatalog.h"
#include "BlockTextureManifest.h"
#include "TextureAtlas.h"

#include <string>
#include <unordered_map>
#include <vector>

class RhiDevice;

namespace resource {

struct IndexedTextureAtlas {
    TextureAtlas atlas;
    std::vector<unsigned char> pixels;
    std::unordered_map<std::string, int> indices;
};

[[nodiscard]] IndexedTextureAtlas buildItemTextureAtlas(const std::string& directory,
                                                        int tileSize,
                                                        const BlockTextureCatalog& catalog,
                                                        RhiDevice& rhiDevice);

[[nodiscard]] IndexedTextureAtlas buildBlockTextureAtlas(const std::string& directory,
                                                         int tileSize,
                                                         const BlockTextureCatalog& catalog,
                                                         RhiDevice& rhiDevice);

[[nodiscard]] IndexedTextureAtlas buildBlockTextureAtlas(const BlockTextureManifest& manifest,
                                                         int tileSize,
                                                         const BlockTextureCatalog& catalog,
                                                         RhiDevice& rhiDevice);

[[nodiscard]] IndexedTextureAtlas buildHudIconAtlas(const std::string& directory,
                                                    int iconSize,
                                                    RhiDevice& rhiDevice);

} // namespace resource

#endif // MECRAFT_TEXTURE_ATLAS_BUILDERS_H
