#ifndef MECRAFT_BLOCK_TEXTURE_CATALOG_H
#define MECRAFT_BLOCK_TEXTURE_CATALOG_H

#include <cstdint>
#include <string>
#include <unordered_map>

struct TextureAnimationInfo {
    int firstLayer = 0;
    int frameCount = 1;
    float fps = 0.0f;
    bool isAnimated = false;
};

enum class ResourceTextureTint : uint8_t {
    None = 0,
    Grass = 1,
    Foliage = 2,
};

struct BlockTextureCatalogEntry {
    TextureAnimationInfo animation;
    ResourceTextureTint tint = ResourceTextureTint::None;
    bool verticalFrames = false;
    bool topFrameFirst = true;
};

class BlockTextureCatalog {
public:
    using EntryMap = std::unordered_map<std::string, BlockTextureCatalogEntry>;

    void load(const std::string& textureConfigPath);
    void clear();

    [[nodiscard]] const BlockTextureCatalogEntry* find(const std::string& name) const;
    [[nodiscard]] BlockTextureCatalogEntry* findMutable(const std::string& name);
    [[nodiscard]] ResourceTextureTint tintFor(const std::string& name) const;

    [[nodiscard]] const EntryMap& entries() const;
    [[nodiscard]] EntryMap& entries();

private:
    EntryMap m_entries;
};

#endif // MECRAFT_BLOCK_TEXTURE_CATALOG_H
