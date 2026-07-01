#include "ResourcePackArchiveExtractor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <system_error>
#include <stdexcept>
#include <string>
#include <unordered_set>

#include <zip.h>

namespace {

constexpr const char* kCacheDirectoryName = ".cache";
constexpr const char* kResourcePackCacheDirectoryName = "resourcepacks";
constexpr const char* kManifestFileName = ".mecraft_resource_pack_cache";
constexpr std::size_t kCopyBufferSize = 1024U * 128U;

std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

bool hasZipExtension(const std::filesystem::path& path) {
    return toLowerAscii(path.extension().string()) == ".zip";
}

std::string filesystemErrorMessage(const char* operation,
                                   const std::filesystem::path& path,
                                   const std::error_code& errorCode) {
    return std::string(operation) + ": " + path.string() + " (" + errorCode.message() + ")";
}

std::string filesystemErrorMessage(const char* operation,
                                   const std::filesystem::path& source,
                                   const std::filesystem::path& destination,
                                   const std::error_code& errorCode) {
    return std::string(operation) + ": " + source.string() + " -> " + destination.string() + " (" +
           errorCode.message() + ")";
}

void createDirectoryTree(const std::filesystem::path& path, const char* operation) {
    std::error_code errorCode;
    std::filesystem::create_directories(path, errorCode);
    if (errorCode) {
        throw std::runtime_error(filesystemErrorMessage(operation, path, errorCode));
    }
}

void removeDirectoryTree(const std::filesystem::path& path, const char* operation) {
    std::error_code errorCode;
    std::filesystem::remove_all(path, errorCode);
    if (errorCode) {
        throw std::runtime_error(filesystemErrorMessage(operation, path, errorCode));
    }
}

void renameDirectoryTree(const std::filesystem::path& source,
                         const std::filesystem::path& destination,
                         const char* operation) {
    std::error_code errorCode;
    std::filesystem::rename(source, destination, errorCode);
    if (errorCode) {
        throw std::runtime_error(filesystemErrorMessage(operation, source, destination, errorCode));
    }
}

long long fileTimestampToken(const std::filesystem::path& path) {
    const auto timestamp = std::filesystem::last_write_time(path).time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(timestamp).count();
}

std::string expectedManifestText(const std::filesystem::path& archivePath) {
    return std::to_string(static_cast<unsigned long long>(std::filesystem::file_size(archivePath))) + "\n" +
           std::to_string(fileTimestampToken(archivePath)) + "\n";
}

bool cacheIsCurrent(const std::filesystem::path& archivePath, const std::filesystem::path& targetRoot) {
    const std::filesystem::path manifestPath = targetRoot / kManifestFileName;
    if (!std::filesystem::exists(manifestPath) || !std::filesystem::is_regular_file(manifestPath)) {
        return false;
    }

    std::ifstream file(manifestPath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open resource pack cache manifest: " + manifestPath.string());
    }

    const std::string actual((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return actual == expectedManifestText(archivePath);
}

void writeCacheManifest(const std::filesystem::path& archivePath, const std::filesystem::path& targetRoot) {
    const std::filesystem::path manifestPath = targetRoot / kManifestFileName;
    std::ofstream file(manifestPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to write resource pack cache manifest: " + manifestPath.string());
    }
    file << expectedManifestText(archivePath);
}

void throwZipOpenError(const std::filesystem::path& archivePath, const int errorCode) {
    zip_error_t error;
    zip_error_init_with_code(&error, errorCode);
    const std::string message = zip_error_strerror(&error);
    zip_error_fini(&error);
    throw std::runtime_error("Failed to open resource pack archive: " + archivePath.string() + " (" + message + ")");
}

std::string normalizedEntryName(const char* entryName, const std::filesystem::path& archivePath) {
    if (entryName == nullptr || entryName[0] == '\0') {
        throw std::runtime_error("Resource pack archive contains an empty entry name: " + archivePath.string());
    }

    std::string normalized(entryName);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    if (normalized.front() == '/' ||
        normalized.find(':') != std::string::npos ||
        normalized.find("//") != std::string::npos) {
        throw std::runtime_error("Resource pack archive contains an unsafe entry path: " + normalized);
    }

    std::filesystem::path path(normalized);
    for (const std::filesystem::path& part : path) {
        const std::string token = part.string();
        if (token == ".." || token == ".") {
            throw std::runtime_error("Resource pack archive contains a relative entry path: " + normalized);
        }
    }

    return normalized;
}

bool isDirectoryEntry(const std::string& entryName) {
    return !entryName.empty() && entryName.back() == '/';
}

std::string firstPathComponent(const std::string& entryName) {
    const std::size_t slash = entryName.find('/');
    if (slash == std::string::npos) {
        return entryName;
    }
    return entryName.substr(0, slash);
}

bool hasRootLevelMinecraftResource(const std::string& entryName) {
    return entryName == "pack.mcmeta" ||
           entryName.rfind("assets/", 0) == 0 ||
           entryName.rfind("textures/", 0) == 0 ||
           entryName.rfind("optifine/", 0) == 0 ||
           entryName.rfind("mcpatcher/", 0) == 0;
}

std::string detectSingleTopLevelPrefix(zip_t* archive, const std::filesystem::path& archivePath) {
    const zip_int64_t entryCount = zip_get_num_entries(archive, 0);
    if (entryCount < 0) {
        throw std::runtime_error("Failed to enumerate resource pack archive: " + archivePath.string());
    }

    std::unordered_set<std::string> roots;
    bool hasRootResource = false;
    for (zip_uint64_t index = 0; index < static_cast<zip_uint64_t>(entryCount); ++index) {
        const std::string entryName = normalizedEntryName(zip_get_name(archive, index, 0), archivePath);
        if (isDirectoryEntry(entryName)) {
            continue;
        }
        if (hasRootLevelMinecraftResource(entryName)) {
            hasRootResource = true;
        }
        roots.insert(firstPathComponent(entryName));
    }

    if (!hasRootResource && roots.size() == 1U) {
        return *roots.begin() + "/";
    }
    return {};
}

std::string stripPrefix(std::string entryName, const std::string& prefix) {
    if (!prefix.empty() && entryName.rfind(prefix, 0) == 0) {
        entryName.erase(0, prefix.size());
    }
    return entryName;
}

void extractArchiveEntry(zip_t* archive,
                         const zip_uint64_t index,
                         const std::filesystem::path& targetRoot,
                         const std::string& entryName,
                         const std::filesystem::path& archivePath) {
    if (entryName.empty() || isDirectoryEntry(entryName)) {
        return;
    }

    const std::filesystem::path outputPath = targetRoot / std::filesystem::path(entryName);
    createDirectoryTree(outputPath.parent_path(), "Failed to create resource pack entry directory");

    zip_file_t* file = zip_fopen_index(archive, index, 0);
    if (file == nullptr) {
        throw std::runtime_error("Failed to open resource pack archive entry: " + entryName);
    }

    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        zip_fclose(file);
        throw std::runtime_error("Failed to create extracted resource pack file: " + outputPath.string());
    }

    std::array<char, kCopyBufferSize> buffer{};
    while (true) {
        const zip_int64_t bytesRead = zip_fread(file, buffer.data(), buffer.size());
        if (bytesRead < 0) {
            zip_fclose(file);
            throw std::runtime_error("Failed to read resource pack archive entry: " + archivePath.string() + ":" +
                                     entryName);
        }
        if (bytesRead == 0) {
            break;
        }
        output.write(buffer.data(), static_cast<std::streamsize>(bytesRead));
        if (!output.good()) {
            zip_fclose(file);
            throw std::runtime_error("Failed to write extracted resource pack file: " + outputPath.string());
        }
    }

    if (zip_fclose(file) != 0) {
        throw std::runtime_error("Failed to close resource pack archive entry: " + entryName);
    }
}

void extractArchive(const std::filesystem::path& archivePath, const std::filesystem::path& targetRoot) {
    int errorCode = 0;
    zip_t* archive = zip_open(archivePath.string().c_str(), ZIP_RDONLY, &errorCode);
    if (archive == nullptr) {
        throwZipOpenError(archivePath, errorCode);
    }

    const std::string prefix = detectSingleTopLevelPrefix(archive, archivePath);
    const std::filesystem::path tempRoot = targetRoot.parent_path() / (targetRoot.filename().string() + ".extracting");

    createDirectoryTree(targetRoot.parent_path(), "Failed to create resource pack cache directory");
    removeDirectoryTree(tempRoot, "Failed to clean resource pack extraction directory");
    createDirectoryTree(tempRoot, "Failed to create resource pack extraction directory");

    const zip_int64_t entryCount = zip_get_num_entries(archive, 0);
    if (entryCount < 0) {
        zip_discard(archive);
        throw std::runtime_error("Failed to enumerate resource pack archive: " + archivePath.string());
    }

    for (zip_uint64_t index = 0; index < static_cast<zip_uint64_t>(entryCount); ++index) {
        std::string entryName = normalizedEntryName(zip_get_name(archive, index, 0), archivePath);
        entryName = stripPrefix(std::move(entryName), prefix);
        extractArchiveEntry(archive, index, tempRoot, entryName, archivePath);
    }

    if (zip_close(archive) != 0) {
        throw std::runtime_error("Failed to close resource pack archive: " + archivePath.string());
    }

    writeCacheManifest(archivePath, tempRoot);
    removeDirectoryTree(targetRoot, "Failed to remove old resource pack cache directory");
    renameDirectoryTree(tempRoot, targetRoot, "Failed to install extracted resource pack cache");
}

std::filesystem::path cacheRootForArchives(const std::filesystem::path& resourcePacksRoot) {
    return resourcePacksRoot.parent_path() / kCacheDirectoryName / kResourcePackCacheDirectoryName;
}

} // namespace

namespace resource {

std::vector<std::filesystem::path> prepareResourcePackArchives(const std::filesystem::path& resourcePacksRoot) {
    std::vector<std::filesystem::path> extractedRoots;
    if (!std::filesystem::exists(resourcePacksRoot) || !std::filesystem::is_directory(resourcePacksRoot)) {
        return extractedRoots;
    }

    const std::filesystem::path cacheRoot = cacheRootForArchives(resourcePacksRoot);
    createDirectoryTree(cacheRoot, "Failed to create resource pack archive cache root");

    std::vector<std::filesystem::path> archives;
    for (const auto& entry : std::filesystem::directory_iterator(resourcePacksRoot)) {
        if (entry.is_regular_file() && hasZipExtension(entry.path())) {
            archives.push_back(entry.path());
        }
    }
    std::sort(archives.begin(), archives.end());

    extractedRoots.reserve(archives.size());
    for (const std::filesystem::path& archivePath : archives) {
        const std::filesystem::path targetRoot = cacheRoot / archivePath.stem();
        if (!cacheIsCurrent(archivePath, targetRoot)) {
            extractArchive(archivePath, targetRoot);
        }
        extractedRoots.push_back(targetRoot);
    }

    return extractedRoots;
}

} // namespace resource
