#ifndef MECRAFT_CONNECTED_TEXTURE_PACK_H
#define MECRAFT_CONNECTED_TEXTURE_PACK_H

#include "BlockTextureManifest.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace resource {

enum class ConnectedTextureMethod {
    Ctm,
    Fixed,
    Horizontal,
    Random,
    Repeat,
    Vertical,
};

struct ConnectedTextureReplacement {
    std::string matchTextureName;
    std::filesystem::path albedoPath;
    std::optional<std::filesystem::path> normalPath;
    std::optional<std::filesystem::path> specularPath;
    std::optional<BlockTextureAnimationMetadata> animationMetadata;
    ConnectedTextureMethod method = ConnectedTextureMethod::Fixed;
};

[[nodiscard]] std::vector<ConnectedTextureReplacement> collectConnectedTextureReplacements(
    const std::vector<std::string>& directories);

} // namespace resource

#endif // MECRAFT_CONNECTED_TEXTURE_PACK_H
