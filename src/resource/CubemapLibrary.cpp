#include "CubemapLibrary.h"

#include "../Diagnostics.h"
#include "../third_party/stb/stb_image.h"
#include "renderer/rhi/RhiDevice.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

void CubemapLibrary::init(RhiDevice& rhiDevice) {
    assert(m_rhiDevice == nullptr);
    m_rhiDevice = &rhiDevice;
}

RhiTextureHandle CubemapLibrary::load(const std::string& name,
                                      const std::string& rightPath,
                                      const std::string& leftPath,
                                      const std::string& topPath,
                                      const std::string& bottomPath,
                                      const std::string& frontPath,
                                      const std::string& backPath) {
    assert(m_rhiDevice != nullptr);
    const auto existing = m_cubemaps.find(name);
    if (existing != m_cubemaps.end()) {
        return existing->second;
    }

    const std::array<std::string, 6> paths = {
        rightPath, leftPath, topPath, bottomPath, frontPath, backPath
    };

    stbi_set_flip_vertically_on_load(false);

    int cubemapWidth = 0;
    int cubemapHeight = 0;
    std::vector<unsigned char> pixels;

    for (int i = 0; i < 6; ++i) {
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* data = stbi_load(paths[i].c_str(), &width, &height, &channels, 4);
        if (!data || width <= 0 || height <= 0) {
            if (data != nullptr) {
                stbi_image_free(data);
            }
            MECRAFT_LOG_FPRINTF(stderr, "[Resource] Failed to load cubemap face '%s': %s\n",
                                name.c_str(), paths[i].c_str());
            return {};
        }
        if (i == 0) {
            cubemapWidth = width;
            cubemapHeight = height;
        } else if (width != cubemapWidth || height != cubemapHeight) {
            stbi_image_free(data);
            MECRAFT_LOG_FPRINTF(stderr, "[Resource] Cubemap face size mismatch '%s': %s\n",
                                name.c_str(), paths[i].c_str());
            return {};
        }
        const size_t faceSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
        pixels.insert(pixels.end(), data, data + faceSize);
        stbi_image_free(data);
    }

    RhiTextureDesc textureDesc;
    textureDesc.debugName = name.c_str();
    textureDesc.dimension = RhiTextureDimension::Cube;
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = static_cast<uint32_t>(cubemapWidth);
    textureDesc.height = static_cast<uint32_t>(cubemapHeight);
    textureDesc.depthOrLayers = 6u;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                        rhiFlag(RhiTextureUsage::TransferDst);
    RhiTextureInitialData initialData;
    initialData.pixels = pixels.data();
    initialData.sizeBytes = pixels.size();
    initialData.layerCount = 6u;
    initialData.finalState = RhiResourceState::ShaderRead;
    RhiTextureHandle texture = m_rhiDevice->createTexture(textureDesc, &initialData);
    if (!texture.isValid()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Failed to create cubemap RHI resource '%s'\n",
                            name.c_str());
        return {};
    }

    m_cubemaps[name] = texture;
    return texture;
}

RhiTextureHandle CubemapLibrary::get(const std::string& name) const {
    const auto it = m_cubemaps.find(name);
    if (it != m_cubemaps.end()) {
        return it->second;
    }
    return {};
}

void CubemapLibrary::shutdown() {
    assert(m_rhiDevice != nullptr);
    for (auto& [_, texture] : m_cubemaps) {
        m_rhiDevice->destroyTexture(texture);
    }
    m_cubemaps.clear();
    m_rhiDevice = nullptr;
}
