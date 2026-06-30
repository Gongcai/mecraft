#ifndef MECRAFT_ENVIRONMENT_TEXTURE_LIBRARY_H
#define MECRAFT_ENVIRONMENT_TEXTURE_LIBRARY_H

#include <glad/glad.h>

#include <string>

class EnvironmentTextureLibrary {
public:
    void loadLightmaps(const std::string& dayPath, const std::string& nightPath);
    void loadColormaps(const std::string& grassPath, const std::string& foliagePath);

    [[nodiscard]] GLuint getLightmapDay() const;
    [[nodiscard]] GLuint getLightmapNight() const;
    [[nodiscard]] GLuint getGrassColormap() const;
    [[nodiscard]] GLuint getFoliageColormap() const;

    void shutdown();

private:
    static GLuint loadLinearTexture(const std::string& path, const char* label);
    static void deleteTexture(GLuint& texture);

    GLuint m_lightmapDay = 0;
    GLuint m_lightmapNight = 0;
    GLuint m_grassColormap = 0;
    GLuint m_foliageColormap = 0;
};

#endif // MECRAFT_ENVIRONMENT_TEXTURE_LIBRARY_H
