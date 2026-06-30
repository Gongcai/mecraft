//
// Created by Caiwe on 2026/3/21.
//

#include "ResourceMgr.h"
#include "BlockIconAtlasBuilder.h"
#include "DefaultShaderCatalog.h"
#include "EntityTexturePreloader.h"
#include "TextureAtlasBuilders.h"
#include "../Diagnostics.h"
#include "Paths.h"
#include <cstdint>
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include <vector>

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

    if (m_itemIconAtlas.textureID != 0) {
        glDeleteTextures(1, &m_itemIconAtlas.textureID);
        m_itemIconAtlas.textureID = 0;
    }

    if (m_itemTextureAtlas.textureID != 0) {
        glDeleteTextures(1, &m_itemTextureAtlas.textureID);
        m_itemTextureAtlas.textureID = 0;
    }

    if (m_hudIconAtlas.textureID != 0) {
        glDeleteTextures(1, &m_hudIconAtlas.textureID);
        m_hudIconAtlas.textureID = 0;
    }

    m_environmentTextures.shutdown();

    m_cubemaps.shutdown();

    m_itemAtlasPixels.clear();
    m_itemTextureIndices.clear();
    m_hudIconIndices.clear();
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

bool ResourceMgr::probeAtmosphereLut(const std::string& name,
                                     const std::string& path,
                                     const size_t expectedBytes) const {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path lutPath(path);
    const bool exists = fs::exists(lutPath, ec);
    if (ec || !exists) {
        MECRAFT_LOG_STREAM(std::cerr << "Atmosphere LUT missing: " << name << " at " << path << "\n");
        return false;
    }

    const uintmax_t size = fs::file_size(lutPath, ec);
    if (ec) {
        MECRAFT_LOG_STREAM(std::cerr << "Atmosphere LUT unreadable: " << name << " at " << path << "\n");
        return false;
    }

    MECRAFT_LOG_STREAM(std::cerr << "Atmosphere LUT: " << name << " bytes=" << size);
    if (expectedBytes > 0) {
        MECRAFT_LOG_STREAM(std::cerr << " expected=" << expectedBytes);
        if (size != static_cast<uintmax_t>(expectedBytes)) {
            MECRAFT_LOG_STREAM(std::cerr << " mismatch");
        }
    }
    MECRAFT_LOG_STREAM(std::cerr << "\n");
    return expectedBytes == 0 || size == static_cast<uintmax_t>(expectedBytes);
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
    const TextureAtlas& blockAtlas = m_blockTextures.atlas();
    const std::vector<unsigned char>& blockAtlasPixels = m_blockTextures.atlasPixels();
    if (blockAtlas.textureID == 0 || blockAtlasPixels.empty() || blockAtlas.tileSize <= 0 || blockAtlas.tilesPerRow <= 0) {
        throw std::runtime_error("Block icon atlas requires the block texture atlas to be built first");
    }

    if (m_itemIconAtlas.textureID != 0) {
        glDeleteTextures(1, &m_itemIconAtlas.textureID);
        m_itemIconAtlas.textureID = 0;
    }

    m_itemIconAtlas = resource::buildBlockIconAtlas(iconSize, blockAtlas, blockAtlasPixels, *this);
}

const TextureAtlas& ResourceMgr::getItemIconAtlas() const {
    return m_itemIconAtlas;
}

void ResourceMgr::buildItemTextureAtlas(const std::string& directory, int tileSize) {
    if (m_itemTextureAtlas.textureID != 0) {
        glDeleteTextures(1, &m_itemTextureAtlas.textureID);
        m_itemTextureAtlas.textureID = 0;
    }

    resource::IndexedTextureAtlas atlas = resource::buildItemTextureAtlas(directory, tileSize, m_blockTextures.catalog());
    m_itemTextureAtlas = atlas.atlas;
    m_itemAtlasPixels = std::move(atlas.pixels);
    m_itemTextureIndices = std::move(atlas.indices);
}

const TextureAtlas& ResourceMgr::getItemTextureAtlas() const {
    return m_itemTextureAtlas;
}

int ResourceMgr::getItemTextureIndex(const std::string& textureName) const {
    const auto it = m_itemTextureIndices.find(textureName);
    if (it == m_itemTextureIndices.end()) {
        return -1;
    }
    return it->second;
}

const std::vector<unsigned char>& ResourceMgr::getItemTexturePixels() const {
    return m_itemAtlasPixels;
}

void ResourceMgr::buildHudIconAtlas(const std::string& directory, int iconSize) {
    if (m_hudIconAtlas.textureID != 0) {
        glDeleteTextures(1, &m_hudIconAtlas.textureID);
        m_hudIconAtlas.textureID = 0;
    }

    resource::IndexedTextureAtlas atlas = resource::buildHudIconAtlas(directory, iconSize);
    m_hudIconAtlas = atlas.atlas;
    m_hudIconIndices = std::move(atlas.indices);
}

const TextureAtlas& ResourceMgr::getHudIconAtlas() const {
    return m_hudIconAtlas;
}

int ResourceMgr::getHudIconIndex(const std::string& iconName) const {
    const auto it = m_hudIconIndices.find(iconName);
    if (it == m_hudIconIndices.end()) {
        return -1;
    }
    return it->second;
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
