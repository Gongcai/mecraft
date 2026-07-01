#include "BlockTextureManifest.h"

#include "BlockTextureCatalog.h"
#include "../third_party/stb/stb_image.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

namespace {

enum class TextureFileRole {
    Albedo,
    Normal,
    Specular,
};

struct ClassifiedTextureFile {
    std::string textureName;
    TextureFileRole role = TextureFileRole::Albedo;
    std::filesystem::path path;
    std::optional<resource::BlockTextureAnimationMetadata> animationMetadata;
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

std::optional<resource::BlockTextureAnimationMetadata> loadAnimationMetadata(const std::filesystem::path& imagePath) {
    const std::filesystem::path metadataPath(imagePath.string() + ".mcmeta");
    if (!std::filesystem::exists(metadataPath)) {
        return std::nullopt;
    }

    std::ifstream file(metadataPath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open block texture metadata: " + metadataPath.string());
    }

    nlohmann::json root;
    file >> root;
    const auto animationIt = root.find("animation");
    if (animationIt == root.end() || !animationIt->is_object()) {
        throw std::runtime_error("Block texture metadata requires an animation object: " + metadataPath.string());
    }

    resource::BlockTextureAnimationMetadata metadata;
    const auto frameTimeIt = animationIt->find("frametime");
    if (frameTimeIt != animationIt->end()) {
        if (!frameTimeIt->is_number_integer()) {
            throw std::runtime_error("Block texture animation frametime must be an integer: " + metadataPath.string());
        }
        metadata.frameTimeTicks = frameTimeIt->get<int>();
        if (metadata.frameTimeTicks <= 0) {
            throw std::runtime_error("Block texture animation frametime must be positive: " + metadataPath.string());
        }
    }

    const auto framesIt = animationIt->find("frames");
    if (framesIt != animationIt->end()) {
        if (!framesIt->is_array()) {
            throw std::runtime_error("Block texture animation frames must be an array: " + metadataPath.string());
        }
        if (framesIt->empty()) {
            throw std::runtime_error("Block texture animation frames must not be empty: " + metadataPath.string());
        }
        for (const nlohmann::json& frame : *framesIt) {
            int frameIndex = -1;
            if (frame.is_number_integer()) {
                frameIndex = frame.get<int>();
            } else if (frame.is_object()) {
                const auto indexIt = frame.find("index");
                if (indexIt == frame.end() || !indexIt->is_number_integer()) {
                    throw std::runtime_error("Block texture animation frame object requires an integer index: " +
                                             metadataPath.string());
                }
                frameIndex = indexIt->get<int>();
            } else {
                throw std::runtime_error("Block texture animation frame must be an integer or object: " +
                                         metadataPath.string());
            }
            if (frameIndex < 0) {
                throw std::runtime_error("Block texture animation frame index must be non-negative: " +
                                         metadataPath.string());
            }
            metadata.maxExplicitFrameIndex = std::max(metadata.maxExplicitFrameIndex, frameIndex);
        }
    }

    return metadata;
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
    file.animationMetadata = loadAnimationMetadata(path);

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

    if (!fs::exists(directory)) {
        throw std::runtime_error("Block texture directory does not exist: " + directory);
    }

    std::vector<ClassifiedTextureFile> files;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_regular_file() && hasPngExtension(entry.path())) {
            files.push_back(classifyTextureFile(entry.path()));
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
        throw std::runtime_error("Block texture directory contains no PNG files: " + directory);
    }

    return files;
}

struct PendingManifestEntry {
    std::optional<std::filesystem::path> albedoPath;
    std::optional<std::filesystem::path> normalPath;
    std::optional<std::filesystem::path> specularPath;
    std::optional<resource::BlockTextureAnimationMetadata> albedoAnimationMetadata;
    int albedoSourceIndex = -1;
    int normalSourceIndex = -1;
    int specularSourceIndex = -1;
};

void assignTexturePath(PendingManifestEntry& entry,
                       const ClassifiedTextureFile& file,
                       const int sourceIndex) {
    if (file.role == TextureFileRole::Albedo) {
        if (entry.albedoPath.has_value() && entry.albedoSourceIndex == sourceIndex) {
            throw std::runtime_error("Duplicate block albedo texture: " + file.textureName);
        }
        if (sourceIndex < entry.albedoSourceIndex) {
            return;
        }
        entry.albedoPath = file.path;
        entry.albedoAnimationMetadata = file.animationMetadata;
        entry.albedoSourceIndex = sourceIndex;
        return;
    }
    if (file.role == TextureFileRole::Normal) {
        if (entry.normalPath.has_value() && entry.normalSourceIndex == sourceIndex) {
            throw std::runtime_error("Duplicate block normal texture: " + file.textureName);
        }
        if (sourceIndex < entry.normalSourceIndex) {
            return;
        }
        entry.normalPath = file.path;
        entry.normalSourceIndex = sourceIndex;
        return;
    }
    if (entry.specularPath.has_value() && entry.specularSourceIndex == sourceIndex) {
        throw std::runtime_error("Duplicate block specular texture: " + file.textureName);
    }
    if (sourceIndex < entry.specularSourceIndex) {
        return;
    }
    entry.specularPath = file.path;
    entry.specularSourceIndex = sourceIndex;
}

struct ImageDimensions {
    int width = 0;
    int height = 0;
    int channels = 0;
};

ImageDimensions inspectImageDimensions(const std::filesystem::path& path) {
    ImageDimensions dimensions;
    if (!stbi_info(path.string().c_str(), &dimensions.width, &dimensions.height, &dimensions.channels)) {
        throw std::runtime_error("Failed to inspect block texture dimensions: " + path.string());
    }
    if (dimensions.width <= 0 || dimensions.height <= 0) {
        throw std::runtime_error("Invalid block texture dimensions: " + path.string());
    }
    return dimensions;
}

bool isVerticalAnimatedTexture(const BlockTextureCatalogEntry* catalogEntry) {
    return catalogEntry != nullptr &&
           catalogEntry->verticalFrames &&
           catalogEntry->animation.frameCount > 1;
}

bool hasAnimationMetadata(const resource::BlockTextureManifestEntry& entry) {
    return entry.animationMetadata.has_value();
}

bool hasVerticalAnimation(const resource::BlockTextureManifestEntry& entry,
                          const BlockTextureCatalogEntry* catalogEntry) {
    return hasAnimationMetadata(entry) || isVerticalAnimatedTexture(catalogEntry);
}

int verticalFrameCountFromDimensions(const ImageDimensions& dimensions,
                                     const std::filesystem::path& path,
                                     const char* roleName) {
    if (dimensions.width <= 0 || dimensions.height <= 0 || dimensions.height % dimensions.width != 0) {
        throw std::runtime_error(std::string("Block ") + roleName +
                                 " animated texture height must be a multiple of width: " + path.string());
    }
    return dimensions.height / dimensions.width;
}

void validateExplicitAnimationFrames(const resource::BlockTextureManifestEntry& entry,
                                     const int physicalFrameCount) {
    if (!entry.animationMetadata.has_value()) {
        return;
    }
    const int maxFrameIndex = entry.animationMetadata->maxExplicitFrameIndex;
    if (maxFrameIndex >= physicalFrameCount) {
        throw std::runtime_error("Block texture animation frame index exceeds source frame count: " +
                                 entry.albedoPath.string());
    }
}

void validateAlbedoDimensions(const resource::BlockTextureManifestEntry& entry,
                              const ImageDimensions& dimensions,
                              const BlockTextureCatalog& catalog) {
    const BlockTextureCatalogEntry* catalogEntry = catalog.find(entry.name);
    if (hasVerticalAnimation(entry, catalogEntry)) {
        if (!entry.animationMetadata.has_value() && dimensions.width == dimensions.height) {
            return;
        }
        const int physicalFrameCount = verticalFrameCountFromDimensions(dimensions, entry.albedoPath, "albedo");
        if (!entry.animationMetadata.has_value() &&
            catalogEntry != nullptr &&
            physicalFrameCount != catalogEntry->animation.frameCount) {
            throw std::runtime_error("Block animated albedo texture dimensions do not match catalog frames: " +
                                     entry.albedoPath.string());
        }
        validateExplicitAnimationFrames(entry, physicalFrameCount);
        return;
    }

    if (dimensions.width != dimensions.height) {
        throw std::runtime_error("Block albedo texture dimensions must be square: " +
                                 entry.albedoPath.string());
    }
}

void validateMaterialMapDimensions(const resource::BlockTextureManifestEntry& entry,
                                   const std::filesystem::path& path,
                                   const ImageDimensions& dimensions,
                                   const BlockTextureCatalog& catalog,
                                   const char* roleName) {
    const BlockTextureCatalogEntry* catalogEntry = catalog.find(entry.name);
    const bool animated = hasVerticalAnimation(entry, catalogEntry);
    const bool singleFrame = dimensions.width == dimensions.height;
    const bool verticalFrames = animated &&
                                dimensions.height % dimensions.width == 0;
    if (!singleFrame && !verticalFrames) {
        throw std::runtime_error(std::string("Block ") + roleName +
                                 " texture dimensions do not match its frame layout: " +
                                 path.string());
    }
}

void expandTileSize(const int candidateTileSize,
                    int& tileSize,
                    const std::filesystem::path& path,
                    const char* roleName) {
    if (candidateTileSize <= 0) {
        throw std::runtime_error(std::string("Block ") + roleName +
                                 " texture tile size must be positive: " + path.string());
    }
    tileSize = std::max(tileSize, candidateTileSize);
}

} // namespace

namespace resource {

void BlockTextureManifest::addEntry(BlockTextureManifestEntry entry) {
    if (entry.name.empty()) {
        throw std::runtime_error("Block texture manifest entry requires a name");
    }
    if (m_indicesByName.find(entry.name) != m_indicesByName.end()) {
        throw std::runtime_error("Duplicate block texture manifest entry: " + entry.name);
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
    return buildBlockTextureManifest(std::vector<std::string>{directory});
}

BlockTextureManifest buildBlockTextureManifest(const std::vector<std::string>& directories) {
    if (directories.empty()) {
        throw std::runtime_error("Block texture manifest requires at least one source directory");
    }
    std::unordered_map<std::string, PendingManifestEntry> pendingEntries;
    for (size_t sourceIndex = 0; sourceIndex < directories.size(); ++sourceIndex) {
        const std::vector<ClassifiedTextureFile> files = collectClassifiedTextureFiles(directories[sourceIndex]);
        for (const ClassifiedTextureFile& file : files) {
            assignTexturePath(pendingEntries[file.textureName], file, static_cast<int>(sourceIndex));
        }
    }

    std::vector<std::string> names;
    names.reserve(pendingEntries.size());
    for (const auto& [name, entry] : pendingEntries) {
        if (!entry.albedoPath.has_value()) {
            throw std::runtime_error("Block PBR texture requires a matching albedo texture: " + name);
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
        entry.animationMetadata = pending.albedoAnimationMetadata;
        manifest.addEntry(std::move(entry));
    }
    return manifest;
}

BlockTextureTileSizes inferBlockTextureTileSizes(const BlockTextureManifest& manifest,
                                                 const BlockTextureCatalog& catalog) {
    BlockTextureTileSizes tileSizes;
    const std::vector<BlockTextureManifestEntry>& entries = manifest.entries();
    if (entries.empty()) {
        throw std::runtime_error("Block texture tile size inference requires at least one manifest entry");
    }

    for (const BlockTextureManifestEntry& entry : entries) {
        const ImageDimensions albedoDimensions = inspectImageDimensions(entry.albedoPath);
        validateAlbedoDimensions(entry, albedoDimensions, catalog);
        expandTileSize(albedoDimensions.width, tileSizes.albedo, entry.albedoPath, "albedo");

        if (entry.normalPath.has_value()) {
            const ImageDimensions normalDimensions = inspectImageDimensions(entry.normalPath.value());
            validateMaterialMapDimensions(entry, entry.normalPath.value(), normalDimensions,
                                          catalog, "normal");
            expandTileSize(normalDimensions.width, tileSizes.normal, entry.normalPath.value(), "normal");
        }

        if (entry.specularPath.has_value()) {
            const ImageDimensions specularDimensions = inspectImageDimensions(entry.specularPath.value());
            validateMaterialMapDimensions(entry, entry.specularPath.value(), specularDimensions,
                                          catalog, "specular");
            expandTileSize(specularDimensions.width, tileSizes.specular, entry.specularPath.value(), "specular");
        }
    }

    if (tileSizes.albedo <= 0) {
        throw std::runtime_error("Block albedo tile size inference failed");
    }
    return tileSizes;
}

} // namespace resource
