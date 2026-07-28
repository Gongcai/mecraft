#include "Texture2DLibrary.h"

#include "../Diagnostics.h"
#include "renderer/rhi/RhiDevice.h"
#include "../third_party/stb/stb_image.h"

#include <cassert>
#include <cstdio>

namespace {

[[nodiscard]] RhiTextureHandle createTexture2D(RhiDevice& rhiDevice,
                                               const char* debugName,
                                               const RhiTextureFormat format,
                                               const int width,
                                               const int height,
                                               const unsigned char* pixels) {
    RhiTextureDesc textureDesc;
    textureDesc.debugName = debugName;
    textureDesc.dimension = RhiTextureDimension::Texture2D;
    textureDesc.format = format;
    textureDesc.width = static_cast<uint32_t>(width);
    textureDesc.height = static_cast<uint32_t>(height);
    textureDesc.depthOrLayers = 1;
    textureDesc.mipLevels = 1;
    textureDesc.sampleCount = 1;
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                        rhiFlag(RhiTextureUsage::TransferDst);
    textureDesc.memoryCategory = RhiMemoryCategory::Texture;

    RhiTextureInitialData initialData;
    initialData.pixels = pixels;
    initialData.sizeBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    initialData.finalState = RhiResourceState::ShaderRead;
    return rhiDevice.createTexture(textureDesc, &initialData);
}

} // namespace

void Texture2DLibrary::init(RhiDevice& rhiDevice) {
    assert(m_rhiDevice == nullptr);
    m_rhiDevice = &rhiDevice;
}

RhiTextureHandle Texture2DLibrary::load(const std::string& name,
                                        const std::string& path,
                                        const bool srgb,
                                        const bool flipVertically) {
    assert(m_rhiDevice != nullptr);
    const auto existing = m_textures.find(name);
    if (existing != m_textures.end()) {
        return existing->second.texture;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data || width <= 0 || height <= 0) {
        if (data != nullptr) {
            stbi_image_free(data);
        }
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Failed to load texture2D '%s': %s\n",
                            name.c_str(), path.c_str());
        return {};
    }

    Texture2DInfo info;
    info.texture = createTexture2D(*m_rhiDevice,
                                   name.c_str(),
                                   srgb ? RhiTextureFormat::Rgba8Srgb : RhiTextureFormat::Rgba8Unorm,
                                   width,
                                   height,
                                   data);
    stbi_image_free(data);
    if (!info.texture.isValid()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Failed to create texture2D RHI resource '%s': %s\n",
                            name.c_str(), path.c_str());
        return {};
    }

    m_textures[name] = info;
    return info.texture;
}

RhiTextureHandle Texture2DLibrary::getHandle(const std::string& name) const {
    const auto it = m_textures.find(name);
    if (it != m_textures.end()) {
        return it->second.texture;
    }
    return {};
}

RhiTextureHandle Texture2DLibrary::loadGui(const std::string& name,
                                           const std::string& path,
                                           const bool flipVertically) {
    int dummyW = 0;
    int dummyH = 0;
    return loadGui(name, path, dummyW, dummyH, flipVertically);
}

RhiTextureHandle Texture2DLibrary::loadGui(const std::string& name,
                                           const std::string& path,
                                           int& outWidth,
                                           int& outHeight,
                                           const bool flipVertically) {
    assert(m_rhiDevice != nullptr);
    const auto existing = m_guiTextures.find(name);
    if (existing != m_guiTextures.end()) {
        outWidth = existing->second.width;
        outHeight = existing->second.height;
        return existing->second.texture;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data || width <= 0 || height <= 0) {
        if (data != nullptr) {
            stbi_image_free(data);
        }
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Failed to load GUI texture '%s': %s\n",
                            name.c_str(), path.c_str());
        outWidth = 0;
        outHeight = 0;
        return {};
    }

    outWidth = width;
    outHeight = height;

    GuiTextureInfo info;
    info.texture = createTexture2D(*m_rhiDevice,
                                   name.c_str(),
                                   RhiTextureFormat::Rgba8Unorm,
                                   width,
                                   height,
                                   data);
    stbi_image_free(data);
    if (!info.texture.isValid()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Failed to create GUI texture RHI resource '%s': %s\n",
                            name.c_str(), path.c_str());
        outWidth = 0;
        outHeight = 0;
        return {};
    }
    info.width = width;
    info.height = height;
    m_guiTextures[name] = info;
    return info.texture;
}

RhiTextureHandle Texture2DLibrary::getGuiHandle(const std::string& name) const {
    const auto it = m_guiTextures.find(name);
    if (it != m_guiTextures.end()) {
        return it->second.texture;
    }
    return {};
}

void Texture2DLibrary::shutdown() {
    assert(m_rhiDevice != nullptr);
    for (auto& [_, texInfo] : m_guiTextures) {
        m_rhiDevice->destroyTexture(texInfo.texture);
    }
    m_guiTextures.clear();

    for (auto& [_, texInfo] : m_textures) {
        m_rhiDevice->destroyTexture(texInfo.texture);
    }
    m_textures.clear();
    m_rhiDevice = nullptr;
}
