#include "EntityTexturePreloader.h"

#include "Paths.h"
#include "ResourceMgr.h"
#include "../Diagnostics.h"

#include <cstdio>
#include <fstream>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace resource {

bool preloadEntityTexturesFromConfig(ResourceMgr& resourceMgr, const std::string& entitiesConfigPath) {
    std::ifstream file(entitiesConfigPath);
    if (!file.is_open()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Failed to open entity config for texture preload: %s\n",
                            entitiesConfigPath.c_str());
        return false;
    }

    nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Failed to parse entity config for texture preload: invalid JSON\n");
        return false;
    }

    const auto entitiesIt = root.find("entities");
    if (entitiesIt == root.end() || !entitiesIt->is_array()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Entity config must contain an entities array for texture preload\n");
        return false;
    }

    std::unordered_set<std::string> loadedTextureKeys;
    for (const auto& entityJson : *entitiesIt) {
        if (!entityJson.is_object()) {
            MECRAFT_LOG_FPRINTF(stderr, "[Resource] Entity config entry must be an object\n");
            return false;
        }

        const auto kindIt = entityJson.find("kind");
        if (kindIt != entityJson.end() && (!kindIt->is_string() || kindIt->get<std::string>() != "mob")) {
            continue;
        }

        const auto textureIt = entityJson.find("texture");
        if (textureIt == entityJson.end() || !textureIt->is_string()) {
            MECRAFT_LOG_FPRINTF(stderr, "[Resource] Mob entity config entry requires a string texture\n");
            return false;
        }

        const std::string textureKey = textureIt->get<std::string>();
        if (textureKey.empty() || textureKey.find('/') != std::string::npos ||
            textureKey.find('\\') != std::string::npos) {
            MECRAFT_LOG_FPRINTF(stderr, "[Resource] Invalid mob entity texture key: %s\n", textureKey.c_str());
            return false;
        }
        if (loadedTextureKeys.find(textureKey) != loadedTextureKeys.end()) {
            continue;
        }

        loadedTextureKeys.insert(textureKey);
        const std::string texturePath = std::string(MOBS_TEXTURE_DIR) + "/" + textureKey + ".png";
        resourceMgr.loadGuiTexture(textureKey, texturePath, true);
    }
    return true;
}

} // namespace resource
