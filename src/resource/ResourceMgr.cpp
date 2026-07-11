//
// Created by Caiwe on 2026/3/21.
//

#include "ResourceMgr.h"
#include "BlockTextureLibrary.h"
#include "CubemapLibrary.h"
#include "DefaultShaderCatalog.h"
#include "EntityTexturePreloader.h"
#include "EnvironmentTextureLibrary.h"
#include "Paths.h"
#include "ShaderLibrary.h"
#include "Texture2DLibrary.h"
#include "UiTextureAtlasLibrary.h"
#include "renderer/rhi/RhiDevice.h"

#include <cassert>
#include <memory>

struct ResourceMgr::Impl {
    RhiDevice* rhiDevice = nullptr;
    ShaderLibrary shaders;
    Texture2DLibrary texture2D;
    BlockTextureLibrary blockTextures;
    UiTextureAtlasLibrary uiTextures;
    EnvironmentTextureLibrary environmentTextures;
    CubemapLibrary cubemaps;
};

ResourceMgr::ResourceMgr()
    : m_impl(std::make_unique<Impl>()) {
}

ResourceMgr::~ResourceMgr() {
}

ResourceMgr::ResourceMgr(ResourceMgr&&) noexcept = default;

ResourceMgr& ResourceMgr::operator=(ResourceMgr&&) noexcept = default;

void ResourceMgr::init(RhiDevice& rhiDevice) {
    m_impl->rhiDevice = &rhiDevice;
    m_impl->texture2D.init(rhiDevice);
    resource::loadDefaultShaders(m_impl->shaders);

    loadCubemap("menu_skybox",
                SKYBOX_TEXTURES_DIR "/right.png",
                SKYBOX_TEXTURES_DIR "/left.png",
                SKYBOX_TEXTURES_DIR "/top.png",
                SKYBOX_TEXTURES_DIR "/bottom.png",
                SKYBOX_TEXTURES_DIR "/front.png",
                SKYBOX_TEXTURES_DIR "/back.png");
}

void ResourceMgr::shutdown() {
    m_impl->texture2D.shutdown();

    m_impl->blockTextures.shutdown();
    m_impl->uiTextures.shutdown();

    m_impl->environmentTextures.shutdown();

    m_impl->cubemaps.shutdown();

    m_impl->shaders.clear();
    m_impl->rhiDevice = nullptr;
}

RhiDevice& ResourceMgr::rhiDevice() {
    assert(m_impl->rhiDevice != nullptr);
    return *m_impl->rhiDevice;
}

const RhiDevice& ResourceMgr::rhiDevice() const {
    assert(m_impl->rhiDevice != nullptr);
    return *m_impl->rhiDevice;
}

Shader *ResourceMgr::loadShader(const std::string &name, const char *vertPath, const char *fragPath) {
    return m_impl->shaders.load(name, vertPath, fragPath);
}

Shader *ResourceMgr::getShader(const std::string &name) {
    return m_impl->shaders.get(name);
}

RhiTextureHandle ResourceMgr::loadTexture2D(const std::string& name,
                                            const std::string& path,
                                            const bool srgb,
                                            const bool flipVertically) {
    return m_impl->texture2D.load(name, path, srgb, flipVertically);
}

RhiTextureHandle ResourceMgr::getTexture2DHandle(const std::string& name) const {
    return m_impl->texture2D.getHandle(name);
}

RhiTextureHandle ResourceMgr::loadGuiTexture(const std::string& name, const std::string& path, bool flipVertically) {
    return m_impl->texture2D.loadGui(name, path, flipVertically);
}

RhiTextureHandle ResourceMgr::loadGuiTexture(const std::string& name, const std::string& path, int& outWidth, int& outHeight, bool flipVertically) {
    return m_impl->texture2D.loadGui(name, path, outWidth, outHeight, flipVertically);
}

RhiTextureHandle ResourceMgr::getGuiTextureHandle(const std::string& name) const {
    return m_impl->texture2D.getGuiHandle(name);
}

void ResourceMgr::buildBlockTextureResources(const std::string& directory, int tileSize) {
    m_impl->blockTextures.buildTextures(directory, tileSize);
}

void ResourceMgr::buildBlockTextureResources(const std::string& directory,
                                             const int tileSize,
                                             const std::unordered_set<std::string>& registeredTextureNames) {
    m_impl->blockTextures.buildTextures(directory, tileSize, registeredTextureNames);
}

void ResourceMgr::buildTextureAtlas(const std::string &directory, int tileSize) {
    m_impl->blockTextures.buildAtlas(directory, tileSize);
}

const TextureAtlas & ResourceMgr::getAtlas() const {
    return m_impl->blockTextures.atlas();
}

void ResourceMgr::buildTextureArray(const std::string &directory, int tileSize) {
    m_impl->blockTextures.buildTextureArray(directory, tileSize);
}

const TextureArray& ResourceMgr::getTextureArray() const {
    return m_impl->blockTextures.textureArray();
}

const TextureArray& ResourceMgr::getBlockNormalTextureArray() const {
    return m_impl->blockTextures.normalTextureArray();
}

const TextureArray& ResourceMgr::getBlockSpecularTextureArray() const {
    return m_impl->blockTextures.specularTextureArray();
}

bool ResourceMgr::hasBlockNormalMaps() const {
    return m_impl->blockTextures.hasNormalMaps();
}

bool ResourceMgr::hasBlockSpecularMaps() const {
    return m_impl->blockTextures.hasSpecularMaps();
}

bool ResourceMgr::loadBlockTextureCatalog(const std::string& textureConfigPath) {
    return m_impl->blockTextures.loadCatalog(textureConfigPath);
}

bool ResourceMgr::loadBlockTextureCatalog(const std::string& textureConfigPath,
                                          const std::string& packConfigPath) {
    return m_impl->blockTextures.loadCatalog(textureConfigPath, packConfigPath);
}

int ResourceMgr::getBlockTextureTileSize() const {
    return m_impl->blockTextures.tileSize();
}

bool ResourceMgr::preloadEntityTexturesFromConfig(const std::string& entitiesConfigPath) {
    return resource::preloadEntityTexturesFromConfig(*this, entitiesConfigPath);
}

int ResourceMgr::getTextureArrayLayer(const std::string& name) const {
    return m_impl->blockTextures.textureArrayLayer(name);
}

TextureAnimationInfo ResourceMgr::getTextureAnimation(const std::string& name) const {
    return m_impl->blockTextures.textureAnimation(name);
}

ResourceTextureTint ResourceMgr::getTextureTint(const std::string& name) const {
    return m_impl->blockTextures.textureTint(name);
}

const glm::vec3& ResourceMgr::blockTextureAverageColor(const int arrayLayer) const {
    return m_impl->blockTextures.textureAverageColor(arrayLayer);
}

int ResourceMgr::arrayLayerToAtlasTile(const int arrayLayer) const {
    return m_impl->blockTextures.arrayLayerToAtlasTile(arrayLayer);
}

void ResourceMgr::loadLightmapTextures(const std::string& dayPath, const std::string& nightPath) {
    m_impl->environmentTextures.loadLightmaps(dayPath, nightPath);
}

RhiTextureHandle ResourceMgr::getLightmapDay() const {
    return m_impl->environmentTextures.getLightmapDay();
}

RhiTextureHandle ResourceMgr::getLightmapNight() const {
    return m_impl->environmentTextures.getLightmapNight();
}

void ResourceMgr::loadColormapTextures(const std::string& grassPath, const std::string& foliagePath) {
    m_impl->environmentTextures.loadColormaps(grassPath, foliagePath);
}

RhiTextureHandle ResourceMgr::getGrassColormap() const {
    return m_impl->environmentTextures.getGrassColormap();
}

RhiTextureHandle ResourceMgr::getFoliageColormap() const {
    return m_impl->environmentTextures.getFoliageColormap();
}

RhiTextureHandle ResourceMgr::loadCubemap(const std::string& name,
                                          const std::string& rightPath, const std::string& leftPath,
                                          const std::string& topPath, const std::string& bottomPath,
                                          const std::string& frontPath, const std::string& backPath) {
    return m_impl->cubemaps.load(name, rightPath, leftPath, topPath, bottomPath, frontPath, backPath);
}

RhiTextureHandle ResourceMgr::getCubemap(const std::string& name) const {
    return m_impl->cubemaps.get(name);
}

void ResourceMgr::buildBlockIconAtlas(int iconSize) {
    m_impl->uiTextures.buildBlockIconAtlas(iconSize, m_impl->blockTextures);
}

const TextureAtlas& ResourceMgr::getItemIconAtlas() const {
    return m_impl->uiTextures.blockIconAtlas();
}

void ResourceMgr::buildItemTextureAtlas(const std::string& directory, int tileSize) {
    m_impl->uiTextures.buildItemTextureAtlas(directory, tileSize, m_impl->blockTextures.catalog());
}

const TextureAtlas& ResourceMgr::getItemTextureAtlas() const {
    return m_impl->uiTextures.itemTextureAtlas();
}

int ResourceMgr::getItemTextureIndex(const std::string& textureName) const {
    return m_impl->uiTextures.itemTextureIndex(textureName);
}

const std::vector<unsigned char>& ResourceMgr::getItemTexturePixels() const {
    return m_impl->uiTextures.itemTexturePixels();
}

void ResourceMgr::buildHudIconAtlas(const std::string& directory, int iconSize) {
    m_impl->uiTextures.buildHudIconAtlas(directory, iconSize);
}

const TextureAtlas& ResourceMgr::getHudIconAtlas() const {
    return m_impl->uiTextures.hudIconAtlas();
}

int ResourceMgr::getHudIconIndex(const std::string& iconName) const {
    return m_impl->uiTextures.hudIconIndex(iconName);
}

float ResourceMgr::getAtlasAnisotropy() const {
    return m_impl->blockTextures.anisotropy();
}

float ResourceMgr::getAtlasMaxAnisotropy() const {
    return m_impl->blockTextures.maxAnisotropy();
}

void ResourceMgr::setAtlasAnisotropy(const float anisotropy) {
    m_impl->blockTextures.setAnisotropy(anisotropy);
}
