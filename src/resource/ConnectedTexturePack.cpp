#include "ConnectedTexturePack.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace {

std::string trimAscii(const std::string& value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }
    return value.substr(first, last - first);
}

std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

bool isIntegerText(const std::string& value) {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(),
                       [](const unsigned char c) {
                           return std::isdigit(c) != 0;
                       });
}

std::vector<std::string> splitWhitespace(const std::string& value) {
    std::vector<std::string> result;
    size_t cursor = 0;
    while (cursor < value.size()) {
        while (cursor < value.size() && std::isspace(static_cast<unsigned char>(value[cursor])) != 0) {
            ++cursor;
        }
        const size_t tokenStart = cursor;
        while (cursor < value.size() && std::isspace(static_cast<unsigned char>(value[cursor])) == 0) {
            ++cursor;
        }
        if (tokenStart < cursor) {
            result.push_back(value.substr(tokenStart, cursor - tokenStart));
        }
    }
    return result;
}

std::unordered_map<std::string, std::string> loadProperties(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open connected texture properties: " + path.string());
    }

    std::unordered_map<std::string, std::string> values;
    std::string line;
    while (std::getline(file, line)) {
        line = trimAscii(line);
        if (line.empty() || line.front() == '#' || line.front() == '!') {
            continue;
        }

        const size_t separator = line.find('=');
        if (separator == std::string::npos) {
            throw std::runtime_error("Connected texture property requires key=value: " + path.string());
        }

        std::string key = trimAscii(line.substr(0, separator));
        std::string value = trimAscii(line.substr(separator + 1));
        if (key.empty()) {
            throw std::runtime_error("Connected texture property key must not be empty: " + path.string());
        }
        values[toLowerAscii(std::move(key))] = std::move(value);
    }
    return values;
}

std::optional<resource::BlockTextureAnimationMetadata> loadAnimationMetadata(const std::filesystem::path& imagePath) {
    const std::filesystem::path metadataPath(imagePath.string() + ".mcmeta");
    if (!std::filesystem::exists(metadataPath)) {
        return std::nullopt;
    }

    std::ifstream file(metadataPath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open connected texture metadata: " + metadataPath.string());
    }

    nlohmann::json root;
    file >> root;
    const auto animationIt = root.find("animation");
    if (animationIt == root.end() || !animationIt->is_object()) {
        throw std::runtime_error("Connected texture metadata requires an animation object: " + metadataPath.string());
    }

    resource::BlockTextureAnimationMetadata metadata;
    const auto frameTimeIt = animationIt->find("frametime");
    if (frameTimeIt != animationIt->end()) {
        if (!frameTimeIt->is_number_integer()) {
            throw std::runtime_error("Connected texture animation frametime must be an integer: " +
                                     metadataPath.string());
        }
        metadata.frameTimeTicks = frameTimeIt->get<int>();
        if (metadata.frameTimeTicks <= 0) {
            throw std::runtime_error("Connected texture animation frametime must be positive: " +
                                     metadataPath.string());
        }
    }

    const auto framesIt = animationIt->find("frames");
    if (framesIt != animationIt->end()) {
        if (!framesIt->is_array()) {
            throw std::runtime_error("Connected texture animation frames must be an array: " + metadataPath.string());
        }
        if (framesIt->empty()) {
            throw std::runtime_error("Connected texture animation frames must not be empty: " + metadataPath.string());
        }
        for (const nlohmann::json& frame : *framesIt) {
            int frameIndex = -1;
            if (frame.is_number_integer()) {
                frameIndex = frame.get<int>();
            } else if (frame.is_object()) {
                const auto indexIt = frame.find("index");
                if (indexIt == frame.end() || !indexIt->is_number_integer()) {
                    throw std::runtime_error("Connected texture animation frame object requires an integer index: " +
                                             metadataPath.string());
                }
                frameIndex = indexIt->get<int>();
            } else {
                throw std::runtime_error("Connected texture animation frame must be an integer or object: " +
                                         metadataPath.string());
            }
            if (frameIndex < 0) {
                throw std::runtime_error("Connected texture animation frame index must be non-negative: " +
                                         metadataPath.string());
            }
            metadata.maxExplicitFrameIndex = std::max(metadata.maxExplicitFrameIndex, frameIndex);
        }
    }

    return metadata;
}

std::optional<resource::ConnectedTextureMethod> parseMethod(const std::string& methodText) {
    const std::string method = toLowerAscii(methodText);
    if (method == "ctm") {
        return resource::ConnectedTextureMethod::Ctm;
    }
    if (method == "fixed") {
        return resource::ConnectedTextureMethod::Fixed;
    }
    if (method == "horizontal") {
        return resource::ConnectedTextureMethod::Horizontal;
    }
    if (method == "random") {
        return resource::ConnectedTextureMethod::Random;
    }
    if (method == "repeat") {
        return resource::ConnectedTextureMethod::Repeat;
    }
    if (method == "vertical") {
        return resource::ConnectedTextureMethod::Vertical;
    }
    return std::nullopt;
}

std::string normalizeMatchTextureName(std::string value) {
    value = toLowerAscii(trimAscii(std::move(value)));
    constexpr const char* namespacePrefix = "minecraft:";
    if (value.rfind(namespacePrefix, 0) == 0) {
        value.erase(0, std::char_traits<char>::length(namespacePrefix));
    }

    std::replace(value.begin(), value.end(), '\\', '/');
    constexpr const char* blockPath = "textures/block/";
    constexpr const char* blocksPath = "textures/blocks/";
    if (value.rfind(blockPath, 0) == 0) {
        value.erase(0, std::char_traits<char>::length(blockPath));
    } else if (value.rfind(blocksPath, 0) == 0) {
        value.erase(0, std::char_traits<char>::length(blocksPath));
    }

    if (value.size() > 4 && value.compare(value.size() - 4, 4, ".png") == 0) {
        value.erase(value.size() - 4);
    }

    const size_t slash = value.find_last_of('/');
    if (slash != std::string::npos) {
        value.erase(0, slash + 1);
    }
    return value;
}

std::vector<std::string> parseTileTokens(const std::string& value) {
    std::vector<std::string> tokens;
    for (const std::string& token : splitWhitespace(value)) {
        const size_t dash = token.find('-');
        if (dash != std::string::npos) {
            const std::string beginText = token.substr(0, dash);
            const std::string endText = token.substr(dash + 1);
            if (isIntegerText(beginText) && isIntegerText(endText)) {
                const int begin = std::stoi(beginText);
                const int end = std::stoi(endText);
                if (begin <= 0 || end < begin) {
                    throw std::runtime_error("Connected texture tile range is invalid: " + token);
                }
                for (int tile = begin; tile <= end; ++tile) {
                    tokens.push_back(std::to_string(tile));
                }
                continue;
            }
        }
        tokens.push_back(token);
    }
    return tokens;
}

std::filesystem::path minecraftRootForCtmDirectory(const std::filesystem::path& ctmDirectory) {
    return ctmDirectory.parent_path().parent_path();
}

std::filesystem::path tilePathForToken(const std::filesystem::path& propertiesPath,
                                       const std::filesystem::path& ctmDirectory,
                                       std::string token) {
    token = trimAscii(std::move(token));
    if (token.empty() || token == "<default>") {
        return {};
    }

    std::replace(token.begin(), token.end(), '\\', '/');
    std::filesystem::path relative(token);
    if (!relative.has_extension()) {
        relative += ".png";
    }

    const std::string generic = relative.generic_string();
    if (generic.rfind("textures/", 0) == 0) {
        return minecraftRootForCtmDirectory(ctmDirectory) / relative;
    }
    return propertiesPath.parent_path() / relative;
}

std::optional<std::filesystem::path> existingSiblingWithSuffix(const std::filesystem::path& albedoPath,
                                                               const std::vector<const char*>& suffixes) {
    const std::filesystem::path directory = albedoPath.parent_path();
    const std::string stem = albedoPath.stem().string();
    for (const char* suffix : suffixes) {
        const std::filesystem::path candidate = directory / (stem + suffix + ".png");
        if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::vector<resource::ConnectedTextureReplacement> parseReplacements(
    const std::filesystem::path& propertiesPath,
    const std::filesystem::path& ctmDirectory) {
    const std::unordered_map<std::string, std::string> properties = loadProperties(propertiesPath);

    const auto methodIt = properties.find("method");
    if (methodIt == properties.end()) {
        return {};
    }
    const std::optional<resource::ConnectedTextureMethod> method = parseMethod(methodIt->second);
    if (!method.has_value()) {
        return {};
    }

    const auto tilesIt = properties.find("tiles");
    const auto matchTilesIt = properties.find("matchtiles");
    if (tilesIt == properties.end() || matchTilesIt == properties.end()) {
        return {};
    }

    std::filesystem::path representativeTile;
    for (const std::string& tileToken : parseTileTokens(tilesIt->second)) {
        representativeTile = tilePathForToken(propertiesPath, ctmDirectory, tileToken);
        if (!representativeTile.empty()) {
            break;
        }
    }
    if (representativeTile.empty() ||
        !std::filesystem::exists(representativeTile) ||
        !std::filesystem::is_regular_file(representativeTile)) {
        return {};
    }

    const std::vector<std::string> matchTiles = splitWhitespace(matchTilesIt->second);
    if (matchTiles.empty()) {
        return {};
    }

    const std::optional<resource::BlockTextureAnimationMetadata> animationMetadata =
        loadAnimationMetadata(representativeTile);

    std::vector<resource::ConnectedTextureReplacement> replacements;
    replacements.reserve(matchTiles.size());
    for (const std::string& matchTile : matchTiles) {
        resource::ConnectedTextureReplacement replacement;
        replacement.matchTextureName = normalizeMatchTextureName(matchTile);
        if (replacement.matchTextureName.empty()) {
            continue;
        }
        replacement.albedoPath = representativeTile;
        replacement.normalPath = existingSiblingWithSuffix(representativeTile, {"_n", "_normal"});
        replacement.specularPath = existingSiblingWithSuffix(representativeTile, {"_s", "_spec", "_specular"});
        replacement.animationMetadata = animationMetadata;
        replacement.method = method.value();
        replacements.push_back(std::move(replacement));
    }
    return replacements;
}

} // namespace

namespace resource {

std::vector<ConnectedTextureReplacement> collectConnectedTextureReplacements(
    const std::vector<std::string>& directories) {
    namespace fs = std::filesystem;

    std::vector<ConnectedTextureReplacement> replacements;
    for (const std::string& directoryText : directories) {
        const fs::path directory(directoryText);
        if (!fs::exists(directory) || !fs::is_directory(directory)) {
            continue;
        }

        std::vector<fs::path> propertyFiles;
        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file() && toLowerAscii(entry.path().extension().string()) == ".properties") {
                propertyFiles.push_back(entry.path());
            }
        }
        std::sort(propertyFiles.begin(), propertyFiles.end());

        for (const fs::path& propertyFile : propertyFiles) {
            std::vector<ConnectedTextureReplacement> parsedReplacements = parseReplacements(propertyFile, directory);
            for (ConnectedTextureReplacement& replacement : parsedReplacements) {
                replacements.push_back(std::move(replacement));
            }
        }
    }

    return replacements;
}

} // namespace resource
