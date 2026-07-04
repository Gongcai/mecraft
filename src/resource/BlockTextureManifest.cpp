#include "BlockTextureManifest.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>

namespace {

[[noreturn]] void failBlockTextureManifest(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

enum class TextureFileRole {
    Albedo,
    Normal,
    Specular,
};

struct ClassifiedTextureFile {
    std::string textureName;
    TextureFileRole role = TextureFileRole::Albedo;
    std::filesystem::path path;
};

std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

bool hasPngExtension(const std::filesystem::path& path) {
    return toLowerAscii(path.extension().string()) == ".png";
}

bool removeSuffix(std::string& value, const char* suffix) {
    const std::string suffixText(suffix);
    if (value.size() <= suffixText.size()) {
        return false;
    }
    if (value.compare(value.size() - suffixText.size(), suffixText.size(), suffixText) != 0) {
        return false;
    }
    value.erase(value.size() - suffixText.size());
    return true;
}

ClassifiedTextureFile classifyTextureFile(const std::filesystem::path& path) {
    ClassifiedTextureFile file;
    file.path = path;
    file.textureName = path.stem().string();

    std::string normalizedStem = toLowerAscii(file.textureName);
    if (removeSuffix(normalizedStem, "_normal") ||
        removeSuffix(normalizedStem, "_n")) {
        file.textureName = normalizedStem;
        file.role = TextureFileRole::Normal;
        return file;
    }
    if (removeSuffix(normalizedStem, "_specular") ||
        removeSuffix(normalizedStem, "_spec") ||
        removeSuffix(normalizedStem, "_s")) {
        file.textureName = normalizedStem;
        file.role = TextureFileRole::Specular;
        return file;
    }

    file.textureName = normalizedStem;
    file.role = TextureFileRole::Albedo;
    return file;
}

std::vector<ClassifiedTextureFile> collectClassifiedTextureFiles(const std::string& directory) {
    namespace fs = std::filesystem;

    std::error_code fsError;
    if (!fs::exists(directory, fsError)) {
        if (fsError) {
            failBlockTextureManifest("Failed to inspect block texture directory: " +
                                     directory + ": " + fsError.message());
        }
        failBlockTextureManifest("Block texture directory does not exist: " + directory);
    }

    std::vector<ClassifiedTextureFile> files;
    fs::directory_iterator it(directory, fsError);
    if (fsError) {
        failBlockTextureManifest("Failed to iterate block texture directory: " +
                                 directory + ": " + fsError.message());
    }
    const fs::directory_iterator end;
    while (it != end) {
        const fs::directory_entry& entry = *it;
        fsError.clear();
        const bool regularFile = entry.is_regular_file(fsError);
        if (fsError) {
            failBlockTextureManifest("Failed to inspect block texture path: " +
                                     entry.path().string() + ": " + fsError.message());
        }
        if (regularFile && hasPngExtension(entry.path())) {
            files.push_back(classifyTextureFile(entry.path()));
        }
        it.increment(fsError);
        if (fsError) {
            failBlockTextureManifest("Failed to continue iterating block texture directory: " +
                                     directory + ": " + fsError.message());
        }
    }

    std::sort(files.begin(), files.end(),
              [](const ClassifiedTextureFile& a, const ClassifiedTextureFile& b) {
                  if (a.textureName != b.textureName) {
                      return a.textureName < b.textureName;
                  }
                  return static_cast<int>(a.role) < static_cast<int>(b.role);
              });

    if (files.empty()) {
        failBlockTextureManifest("Block texture directory contains no PNG files: " + directory);
    }

    return files;
}

struct PendingManifestEntry {
    std::optional<std::filesystem::path> albedoPath;
    std::optional<std::filesystem::path> normalPath;
    std::optional<std::filesystem::path> specularPath;
};

void assignTexturePath(PendingManifestEntry& entry,
                       const ClassifiedTextureFile& file) {
    if (file.role == TextureFileRole::Albedo) {
        if (entry.albedoPath.has_value()) {
            failBlockTextureManifest("Duplicate block albedo texture: " + file.textureName);
        }
        entry.albedoPath = file.path;
        return;
    }
    if (file.role == TextureFileRole::Normal) {
        if (entry.normalPath.has_value()) {
            failBlockTextureManifest("Duplicate block normal texture: " + file.textureName);
        }
        entry.normalPath = file.path;
        return;
    }
    if (entry.specularPath.has_value()) {
        failBlockTextureManifest("Duplicate block specular texture: " + file.textureName);
    }
    entry.specularPath = file.path;
}

} // namespace

namespace resource {

void BlockTextureManifest::addEntry(BlockTextureManifestEntry entry) {
    if (entry.name.empty()) {
        failBlockTextureManifest("Block texture manifest entry requires a name");
    }
    if (m_indicesByName.find(entry.name) != m_indicesByName.end()) {
        failBlockTextureManifest("Duplicate block texture manifest entry: " + entry.name);
    }

    if (entry.normalPath.has_value()) {
        m_hasNormalMaps = true;
    }
    if (entry.specularPath.has_value()) {
        m_hasSpecularMaps = true;
    }

    m_indicesByName.emplace(entry.name, m_entries.size());
    m_entries.push_back(std::move(entry));
}

void BlockTextureManifest::clear() {
    m_entries.clear();
    m_indicesByName.clear();
    m_hasNormalMaps = false;
    m_hasSpecularMaps = false;
}

const std::vector<BlockTextureManifestEntry>& BlockTextureManifest::entries() const {
    return m_entries;
}

const BlockTextureManifestEntry* BlockTextureManifest::find(const std::string& name) const {
    const auto it = m_indicesByName.find(name);
    if (it == m_indicesByName.end()) {
        return nullptr;
    }
    return &m_entries[it->second];
}

bool BlockTextureManifest::hasNormalMaps() const {
    return m_hasNormalMaps;
}

bool BlockTextureManifest::hasSpecularMaps() const {
    return m_hasSpecularMaps;
}

BlockTextureManifest buildBlockTextureManifest(const std::string& directory) {
    const std::vector<ClassifiedTextureFile> files = collectClassifiedTextureFiles(directory);

    std::unordered_map<std::string, PendingManifestEntry> pendingEntries;
    for (const ClassifiedTextureFile& file : files) {
        assignTexturePath(pendingEntries[file.textureName], file);
    }

    std::vector<std::string> names;
    names.reserve(pendingEntries.size());
    for (const auto& [name, entry] : pendingEntries) {
        if (!entry.albedoPath.has_value()) {
            failBlockTextureManifest("Block PBR texture requires a matching albedo texture: " + name);
        }
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());

    BlockTextureManifest manifest;
    for (const std::string& name : names) {
        const PendingManifestEntry& pending = pendingEntries.at(name);
        BlockTextureManifestEntry entry;
        entry.name = name;
        entry.albedoPath = pending.albedoPath.value();
        entry.normalPath = pending.normalPath;
        entry.specularPath = pending.specularPath;
        manifest.addEntry(std::move(entry));
    }
    return manifest;
}

} // namespace resource
