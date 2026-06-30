#ifndef MECRAFT_TEXTURE_2D_LIBRARY_H
#define MECRAFT_TEXTURE_2D_LIBRARY_H

#include <glad/glad.h>

#include <string>
#include <unordered_map>

// Metadata for cached GUI textures: store GL id plus original image size.
struct GuiTextureInfo {
    GLuint textureID = 0;
    int width = 0;
    int height = 0;
};

class Texture2DLibrary {
public:
    GLuint load(const std::string& name,
                const std::string& path,
                bool srgb = false,
                bool repeat = false,
                bool linear = true,
                bool flipVertically = false);

    [[nodiscard]] GLuint get(const std::string& name) const;

    GLuint loadGui(const std::string& name,
                   const std::string& path,
                   bool flipVertically = true);

    GLuint loadGui(const std::string& name,
                   const std::string& path,
                   int& outWidth,
                   int& outHeight,
                   bool flipVertically = true);

    [[nodiscard]] GLuint getGui(const std::string& name) const;

    void shutdown();

private:
    std::unordered_map<std::string, GLuint> m_textures;
    std::unordered_map<std::string, GuiTextureInfo> m_guiTextures;
};

#endif // MECRAFT_TEXTURE_2D_LIBRARY_H
