#include "EntityTexturePreloader.h"

#include "Paths.h"
#include "ResourceMgr.h"

#include <fstream>
#include <stdexcept>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace resource {

void preloadEntityTexturesFromConfig(ResourceMgr& resourceMgr, const std::string& entitiesConfigPath) {
    std::ifstream file(entitiesConfigPath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open entity config for texture preload: " + entitiesConfigPath);
    }

    nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded()) {
        throw std::runtime_error("Failed to parse entity config for texture preload: invalid JSON");
    }

    const auto entitiesIt = root.find("entities");
    if (entitiesIt == root.end() || !entitiesIt->is_array()) {
        throw std::runtime_error("Entity config must contain an entities array for texture preload");
    }

    std::unordered_set<std::string> loadedTextureKeys;
    for (const auto& entityJson : *entitiesIt) {
        if (!entityJson.is_object()) {
            throw std::runtime_error("Entity config entry must be an object");
        }

        const auto kindIt = entityJson.find("kind");
        if (kindIt != entityJson.end() && (!kindIt->is_string() || kindIt->get<std::string>() != "mob")) {
            continue;
        }

        const auto textureIt = entityJson.find("texture");
        if (textureIt == entityJson.end() || !textureIt->is_string()) {
            throw std::runtime_error("Mob entity config entry requires a string texture");
        }

        const std::string textureKey = textureIt->get<std::string>();
        if (textureKey.empty() ||
            textureKey.find('/') != std::string::npos ||
            textureKey.find('\\') != std::string::npos) {
            throw std::runtime_error("Invalid mob entity texture key: " + textureKey);
        }
        if (loadedTextureKeys.find(textureKey) != loadedTextureKeys.end()) {
            continue;
        }

        loadedTextureKeys.insert(textureKey);
        const std::string texturePath = std::string(MOBS_TEXTURE_DIR) + "/" + textureKey + ".png";
        resourceMgr.loadGuiTexture(textureKey, texturePath, true);
    }
}

} // namespace resource
