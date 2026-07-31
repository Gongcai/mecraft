#ifndef MECRAFT_UI_TEXTURE_ATLAS_LIBRARY_H
#define MECRAFT_UI_TEXTURE_ATLAS_LIBRARY_H

#include "BlockTextureLibrary.h"
#include "TextureAtlas.h"

#include <string>
#include <unordered_map>
#include <vector>

class RhiDevice;
class RhiCommandListPool;

class UiTextureAtlasLibrary {
public:
    UiTextureAtlasLibrary() = default;
    UiTextureAtlasLibrary(const UiTextureAtlasLibrary&) = delete;
    UiTextureAtlasLibrary& operator=(const UiTextureAtlasLibrary&) = delete;

    void init(RhiDevice& rhiDevice, RhiCommandListPool& commandListPool);
    void shutdown();

    void buildBlockIconAtlas(int iconSize, const BlockTextureLibrary& blockTextures);
    [[nodiscard]] const TextureAtlas& blockIconAtlas() const;

    void buildItemTextureAtlas(const std::string& directory, int tileSize, const BlockTextureCatalog& catalog);
    [[nodiscard]] const TextureAtlas& itemTextureAtlas() const;
    [[nodiscard]] int itemTextureIndex(const std::string& textureName) const;
    [[nodiscard]] const std::vector<unsigned char>& itemTexturePixels() const;

    void buildHudIconAtlas(const std::string& directory, int iconSize);
    [[nodiscard]] const TextureAtlas& hudIconAtlas() const;
    [[nodiscard]] int hudIconIndex(const std::string& iconName) const;

private:
    void deleteTextureAtlas(TextureAtlas& atlas);

    RhiDevice* m_rhiDevice = nullptr;
    RhiCommandListPool* m_commandListPool = nullptr;
    TextureAtlas m_blockIconAtlas;
    TextureAtlas m_itemTextureAtlas;
    TextureAtlas m_hudIconAtlas;
    std::vector<unsigned char> m_itemTexturePixels;
    std::unordered_map<std::string, int> m_itemTextureIndices;
    std::unordered_map<std::string, int> m_hudIconIndices;
};

#endif // MECRAFT_UI_TEXTURE_ATLAS_LIBRARY_H
