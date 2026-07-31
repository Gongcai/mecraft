#ifndef MECRAFT_BLOCK_TEXTURE_MANIFEST_H
#define MECRAFT_BLOCK_TEXTURE_MANIFEST_H

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace resource {

struct BlockTextureManifestEntry {
    std::string name;
    std::filesystem::path albedoPath;
    std::optional<std::filesystem::path> normalPath;
    std::optional<std::filesystem::path> specularPath;
};

class BlockTextureManifest {
public:
    void addEntry(BlockTextureManifestEntry entry);
    void clear();

    [[nodiscard]] const std::vector<BlockTextureManifestEntry>& entries() const;
    [[nodiscard]] const BlockTextureManifestEntry* find(const std::string& name) const;
    [[nodiscard]] bool hasNormalMaps() const;
    [[nodiscard]] bool hasSpecularMaps() const;

private:
    std::vector<BlockTextureManifestEntry> m_entries;
    std::unordered_map<std::string, size_t> m_indicesByName;
    bool m_hasNormalMaps = false;
    bool m_hasSpecularMaps = false;
};

[[nodiscard]] BlockTextureManifest buildBlockTextureManifest(const std::string& directory);
[[nodiscard]] BlockTextureManifest
buildBlockTextureManifest(const std::string& directory, const std::unordered_set<std::string>& registeredTextureNames);

} // namespace resource

#endif // MECRAFT_BLOCK_TEXTURE_MANIFEST_H
