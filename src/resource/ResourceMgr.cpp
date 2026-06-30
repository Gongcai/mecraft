//
// Created by Caiwe on 2026/3/21.
//

#include "ResourceMgr.h"
#include "DefaultShaderCatalog.h"
#include "EntityTexturePreloader.h"
#include "Paths.h"

void ResourceMgr::init() {
    resource::loadDefaultShaders(m_shaders);

    loadCubemap("menu_skybox",
                SKYBOX_TEXTURES_DIR "/right.png",
                SKYBOX_TEXTURES_DIR "/left.png",
                SKYBOX_TEXTURES_DIR "/top.png",
                SKYBOX_TEXTURES_DIR "/bottom.png",
                SKYBOX_TEXTURES_DIR "/front.png",
                SKYBOX_TEXTURES_DIR "/back.png");
}

void ResourceMgr::shutdown() {
    m_texture2D.shutdown();

    m_blockTextures.shutdown();
    m_uiTextures.shutdown();

    m_environmentTextures.shutdown();

    m_cubemaps.shutdown();

    m_shaders.clear();
}

Shader *ResourceMgr::loadShader(const std::string &name, const char *vertPath, const char *fragPath) {
    return m_shaders.load(name, vertPath, fragPath);
}

Shader *ResourceMgr::getShader(const std::string &name) {
    return m_shaders.get(name);
}

GLuint ResourceMgr::loadTexture2D(const std::string& name,
                                  const std::string& path,
                                  const bool srgb,
                                  const bool repeat,
                                  const bool linear,
                                  const bool flipVertically) {
    return m_texture2D.load(name, path, srgb, repeat, linear, flipVertically);
}

GLuint ResourceMgr::getTexture2D(const std::string& name) const {
    return m_texture2D.get(name);
}

GLuint ResourceMgr::loadGuiTexture(const std::string& name, const std::string& path, bool flipVertically) {
    return m_texture2D.loadGui(name, path, flipVertically);
}

GLuint ResourceMgr::loadGuiTexture(const std::string& name, const std::string& path, int& outWidth, int& outHeight, bool flipVertically) {
    return m_texture2D.loadGui(name, path, outWidth, outHeight, flipVertically);
}

GLuint ResourceMgr::getGuiTexture(const std::string& name) const {
    return m_texture2D.getGui(name);
}

void ResourceMgr::buildTextureAtlas(const std::string &directory, int tileSize) {
    m_blockTextures.buildAtlas(directory, tileSize);
}

const TextureAtlas & ResourceMgr::getAtlas() const {
    return m_blockTextures.atlas();
}

void ResourceMgr::buildTextureArray(const std::string &directory, int tileSize) {
    m_blockTextures.buildTextureArray(directory, tileSize);
}

const TextureArray& ResourceMgr::getTextureArray() const {
    return m_blockTextures.textureArray();
}

void ResourceMgr::loadBlockTextureCatalog(const std::string& textureConfigPath) {
    m_blockTextures.loadCatalog(textureConfigPath);
}

void ResourceMgr::preloadEntityTexturesFromConfig(const std::string& entitiesConfigPath) {
    resource::preloadEntityTexturesFromConfig(*this, entitiesConfigPath);
}

int ResourceMgr::getTextureArrayLayer(const std::string& name) const {
    return m_blockTextures.textureArrayLayer(name);
}

TextureAnimationInfo ResourceMgr::getTextureAnimation(const std::string& name) const {
    return m_blockTextures.textureAnimation(name);
}

ResourceTextureTint ResourceMgr::getTextureTint(const std::string& name) const {
    return m_blockTextures.textureTint(name);
}

int ResourceMgr::arrayLayerToAtlasTile(const int arrayLayer) const {
    return m_blockTextures.arrayLayerToAtlasTile(arrayLayer);
}

void ResourceMgr::loadLightmapTextures(const std::string& dayPath, const std::string& nightPath) {
    m_environmentTextures.loadLightmaps(dayPath, nightPath);
}

GLuint ResourceMgr::getLightmapDay() const {
    return m_environmentTextures.getLightmapDay();
}

GLuint ResourceMgr::getLightmapNight() const {
    return m_environmentTextures.getLightmapNight();
}

void ResourceMgr::loadColormapTextures(const std::string& grassPath, const std::string& foliagePath) {
    m_environmentTextures.loadColormaps(grassPath, foliagePath);
}

GLuint ResourceMgr::getGrassColormap() const {
    return m_environmentTextures.getGrassColormap();
}

GLuint ResourceMgr::getFoliageColormap() const {
    return m_environmentTextures.getFoliageColormap();
}

GLuint ResourceMgr::loadCubemap(const std::string& name,
                                 const std::string& rightPath, const std::string& leftPath,
                                 const std::string& topPath, const std::string& bottomPath,
                                 const std::string& frontPath, const std::string& backPath) {
    return m_cubemaps.load(name, rightPath, leftPath, topPath, bottomPath, frontPath, backPath);
}

GLuint ResourceMgr::getCubemap(const std::string& name) const {
    return m_cubemaps.get(name);
}

void ResourceMgr::buildBlockIconAtlas(int iconSize) {
    m_uiTextures.buildBlockIconAtlas(iconSize, m_blockTextures);
}

const TextureAtlas& ResourceMgr::getItemIconAtlas() const {
    return m_uiTextures.blockIconAtlas();
}

void ResourceMgr::buildItemTextureAtlas(const std::string& directory, int tileSize) {
    m_uiTextures.buildItemTextureAtlas(directory, tileSize, m_blockTextures.catalog());
}

const TextureAtlas& ResourceMgr::getItemTextureAtlas() const {
    return m_uiTextures.itemTextureAtlas();
}

int ResourceMgr::getItemTextureIndex(const std::string& textureName) const {
    return m_uiTextures.itemTextureIndex(textureName);
}

const std::vector<unsigned char>& ResourceMgr::getItemTexturePixels() const {
    return m_uiTextures.itemTexturePixels();
}

void ResourceMgr::buildHudIconAtlas(const std::string& directory, int iconSize) {
    m_uiTextures.buildHudIconAtlas(directory, iconSize);
}

const TextureAtlas& ResourceMgr::getHudIconAtlas() const {
    return m_uiTextures.hudIconAtlas();
}

int ResourceMgr::getHudIconIndex(const std::string& iconName) const {
    return m_uiTextures.hudIconIndex(iconName);
}

float ResourceMgr::getAtlasAnisotropy() const {
    return m_blockTextures.anisotropy();
}

float ResourceMgr::getAtlasMaxAnisotropy() const {
    return m_blockTextures.maxAnisotropy();
}

void ResourceMgr::setAtlasAnisotropy(const float anisotropy) {
    m_blockTextures.setAnisotropy(anisotropy);
}
