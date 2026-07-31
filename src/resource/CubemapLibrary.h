#ifndef MECRAFT_CUBEMAP_LIBRARY_H
#define MECRAFT_CUBEMAP_LIBRARY_H

#include "renderer/rhi/RhiHandles.h"

#include <string>
#include <unordered_map>

class RhiDevice;

class CubemapLibrary {
public:
    void init(RhiDevice& rhiDevice);

    RhiTextureHandle load(const std::string& name, const std::string& rightPath, const std::string& leftPath,
                          const std::string& topPath, const std::string& bottomPath, const std::string& frontPath,
                          const std::string& backPath);

    [[nodiscard]] RhiTextureHandle get(const std::string& name) const;
    void shutdown();

private:
    RhiDevice* m_rhiDevice = nullptr;
    std::unordered_map<std::string, RhiTextureHandle> m_cubemaps;
};

#endif // MECRAFT_CUBEMAP_LIBRARY_H
