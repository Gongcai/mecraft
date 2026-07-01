#include "BlockTextureCatalog.h"

#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace {

ResourceTextureTint parseResourceTextureTint(const nlohmann::json& textureJson) {
    const auto tintIt = textureJson.find("tint");
    if (tintIt == textureJson.end()) {
        return ResourceTextureTint::None;
    }
    if (!tintIt->is_string()) {
        throw std::runtime_error("block_textures.json texture tint must be a string");
    }

    const std::string tint = tintIt->get<std::string>();
    if (tint == "none") {
        return ResourceTextureTint::None;
    }
    if (tint == "grass") {
        return ResourceTextureTint::Grass;
    }
    if (tint == "foliage") {
        return ResourceTextureTint::Foliage;
    }
    throw std::runtime_error("Unknown block texture tint: " + tint);
}

} // namespace

void BlockTextureCatalog::load(const std::string& textureConfigPath) {
    m_entries.clear();

    std::ifstream file(textureConfigPath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open block texture catalog: " + textureConfigPath);
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to parse block texture catalog: ") + e.what());
    }

    const auto tileSizeIt = root.find("tileSize");
    if (tileSizeIt != root.end()) {
        if (!tileSizeIt->is_number_integer()) {
            throw std::runtime_error("block_textures.json tileSize must be an integer");
        }
        const int catalogTileSize = tileSizeIt->get<int>();
        if (catalogTileSize <= 0) {
            throw std::runtime_error("block_textures.json tileSize must be positive");
        }
    }

    const auto texturesIt = root.find("textures");
    if (texturesIt == root.end() || !texturesIt->is_array()) {
        throw std::runtime_error("block_textures.json must contain a textures array");
    }

    for (const auto& textureJson : *texturesIt) {
        if (!textureJson.is_object()) {
            throw std::runtime_error("block_textures.json texture entry must be an object");
        }

        const auto nameIt = textureJson.find("name");
        if (nameIt == textureJson.end() || !nameIt->is_string()) {
            throw std::runtime_error("block_textures.json texture entry requires a string name");
        }
        const std::string name = nameIt->get<std::string>();
        if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
            throw std::runtime_error("Invalid block texture name: " + name);
        }
        if (m_entries.find(name) != m_entries.end()) {
            throw std::runtime_error("Duplicate block texture catalog entry: " + name);
        }

        BlockTextureCatalogEntry entry;
        entry.tint = parseResourceTextureTint(textureJson);

        const auto framesIt = textureJson.find("frames");
        if (framesIt != textureJson.end()) {
            if (!framesIt->is_number_integer()) {
                throw std::runtime_error("block_textures.json frames must be an integer for " + name);
            }
            entry.animation.frameCount = framesIt->get<int>();
            if (entry.animation.frameCount <= 0 || entry.animation.frameCount > 63) {
                throw std::runtime_error("block_textures.json frames out of range for " + name);
            }
        }

        const auto fpsIt = textureJson.find("fps");
        if (fpsIt != textureJson.end()) {
            if (!fpsIt->is_number()) {
                throw std::runtime_error("block_textures.json fps must be numeric for " + name);
            }
            entry.animation.fps = fpsIt->get<float>();
            if (entry.animation.fps < 0.0f || entry.animation.fps > 63.0f) {
                throw std::runtime_error("block_textures.json fps out of range for " + name);
            }
        }

        if (entry.animation.frameCount > 1) {
            const auto layoutIt = textureJson.find("frameLayout");
            if (layoutIt == textureJson.end() || !layoutIt->is_string()) {
                throw std::runtime_error("Animated block texture requires frameLayout for " + name);
            }
            const std::string frameLayout = layoutIt->get<std::string>();
            if (frameLayout != "vertical") {
                throw std::runtime_error("Unsupported block texture frameLayout for " + name + ": " + frameLayout);
            }
            entry.verticalFrames = true;
            entry.animation.isAnimated = true;

            const auto frameOrderIt = textureJson.find("frameOrder");
            if (frameOrderIt == textureJson.end() || !frameOrderIt->is_string()) {
                throw std::runtime_error("Animated block texture requires frameOrder for " + name);
            }
            const std::string frameOrder = frameOrderIt->get<std::string>();
            if (frameOrder == "top_to_bottom") {
                entry.topFrameFirst = true;
            } else if (frameOrder == "bottom_to_top") {
                entry.topFrameFirst = false;
            } else {
                throw std::runtime_error("Unsupported block texture frameOrder for " + name + ": " + frameOrder);
            }
        }

        m_entries.emplace(name, entry);
    }
}

void BlockTextureCatalog::clear() {
    m_entries.clear();
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

const BlockTextureCatalog::EntryMap& BlockTextureCatalog::entries() const {
    return m_entries;
}

BlockTextureCatalog::EntryMap& BlockTextureCatalog::entries() {
    return m_entries;
}
