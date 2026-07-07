#ifndef MECRAFT_TEXTURE_2D_LIBRARY_H
#define MECRAFT_TEXTURE_2D_LIBRARY_H

#include "renderer/rhi/RhiHandles.h"

#include <cstdint>
#include <string>
#include <unordered_map>

struct Texture2DInfo {
    RhiTextureHandle texture;
};

// Metadata for cached GUI textures and original image size.
struct GuiTextureInfo {
    RhiTextureHandle texture;
    int width = 0;
    int height = 0;
};

class Texture2DLibrary {
public:
    RhiTextureHandle load(const std::string& name,
                          const std::string& path,
                          bool srgb = false,
                          bool repeat = false,
                          bool linear = true,
                          bool flipVertically = false);

    [[nodiscard]] RhiTextureHandle getHandle(const std::string& name) const;

    RhiTextureHandle loadGui(const std::string& name,
                             const std::string& path,
                             bool flipVertically = true);

    RhiTextureHandle loadGui(const std::string& name,
                             const std::string& path,
                             int& outWidth,
                             int& outHeight,
                             bool flipVertically = true);

    [[nodiscard]] RhiTextureHandle getGuiHandle(const std::string& name) const;

    void shutdown();

private:
    std::unordered_map<std::string, Texture2DInfo> m_textures;
    std::unordered_map<std::string, GuiTextureInfo> m_guiTextures;
};

#endif // MECRAFT_TEXTURE_2D_LIBRARY_H
