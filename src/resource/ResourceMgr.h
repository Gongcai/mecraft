//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_RESOURCEMGR_H
#define MECRAFT_RESOURCEMGR_H
#include <memory>
#include <string>
#include <vector>
#include <glad/glad.h>
#include "BlockTextureCatalog.h"
#include "BlockTextureColorProvider.h"
#include "TextureAtlas.h"
#include "../renderer/core/Shader.h"

class ResourceMgr : public IBlockTextureColorProvider {
public:
    ResourceMgr();
    ~ResourceMgr() override;

    ResourceMgr(const ResourceMgr&) = delete;
    ResourceMgr& operator=(const ResourceMgr&) = delete;
    ResourceMgr(ResourceMgr&&) noexcept;
    ResourceMgr& operator=(ResourceMgr&&) noexcept;

    void init();
    void shutdown();

    Shader *loadShader(const std::string &name,
                       const char *vertPath,
                       const char *fragPath);

    Shader *getShader(const std::string &name);

    GLuint loadTexture2D(const std::string& name,
                         const std::string& path,
                         bool srgb = false,
                         bool repeat = false,
                         bool linear = true,
                         bool flipVertically = false);
    [[nodiscard]] GLuint getTexture2D(const std::string& name) const;

    // Standalone named textures (non-atlas), e.g. GUI sheets.
    GLuint loadGuiTexture(const std::string& name, const std::string& path, bool flipVertically = true);
    GLuint loadGuiTexture(const std::string& name, const std::string& path, int& outWidth, int& outHeight, bool flipVertically = true);
    [[nodiscard]] GLuint getGuiTexture(const std::string& name) const;

    // Texture atlas used by block-facing UI rendering.
    void buildBlockTextureResources(const std::string& directory, int tileSize = 16);
    void buildTextureAtlas(const std::string& directory, int tileSize = 16);
    [[nodiscard]] const TextureAtlas& getAtlas() const;

    // Texture array used by block rendering.
    void buildTextureArray(const std::string& directory, int tileSize = 16);
    [[nodiscard]] const TextureArray& getTextureArray() const;
    [[nodiscard]] const TextureArray& getBlockNormalTextureArray() const;
    [[nodiscard]] const TextureArray& getBlockSpecularTextureArray() const;
    [[nodiscard]] bool hasBlockNormalMaps() const;
    [[nodiscard]] bool hasBlockSpecularMaps() const;
    [[nodiscard]] bool loadBlockTextureCatalog(const std::string& textureConfigPath);
    [[nodiscard]] int getBlockTextureTileSize() const;
    [[nodiscard]] bool preloadEntityTexturesFromConfig(const std::string& entitiesConfigPath);
    [[nodiscard]] int getTextureArrayLayer(const std::string& name) const;
    [[nodiscard]] TextureAnimationInfo getTextureAnimation(const std::string& name) const;
    [[nodiscard]] ResourceTextureTint getTextureTint(const std::string& name) const;
    [[nodiscard]] const glm::vec3& blockTextureAverageColor(int arrayLayer) const override;

    // Mapping from TextureArray layer index back to Atlas tile index.
    // Used by UI icon generation (buildBlockIconAtlas) to convert face layers to atlas tiles.
    [[nodiscard]] int arrayLayerToAtlasTile(int arrayLayer) const;

    // Lightmap textures (16x16, maps blockLight x skyLight -> RGB brightness)
    void loadLightmapTextures(const std::string& dayPath, const std::string& nightPath);
    [[nodiscard]] GLuint getLightmapDay() const;
    [[nodiscard]] GLuint getLightmapNight() const;

    void loadColormapTextures(const std::string& grassPath, const std::string& foliagePath);
    [[nodiscard]] GLuint getGrassColormap() const;
    [[nodiscard]] GLuint getFoliageColormap() const;

    // Cubemap texture (6 face textures for skybox rendering)
    GLuint loadCubemap(const std::string& name,
                       const std::string& rightPath, const std::string& leftPath,
                       const std::string& topPath, const std::string& bottomPath,
                       const std::string& frontPath, const std::string& backPath);
    [[nodiscard]] GLuint getCubemap(const std::string& name) const;

    // Prebaked block item icons (isometric-like) packed in a single atlas texture.
    void buildBlockIconAtlas(int iconSize = 64);
    [[nodiscard]] const TextureAtlas& getItemIconAtlas() const;

    // Item textures packed from assets/textures/items for UI icons and 3D item models.
    void buildItemTextureAtlas(const std::string& directory, int tileSize = 16);
    [[nodiscard]] const TextureAtlas& getItemTextureAtlas() const;
    [[nodiscard]] int getItemTextureIndex(const std::string& textureName) const;
    [[nodiscard]] const std::vector<unsigned char>& getItemTexturePixels() const;

    // HUD icon atlas (hearts, armor, food, air) packed from assets/textures/gui/hud.
    void buildHudIconAtlas(const std::string& directory, int iconSize = 8);
    [[nodiscard]] const TextureAtlas& getHudIconAtlas() const;
    [[nodiscard]] int getHudIconIndex(const std::string& iconName) const;

    // Atlas sampler controls (for world block atlas).
    void setAtlasAnisotropy(float anisotropy);
    [[nodiscard]] float getAtlasAnisotropy() const;
    [[nodiscard]] float getAtlasMaxAnisotropy() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};



#endif //MECRAFT_RESOURCEMGR_H
