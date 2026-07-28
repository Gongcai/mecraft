#include "EnvironmentTextureLibrary.h"

#include "../Diagnostics.h"
#include "../third_party/stb/stb_image.h"
#include "renderer/rhi/RhiDevice.h"

#include <cassert>
#include <cstdio>

void EnvironmentTextureLibrary::init(RhiDevice& rhiDevice) {
    assert(m_rhiDevice == nullptr);
    m_rhiDevice = &rhiDevice;
}

RhiTextureHandle EnvironmentTextureLibrary::loadTexture(const std::string& path,
                                                         const char* label) const {
    assert(m_rhiDevice != nullptr);
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data || width <= 0 || height <= 0) {
        if (data != nullptr) {
            stbi_image_free(data);
        }
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Failed to load %s texture: %s\n", label, path.c_str());
        return {};
    }

    RhiTextureDesc textureDesc;
    textureDesc.debugName = label;
    textureDesc.dimension = RhiTextureDimension::Texture2D;
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = static_cast<uint32_t>(width);
    textureDesc.height = static_cast<uint32_t>(height);
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                        rhiFlag(RhiTextureUsage::TransferDst);
    textureDesc.memoryCategory = RhiMemoryCategory::Texture;
    RhiTextureInitialData initialData;
    initialData.pixels = data;
    initialData.sizeBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    initialData.finalState = RhiResourceState::ShaderRead;
    RhiTextureHandle texture = m_rhiDevice->createTexture(textureDesc, &initialData);
    stbi_image_free(data);
    if (!texture.isValid()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Failed to create %s RHI texture: %s\n",
                            label, path.c_str());
        return {};
    }

    return texture;
}

void EnvironmentTextureLibrary::deleteTexture(RhiTextureHandle& texture) const {
    assert(m_rhiDevice != nullptr);
    if (texture.isValid()) {
        m_rhiDevice->destroyTexture(texture);
        texture = {};
    }
}

void EnvironmentTextureLibrary::loadLightmaps(const std::string& dayPath, const std::string& nightPath) {
    deleteTexture(m_lightmapDay);
    deleteTexture(m_lightmapNight);

    m_lightmapDay = loadTexture(dayPath, "Lightmap.Day");
    m_lightmapNight = loadTexture(nightPath, "Lightmap.Night");
}

void EnvironmentTextureLibrary::loadColormaps(const std::string& grassPath, const std::string& foliagePath) {
    deleteTexture(m_grassColormap);
    deleteTexture(m_foliageColormap);

    m_grassColormap = loadTexture(grassPath, "Colormap.Grass");
    m_foliageColormap = loadTexture(foliagePath, "Colormap.Foliage");
}

RhiTextureHandle EnvironmentTextureLibrary::getLightmapDay() const {
    return m_lightmapDay;
}

RhiTextureHandle EnvironmentTextureLibrary::getLightmapNight() const {
    return m_lightmapNight;
}

RhiTextureHandle EnvironmentTextureLibrary::getGrassColormap() const {
    return m_grassColormap;
}

RhiTextureHandle EnvironmentTextureLibrary::getFoliageColormap() const {
    return m_foliageColormap;
}

void EnvironmentTextureLibrary::shutdown() {
    deleteTexture(m_lightmapDay);
    deleteTexture(m_lightmapNight);
    deleteTexture(m_grassColormap);
    deleteTexture(m_foliageColormap);
    m_rhiDevice = nullptr;
}
