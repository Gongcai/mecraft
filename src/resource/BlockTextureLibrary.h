#ifndef MECRAFT_BLOCK_TEXTURE_LIBRARY_H
#define MECRAFT_BLOCK_TEXTURE_LIBRARY_H

#include "BlockTextureCatalog.h"
#include "BlockTextureManifest.h"
#include "TextureAtlas.h"
#include "TextureSamplerController.h"
#include "renderer/contracts/TerrainOpacityMicromapContract.h"

#include <glm/vec3.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class RhiDevice;
class RhiCommandListPool;

class BlockTextureLibrary {
public:
    BlockTextureLibrary() = default;
    BlockTextureLibrary(const BlockTextureLibrary&) = delete;
    BlockTextureLibrary& operator=(const BlockTextureLibrary&) = delete;

    void init(RhiDevice& rhiDevice, RhiCommandListPool& commandListPool);
    void shutdown();

    [[nodiscard]] bool loadCatalog(const std::string& textureConfigPath);
    [[nodiscard]] bool loadCatalog(const std::string& textureConfigPath, const std::string& packConfigPath);
    void buildTextures(const std::string& directory, int tileSize);
    void buildTextures(const std::string& directory, int tileSize,
                       const std::unordered_set<std::string>& registeredTextureNames);
    void buildAtlas(const std::string& directory, int tileSize);
    void buildTextureArray(const std::string& directory, int tileSize);

    [[nodiscard]] const TextureAtlas& atlas() const;
    [[nodiscard]] const TextureArray& textureArray() const;
    [[nodiscard]] const TextureArray& normalTextureArray() const;
    [[nodiscard]] const TextureArray& specularTextureArray() const;
    [[nodiscard]] bool hasNormalMaps() const;
    [[nodiscard]] bool hasSpecularMaps() const;
    [[nodiscard]] const std::vector<unsigned char>& atlasPixels() const;
    /// Returns the exact RGBA8 level-zero pixels uploaded to the block albedo texture array.
    [[nodiscard]] const std::vector<uint8_t>& textureArrayPixels() const;
    /// Returns a stable view of the normalized albedo array used by terrain OMM classification.
    [[nodiscard]] renderer::contracts::TerrainOpacityMicromapSource terrainOpacityMicromapSource() const;
    [[nodiscard]] const BlockTextureCatalog& catalog() const;
    [[nodiscard]] int tileSize() const;
    [[nodiscard]] const glm::vec3& textureAverageColor(int arrayLayer) const;

    [[nodiscard]] int textureArrayLayer(const std::string& name) const;
    [[nodiscard]] TextureAnimationInfo textureAnimation(const std::string& name) const;
    [[nodiscard]] ResourceTextureTint textureTint(const std::string& name) const;
    [[nodiscard]] int arrayLayerToAtlasTile(int arrayLayer) const;

    void setAnisotropy(float anisotropy);
    [[nodiscard]] float anisotropy() const;
    [[nodiscard]] float maxAnisotropy() const;

private:
    void deleteTextureAtlas(TextureAtlas& atlas);
    void deleteTextureArray(TextureArray& textureArray);

    TextureAtlas m_atlas;
    TextureArray m_textureArray;
    TextureArray m_normalTextureArray;
    TextureArray m_specularTextureArray;
    TextureSamplerController m_sampler;
    std::vector<unsigned char> m_atlasPixels;
    std::vector<uint8_t> m_textureArrayPixels;
    uint64_t m_textureArrayAlphaHash = 0u;
    std::unordered_map<std::string, int> m_textureArrayLayers;
    std::vector<glm::vec3> m_textureAverageColors;
    BlockTextureCatalog m_catalog;
    resource::BlockTextureManifest m_manifest;
    std::unordered_map<int, int> m_arrayLayerToAtlasTile;
    bool m_hasNormalMaps = false;
    bool m_hasSpecularMaps = false;
    RhiDevice* m_rhiDevice = nullptr;
    RhiCommandListPool* m_commandListPool = nullptr;
};

#endif // MECRAFT_BLOCK_TEXTURE_LIBRARY_H
