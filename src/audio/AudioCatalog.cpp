#include "AudioCatalog.h"

#include "AudioDecoders.h"
#include "AudioFileDiscovery.h"

#include <algorithm>
#include <fstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace audio {
namespace fs = std::filesystem;
namespace {

using json = nlohmann::json;

std::string jsonTypeName(const json& value) {
    return value.type_name();
}

fs::path appendExtension(fs::path path, const std::string& extension) {
    path += normalizeAudioExtension(extension);
    return path;
}

bool resolveVariantPath(const fs::path& rootDirectory, const fs::path& relativePath, fs::path& outPath,
                        std::string& error) {
    const auto& registry = AudioDecoderRegistry::instance();
    if (relativePath.empty()) {
        error = "variant file path is empty";
        return false;
    }

    if (relativePath.has_extension()) {
        const std::string extension = relativePath.extension().string();
        if (!registry.isSupportedExtension(extension)) {
            error = "unsupported audio extension in manifest: " + extension;
            return false;
        }

        const fs::path candidate = rootDirectory / relativePath;
        if (!fs::exists(candidate) || !fs::is_regular_file(candidate)) {
            error = "audio file not found: " + pathToUtf8(candidate);
            return false;
        }

        outPath = candidate;
        return true;
    }

    const auto discovered = findAudioFileByStem(rootDirectory, relativePath);
    if (!discovered.has_value()) {
        error = "audio file not found for manifest stem: " + pathToUtf8(rootDirectory / relativePath);
        return false;
    }

    outPath = *discovered;
    return true;
}

bool parseVariant(const json& node, const fs::path& rootDirectory, SoundVariant& out, std::string& error) {
    fs::path relativePath;
    float weight = 1.0f;

    if (node.is_string()) {
        relativePath = node.get<std::string>();
    } else if (node.is_object()) {
        if (node.contains("path")) {
            if (!node["path"].is_string()) {
                error = "variant path must be a string";
                return false;
            }
            relativePath = node["path"].get<std::string>();
        } else if (node.contains("file")) {
            if (!node["file"].is_string()) {
                error = "variant file must be a string";
                return false;
            }
            relativePath = node["file"].get<std::string>();
        } else {
            error = "variant object must contain file or path";
            return false;
        }

        if (node.contains("extension")) {
            if (!node["extension"].is_string()) {
                error = "variant extension must be a string";
                return false;
            }
            relativePath = appendExtension(relativePath, node["extension"].get<std::string>());
        }

        if (node.contains("weight")) {
            if (!node["weight"].is_number()) {
                error = "variant weight must be a number";
                return false;
            }
            weight = std::max(0.0f, node["weight"].get<float>());
        }
    } else {
        error = "variant must be a string or object, got " + jsonTypeName(node);
        return false;
    }

    fs::path resolvedPath;
    if (!resolveVariantPath(rootDirectory, relativePath, resolvedPath, error)) {
        return false;
    }

    out.filePath = resolvedPath;
    out.weight = weight;
    return true;
}

bool appendVariantList(const json& node, const fs::path& rootDirectory, std::vector<SoundVariant>& variants,
                       std::string& error) {
    if (node.is_array()) {
        for (const json& variantNode : node) {
            SoundVariant variant;
            if (!parseVariant(variantNode, rootDirectory, variant, error)) {
                return false;
            }
            variants.push_back(std::move(variant));
        }
        return true;
    }

    SoundVariant variant;
    if (!parseVariant(node, rootDirectory, variant, error)) {
        return false;
    }
    variants.push_back(std::move(variant));
    return true;
}

bool parseEntry(const std::string& soundId, const json& node, const fs::path& rootDirectory,
                const std::string& defaultGroup, bool defaultPreload, SoundEntry& out, std::string& error) {
    out = SoundEntry{};
    out.id = soundId;
    out.group = defaultGroup;
    out.preload = defaultPreload;

    if (node.is_string() || node.is_array()) {
        if (!appendVariantList(node, rootDirectory, out.variants, error)) {
            return false;
        }
    } else if (node.is_object()) {
        out.group = node.value("group", defaultGroup);
        out.preload = node.value("preload", defaultPreload);
        out.volume = std::clamp(node.value("volume", 1.0f), 0.0f, 4.0f);

        if (node.contains("variants")) {
            if (!appendVariantList(node["variants"], rootDirectory, out.variants, error)) {
                return false;
            }
        } else if (node.contains("files")) {
            if (!appendVariantList(node["files"], rootDirectory, out.variants, error)) {
                return false;
            }
        } else if (node.contains("file") || node.contains("path")) {
            if (!appendVariantList(node, rootDirectory, out.variants, error)) {
                return false;
            }
        } else {
            error = "sound entry must contain variants, files, file, or path";
            return false;
        }
    } else {
        error = "sound entry must be a string, array, or object, got " + jsonTypeName(node);
        return false;
    }

    if (out.variants.empty()) {
        error = "sound entry has no variants";
        return false;
    }
    return true;
}

} // namespace

bool AudioCatalog::loadFromFile(const fs::path& manifestPath, const fs::path& rootDirectory,
                                const std::string& defaultGroup, const bool defaultPreload, std::string& error) {
    std::ifstream file(manifestPath);
    if (!file.is_open()) {
        error = "failed to open audio catalog: " + pathToUtf8(manifestPath);
        return false;
    }

    json manifest = json::parse(file, nullptr, false);
    if (manifest.is_discarded()) {
        error = "failed to parse audio catalog " + pathToUtf8(manifestPath) + ": invalid JSON";
        return false;
    }

    if (!manifest.is_object()) {
        error = "audio catalog root must be an object";
        return false;
    }
    if (!manifest.contains("sounds") || !manifest["sounds"].is_object()) {
        error = "audio catalog must contain a sounds object";
        return false;
    }

    std::vector<SoundEntry> pendingEntries;
    pendingEntries.reserve(manifest["sounds"].size());

    for (auto it = manifest["sounds"].begin(); it != manifest["sounds"].end(); ++it) {
        const std::string soundId = it.key();
        if (soundId.empty()) {
            error = "audio catalog contains an empty sound id";
            return false;
        }
        if (m_entries.find(soundId) != m_entries.end()) {
            error = "duplicate audio catalog sound id: " + soundId;
            return false;
        }

        SoundEntry entry;
        std::string entryError;
        if (!parseEntry(soundId, it.value(), rootDirectory, defaultGroup, defaultPreload, entry, entryError)) {
            error = "invalid sound entry '" + soundId + "': " + entryError;
            return false;
        }
        pendingEntries.push_back(std::move(entry));
    }

    for (SoundEntry& entry : pendingEntries) {
        m_order.push_back(entry.id);
        m_entries.emplace(entry.id, std::move(entry));
    }

    return true;
}

const SoundEntry* AudioCatalog::find(const std::string& soundId) const {
    const auto it = m_entries.find(soundId);
    return it == m_entries.end() ? nullptr : &it->second;
}

std::vector<std::string> AudioCatalog::soundIdsByGroup(const std::string& group) const {
    std::vector<std::string> result;
    for (const std::string& soundId : m_order) {
        const SoundEntry* entry = find(soundId);
        if (entry != nullptr && entry->group == group) {
            result.push_back(soundId);
        }
    }
    return result;
}

} // namespace audio
