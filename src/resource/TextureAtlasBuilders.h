#ifndef MECRAFT_TEXTURE_ATLAS_BUILDERS_H
#define MECRAFT_TEXTURE_ATLAS_BUILDERS_H

#include "BlockTextureCatalog.h"
#include "BlockTextureManifest.h"
#include "TextureAtlas.h"

#include <string>
#include <unordered_map>
#include <vector>

class RhiDevice;
class RhiCommandListPool;

namespace resource {

struct IndexedTextureAtlas {
    TextureAtlas atlas;
    std::vector<unsigned char> pixels;
    std::unordered_map<std::string, int> indices;
};

[[nodiscard]] IndexedTextureAtlas buildItemTextureAtlas(const std::string& directory, int tileSize,
                                                        const BlockTextureCatalog& catalog, RhiDevice& rhiDevice,
                                                        RhiCommandListPool& commandListPool);

[[nodiscard]] IndexedTextureAtlas buildBlockTextureAtlas(const std::string& directory, int tileSize,
                                                         const BlockTextureCatalog& catalog, RhiDevice& rhiDevice,
                                                         RhiCommandListPool& commandListPool);

[[nodiscard]] IndexedTextureAtlas buildBlockTextureAtlas(const BlockTextureManifest& manifest, int tileSize,
                                                         const BlockTextureCatalog& catalog, RhiDevice& rhiDevice,
                                                         RhiCommandListPool& commandListPool);

[[nodiscard]] IndexedTextureAtlas buildHudIconAtlas(const std::string& directory, int iconSize, RhiDevice& rhiDevice,
                                                    RhiCommandListPool& commandListPool);

} // namespace resource

#endif // MECRAFT_TEXTURE_ATLAS_BUILDERS_H
