#ifndef MECRAFT_BLOCK_TEXTURE_SOURCE_SET_H
#define MECRAFT_BLOCK_TEXTURE_SOURCE_SET_H

#include <string>
#include <vector>

namespace resource {

struct BlockTextureSourceSet {
    std::vector<std::string> textureDirectories;
    std::vector<std::string> connectedTextureDirectories;
};

} // namespace resource

#endif // MECRAFT_BLOCK_TEXTURE_SOURCE_SET_H
