#include "GameResourceBootstrapper.h"

#include "../Paths.h"
#include "../game/interaction/BlockInteractionRegistry.h"
#include "../game/inventory/ContainerBehaviorRegistry.h"
#include "../item/Item.h"
#include "../resource/AppTextureManifest.h"
#include "../resource/AtmosphereLutProbe.h"
#include "../resource/ResourceMgr.h"
#include "../ui/inventory/ContainerUiRegistry.h"
#include "../world/block/Block.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace app {

namespace {

using BlockTextureNameSet = std::unordered_set<std::string>;

constexpr const char* kBlockModelsDir = ASSETS_DIR "/models/blocks";

void addBlockTextureName(const std::string& rawName, BlockTextureNameSet& textureNames) {
    if (rawName.empty() || rawName.front() == '#') {
        return;
    }

    constexpr const char* kBlockPrefix = "block/";
    std::string name = rawName;
    if (name.rfind(kBlockPrefix, 0) == 0) {
        name.erase(0, std::char_traits<char>::length(kBlockPrefix));
    }
    if (!name.empty()) {
        textureNames.insert(std::move(name));
    }
}

void collectTextureObjectNames(const nlohmann::json& textureObject, BlockTextureNameSet& textureNames) {
    if (!textureObject.is_object()) {
        return;
    }
    for (auto it = textureObject.begin(); it != textureObject.end(); ++it) {
        if (it.value().is_string()) {
            addBlockTextureName(it.value().get<std::string>(), textureNames);
        }
    }
}

bool collectBlockConfigTextureNames(const std::string& configPath, BlockTextureNameSet& textureNames) {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        std::cerr << "Failed to open block config for texture registration: " << configPath << '\n';
        return false;
    }

    nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded()) {
        std::cerr << "Failed to parse block config for texture registration: " << configPath << '\n';
        return false;
    }

    const auto blocksIt = root.find("blocks");
    if (blocksIt == root.end() || !blocksIt->is_array()) {
        std::cerr << "Block config requires a blocks array for texture registration\n";
        return false;
    }

    for (const nlohmann::json& blockJson : *blocksIt) {
        if (!blockJson.is_object()) {
            std::cerr << "Block config entry must be an object for texture registration\n";
            return false;
        }

        if (const auto texturesIt = blockJson.find("textures"); texturesIt != blockJson.end()) {
            collectTextureObjectNames(*texturesIt, textureNames);
        }

        if (const auto stateTexturesIt = blockJson.find("stateTextures"); stateTexturesIt != blockJson.end()) {
            if (!stateTexturesIt->is_object()) {
                std::cerr << "Block stateTextures must be an object for texture registration\n";
                return false;
            }
            for (auto propertyIt = stateTexturesIt->begin(); propertyIt != stateTexturesIt->end(); ++propertyIt) {
                if (!propertyIt.value().is_object()) {
                    std::cerr << "Block stateTextures property must be an object for texture registration\n";
                    return false;
                }
                for (auto valueIt = propertyIt.value().begin(); valueIt != propertyIt.value().end(); ++valueIt) {
                    collectTextureObjectNames(valueIt.value(), textureNames);
                }
            }
        }

        if (const auto animatedIt = blockJson.find("animatedTextures"); animatedIt != blockJson.end()) {
            if (!animatedIt->is_object()) {
                std::cerr << "Block animatedTextures must be an object for texture registration\n";
                return false;
            }
            for (auto it = animatedIt->begin(); it != animatedIt->end(); ++it) {
                if (!it.value().is_object()) {
                    continue;
                }
                const auto textureIt = it.value().find("texture");
                if (textureIt != it.value().end() && textureIt->is_string()) {
                    addBlockTextureName(textureIt->get<std::string>(), textureNames);
                }
            }
        }
    }

    return true;
}

bool collectModelTextureNames(const std::string& modelDirectory, BlockTextureNameSet& textureNames) {
    namespace fs = std::filesystem;

    std::error_code fsError;
    if (!fs::exists(modelDirectory, fsError)) {
        if (fsError) {
            std::cerr << "Failed to inspect block model directory for texture registration: " << modelDirectory << ": "
                      << fsError.message() << '\n';
        } else {
            std::cerr << "Block model directory does not exist for texture registration: " << modelDirectory << '\n';
        }
        return false;
    }

    fs::recursive_directory_iterator it(modelDirectory, fs::directory_options::none, fsError);
    if (fsError) {
        std::cerr << "Failed to iterate block model directory for texture registration: " << modelDirectory << ": "
                  << fsError.message() << '\n';
        return false;
    }

    const fs::recursive_directory_iterator end;
    while (it != end) {
        const fs::directory_entry& entry = *it;
        fsError.clear();
        const bool regularFile = entry.is_regular_file(fsError);
        if (fsError) {
            std::cerr << "Failed to inspect block model path for texture registration: " << entry.path().string()
                      << ": " << fsError.message() << '\n';
            return false;
        }

        if (regularFile && entry.path().extension() == ".json") {
            std::ifstream file(entry.path());
            if (!file.is_open()) {
                std::cerr << "Failed to open block model for texture registration: " << entry.path().string() << '\n';
                return false;
            }

            nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
            if (root.is_discarded()) {
                std::cerr << "Failed to parse block model for texture registration: " << entry.path().string() << '\n';
                return false;
            }

            if (const auto texturesIt = root.find("textures"); texturesIt != root.end()) {
                collectTextureObjectNames(*texturesIt, textureNames);
            }
        }

        it.increment(fsError);
        if (fsError) {
            std::cerr << "Failed to continue iterating block model directory for texture registration: "
                      << modelDirectory << ": " << fsError.message() << '\n';
            return false;
        }
    }

    return true;
}

bool collectRegisteredBlockTextureNames(BlockTextureNameSet& textureNames) {
    if (!collectBlockConfigTextureNames(BLOCKS_CONFIG_PATH, textureNames)) {
        return false;
    }
    if (!collectModelTextureNames(kBlockModelsDir, textureNames)) {
        return false;
    }
    if (textureNames.empty()) {
        std::cerr << "Block texture registration produced no texture names\n";
        return false;
    }
    return true;
}

} // namespace

bool bootstrapGameResources(ResourceMgr& resourceMgr, RhiDevice& rhiDevice, RhiCommandListPool& commandListPool) {
    resourceMgr.init(rhiDevice, commandListPool);
    if (!resourceMgr.loadBlockTextureCatalog(BLOCK_TEXTURES_CONFIG_PATH, BLOCK_TEXTURE_PACK_CONFIG_PATH)) {
        return false;
    }
    BlockTextureNameSet blockTextureNames;
    if (!collectRegisteredBlockTextureNames(blockTextureNames)) {
        return false;
    }
    resourceMgr.buildBlockTextureResources(BLOCKS_TEXTURES_DIR, resourceMgr.getBlockTextureTileSize(),
                                           blockTextureNames);
    resourceMgr.loadLightmapTextures(LIGHTMAP_DAY_PATH, LIGHTMAP_NIGHT_PATH);
    resourceMgr.loadColormapTextures(GRASS_TEXTURE_PATH, FOLIAGE_TEXTURE_PATH);
    if (!resource::loadAppTextureManifest(resourceMgr, APP_TEXTURES_MANIFEST_PATH)) {
        return false;
    }

    resource::probeAtmosphereLut("Transmittance", SHADERPACK_TRANSMITTANCE_LUT_PATH, 256U * 64U * 16U);
    resource::probeAtmosphereLut("Scattering", SHADERPACK_SCATTERING_LUT_PATH, 32U * 128U * 32U * 8U * 16U);
    resource::probeAtmosphereLut("Irradiance", SHADERPACK_IRRADIANCE_LUT_PATH, 64U * 16U * 16U);
    resource::probeAtmosphereLut("Final", SHADERPACK_FINAL_LUT_PATH);

    resourceMgr.buildItemTextureAtlas(ITEMS_TEXTURES_DIR, 16);

    if (!ContainerBehaviorRegistry::init() || !BlockInteractionRegistry::init() || !ui::ContainerUiRegistry::init()) {
        return false;
    }
    for (const auto& [id, def] : ui::ContainerUiRegistry::all()) {
        const std::string texturePath = std::string(ASSETS_DIR) + "/" + def.backgroundTexturePath;
        if (!resourceMgr.loadGuiTexture(def.backgroundTexture, texturePath, true).isValid()) {
            std::cerr << "Failed to load container UI texture for " << id << ": " << texturePath << '\n';
            return false;
        }
    }

    if (!resourceMgr.preloadEntityTexturesFromConfig(ENTITIES_CONFIG_PATH)) {
        return false;
    }

    resourceMgr.buildHudIconAtlas(ICONS_TEXTURE_DIR, 8);

    BlockRegistry::init(&resourceMgr);
    if (!ItemRegistry::init()) {
        return false;
    }
    resourceMgr.buildBlockIconAtlas(64);
    return true;
}

} // namespace app
