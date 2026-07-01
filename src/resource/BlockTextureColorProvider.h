#ifndef MECRAFT_BLOCK_TEXTURE_COLOR_PROVIDER_H
#define MECRAFT_BLOCK_TEXTURE_COLOR_PROVIDER_H

#include <glm/vec3.hpp>

class IBlockTextureColorProvider {
public:
    virtual ~IBlockTextureColorProvider() = default;

    [[nodiscard]] virtual const glm::vec3& blockTextureAverageColor(int arrayLayer) const = 0;
};

#endif // MECRAFT_BLOCK_TEXTURE_COLOR_PROVIDER_H
