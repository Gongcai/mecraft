#ifndef MECRAFT_BLOCK_TEXTURE_ARRAY_BUILDER_H
#define MECRAFT_BLOCK_TEXTURE_ARRAY_BUILDER_H

#include "BlockTextureCatalog.h"
#include "BlockTextureManifest.h"
#include "TextureAtlas.h"

#include <string>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <glm/vec3.hpp>

class RhiDevice;
class RhiCommandListPool;

namespace resource {

struct BlockTextureArraySet {
    TextureArray albedoArray;
    TextureArray normalArray;
    TextureArray specularArray;
    std::unordered_map<std::string, int> layers;
    std::unordered_map<int, int> layerToAtlasTile;
    std::vector<glm::vec3> layerAverageColors;
    std::vector<uint8_t> albedoPixels;
    bool hasNormalMaps = false;
    bool hasSpecularMaps = false;
};

[[nodiscard]] BlockTextureArraySet buildBlockTextureArraySet(const std::string& directory, int tileSize,
                                                             BlockTextureCatalog& catalog, RhiDevice& rhiDevice,
                                                             RhiCommandListPool& commandListPool);

[[nodiscard]] BlockTextureArraySet buildBlockTextureArraySet(const BlockTextureManifest& manifest, int tileSize,
                                                             BlockTextureCatalog& catalog, RhiDevice& rhiDevice,
                                                             RhiCommandListPool& commandListPool);

} // namespace resource

#endif // MECRAFT_BLOCK_TEXTURE_ARRAY_BUILDER_H
