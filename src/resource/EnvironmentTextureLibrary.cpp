#include "EnvironmentTextureLibrary.h"

#include "../Diagnostics.h"
#include "../third_party/stb/stb_image.h"
#include "renderer/rhi/RhiTypes.h"
#include "renderer/rhi/gl/GlRhiTextureRegistry.h"

#include <glad/glad.h>

#include <cstdio>

namespace {

[[nodiscard]] RhiTextureHandle registerLinearTexture(const GLuint textureID,
                                                     const int width,
                                                     const int height) {
    return renderer::rhi::gl::registerTexture({
        textureID,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba8Unorm,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled),
        false
    });
}

} // namespace

RhiTextureHandle EnvironmentTextureLibrary::loadLinearTexture(const std::string& path, const char* label) {
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
    RhiTextureHandle texture = registerLinearTexture(textureID, width, height);
    if (!texture.isValid()) {
        glDeleteTextures(1, &textureID);
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Failed to register %s texture RHI handle: %s\n",
                            label, path.c_str());
        return {};
    }

    return texture;
}

void EnvironmentTextureLibrary::deleteTexture(RhiTextureHandle& texture) {
    const GLuint textureID = static_cast<GLuint>(renderer::rhi::gl::textureId(texture));
    renderer::rhi::gl::unregisterTextureAndReset(texture);
    if (textureID != 0) {
        glDeleteTextures(1, &textureID);
    }
}

void EnvironmentTextureLibrary::loadLightmaps(const std::string& dayPath, const std::string& nightPath) {
    deleteTexture(m_lightmapDay);
    deleteTexture(m_lightmapNight);

    m_lightmapDay = loadLinearTexture(dayPath, "lightmap");
    m_lightmapNight = loadLinearTexture(nightPath, "lightmap");
}

void EnvironmentTextureLibrary::loadColormaps(const std::string& grassPath, const std::string& foliagePath) {
    deleteTexture(m_grassColormap);
    deleteTexture(m_foliageColormap);

    m_grassColormap = loadLinearTexture(grassPath, "grass colormap");
    m_foliageColormap = loadLinearTexture(foliagePath, "foliage colormap");
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
}
