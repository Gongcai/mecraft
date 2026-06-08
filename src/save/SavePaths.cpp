#include "SavePaths.h"

#include <algorithm>
#include <cctype>

namespace save {

SavePaths::SavePaths(std::filesystem::path saveRoot)
    : m_root(std::move(saveRoot)) {}

std::filesystem::path SavePaths::chunksDir() const {
    return m_root / "chunks";
}

std::filesystem::path SavePaths::entitiesDir() const {
    return m_root / "entities";
}

std::filesystem::path SavePaths::overworldEntitiesPath() const {
    return entitiesDir() / "dimension_overworld.json";
}

std::filesystem::path SavePaths::chunkPath(int cx, int cz) const {
    return chunksDir() / ("c." + std::to_string(cx) + "." + std::to_string(cz) + ".mchk");
}

std::filesystem::path SavePaths::chunkTmpPath(int cx, int cz) const {
    return chunksDir() / ("c." + std::to_string(cx) + "." + std::to_string(cz) + ".mchk.tmp");
}

std::filesystem::path SavePaths::chunkBakPath(int cx, int cz) const {
    return chunksDir() / ("c." + std::to_string(cx) + "." + std::to_string(cz) + ".mchk.bak");
}

std::filesystem::path SavePaths::levelPath() const {
    return m_root / "level.json";
}

std::filesystem::path SavePaths::playersDir() const {
    return m_root / "players";
}

std::filesystem::path SavePaths::localPlayerPath() const {
    return playersDir() / "local.json";
}

std::filesystem::path SavePaths::screenshotPath() const {
    return m_root / "thumb.png";
}

void SavePaths::ensureDirectories() const {
    std::filesystem::create_directories(chunksDir());
    std::filesystem::create_directories(entitiesDir());
    std::filesystem::create_directories(playersDir());
}

std::string SavePaths::sanitizeWorldName(const std::string& name) {
    static constexpr const char* FORBIDDEN = R"(\/:*?"<>|)";
    std::string result;
    result.reserve(name.size());

    for (char c : name) {
        bool forbidden = false;
        for (const char* f = FORBIDDEN; *f; ++f) {
            if (c == *f) {
                forbidden = true;
                break;
            }
        }
        result += forbidden ? '_' : c;
    }

    // Trim leading/trailing whitespace
    auto start = result.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return "New World";
    }
    auto end = result.find_last_not_of(" \t\n\r");
    result = result.substr(start, end - start + 1);

    return result.empty() ? "New World" : result;
}

bool SavePaths::chunkFileExists(int cx, int cz) const {
    std::error_code ec;
    return std::filesystem::exists(chunkPath(cx, cz), ec);
}

} // namespace save
