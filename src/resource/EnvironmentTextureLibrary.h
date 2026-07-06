#ifndef MECRAFT_ENVIRONMENT_TEXTURE_LIBRARY_H
#define MECRAFT_ENVIRONMENT_TEXTURE_LIBRARY_H

#include <cstdint>
#include <string>

class EnvironmentTextureLibrary {
public:
    void loadLightmaps(const std::string& dayPath, const std::string& nightPath);
    void loadColormaps(const std::string& grassPath, const std::string& foliagePath);

    [[nodiscard]] uint32_t getLightmapDay() const;
    [[nodiscard]] uint32_t getLightmapNight() const;
    [[nodiscard]] uint32_t getGrassColormap() const;
    [[nodiscard]] uint32_t getFoliageColormap() const;

    void shutdown();

private:
    static uint32_t loadLinearTexture(const std::string& path, const char* label);
    static void deleteTexture(uint32_t& texture);

    uint32_t m_lightmapDay = 0;
    uint32_t m_lightmapNight = 0;
    uint32_t m_grassColormap = 0;
    uint32_t m_foliageColormap = 0;
};

#endif // MECRAFT_ENVIRONMENT_TEXTURE_LIBRARY_H
