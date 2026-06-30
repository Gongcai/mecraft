//
// Created by Caiwe on 2026/3/21.
//

#include "ResourceMgr.h"
#include "BlockIconAtlasBuilder.h"
#include "../Diagnostics.h"
#include "Paths.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <unordered_set>
#include <vector>
#include <nlohmann/json.hpp>
#include "../third_party/stb/stb_image.h"

namespace {

struct ResourceTint {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

ResourceTint textureTintColor(const ResourceTextureTint tint) {
    switch (tint) {
        case ResourceTextureTint::Grass:
        case ResourceTextureTint::Foliage:
            return {0.50f, 0.78f, 0.34f};
        case ResourceTextureTint::None:
        default:
            return {};
    }
}

} // namespace

std::pair<glm::vec2, glm::vec2> TextureAtlas::getUV(int tileIndex) const {
    if (tilesPerRow == 0 || atlasWidth == 0 || atlasHeight == 0 || tileSize <= 0) {
        return {{0,0}, {0,0}};
    }

    const int stride = std::max(1, tileStride);
    int tileCol = tileIndex % tilesPerRow;
    int tileRow = tileIndex / tilesPerRow;

    const int startX = tileCol * stride + tilePadding;
    const int startY = tileRow * stride + tilePadding;

    // Use tile boundaries (not texel centers) so the full 16x16 area maps onto a face.
    // A tiny inset keeps UVs strictly inside the tile to avoid border tie-break artifacts.
    const float innerInsetPx = 1e-3f;
    float uMin = (static_cast<float>(startX) + innerInsetPx) / static_cast<float>(atlasWidth);
    float vMin = (static_cast<float>(startY) + innerInsetPx) / static_cast<float>(atlasHeight);
    float uMax = (static_cast<float>(startX + tileSize) - innerInsetPx) / static_cast<float>(atlasWidth);
    float vMax = (static_cast<float>(startY + tileSize) - innerInsetPx) / static_cast<float>(atlasHeight);

    if (uMin > uMax) {
        const float uCenter = (uMin + uMax) * 0.5f;
        uMin = uCenter;
        uMax = uCenter;
    }
    if (vMin > vMax) {
        const float vCenter = (vMin + vMax) * 0.5f;
        vMin = vCenter;
        vMax = vCenter;
    }

    // Return the lower-left and upper-right UV corners.
    return { glm::vec2(uMin, vMin), glm::vec2(uMax, vMax) };
}

void ResourceMgr::init() {
    loadShader("chunk", SHADERS_DIR "/chunk.vert", SHADERS_DIR "/chunk.frag");
    loadShader("chunk_lit", SHADERS_DIR "/chunk_lit.vert", SHADERS_DIR "/chunk_lit.frag");
    loadShader("transparent_composite", SHADERS_DIR "/chunk_lit.vert", SHADERS_DIR "/transparent_composite.frag");
    loadShader("water_composite", SHADERS_DIR "/chunk_lit.vert", SHADERS_DIR "/water_composite.frag");
    loadShader("block_item_lit", SHADERS_DIR "/chunk_lit.vert", SHADERS_DIR "/block_item_lit.frag");
    loadShader("block_item_forward", SHADERS_DIR "/chunk_lit.vert", SHADERS_DIR "/block_item_forward.frag");
    loadShader("forward_basic_terrain", SHADERS_DIR "/chunk_lit.vert", SHADERS_DIR "/forward_basic_terrain.frag");
    loadShader("chunk_gbuffer", SHADERS_DIR "/chunk_gbuffer.vert", SHADERS_DIR "/chunk_gbuffer.frag");
    loadShader("shadow_depth", SHADERS_DIR "/shadow_depth.vert", SHADERS_DIR "/shadow_depth.frag");
    loadShader("deferred_lighting", SHADERS_DIR "/deferred_lighting.vert", SHADERS_DIR "/deferred_lighting.frag");
    loadShader("scene_composite", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/scene_composite.frag");
    loadShader("deferred_debug", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/deferred_debug.frag");
    loadShader("ssao", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/ssao.frag");
    loadShader("velocity_resolve", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/velocity_resolve.frag");
    loadShader("volumetric_fog", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/volumetric_fog.frag");
    loadShader("volumetric_temporal", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/volumetric_temporal.frag");
    loadShader("volumetric_composite", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/volumetric_composite.frag");
    loadShader("reflection_probe", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/reflection_probe.frag");
    loadShader("cloud_target", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/cloud_target.frag");
    loadShader("temporal_resolve", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/temporal_resolve.frag");
    loadShader("reflection_filter", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/reflection_filter.frag");
    loadShader("ssao_filter", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/ssao_filter.frag");
    loadShader("ssao_temporal", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/ssao_temporal.frag");
    loadShader("ssao_upsample", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/ssao_upsample.frag");
    loadShader("motion_blur", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/motion_blur.frag");
    loadShader("dof", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/dof.frag");
    loadShader("bloom_extract", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/bloom_extract.frag");
    loadShader("bloom_blur", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/bloom_blur.frag");
    loadShader("exposure_downsample", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/exposure_downsample.frag");
    loadShader("exposure_resolve", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/exposure_resolve.frag");
    loadShader("fsr1_easu", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/fsr1_easu.frag");
    loadShader("fsr1_rcas", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/fsr1_rcas.frag");
    loadShader("drop_block", SHADERS_DIR "/drop_block.vert", SHADERS_DIR "/drop_block.frag");
    loadShader("drop_block_forward", SHADERS_DIR "/drop_block.vert", SHADERS_DIR "/drop_block_forward.frag");
    loadShader("outline", SHADERS_DIR "/outline.vert", SHADERS_DIR "/outline.frag");
    loadShader("break_overlay", SHADERS_DIR "/break_overlay.vert", SHADERS_DIR "/break_overlay.frag");
    loadShader("crosshair", SHADERS_DIR "/crosshair.vert", SHADERS_DIR "/crosshair.frag");
    loadShader("inventory", SHADERS_DIR "/inventory.vert", SHADERS_DIR "/inventory.frag");
    loadShader("text", SHADERS_DIR "/text.vert", SHADERS_DIR "/text.frag");
    loadShader("particle", SHADERS_DIR "/particle.vert", SHADERS_DIR "/particle.frag");
    loadShader("particle_gbuffer", SHADERS_DIR "/particle_gbuffer.vert", SHADERS_DIR "/particle_gbuffer.frag");
    loadShader("rain", SHADERS_DIR "/rain.vert", SHADERS_DIR "/rain.frag");
    loadShader("postprocess", SHADERS_DIR "/postprocess.vert", SHADERS_DIR "/postprocess.frag");
    loadShader("blit_texture", SHADERS_DIR "/postprocess.vert", SHADERS_DIR "/blit_texture.frag");
    loadShader("item_model", SHADERS_DIR "/item_model.vert", SHADERS_DIR "/item_model.frag");
    loadShader("item_model_forward", SHADERS_DIR "/item_model.vert", SHADERS_DIR "/item_model_forward.frag");
    loadShader("steve", SHADERS_DIR "/steve.vert", SHADERS_DIR "/steve.frag");
    loadShader("steve_forward", SHADERS_DIR "/steve.vert", SHADERS_DIR "/steve_forward.frag");
    loadShader("entity_gbuffer", SHADERS_DIR "/entity_gbuffer.vert", SHADERS_DIR "/entity_gbuffer.frag");
    loadShader("entity_shadow", SHADERS_DIR "/entity_shadow.vert", SHADERS_DIR "/entity_shadow.frag");
    loadShader("block_entity_gbuffer", SHADERS_DIR "/block_entity_gbuffer.vert", SHADERS_DIR "/block_entity_gbuffer.frag");
    loadShader("block_entity_shadow", SHADERS_DIR "/block_entity_shadow.vert", SHADERS_DIR "/block_entity_shadow.frag");
    loadShader("drop_gbuffer", SHADERS_DIR "/drop_gbuffer.vert", SHADERS_DIR "/drop_gbuffer.frag");
    loadShader("item_gbuffer", SHADERS_DIR "/item_gbuffer.vert", SHADERS_DIR "/item_gbuffer.frag");
    loadShader("item_shadow", SHADERS_DIR "/item_shadow.vert", SHADERS_DIR "/item_shadow.frag");
    loadShader("ui_color", SHADERS_DIR "/ui_color.vert", SHADERS_DIR "/ui_color.frag");
    loadShader("ui_glass", SHADERS_DIR "/ui_glass.vert", SHADERS_DIR "/ui_glass.frag");
    loadShader("skybox", SHADERS_DIR "/skybox.vert", SHADERS_DIR "/skybox.frag");
    loadShader("gameplay_sky", SHADERS_DIR "/gameplay_sky.vert", SHADERS_DIR "/gameplay_sky.frag");
    loadShader("gameplay_sky_forward", SHADERS_DIR "/gameplay_sky.vert", SHADERS_DIR "/gameplay_sky_forward.frag");
    loadShader("blur", SHADERS_DIR "/blur.vert", SHADERS_DIR "/blur.frag");

    loadCubemap("menu_skybox",
                SKYBOX_TEXTURES_DIR "/right.png",
                SKYBOX_TEXTURES_DIR "/left.png",
                SKYBOX_TEXTURES_DIR "/top.png",
                SKYBOX_TEXTURES_DIR "/bottom.png",
                SKYBOX_TEXTURES_DIR "/front.png",
                SKYBOX_TEXTURES_DIR "/back.png");
}

void ResourceMgr::shutdown() {
    for (auto& [_, texInfo] : m_guiTextures) {
        if (texInfo.textureID != 0) {
            glDeleteTextures(1, &texInfo.textureID);
        }
    }
    m_guiTextures.clear();

    if (m_atlas.textureID != 0) {
        glDeleteTextures(1, &m_atlas.textureID);
        m_atlas.textureID = 0;
    }

    if (m_itemIconAtlas.textureID != 0) {
        glDeleteTextures(1, &m_itemIconAtlas.textureID);
        m_itemIconAtlas.textureID = 0;
    }

    if (m_itemTextureAtlas.textureID != 0) {
        glDeleteTextures(1, &m_itemTextureAtlas.textureID);
        m_itemTextureAtlas.textureID = 0;
    }

    if (m_textureArray.textureID != 0) {
        glDeleteTextures(1, &m_textureArray.textureID);
        m_textureArray.textureID = 0;
    }

    if (m_lightmapDay != 0) {
        glDeleteTextures(1, &m_lightmapDay);
        m_lightmapDay = 0;
    }
    if (m_lightmapNight != 0) {
        glDeleteTextures(1, &m_lightmapNight);
        m_lightmapNight = 0;
    }
    if (m_grassColormap != 0) {
        glDeleteTextures(1, &m_grassColormap);
        m_grassColormap = 0;
    }
    if (m_foliageColormap != 0) {
        glDeleteTextures(1, &m_foliageColormap);
        m_foliageColormap = 0;
    }

    for (auto& [_, texId] : m_texture2D) {
        if (texId != 0) {
            glDeleteTextures(1, &texId);
        }
    }
    m_texture2D.clear();

    for (auto& [_, texId] : m_cubemaps) {
        if (texId != 0) {
            glDeleteTextures(1, &texId);
        }
    }
    m_cubemaps.clear();

    m_blockAtlasPixels.clear();
    m_itemAtlasPixels.clear();
    m_itemTextureIndices.clear();
    m_textureArrayLayers.clear();
    m_blockTextureCatalog.clear();
    m_shaders.clear();
}

Shader *ResourceMgr::loadShader(const std::string &name, const char *vertPath, const char *fragPath) {
    return m_shaders.load(name, vertPath, fragPath);
}

Shader *ResourceMgr::getShader(const std::string &name) {
    return m_shaders.get(name);
}

GLuint ResourceMgr::loadTexture(const std::string &path, bool alpha) {
    //TODO: 完善贴图读取
    return 0;
}

GLuint ResourceMgr::loadTexture2D(const std::string& name,
                                  const std::string& path,
                                  const bool srgb,
                                  const bool repeat,
                                  const bool linear,
                                  const bool flipVertically) {
    auto existing = m_texture2D.find(name);
    if (existing != m_texture2D.end()) {
        return existing->second;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data || width <= 0 || height <= 0) {
#ifdef MECRAFT_DEBUG
        MECRAFT_LOG_STREAM(std::cerr << "Failed to load texture2D '" << name << "': " << path << "\n");
#endif
        if (data != nullptr) {
            stbi_image_free(data);
        }
        return 0;
    }

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8,
                 width,
                 height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 data);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);

    m_texture2D[name] = textureID;
    return textureID;
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
    auto it = m_texture2D.find(name);
    if (it != m_texture2D.end()) {
        return it->second;
    }
    return 0;
}

GLuint ResourceMgr::loadGuiTexture(const std::string& name, const std::string& path, bool flipVertically) {
    int dummyW = 0, dummyH = 0;
    return loadGuiTexture(name, path, dummyW, dummyH, flipVertically);
}

GLuint ResourceMgr::loadGuiTexture(const std::string& name, const std::string& path, int& outWidth, int& outHeight, bool flipVertically) {
    auto existing = m_guiTextures.find(name);
    if (existing != m_guiTextures.end()) {
        outWidth = existing->second.width;
        outHeight = existing->second.height;
        return existing->second.textureID;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) {
#ifdef MECRAFT_DEBUG
        MECRAFT_LOG_STREAM(std::cerr << "Failed to load GUI texture: " << path << "\n");
#endif
        return 0;
    }

    outWidth = width;
    outHeight = height;

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);

    GuiTextureInfo info;
    info.textureID = textureID;
    info.width = width;
    info.height = height;
    m_guiTextures[name] = info;
    return textureID;
}

GLuint ResourceMgr::getGuiTexture(const std::string& name) const {
    auto it = m_guiTextures.find(name);
    if (it != m_guiTextures.end()) {
        return it->second.textureID;
    }
    return 0;
}

GLuint ResourceMgr::getTexture(const std::string &name) const {
    auto it = m_textures.find(name);
    if (it != m_textures.end()) {
        return it->second;
    }
    return 0;
}

#if defined(GL_TEXTURE_MAX_ANISOTROPY)
constexpr GLenum kTextureMaxAnisotropyPName = GL_TEXTURE_MAX_ANISOTROPY;
#elif defined(GL_TEXTURE_MAX_ANISOTROPY_EXT)
constexpr GLenum kTextureMaxAnisotropyPName = GL_TEXTURE_MAX_ANISOTROPY_EXT;
#else
constexpr GLenum kTextureMaxAnisotropyPName = 0;
#endif

#if defined(GL_MAX_TEXTURE_MAX_ANISOTROPY)
constexpr GLenum kMaxTextureMaxAnisotropyPName = GL_MAX_TEXTURE_MAX_ANISOTROPY;
#elif defined(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT)
constexpr GLenum kMaxTextureMaxAnisotropyPName = GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT;
#else
constexpr GLenum kMaxTextureMaxAnisotropyPName = 0;
#endif

inline bool supportsAnisotropicFiltering() {
    return kTextureMaxAnisotropyPName != 0 && kMaxTextureMaxAnisotropyPName != 0;
}

namespace {
int computeTextureArrayLayerCount(const std::filesystem::path& imagePath,
                                  const int tileSize,
                                  const std::unordered_map<std::string, BlockTextureCatalogEntry>& catalog) {
    const std::string textureName = imagePath.stem().string();
    const auto catalogIt = catalog.find(textureName);
    if (catalogIt == catalog.end() ||
        !catalogIt->second.verticalFrames ||
        catalogIt->second.animation.frameCount <= 1) {
        return 1;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info(imagePath.string().c_str(), &width, &height, &channels)) {
        return 1;
    }

    const TextureAnimationInfo& animation = catalogIt->second.animation;
    if (width != tileSize || height != tileSize * animation.frameCount) {
        throw std::runtime_error("Texture catalog dimensions do not match vertical frames for " + imagePath.string());
    }

    return animation.frameCount;
}

ResourceTextureTint parseResourceTextureTint(const nlohmann::json& textureJson) {
    const auto tintIt = textureJson.find("tint");
    if (tintIt == textureJson.end()) {
        return ResourceTextureTint::None;
    }
    if (!tintIt->is_string()) {
        throw std::runtime_error("block_textures.json texture tint must be a string");
    }

    const std::string tint = tintIt->get<std::string>();
    if (tint == "none") {
        return ResourceTextureTint::None;
    }
    if (tint == "grass") {
        return ResourceTextureTint::Grass;
    }
    if (tint == "foliage") {
        return ResourceTextureTint::Foliage;
    }
    throw std::runtime_error("Unknown block texture tint: " + tint);
}
}

void ResourceMgr::buildTextureAtlas(const std::string &directory, int tileSize) {
    namespace fs = std::filesystem;
    std::vector<fs::path> imagePaths;
    m_textures.clear();

    if (fs::exists(directory)) {
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.path().extension() == ".png") {
                imagePaths.push_back(entry.path());
            }
        }
    }

    // Keep atlas indices stable across runs/platforms.
    std::sort(imagePaths.begin(), imagePaths.end(),
              [](const fs::path& a, const fs::path& b) {
                  return a.filename().string() < b.filename().string();
              });

    if (imagePaths.empty()) {
#ifdef MECRAFT_DEBUG
        MECRAFT_LOG_STREAM(std::cerr << "Texture Atlas generated with 0 images!\n");
#endif
        return;
    }

    const int numTiles = static_cast<int>(imagePaths.size());
    const int tilesPerRow = static_cast<int>(std::ceil(std::sqrt(numTiles)));
    const int numRows = static_cast<int>(std::ceil(static_cast<float>(numTiles) / static_cast<float>(tilesPerRow)));

    // Per-tile gutters prevent cross-tile color bleeding in mip levels.
    constexpr int kTilePadding = 2;
    const int tileStride = tileSize + kTilePadding * 2;

    const int atlasWidth = tilesPerRow * tileStride;
    const int atlasHeight = numRows * tileStride;
    std::vector<unsigned char> atlasPixels(static_cast<size_t>(atlasWidth) * static_cast<size_t>(atlasHeight) * 4, 0);

    stbi_set_flip_vertically_on_load(true);

    for (int i = 0; i < numTiles; ++i) {
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* data = stbi_load(imagePaths[i].string().c_str(), &width, &height, &channels, 4);

        if (!data) {
#ifdef MECRAFT_DEBUG
            MECRAFT_LOG_STREAM(std::cerr << "Failed to load image: " << imagePaths[i] << "\n");
#endif
            continue;
        }

        const std::string texName = imagePaths[i].stem().string();
        const auto catalogIt = m_blockTextureCatalog.find(texName);
        const bool useVerticalFrame =
            catalogIt != m_blockTextureCatalog.end() &&
            catalogIt->second.verticalFrames &&
            catalogIt->second.animation.frameCount > 1;
        int sourceHeight = height;
        const unsigned char* sourcePixels = data;
        if (useVerticalFrame) {
            const int frameCount = catalogIt->second.animation.frameCount;
            if (width != tileSize || height != tileSize * frameCount) {
                stbi_image_free(data);
                throw std::runtime_error("Texture catalog dimensions do not match atlas source for " + imagePaths[i].string());
            }
            const int frameIndex = catalogIt->second.topFrameFirst ? frameCount - 1 : 0;
            sourcePixels = data + static_cast<size_t>(frameIndex * tileSize * width) * 4;
            sourceHeight = tileSize;
        }

        if (!useVerticalFrame && (width != tileSize || height != tileSize)) {
#ifdef MECRAFT_DEBUG
            MECRAFT_LOG_STREAM(std::cerr << "Warning: Texture size mismatch! " << imagePaths[i] << "\n");
#endif
        }

        const int copyWidth = std::min(tileSize, width);
        const int copyHeight = std::min(tileSize, sourceHeight);

        const int tileCol = i % tilesPerRow;
        const int tileRow = i / tilesPerRow;
        const int tileBaseX = tileCol * tileStride;
        const int tileBaseY = tileRow * tileStride;
        const int innerStartX = tileBaseX + kTilePadding;
        const int innerStartY = tileBaseY + kTilePadding;

        for (int y = 0; y < copyHeight; ++y) {
            for (int x = 0; x < copyWidth; ++x) {
                const int destIndex = ((innerStartY + y) * atlasWidth + (innerStartX + x)) * 4;
                const int srcIndex = (y * width + x) * 4;
                atlasPixels[destIndex + 0] = sourcePixels[srcIndex + 0];
                atlasPixels[destIndex + 1] = sourcePixels[srcIndex + 1];
                atlasPixels[destIndex + 2] = sourcePixels[srcIndex + 2];
                atlasPixels[destIndex + 3] = sourcePixels[srcIndex + 3];
            }
        }

        // Duplicate edge texels into padding so lower mip levels stay inside the same tile.
        for (int y = 0; y < copyHeight; ++y) {
            const int srcY = innerStartY + y;
            const int leftSrcX = innerStartX;
            const int rightSrcX = innerStartX + copyWidth - 1;
            for (int p = 1; p <= kTilePadding; ++p) {
                const int dstLeftX = innerStartX - p;
                const int dstRightX = innerStartX + copyWidth - 1 + p;
                const int leftSrcIndex = (srcY * atlasWidth + leftSrcX) * 4;
                const int rightSrcIndex = (srcY * atlasWidth + rightSrcX) * 4;
                const int dstLeftIndex = (srcY * atlasWidth + dstLeftX) * 4;
                const int dstRightIndex = (srcY * atlasWidth + dstRightX) * 4;
                for (int c = 0; c < 4; ++c) {
                    atlasPixels[dstLeftIndex + c] = atlasPixels[leftSrcIndex + c];
                    atlasPixels[dstRightIndex + c] = atlasPixels[rightSrcIndex + c];
                }
            }
        }

        for (int x = -kTilePadding; x < copyWidth + kTilePadding; ++x) {
            const int srcX = innerStartX + std::clamp(x, 0, copyWidth - 1);
            const int topSrcY = innerStartY;
            const int bottomSrcY = innerStartY + copyHeight - 1;
            for (int p = 1; p <= kTilePadding; ++p) {
                const int dstTopY = innerStartY - p;
                const int dstBottomY = innerStartY + copyHeight - 1 + p;
                const int topSrcIndex = (topSrcY * atlasWidth + srcX) * 4;
                const int bottomSrcIndex = (bottomSrcY * atlasWidth + srcX) * 4;
                const int dstTopIndex = (dstTopY * atlasWidth + srcX) * 4;
                const int dstBottomIndex = (dstBottomY * atlasWidth + srcX) * 4;
                for (int c = 0; c < 4; ++c) {
                    atlasPixels[dstTopIndex + c] = atlasPixels[topSrcIndex + c];
                    atlasPixels[dstBottomIndex + c] = atlasPixels[bottomSrcIndex + c];
                }
            }
        }

        stbi_image_free(data);

        m_textures[texName] = i;
    }

    if (m_atlas.textureID != 0) {
        glDeleteTextures(1, &m_atlas.textureID);
        m_atlas.textureID = 0;
    }

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlasWidth, atlasHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlasPixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);

    // Guard atlas tiles from cross-bleeding on very low mip levels.
    const int fullChainMaxLevel = static_cast<int>(std::floor(std::log2(static_cast<float>(std::max(atlasWidth, atlasHeight)))));
    const int paddingSafeMaxLevel = (kTilePadding > 0)
        ? static_cast<int>(std::floor(std::log2(static_cast<float>(kTilePadding))))
        : 0;
    const int clampedMaxLevel = std::max(0, std::min(fullChainMaxLevel, paddingSafeMaxLevel));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, clampedMaxLevel);

    if (supportsAnisotropicFiltering()) {
        GLfloat maxAniso = 1.0f;
        glGetFloatv(kMaxTextureMaxAnisotropyPName, &maxAniso);
        m_atlasMaxAnisotropy = std::max(1.0f, static_cast<float>(maxAniso));
        m_atlasAnisotropy = std::clamp(m_atlasAnisotropy, 1.0f, m_atlasMaxAnisotropy);
        glTexParameterf(GL_TEXTURE_2D, kTextureMaxAnisotropyPName, m_atlasAnisotropy);
    } else {
        m_atlasMaxAnisotropy = 1.0f;
        m_atlasAnisotropy = 1.0f;
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    m_atlas.textureID = textureID;
    m_atlas.atlasWidth = atlasWidth;
    m_atlas.atlasHeight = atlasHeight;
    m_atlas.tileSize = tileSize;
    m_atlas.tileStride = tileStride;
    m_atlas.tilePadding = kTilePadding;
    m_atlas.tilesPerRow = tilesPerRow;
    m_blockAtlasPixels = std::move(atlasPixels);
}

const TextureAtlas & ResourceMgr::getAtlas() const {
    return m_atlas;
}

void ResourceMgr::buildTextureArray(const std::string &directory, int tileSize) {
    namespace fs = std::filesystem;

    if (m_textureArray.textureID != 0) {
        glDeleteTextures(1, &m_textureArray.textureID);
        m_textureArray.textureID = 0;
    }
    m_textureArrayLayers.clear();
    for (auto& [_, texture] : m_blockTextureCatalog) {
        texture.animation.firstLayer = 0;
        if (texture.animation.frameCount > 1) {
            texture.animation.isAnimated = false;
        }
    }

    std::vector<fs::path> imagePaths;

    if (fs::exists(directory)) {
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.path().extension() == ".png") {
                imagePaths.push_back(entry.path());
            }
        }
    }

    // Same sort order as buildTextureAtlas for consistent tile indices.
    std::sort(imagePaths.begin(), imagePaths.end(),
              [](const fs::path& a, const fs::path& b) {
                  return a.filename().string() < b.filename().string();
              });

    if (imagePaths.empty()) {
#ifdef MECRAFT_DEBUG
        MECRAFT_LOG_STREAM(std::cerr << "Texture Array generated with 0 images!\n");
#endif
        return;
    }

    std::vector<int> layersPerImage;
    layersPerImage.reserve(imagePaths.size());

    int numLayers = 0;
    for (const fs::path& imagePath : imagePaths) {
        const int layerCount = computeTextureArrayLayerCount(imagePath, tileSize, m_blockTextureCatalog);
        layersPerImage.push_back(layerCount);
        numLayers += layerCount;
    }
    if (numLayers > 1024) {
        throw std::runtime_error("Block texture array exceeds the 1024-layer vertex encoding limit");
    }

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureID);

    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8,
                 tileSize, tileSize, numLayers,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    stbi_set_flip_vertically_on_load(true);

    int currentLayer = 0;
    for (size_t imageIndex = 0; imageIndex < imagePaths.size(); ++imageIndex) {
        const fs::path& imagePath = imagePaths[imageIndex];
        const std::string textureName = imagePath.stem().string();
        m_textureArrayLayers[textureName] = currentLayer;

        auto catalogIt = m_blockTextureCatalog.find(textureName);
        const int declaredFrames = (catalogIt != m_blockTextureCatalog.end()) ? catalogIt->second.animation.frameCount : 1;
        const bool useAnimationFrames = layersPerImage[imageIndex] > 1;
        if (catalogIt != m_blockTextureCatalog.end()) {
            catalogIt->second.animation.firstLayer = currentLayer;
            catalogIt->second.animation.isAnimated = useAnimationFrames && catalogIt->second.animation.frameCount > 1;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* data = stbi_load(imagePath.string().c_str(), &width, &height, &channels, 4);

        if (!data) {
#ifdef MECRAFT_DEBUG
            MECRAFT_LOG_STREAM(std::cerr << "Failed to load image for texture array: " << imagePath << "\n");
#endif
            currentLayer += layersPerImage[imageIndex];
            continue;
        }

        const auto uploadLayer = [&](const int targetLayer, const unsigned char* srcPixels, const int srcWidth, const int srcHeight) {
            const int copyWidth = std::min(tileSize, srcWidth);
            const int copyHeight = std::min(tileSize, srcHeight);
            if (copyWidth == tileSize && copyHeight == tileSize) {
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, targetLayer,
                                tileSize, tileSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, srcPixels);
                return;
            }

            std::vector<unsigned char> padded(static_cast<size_t>(tileSize) * tileSize * 4, 0);
            for (int y = 0; y < copyHeight; ++y) {
                for (int x = 0; x < copyWidth; ++x) {
                    const int dstIdx = (y * tileSize + x) * 4;
                    const int srcIdx = (y * srcWidth + x) * 4;
                    padded[dstIdx + 0] = srcPixels[srcIdx + 0];
                    padded[dstIdx + 1] = srcPixels[srcIdx + 1];
                    padded[dstIdx + 2] = srcPixels[srcIdx + 2];
                    padded[dstIdx + 3] = srcPixels[srcIdx + 3];
                }
            }

            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, targetLayer,
                            tileSize, tileSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, padded.data());
        };

        if (useAnimationFrames) {
            if (width != tileSize || height != tileSize * declaredFrames) {
                stbi_image_free(data);
                throw std::runtime_error("Texture catalog dimensions do not match texture array source for " + imagePath.string());
            } else {
                const bool topFrameFirst = catalogIt != m_blockTextureCatalog.end() && catalogIt->second.topFrameFirst;
                for (int frame = 0; frame < declaredFrames; ++frame) {
                    const int flippedFrameIndex = topFrameFirst ? declaredFrames - 1 - frame : frame;
                    const unsigned char* framePixels = data + static_cast<size_t>(flippedFrameIndex * tileSize * width) * 4;
                    uploadLayer(currentLayer + frame, framePixels, width, tileSize);
                }
            }
        } else {
            if (width != tileSize || height != tileSize) {
#ifdef MECRAFT_DEBUG
                MECRAFT_LOG_STREAM(std::cerr << "Warning: Texture size mismatch in texture array! " << imagePath << "\n");
#endif
            }
            uploadLayer(currentLayer, data, width, height);
        }

        stbi_image_free(data);
        currentLayer += layersPerImage[imageIndex];
    }

    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

    if (supportsAnisotropicFiltering()) {
        GLfloat maxAniso = 1.0f;
        glGetFloatv(kMaxTextureMaxAnisotropyPName, &maxAniso);
        m_atlasMaxAnisotropy = std::max(1.0f, static_cast<float>(maxAniso));
        m_atlasAnisotropy = std::clamp(m_atlasAnisotropy, 1.0f, m_atlasMaxAnisotropy);
        glTexParameterf(GL_TEXTURE_2D_ARRAY, kTextureMaxAnisotropyPName, m_atlasAnisotropy);
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    m_textureArray.textureID = textureID;
    m_textureArray.tileSize = tileSize;
    m_textureArray.layerCount = numLayers;

    // Build the reverse mapping: TextureArray layer -> Atlas tile index.
    // imageIndex in the loop corresponds to the Atlas tile index (same sorted file list).
    // currentLayer is the TextureArray first layer for that image.
    m_arrayLayerToAtlasTile.clear();
    int mapLayer = 0;
    for (size_t mapIdx = 0; mapIdx < imagePaths.size(); ++mapIdx) {
        const int atlasTile = static_cast<int>(mapIdx);
        const int layerCount = layersPerImage[mapIdx];
        // Map all frames of this texture (including animated frames) back to the same atlas tile.
        for (int f = 0; f < layerCount; ++f) {
            m_arrayLayerToAtlasTile[mapLayer + f] = atlasTile;
        }
        mapLayer += layerCount;
    }
}

const TextureArray& ResourceMgr::getTextureArray() const {
    return m_textureArray;
}

void ResourceMgr::loadBlockTextureCatalog(const std::string& textureConfigPath) {
    m_blockTextureCatalog.clear();

    std::ifstream file(textureConfigPath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open block texture catalog: " + textureConfigPath);
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to parse block texture catalog: ") + e.what());
    }

    int tileSize = 16;
    const auto tileSizeIt = root.find("tileSize");
    if (tileSizeIt != root.end()) {
        if (!tileSizeIt->is_number_integer()) {
            throw std::runtime_error("block_textures.json tileSize must be an integer");
        }
        tileSize = tileSizeIt->get<int>();
        if (tileSize <= 0) {
            throw std::runtime_error("block_textures.json tileSize must be positive");
        }
    }

    const auto texturesIt = root.find("textures");
    if (texturesIt == root.end() || !texturesIt->is_array()) {
        throw std::runtime_error("block_textures.json must contain a textures array");
    }

    for (const auto& textureJson : *texturesIt) {
        if (!textureJson.is_object()) {
            throw std::runtime_error("block_textures.json texture entry must be an object");
        }

        const auto nameIt = textureJson.find("name");
        if (nameIt == textureJson.end() || !nameIt->is_string()) {
            throw std::runtime_error("block_textures.json texture entry requires a string name");
        }
        const std::string name = nameIt->get<std::string>();
        if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
            throw std::runtime_error("Invalid block texture name: " + name);
        }
        if (m_blockTextureCatalog.find(name) != m_blockTextureCatalog.end()) {
            throw std::runtime_error("Duplicate block texture catalog entry: " + name);
        }

        BlockTextureCatalogEntry entry;
        entry.tint = parseResourceTextureTint(textureJson);

        const auto framesIt = textureJson.find("frames");
        if (framesIt != textureJson.end()) {
            if (!framesIt->is_number_integer()) {
                throw std::runtime_error("block_textures.json frames must be an integer for " + name);
            }
            entry.animation.frameCount = framesIt->get<int>();
            if (entry.animation.frameCount <= 0 || entry.animation.frameCount > 63) {
                throw std::runtime_error("block_textures.json frames out of range for " + name);
            }
        }

        const auto fpsIt = textureJson.find("fps");
        if (fpsIt != textureJson.end()) {
            if (!fpsIt->is_number()) {
                throw std::runtime_error("block_textures.json fps must be numeric for " + name);
            }
            entry.animation.fps = fpsIt->get<float>();
            if (entry.animation.fps < 0.0f || entry.animation.fps > 63.0f) {
                throw std::runtime_error("block_textures.json fps out of range for " + name);
            }
        }

        if (entry.animation.frameCount > 1) {
            const auto layoutIt = textureJson.find("frameLayout");
            if (layoutIt == textureJson.end() || !layoutIt->is_string()) {
                throw std::runtime_error("Animated block texture requires frameLayout for " + name);
            }
            const std::string frameLayout = layoutIt->get<std::string>();
            if (frameLayout != "vertical") {
                throw std::runtime_error("Unsupported block texture frameLayout for " + name + ": " + frameLayout);
            }
            entry.verticalFrames = true;
            entry.animation.isAnimated = true;

            const auto frameOrderIt = textureJson.find("frameOrder");
            if (frameOrderIt == textureJson.end() || !frameOrderIt->is_string()) {
                throw std::runtime_error("Animated block texture requires frameOrder for " + name);
            }
            const std::string frameOrder = frameOrderIt->get<std::string>();
            if (frameOrder == "top_to_bottom") {
                entry.topFrameFirst = true;
            } else if (frameOrder == "bottom_to_top") {
                entry.topFrameFirst = false;
            } else {
                throw std::runtime_error("Unsupported block texture frameOrder for " + name + ": " + frameOrder);
            }
        }

        if (tileSize != 16) {
            throw std::runtime_error("Only 16px block texture tiles are currently supported");
        }

        m_blockTextureCatalog.emplace(name, entry);
    }
}

void ResourceMgr::preloadEntityTexturesFromConfig(const std::string& entitiesConfigPath) {
    std::ifstream file(entitiesConfigPath);
    if (!file.is_open()) {
        return;
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const std::exception&) {
        return;
    }

    const auto entitiesIt = root.find("entities");
    if (entitiesIt == root.end() || !entitiesIt->is_array()) {
        return;
    }

    std::unordered_set<std::string> loadedTextureKeys;
    for (const auto& entityJson : *entitiesIt) {
        if (!entityJson.is_object()) {
            continue;
        }

        const auto kindIt = entityJson.find("kind");
        if (kindIt != entityJson.end() && (!kindIt->is_string() || kindIt->get<std::string>() != "mob")) {
            continue;
        }

        const auto textureIt = entityJson.find("texture");
        if (textureIt == entityJson.end() || !textureIt->is_string()) {
            continue;
        }

        const std::string textureKey = textureIt->get<std::string>();
        if (textureKey.empty() ||
            textureKey.find('/') != std::string::npos ||
            textureKey.find('\\') != std::string::npos ||
            loadedTextureKeys.find(textureKey) != loadedTextureKeys.end()) {
            continue;
        }

        loadedTextureKeys.insert(textureKey);
        const std::string texturePath = std::string(MOBS_TEXTURE_DIR) + "/" + textureKey + ".png";
        loadGuiTexture(textureKey, texturePath, true);
    }
}

int ResourceMgr::getTextureArrayLayer(const std::string& name) const {
    const auto it = m_textureArrayLayers.find(name);
    if (it != m_textureArrayLayers.end()) {
        return it->second;
    }
    throw std::runtime_error("Unknown block texture array layer: " + name);
}

TextureAnimationInfo ResourceMgr::getTextureAnimation(const std::string& name) const {
    const auto catalogIt = m_blockTextureCatalog.find(name);
    if (catalogIt != m_blockTextureCatalog.end()) {
        TextureAnimationInfo info = catalogIt->second.animation;
        if (info.firstLayer == 0) {
            info.firstLayer = getTextureArrayLayer(name);
        }
        return info;
    }

    TextureAnimationInfo info;
    info.firstLayer = getTextureArrayLayer(name);
    info.frameCount = 1;
    info.fps = 0.0f;
    info.isAnimated = false;
    return info;
}

ResourceTextureTint ResourceMgr::getTextureTint(const std::string& name) const {
    const auto catalogIt = m_blockTextureCatalog.find(name);
    return catalogIt != m_blockTextureCatalog.end() ? catalogIt->second.tint : ResourceTextureTint::None;
}

int ResourceMgr::arrayLayerToAtlasTile(const int arrayLayer) const {
    const auto it = m_arrayLayerToAtlasTile.find(arrayLayer);
    if (it != m_arrayLayerToAtlasTile.end()) {
        return it->second;
    }
#ifdef MECRAFT_DEBUG
    MECRAFT_LOG_STREAM(std::cerr << "[ResourceMgr] arrayLayerToAtlasTile: unmapped layer " << arrayLayer << "\n");
#endif
    return -1;
}

namespace {
GLuint loadLightmapTexture(const std::string& path) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) {
#ifdef MECRAFT_DEBUG
        MECRAFT_LOG_STREAM(std::cerr << "Failed to load lightmap texture: " << path << "\n");
#endif
        return 0;
    }

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    return textureID;
}

GLuint loadColormapTexture(const std::string& path) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) {
#ifdef MECRAFT_DEBUG
        MECRAFT_LOG_STREAM(std::cerr << "Failed to load colormap texture: " << path << "\n");
#endif
        return 0;
    }

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    return textureID;
}

GLuint createWhiteColormapTexture() {
    constexpr unsigned char whitePixel[4] = {255, 255, 255, 255};

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
    glBindTexture(GL_TEXTURE_2D, 0);
    return textureID;
}
}

void ResourceMgr::loadLightmapTextures(const std::string& dayPath, const std::string& nightPath) {
    if (m_lightmapDay != 0) {
        glDeleteTextures(1, &m_lightmapDay);
        m_lightmapDay = 0;
    }
    if (m_lightmapNight != 0) {
        glDeleteTextures(1, &m_lightmapNight);
        m_lightmapNight = 0;
    }

    m_lightmapDay = loadLightmapTexture(dayPath);
    m_lightmapNight = loadLightmapTexture(nightPath);
}

GLuint ResourceMgr::getLightmapDay() const {
    return m_lightmapDay;
}

GLuint ResourceMgr::getLightmapNight() const {
    return m_lightmapNight;
}

void ResourceMgr::loadColormapTextures(const std::string& grassPath, const std::string& foliagePath) {
    if (m_grassColormap != 0) {
        glDeleteTextures(1, &m_grassColormap);
        m_grassColormap = 0;
    }
    if (m_foliageColormap != 0) {
        glDeleteTextures(1, &m_foliageColormap);
        m_foliageColormap = 0;
    }

    m_grassColormap = loadColormapTexture(grassPath);
    m_foliageColormap = loadColormapTexture(foliagePath);
    if (m_grassColormap == 0) {
        m_grassColormap = createWhiteColormapTexture();
    }
    if (m_foliageColormap == 0) {
        m_foliageColormap = createWhiteColormapTexture();
    }
}

GLuint ResourceMgr::getGrassColormap() const {
    return m_grassColormap;
}

GLuint ResourceMgr::getFoliageColormap() const {
    return m_foliageColormap;
}

GLuint ResourceMgr::loadCubemap(const std::string& name,
                                 const std::string& rightPath, const std::string& leftPath,
                                 const std::string& topPath, const std::string& bottomPath,
                                 const std::string& frontPath, const std::string& backPath) {
    auto existing = m_cubemaps.find(name);
    if (existing != m_cubemaps.end()) {
        return existing->second;
    }

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

    const std::string paths[6] = { rightPath, leftPath, topPath, bottomPath, frontPath, backPath };
    const GLenum faces[6] = {
        GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
    };

    stbi_set_flip_vertically_on_load(false);

    for (int i = 0; i < 6; ++i) {
        int width = 0, height = 0, channels = 0;
        unsigned char* data = stbi_load(paths[i].c_str(), &width, &height, &channels, 4);
        if (!data) {
#ifdef MECRAFT_DEBUG
            MECRAFT_LOG_STREAM(std::cerr << "Failed to load cubemap face: " << paths[i] << "\n");
#endif
            glDeleteTextures(1, &textureID);
            return 0;
        }
        glTexImage2D(faces[i], 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    m_cubemaps[name] = textureID;
    return textureID;
}

GLuint ResourceMgr::getCubemap(const std::string& name) const {
    auto it = m_cubemaps.find(name);
    if (it != m_cubemaps.end()) {
        return it->second;
    }
    return 0;
}

void ResourceMgr::buildBlockIconAtlas(int iconSize) {
    if (m_atlas.textureID == 0 || m_blockAtlasPixels.empty() || m_atlas.tileSize <= 0 || m_atlas.tilesPerRow <= 0) {
        return;
    }

    if (m_itemIconAtlas.textureID != 0) {
        glDeleteTextures(1, &m_itemIconAtlas.textureID);
        m_itemIconAtlas.textureID = 0;
    }

    m_itemIconAtlas = resource::buildBlockIconAtlas(iconSize, m_atlas, m_blockAtlasPixels, *this);
}

const TextureAtlas& ResourceMgr::getItemIconAtlas() const {
    return m_itemIconAtlas;
}

void ResourceMgr::buildItemTextureAtlas(const std::string& directory, int tileSize) {
    namespace fs = std::filesystem;
    std::vector<fs::path> imagePaths;
    m_itemTextureIndices.clear();

    if (fs::exists(directory)) {
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.path().extension() == ".png") {
                imagePaths.push_back(entry.path());
            }
        }
    }

    std::sort(imagePaths.begin(), imagePaths.end(),
              [](const fs::path& a, const fs::path& b) {
                  return a.filename().string() < b.filename().string();
              });

    if (imagePaths.empty()) {
#ifdef MECRAFT_DEBUG
        MECRAFT_LOG_STREAM(std::cerr << "Item texture atlas generated with 0 images!\n");
#endif
        return;
    }

    const int numTiles = static_cast<int>(imagePaths.size());
    const int tilesPerRow = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(numTiles))));
    const int numRows = static_cast<int>(std::ceil(static_cast<float>(numTiles) / static_cast<float>(tilesPerRow)));
    constexpr int kTilePadding = 1;
    const int tileStride = tileSize + kTilePadding * 2;
    const int atlasWidth = tilesPerRow * tileStride;
    const int atlasHeight = numRows * tileStride;
    std::vector<unsigned char> atlasPixels(static_cast<size_t>(atlasWidth) * static_cast<size_t>(atlasHeight) * 4, 0);

    stbi_set_flip_vertically_on_load(true);

    for (int i = 0; i < numTiles; ++i) {
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* data = stbi_load(imagePaths[i].string().c_str(), &width, &height, &channels, 4);
        if (!data) {
            continue;
        }

        const std::string textureName = imagePaths[i].stem().string();
        const ResourceTint tint = textureTintColor(getTextureTint(textureName));
        const int copyWidth = std::min(tileSize, width);
        const int copyHeight = std::min(tileSize, height);

        const int tileCol = i % tilesPerRow;
        const int tileRow = i / tilesPerRow;
        const int tileBaseX = tileCol * tileStride;
        const int tileBaseY = tileRow * tileStride;
        const int innerStartX = tileBaseX + kTilePadding;
        const int innerStartY = tileBaseY + kTilePadding;

        for (int y = 0; y < copyHeight; ++y) {
            for (int x = 0; x < copyWidth; ++x) {
                const int dstIndex = ((innerStartY + y) * atlasWidth + (innerStartX + x)) * 4;
                const int srcIndex = (y * width + x) * 4;
                atlasPixels[dstIndex + 0] = static_cast<unsigned char>(
                    std::round(std::clamp(static_cast<float>(data[srcIndex + 0]) * tint.r, 0.0f, 255.0f)));
                atlasPixels[dstIndex + 1] = static_cast<unsigned char>(
                    std::round(std::clamp(static_cast<float>(data[srcIndex + 1]) * tint.g, 0.0f, 255.0f)));
                atlasPixels[dstIndex + 2] = static_cast<unsigned char>(
                    std::round(std::clamp(static_cast<float>(data[srcIndex + 2]) * tint.b, 0.0f, 255.0f)));
                atlasPixels[dstIndex + 3] = data[srcIndex + 3];
            }
        }

        for (int y = 0; y < copyHeight; ++y) {
            const int srcY = innerStartY + y;
            const int leftSrcX = innerStartX;
            const int rightSrcX = innerStartX + copyWidth - 1;
            for (int p = 1; p <= kTilePadding; ++p) {
                const int dstLeftX = innerStartX - p;
                const int dstRightX = innerStartX + copyWidth - 1 + p;
                const int leftSrcIndex = (srcY * atlasWidth + leftSrcX) * 4;
                const int rightSrcIndex = (srcY * atlasWidth + rightSrcX) * 4;
                const int dstLeftIndex = (srcY * atlasWidth + dstLeftX) * 4;
                const int dstRightIndex = (srcY * atlasWidth + dstRightX) * 4;
                for (int c = 0; c < 4; ++c) {
                    atlasPixels[dstLeftIndex + c] = atlasPixels[leftSrcIndex + c];
                    atlasPixels[dstRightIndex + c] = atlasPixels[rightSrcIndex + c];
                }
            }
        }

        for (int x = -kTilePadding; x < copyWidth + kTilePadding; ++x) {
            const int srcX = innerStartX + std::clamp(x, 0, copyWidth - 1);
            const int topSrcY = innerStartY;
            const int bottomSrcY = innerStartY + copyHeight - 1;
            for (int p = 1; p <= kTilePadding; ++p) {
                const int dstTopY = innerStartY - p;
                const int dstBottomY = innerStartY + copyHeight - 1 + p;
                const int topSrcIndex = (topSrcY * atlasWidth + srcX) * 4;
                const int bottomSrcIndex = (bottomSrcY * atlasWidth + srcX) * 4;
                const int dstTopIndex = (dstTopY * atlasWidth + srcX) * 4;
                const int dstBottomIndex = (dstBottomY * atlasWidth + srcX) * 4;
                for (int c = 0; c < 4; ++c) {
                    atlasPixels[dstTopIndex + c] = atlasPixels[topSrcIndex + c];
                    atlasPixels[dstBottomIndex + c] = atlasPixels[bottomSrcIndex + c];
                }
            }
        }

        stbi_image_free(data);
        m_itemTextureIndices[textureName] = i;
    }

    if (m_itemTextureAtlas.textureID != 0) {
        glDeleteTextures(1, &m_itemTextureAtlas.textureID);
        m_itemTextureAtlas.textureID = 0;
    }

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlasWidth, atlasHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlasPixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_itemTextureAtlas.textureID = textureID;
    m_itemTextureAtlas.atlasWidth = atlasWidth;
    m_itemTextureAtlas.atlasHeight = atlasHeight;
    m_itemTextureAtlas.tileSize = tileSize;
    m_itemTextureAtlas.tileStride = tileStride;
    m_itemTextureAtlas.tilePadding = kTilePadding;
    m_itemTextureAtlas.tilesPerRow = tilesPerRow;
    m_itemAtlasPixels = std::move(atlasPixels);
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
    namespace fs = std::filesystem;
    std::vector<fs::path> imagePaths;
    m_hudIconIndices.clear();

    if (fs::exists(directory)) {
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.path().extension() == ".png") {
                imagePaths.push_back(entry.path());
            }
        }
    }

    std::sort(imagePaths.begin(), imagePaths.end(),
              [](const fs::path& a, const fs::path& b) {
                  return a.filename().string() < b.filename().string();
              });

    if (imagePaths.empty()) {
#ifdef MECRAFT_DEBUG
        MECRAFT_LOG_STREAM(std::cerr << "HUD icon atlas generated with 0 images!\n");
#endif
        return;
    }

    const int numTiles = static_cast<int>(imagePaths.size());
    // HUD icons are small (8x8), arrange in a single row for simplicity.
    const int tilesPerRow = numTiles;
    constexpr int kTilePadding = 0; // No padding needed — no mipmaps for HUD.
    const int tileStride = iconSize + kTilePadding * 2;
    const int atlasWidth = tilesPerRow * tileStride;
    const int atlasHeight = tileStride;
    std::vector<unsigned char> atlasPixels(static_cast<size_t>(atlasWidth) * static_cast<size_t>(atlasHeight) * 4, 0);

    stbi_set_flip_vertically_on_load(true);

    for (int i = 0; i < numTiles; ++i) {
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* data = stbi_load(imagePaths[i].string().c_str(), &width, &height, &channels, 4);
        if (!data) {
            continue;
        }

        const int copyWidth = std::min(iconSize, width);
        const int copyHeight = std::min(iconSize, height);

        const int innerStartX = i * tileStride + kTilePadding;
        const int innerStartY = kTilePadding;

        for (int y = 0; y < copyHeight; ++y) {
            for (int x = 0; x < copyWidth; ++x) {
                const int dstIndex = ((innerStartY + y) * atlasWidth + (innerStartX + x)) * 4;
                const int srcIndex = (y * width + x) * 4;
                atlasPixels[dstIndex + 0] = data[srcIndex + 0];
                atlasPixels[dstIndex + 1] = data[srcIndex + 1];
                atlasPixels[dstIndex + 2] = data[srcIndex + 2];
                atlasPixels[dstIndex + 3] = data[srcIndex + 3];
            }
        }

        stbi_image_free(data);
        m_hudIconIndices[imagePaths[i].stem().string()] = i;
    }

    if (m_hudIconAtlas.textureID != 0) {
        glDeleteTextures(1, &m_hudIconAtlas.textureID);
        m_hudIconAtlas.textureID = 0;
    }

    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlasWidth, atlasHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlasPixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    m_hudIconAtlas.textureID = textureID;
    m_hudIconAtlas.atlasWidth = atlasWidth;
    m_hudIconAtlas.atlasHeight = atlasHeight;
    m_hudIconAtlas.tileSize = iconSize;
    m_hudIconAtlas.tileStride = tileStride;
    m_hudIconAtlas.tilePadding = kTilePadding;
    m_hudIconAtlas.tilesPerRow = tilesPerRow;
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
    return m_atlasAnisotropy;
}

float ResourceMgr::getAtlasMaxAnisotropy() const {
    return m_atlasMaxAnisotropy;
}

void ResourceMgr::setAtlasAnisotropy(const float anisotropy) {
    const float clamped = std::clamp(anisotropy, 1.0f, std::max(1.0f, m_atlasMaxAnisotropy));
    if (std::abs(clamped - m_atlasAnisotropy) < 1e-4f) {
        return;
    }

    m_atlasAnisotropy = clamped;

    if (supportsAnisotropicFiltering()) {
        if (m_atlas.textureID != 0) {
            glBindTexture(GL_TEXTURE_2D, m_atlas.textureID);
            glTexParameterf(GL_TEXTURE_2D, kTextureMaxAnisotropyPName, m_atlasAnisotropy);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        if (m_textureArray.textureID != 0) {
            glBindTexture(GL_TEXTURE_2D_ARRAY, m_textureArray.textureID);
            glTexParameterf(GL_TEXTURE_2D_ARRAY, kTextureMaxAnisotropyPName, m_atlasAnisotropy);
            glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        }
    }
}
