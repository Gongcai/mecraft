#include "EnvironmentTextureLibrary.h"

#include "../Diagnostics.h"
#include "../third_party/stb/stb_image.h"

#include <cstdio>

GLuint EnvironmentTextureLibrary::loadLinearTexture(const std::string& path, const char* label) {
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

void EnvironmentTextureLibrary::deleteTexture(GLuint& texture) {
    if (texture != 0) {
        glDeleteTextures(1, &texture);
        texture = 0;
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

GLuint EnvironmentTextureLibrary::getLightmapDay() const {
    return m_lightmapDay;
}

GLuint EnvironmentTextureLibrary::getLightmapNight() const {
    return m_lightmapNight;
}

GLuint EnvironmentTextureLibrary::getGrassColormap() const {
    return m_grassColormap;
}

GLuint EnvironmentTextureLibrary::getFoliageColormap() const {
    return m_foliageColormap;
}

void EnvironmentTextureLibrary::shutdown() {
    deleteTexture(m_lightmapDay);
    deleteTexture(m_lightmapNight);
    deleteTexture(m_grassColormap);
    deleteTexture(m_foliageColormap);
}
