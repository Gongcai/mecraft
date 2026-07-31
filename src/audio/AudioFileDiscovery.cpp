#include "AudioFileDiscovery.h"

#include "AudioDecoders.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace audio {
namespace fs = std::filesystem;
namespace {

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string duplicateKeyForPath(const fs::path& path) {
    return lowerCopy(pathToUtf8(path.stem()));
}

fs::path withAudioExtension(const fs::path& stemPath, const std::string& extension) {
    fs::path candidate = stemPath;
    candidate += extension;
    return candidate;
}

} // namespace

std::string pathToUtf8(const fs::path& path) {
    return path.u8string();
}

std::optional<fs::path> findAudioFileByStem(const fs::path& directory, const fs::path& stemOrFilename) {
    const auto& registry = AudioDecoderRegistry::instance();
    const fs::path base = directory / stemOrFilename;

    if (base.has_extension() && registry.isSupportedExtension(base.extension().string())) {
        if (fs::exists(base) && fs::is_regular_file(base)) {
            return base;
        }
        return std::nullopt;
    }

    for (const std::string& extension : registry.supportedExtensions()) {
        const fs::path candidate = withAudioExtension(base, extension);
        if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
            return candidate;
        }
    }

    return std::nullopt;
}

std::vector<fs::path> listSupportedAudioFiles(const fs::path& directory) {
    std::vector<fs::path> files;
    if (!fs::exists(directory)) {
        return files;
    }

    const auto& registry = AudioDecoderRegistry::instance();
    std::unordered_map<std::string, fs::path> chosenByStem;
    std::unordered_map<std::string, int> priorityByStem;

    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const fs::path& path = entry.path();
        const std::string extension = path.extension().string();
        if (!registry.isSupportedExtension(extension)) {
            continue;
        }

        const std::string key = duplicateKeyForPath(path);
        const int priority = registry.extensionPriority(extension);
        const auto existingPriority = priorityByStem.find(key);
        if (existingPriority == priorityByStem.end() || priority < existingPriority->second) {
            chosenByStem[key] = path;
            priorityByStem[key] = priority;
        }
    }

    files.reserve(chosenByStem.size());
    for (const auto& [key, path] : chosenByStem) {
        static_cast<void>(key);
        files.push_back(path);
    }

    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        return lowerCopy(pathToUtf8(a.stem())) < lowerCopy(pathToUtf8(b.stem()));
    });
    return files;
}

} // namespace audio
