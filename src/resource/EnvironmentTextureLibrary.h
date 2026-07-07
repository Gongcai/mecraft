#ifndef MECRAFT_ENVIRONMENT_TEXTURE_LIBRARY_H
#define MECRAFT_ENVIRONMENT_TEXTURE_LIBRARY_H

#include "renderer/rhi/RhiHandles.h"

#include <string>

class EnvironmentTextureLibrary {
public:
    void loadLightmaps(const std::string& dayPath, const std::string& nightPath);
    void loadColormaps(const std::string& grassPath, const std::string& foliagePath);

    [[nodiscard]] RhiTextureHandle getLightmapDay() const;
    [[nodiscard]] RhiTextureHandle getLightmapNight() const;
    [[nodiscard]] RhiTextureHandle getGrassColormap() const;
    [[nodiscard]] RhiTextureHandle getFoliageColormap() const;

    void shutdown();

private:
    static RhiTextureHandle loadLinearTexture(const std::string& path, const char* label);
    static void deleteTexture(RhiTextureHandle& texture);

    RhiTextureHandle m_lightmapDay;
    RhiTextureHandle m_lightmapNight;
    RhiTextureHandle m_grassColormap;
    RhiTextureHandle m_foliageColormap;
};

#endif // MECRAFT_ENVIRONMENT_TEXTURE_LIBRARY_H
