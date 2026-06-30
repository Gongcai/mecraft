#ifndef MECRAFT_BLOCK_TEXTURE_LIBRARY_H
#define MECRAFT_BLOCK_TEXTURE_LIBRARY_H

#include "BlockTextureCatalog.h"
#include "TextureAtlas.h"
#include "TextureSamplerController.h"

#include <string>
#include <unordered_map>
#include <vector>

class BlockTextureLibrary {
public:
    BlockTextureLibrary() = default;
    BlockTextureLibrary(const BlockTextureLibrary&) = delete;
    BlockTextureLibrary& operator=(const BlockTextureLibrary&) = delete;

    void shutdown();

    void loadCatalog(const std::string& textureConfigPath);
    void buildAtlas(const std::string& directory, int tileSize);
    void buildTextureArray(const std::string& directory, int tileSize);

    [[nodiscard]] const TextureAtlas& atlas() const;
    [[nodiscard]] const TextureArray& textureArray() const;
    [[nodiscard]] const std::vector<unsigned char>& atlasPixels() const;
    [[nodiscard]] const BlockTextureCatalog& catalog() const;

    [[nodiscard]] int textureArrayLayer(const std::string& name) const;
    [[nodiscard]] TextureAnimationInfo textureAnimation(const std::string& name) const;
    [[nodiscard]] ResourceTextureTint textureTint(const std::string& name) const;
    [[nodiscard]] int arrayLayerToAtlasTile(int arrayLayer) const;

    void setAnisotropy(float anisotropy);
    [[nodiscard]] float anisotropy() const;
    [[nodiscard]] float maxAnisotropy() const;

private:
    TextureAtlas m_atlas;
    TextureArray m_textureArray;
    TextureSamplerController m_sampler;
    std::vector<unsigned char> m_atlasPixels;
    std::unordered_map<std::string, int> m_textureArrayLayers;
    BlockTextureCatalog m_catalog;
    std::unordered_map<int, int> m_arrayLayerToAtlasTile;
};

#endif // MECRAFT_BLOCK_TEXTURE_LIBRARY_H
