//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_RESOURCEMGR_H
#define MECRAFT_RESOURCEMGR_H
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <utility>
#include "ShaderLibrary.h"
struct TextureAtlas {
    GLuint textureID = 0;
    int atlasWidth  = 0;     // Atlas width in pixels.
    int atlasHeight = 0;     // Atlas height in pixels.
    int tileSize    = 16;    // Tile size in pixels, excluding padding.
    int tileStride  = 16;    // Tile cell stride: tileSize + 2 * tilePadding.
    int tilePadding = 0;     // Copied edge texel padding around each tile.
    int tilesPerRow = 0;

    // Return UV coordinates for the tile as lower-left and upper-right corners.
    [[nodiscard]] std::pair<glm::vec2, glm::vec2> getUV(int tileIndex) const;
};

struct TextureArray {
    GLuint textureID = 0;
    int tileSize = 16;
    int layerCount = 0;
};

// Metadata for cached GUI textures: store GL id plus original image size.
struct GuiTextureInfo {
    GLuint textureID = 0;
    int width = 0;
    int height = 0;
};

struct TextureAnimationInfo {
    int firstLayer = 0;
    int frameCount = 1;
    float fps = 0.0f;
    bool isAnimated = false;
};

enum class ResourceTextureTint : uint8_t {
    None = 0,
    Grass = 1,
    Foliage = 2,
};

struct BlockTextureCatalogEntry {
    TextureAnimationInfo animation;
    ResourceTextureTint tint = ResourceTextureTint::None;
    bool verticalFrames = false;
    bool topFrameFirst = true;
};

class ResourceMgr {
public:
    void init();
    void shutdown();

    Shader *loadShader(const std::string &name,
                       const char *vertPath,
                       const char *fragPath);

    Shader *getShader(const std::string &name);

    GLuint loadTexture(const std::string& path, bool alpha = false);
    GLuint loadTexture2D(const std::string& name,
                         const std::string& path,
                         bool srgb = false,
                         bool repeat = false,
                         bool linear = true,
                         bool flipVertically = false);
    bool probeAtmosphereLut(const std::string& name,
                            const std::string& path,
                            size_t expectedBytes = 0) const;
    [[nodiscard]] GLuint getTexture2D(const std::string& name) const;
    [[nodiscard]] GLuint getTexture(const std::string& name) const;

    // Standalone named textures (non-atlas), e.g. GUI sheets.
    GLuint loadGuiTexture(const std::string& name, const std::string& path, bool flipVertically = true);
    GLuint loadGuiTexture(const std::string& name, const std::string& path, int& outWidth, int& outHeight, bool flipVertically = true);
    [[nodiscard]] GLuint getGuiTexture(const std::string& name) const;

    // Texture atlas used by block-facing UI rendering.
    void buildTextureAtlas(const std::string& directory, int tileSize = 16);
    [[nodiscard]] const TextureAtlas& getAtlas() const;

    // Texture array used by block rendering.
    void buildTextureArray(const std::string& directory, int tileSize = 16);
    [[nodiscard]] const TextureArray& getTextureArray() const;
    void loadBlockTextureCatalog(const std::string& textureConfigPath);
    void preloadEntityTexturesFromConfig(const std::string& entitiesConfigPath);
    [[nodiscard]] int getTextureArrayLayer(const std::string& name) const;
    [[nodiscard]] TextureAnimationInfo getTextureAnimation(const std::string& name) const;
    [[nodiscard]] ResourceTextureTint getTextureTint(const std::string& name) const;

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
    ShaderLibrary m_shaders;
    std::unordered_map<std::string, GLuint> m_textures;
    std::unordered_map<std::string, GLuint> m_texture2D;
    std::unordered_map<std::string, GuiTextureInfo> m_guiTextures;
    TextureAtlas m_atlas;
    TextureAtlas m_itemIconAtlas;
    TextureAtlas m_itemTextureAtlas;
    TextureAtlas m_hudIconAtlas;
    TextureArray m_textureArray;
    GLuint m_lightmapDay = 0;
    GLuint m_lightmapNight = 0;
    GLuint m_grassColormap = 0;
    GLuint m_foliageColormap = 0;
    std::vector<unsigned char> m_blockAtlasPixels;
    std::vector<unsigned char> m_itemAtlasPixels;
    std::unordered_map<std::string, int> m_itemTextureIndices;
    std::unordered_map<std::string, int> m_hudIconIndices;
    std::unordered_map<std::string, int> m_textureArrayLayers;
    std::unordered_map<std::string, BlockTextureCatalogEntry> m_blockTextureCatalog;
    // Mapping from TextureArray layer -> Atlas tile index. Built during buildTextureArray.
    std::unordered_map<int, int> m_arrayLayerToAtlasTile;
    float m_atlasAnisotropy = 1.0f;
    float m_atlasMaxAnisotropy = 1.0f;
    std::unordered_map<std::string, GLuint> m_cubemaps;
};



#endif //MECRAFT_RESOURCEMGR_H
