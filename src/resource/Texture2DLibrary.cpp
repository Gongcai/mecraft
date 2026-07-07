#include "Texture2DLibrary.h"

#include "../Diagnostics.h"
#include "renderer/rhi/gl/GlRhiTextureRegistry.h"
#include "../third_party/stb/stb_image.h"

#include <glad/glad.h>

#include <cstdio>

namespace {

[[nodiscard]] RhiTextureHandle registerTexture2D(const uint32_t textureID,
                                                 const RhiTextureFormat format,
                                                 const int width,
                                                 const int height) {
    return renderer::rhi::gl::registerTexture({
        textureID,
        RhiTextureDimension::Texture2D,
        format,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled),
        false
    });
}

void deleteRegisteredTexture(RhiTextureHandle& texture) {
    const GLuint textureId = static_cast<GLuint>(renderer::rhi::gl::textureId(texture));
    renderer::rhi::gl::unregisterTextureAndReset(texture);
    if (textureId != 0) {
        glDeleteTextures(1, &textureId);
    }
}

} // namespace

RhiTextureHandle Texture2DLibrary::load(const std::string& name,
                                        const std::string& path,
                                        const bool srgb,
                                        const bool repeat,
                                        const bool linear,
                                        const bool flipVertically) {
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

    Texture2DInfo info;
    info.texture = registerTexture2D(textureID,
                                     srgb ? RhiTextureFormat::Rgba8Srgb : RhiTextureFormat::Rgba8Unorm,
                                     width,
                                     height);
    if (!info.texture.isValid()) {
        glDeleteTextures(1, &textureID);
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Failed to register texture2D RHI handle '%s': %s\n",
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
    info.texture = registerTexture2D(textureID, RhiTextureFormat::Rgba8Unorm, width, height);
    if (!info.texture.isValid()) {
        glDeleteTextures(1, &textureID);
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Failed to register GUI texture RHI handle '%s': %s\n",
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
    for (auto& [_, texInfo] : m_guiTextures) {
        deleteRegisteredTexture(texInfo.texture);
    }
    m_guiTextures.clear();

    for (auto& [_, texInfo] : m_textures) {
        deleteRegisteredTexture(texInfo.texture);
    }
    m_textures.clear();
}
