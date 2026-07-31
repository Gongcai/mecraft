#include "BlockTextureCatalog.h"

#include "../Diagnostics.h"

#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {

bool parseResourceTextureTint(const nlohmann::json& textureJson, const std::string& textureName,
                              ResourceTextureTint& outTint) {
    const auto tintIt = textureJson.find("tint");
    if (tintIt == textureJson.end()) {
        outTint = ResourceTextureTint::None;
        return true;
    }
    if (!tintIt->is_string()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] block_textures.json texture tint must be a string for %s\n",
                            textureName.c_str());
        return false;
    }

    const std::string tint = tintIt->get<std::string>();
    if (tint == "none") {
        outTint = ResourceTextureTint::None;
        return true;
    }
    if (tint == "grass") {
        outTint = ResourceTextureTint::Grass;
        return true;
    }
    if (tint == "foliage") {
        outTint = ResourceTextureTint::Foliage;
        return true;
    }
    MECRAFT_LOG_FPRINTF(stderr, "[Resource] Unknown block texture tint for %s: %s\n", textureName.c_str(),
                        tint.c_str());
    return false;
}

} // namespace

bool BlockTextureCatalog::load(const std::string& textureConfigPath) {
    std::ifstream file(textureConfigPath);
    if (!file.is_open()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Failed to open block texture catalog: %s\n", textureConfigPath.c_str());
        return false;
    }

    nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Failed to parse block texture catalog: invalid JSON\n");
        return false;
    }

    const auto texturesIt = root.find("textures");
    if (texturesIt == root.end() || !texturesIt->is_array()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] block_textures.json must contain a textures array\n");
        return false;
    }

    EntryMap entries;
    for (const auto& textureJson : *texturesIt) {
        if (!textureJson.is_object()) {
            MECRAFT_LOG_FPRINTF(stderr, "[Resource] block_textures.json texture entry must be an object\n");
            return false;
        }

        const auto nameIt = textureJson.find("name");
        if (nameIt == textureJson.end() || !nameIt->is_string()) {
            MECRAFT_LOG_FPRINTF(stderr, "[Resource] block_textures.json texture entry requires a string name\n");
            return false;
        }
        const std::string name = nameIt->get<std::string>();
        if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
            MECRAFT_LOG_FPRINTF(stderr, "[Resource] Invalid block texture name: %s\n", name.c_str());
            return false;
        }
        if (entries.find(name) != entries.end()) {
            MECRAFT_LOG_FPRINTF(stderr, "[Resource] Duplicate block texture catalog entry: %s\n", name.c_str());
            return false;
        }

        BlockTextureCatalogEntry entry;
        if (!parseResourceTextureTint(textureJson, name, entry.tint)) {
            return false;
        }

        const auto framesIt = textureJson.find("frames");
        if (framesIt != textureJson.end()) {
            if (!framesIt->is_number_integer()) {
                MECRAFT_LOG_FPRINTF(stderr, "[Resource] block_textures.json frames must be an integer for %s\n",
                                    name.c_str());
                return false;
            }
            const int64_t frameCount = framesIt->get<int64_t>();
            if (frameCount < 1 || frameCount > 63) {
                MECRAFT_LOG_FPRINTF(stderr, "[Resource] block_textures.json frames out of range for %s\n",
                                    name.c_str());
                return false;
            }
            entry.animation.frameCount = static_cast<int>(frameCount);
            if (entry.animation.frameCount <= 0 || entry.animation.frameCount > 63) {
                MECRAFT_LOG_FPRINTF(stderr, "[Resource] block_textures.json frames out of range for %s\n",
                                    name.c_str());
                return false;
            }
        }

        const auto fpsIt = textureJson.find("fps");
        if (fpsIt != textureJson.end()) {
            if (!fpsIt->is_number()) {
                MECRAFT_LOG_FPRINTF(stderr, "[Resource] block_textures.json fps must be numeric for %s\n",
                                    name.c_str());
                return false;
            }
            entry.animation.fps = fpsIt->get<float>();
            if (entry.animation.fps < 0.0f || entry.animation.fps > 63.0f) {
                MECRAFT_LOG_FPRINTF(stderr, "[Resource] block_textures.json fps out of range for %s\n", name.c_str());
                return false;
            }
        }

        if (entry.animation.frameCount > 1) {
            const auto layoutIt = textureJson.find("frameLayout");
            if (layoutIt == textureJson.end() || !layoutIt->is_string()) {
                MECRAFT_LOG_FPRINTF(stderr, "[Resource] Animated block texture requires frameLayout for %s\n",
                                    name.c_str());
                return false;
            }
            const std::string frameLayout = layoutIt->get<std::string>();
            if (frameLayout != "vertical") {
                MECRAFT_LOG_FPRINTF(stderr, "[Resource] Unsupported block texture frameLayout for %s: %s\n",
                                    name.c_str(), frameLayout.c_str());
                return false;
            }
            entry.verticalFrames = true;
            entry.animation.isAnimated = true;

            const auto frameOrderIt = textureJson.find("frameOrder");
            if (frameOrderIt == textureJson.end() || !frameOrderIt->is_string()) {
                MECRAFT_LOG_FPRINTF(stderr, "[Resource] Animated block texture requires frameOrder for %s\n",
                                    name.c_str());
                return false;
            }
            const std::string frameOrder = frameOrderIt->get<std::string>();
            if (frameOrder == "top_to_bottom") {
                entry.topFrameFirst = true;
            } else if (frameOrder == "bottom_to_top") {
                entry.topFrameFirst = false;
            } else {
                MECRAFT_LOG_FPRINTF(stderr, "[Resource] Unsupported block texture frameOrder for %s: %s\n",
                                    name.c_str(), frameOrder.c_str());
                return false;
            }
        }

        entries.emplace(name, entry);
    }

    m_entries = std::move(entries);
    m_tileSize = 16;
    return true;
}

bool BlockTextureCatalog::loadPackConfig(const std::string& packConfigPath) {
    std::ifstream file(packConfigPath);
    if (!file.is_open()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Failed to open block texture pack config: %s\n",
                            packConfigPath.c_str());
        return false;
    }

    nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] Failed to parse block texture pack config: invalid JSON\n");
        return false;
    }

    const auto gpuTileSizeIt = root.find("gpuTileSize");
    if (gpuTileSizeIt == root.end() || !gpuTileSizeIt->is_number_integer()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] block_texture_pack.json gpuTileSize must be an integer\n");
        return false;
    }

    const int64_t parsedGpuTileSize = gpuTileSizeIt->get<int64_t>();
    if (parsedGpuTileSize < 1 || parsedGpuTileSize > 1024) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] block_texture_pack.json gpuTileSize must be in range [1, 1024]\n");
        return false;
    }

    const int gpuTileSize = static_cast<int>(parsedGpuTileSize);
    if (gpuTileSize <= 0) {
        MECRAFT_LOG_FPRINTF(stderr, "[Resource] block_texture_pack.json gpuTileSize must be positive\n");
        return false;
    }

    m_tileSize = gpuTileSize;
    return true;
}

void BlockTextureCatalog::clear() {
    m_entries.clear();
    m_tileSize = 16;
}

const BlockTextureCatalogEntry* BlockTextureCatalog::find(const std::string& name) const {
    const auto it = m_entries.find(name);
    return it != m_entries.end() ? &it->second : nullptr;
}

BlockTextureCatalogEntry* BlockTextureCatalog::findMutable(const std::string& name) {
    const auto it = m_entries.find(name);
    return it != m_entries.end() ? &it->second : nullptr;
}

ResourceTextureTint BlockTextureCatalog::tintFor(const std::string& name) const {
    const BlockTextureCatalogEntry* entry = find(name);
    return entry != nullptr ? entry->tint : ResourceTextureTint::None;
}

int BlockTextureCatalog::tileSize() const {
    return m_tileSize;
}

const BlockTextureCatalog::EntryMap& BlockTextureCatalog::entries() const {
    return m_entries;
}

BlockTextureCatalog::EntryMap& BlockTextureCatalog::entries() {
    return m_entries;
}
