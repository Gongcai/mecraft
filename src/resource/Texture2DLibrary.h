#ifndef MECRAFT_TEXTURE_2D_LIBRARY_H
#define MECRAFT_TEXTURE_2D_LIBRARY_H

#include <cstdint>
#include <string>
#include <unordered_map>

// Metadata for cached GUI textures: store native texture id plus original image size.
struct GuiTextureInfo {
    uint32_t textureID = 0;
    int width = 0;
    int height = 0;
};

class Texture2DLibrary {
public:
    uint32_t load(const std::string& name,
                  const std::string& path,
                  bool srgb = false,
                  bool repeat = false,
                  bool linear = true,
                  bool flipVertically = false);

    [[nodiscard]] uint32_t get(const std::string& name) const;

    uint32_t loadGui(const std::string& name,
                     const std::string& path,
                     bool flipVertically = true);

    uint32_t loadGui(const std::string& name,
                     const std::string& path,
                     int& outWidth,
                     int& outHeight,
                     bool flipVertically = true);

    [[nodiscard]] uint32_t getGui(const std::string& name) const;

    void shutdown();

private:
    std::unordered_map<std::string, uint32_t> m_textures;
    std::unordered_map<std::string, GuiTextureInfo> m_guiTextures;
};

#endif // MECRAFT_TEXTURE_2D_LIBRARY_H
