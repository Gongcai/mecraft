#include "AppTextureManifest.h"

#include "Paths.h"
#include "GameResources.h"
#include "../Diagnostics.h"
#include "renderer/rhi/RhiResources.h"

#include <fstream>
#include <string>
#include <nlohmann/json.hpp>

namespace resource {

namespace {

constexpr const char* kCubeFaceKeys[] = {"right", "left", "top", "bottom", "front", "back"};

// Builds the absolute asset path from a manifest-relative path.
std::string makeAssetPath(const std::string& relativePath) { return std::string(ASSETS_DIR) + "/" + relativePath; }

bool readString(const nlohmann::json& object, const char* key, std::string& outValue) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() && (outValue = it->get<std::string>(), true);
}

bool readBool(const nlohmann::json& object, const char* key, bool defaultValue, bool& outValue) {
    const auto it = object.find(key);
    if (it == object.end()) {
        outValue = defaultValue;
        return true;
    }
    if (!it->is_boolean()) {
        return false;
    }
    outValue = it->get<bool>();
    return true;
}

bool loadTextureEntry(GameResources& resources, const nlohmann::json& entry) {
    if (!entry.is_object()) {
        MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] texture entry must be an object\n");
        return false;
    }

    std::string name;
    std::string relativePath;
    if (!readString(entry, "name", name) || !readString(entry, "path", relativePath)) {
        MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] texture entry requires string name and path\n");
        return false;
    }
    const std::string fullPath = makeAssetPath(relativePath);

    std::string kind = "2d";
    const auto kindIt = entry.find("kind");
    if (kindIt != entry.end()) {
        if (!kindIt->is_string()) {
            MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] texture %s has non-string kind\n", name.c_str());
            return false;
        }
        kind = kindIt->get<std::string>();
    }

    if (kind == "gui") {
        bool flip = true;
        if (!readBool(entry, "flip", true, flip)) {
            MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] texture %s has non-boolean flip\n", name.c_str());
            return false;
        }
        if (!resources.texture2D.loadGui(name, fullPath, flip).isValid()) {
            MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] failed to load gui texture %s: %s\n", name.c_str(),
                                fullPath.c_str());
            return false;
        }
        return true;
    }

    if (kind == "2d") {
        bool srgb = false;
        if (!readBool(entry, "srgb", false, srgb)) {
            MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] texture %s has non-boolean srgb\n", name.c_str());
            return false;
        }
        bool flip = false;
        if (!readBool(entry, "flip", false, flip)) {
            MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] texture %s has non-boolean flip\n", name.c_str());
            return false;
        }
        RhiTextureQueueSharing queueSharing = RhiTextureQueueSharing::Exclusive;
        const auto sharingIt = entry.find("queueSharing");
        if (sharingIt != entry.end()) {
            if (!sharingIt->is_string()) {
                MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] texture %s has non-string queueSharing\n",
                                    name.c_str());
                return false;
            }
            const std::string sharingValue = sharingIt->get<std::string>();
            if (sharingValue == "exclusive") {
                queueSharing = RhiTextureQueueSharing::Exclusive;
            } else if (sharingValue == "graphicsComputeConcurrent") {
                queueSharing = RhiTextureQueueSharing::GraphicsComputeConcurrent;
            } else {
                MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] texture %s has unknown queueSharing %s\n",
                                    name.c_str(), sharingValue.c_str());
                return false;
            }
        }
        if (!resources.texture2D.load(name, fullPath, srgb, flip, queueSharing).isValid()) {
            MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] failed to load 2d texture %s: %s\n", name.c_str(),
                                fullPath.c_str());
            return false;
        }
        return true;
    }

    MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] texture %s has unknown kind %s\n", name.c_str(), kind.c_str());
    return false;
}

bool loadCubemapEntry(GameResources& resources, const nlohmann::json& entry) {
    if (!entry.is_object()) {
        MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] cubemap entry must be an object\n");
        return false;
    }

    std::string name;
    if (!readString(entry, "name", name)) {
        MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] cubemap entry requires a string name\n");
        return false;
    }
    const auto facesIt = entry.find("faces");
    if (facesIt == entry.end() || !facesIt->is_object()) {
        MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] cubemap %s requires a faces object\n", name.c_str());
        return false;
    }

    std::string facePaths[6];
    for (int i = 0; i < 6; ++i) {
        if (!readString(*facesIt, kCubeFaceKeys[i], facePaths[i])) {
            MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] cubemap %s is missing face %s\n", name.c_str(),
                                kCubeFaceKeys[i]);
            return false;
        }
        facePaths[i] = makeAssetPath(facePaths[i]);
    }

    const RhiTextureHandle handle = resources.cubemaps.load(name, facePaths[0], facePaths[1], facePaths[2],
                                                            facePaths[3], facePaths[4], facePaths[5]);
    if (!handle.isValid()) {
        MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] failed to load cubemap %s\n", name.c_str());
        return false;
    }
    return true;
}

} // namespace

bool loadAppTextureManifest(GameResources& resources, const std::string& manifestPath) {
    std::ifstream file(manifestPath);
    if (!file.is_open()) {
        MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] failed to open manifest: %s\n", manifestPath.c_str());
        return false;
    }

    nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] failed to parse manifest: %s\n", manifestPath.c_str());
        return false;
    }

    const auto texturesIt = root.find("textures");
    if (texturesIt != root.end()) {
        if (!texturesIt->is_array()) {
            MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] textures section must be an array\n");
            return false;
        }
        for (const nlohmann::json& entry : *texturesIt) {
            if (!loadTextureEntry(resources, entry)) {
                return false;
            }
        }
    }

    const auto cubemapsIt = root.find("cubemaps");
    if (cubemapsIt != root.end()) {
        if (!cubemapsIt->is_array()) {
            MECRAFT_LOG_FPRINTF(stderr, "[AppTextureManifest] cubemaps section must be an array\n");
            return false;
        }
        for (const nlohmann::json& entry : *cubemapsIt) {
            if (!loadCubemapEntry(resources, entry)) {
                return false;
            }
        }
    }

    return true;
}

} // namespace resource
