#include "CubemapLibrary.h"

#include "../Diagnostics.h"
#include "../third_party/stb/stb_image.h"
#include "renderer/rhi/RhiTypes.h"
#include "renderer/rhi/gl/GlRhiTextureRegistry.h"

#include <glad/glad.h>

#include <cstdio>

namespace {

[[nodiscard]] RhiTextureHandle registerCubemapTexture(const GLuint textureID,
                                                      const int width,
                                                      const int height) {
    return renderer::rhi::gl::registerTexture({
        textureID,
        RhiTextureDimension::Cube,
        RhiTextureFormat::Rgba8Unorm,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        6,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled),
        false
    });
}

void deleteCubemapTexture(RhiTextureHandle& texture) {
    const GLuint textureID = static_cast<GLuint>(renderer::rhi::gl::textureId(texture));
    renderer::rhi::gl::unregisterTextureAndReset(texture);
    if (textureID != 0) {
        glDeleteTextures(1, &textureID);
    }
}

} // namespace

RhiTextureHandle CubemapLibrary::load(const std::string& name,
                                      const std::string& rightPath,
                                      const std::string& leftPath,
                                      const std::string& topPath,
                                      const std::string& bottomPath,
                                      const std::string& frontPath,
                                      const std::string& backPath) {
    const auto existing = m_cubemaps.find(name);
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

    int cubemapWidth = 0;
    int cubemapHeight = 0;

    for (int i = 0; i < 6; ++i) {
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* data = stbi_load(paths[i].c_str(), &width, &height, &channels, 4);
        if (!data || width <= 0 || height <= 0) {
            if (data != nullptr) {
                stbi_image_free(data);
            }
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            glDeleteTextures(1, &textureID);
            MECRAFT_LOG_FPRINTF(stderr, "[Resource] Failed to load cubemap face '%s': %s\n",
                                name.c_str(), paths[i].c_str());
            return {};
        }
        if (i == 0) {
            cubemapWidth = width;
            cubemapHeight = height;
        } else if (width != cubemapWidth || height != cubemapHeight) {
            stbi_image_free(data);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            glDeleteTextures(1, &textureID);
            MECRAFT_LOG_FPRINTF(stderr, "[Resource] Cubemap face size mismatch '%s': %s\n",
                                name.c_str(), paths[i].c_str());
            return {};
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

    RhiTextureHandle texture = registerCubemapTexture(textureID, cubemapWidth, cubemapHeight);
    if (!texture.isValid()) {
        glDeleteTextures(1, &textureID);
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Failed to register cubemap RHI handle '%s'\n",
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
    for (auto& [_, texture] : m_cubemaps) {
        deleteCubemapTexture(texture);
    }
    m_cubemaps.clear();
}
