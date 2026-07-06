#ifndef MECRAFT_CUBEMAP_LIBRARY_H
#define MECRAFT_CUBEMAP_LIBRARY_H

#include <cstdint>
#include <string>
#include <unordered_map>

class CubemapLibrary {
public:
    uint32_t load(const std::string& name,
                  const std::string& rightPath,
                  const std::string& leftPath,
                  const std::string& topPath,
                  const std::string& bottomPath,
                  const std::string& frontPath,
                  const std::string& backPath);

    [[nodiscard]] uint32_t get(const std::string& name) const;
    void shutdown();

private:
    std::unordered_map<std::string, uint32_t> m_cubemaps;
};

#endif // MECRAFT_CUBEMAP_LIBRARY_H
