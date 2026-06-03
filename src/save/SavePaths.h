#ifndef MECRAFT_SAVE_PATHS_H
#define MECRAFT_SAVE_PATHS_H

// SavePaths: unified path computation for the save system.
// All paths are relative to a save root directory, typically:
//   <saveRoot>/<worldName>/
// where <worldName> has been sanitized for filesystem safety.

#include <filesystem>
#include <string>

namespace save {

class SavePaths {
public:
    explicit SavePaths(std::filesystem::path saveRoot);

    /// Root directory for this save (e.g. "saves/New World").
    [[nodiscard]] const std::filesystem::path& root() const { return m_root; }

    /// Chunks directory: <root>/chunks/
    [[nodiscard]] std::filesystem::path chunksDir() const;

    /// Chunk file path: <root>/chunks/c.<cx>.<cz>.mchk
    [[nodiscard]] std::filesystem::path chunkPath(int cx, int cz) const;

    /// Temp file for atomic writes: <root>/chunks/c.<cx>.<cz>.mchk.tmp
    [[nodiscard]] std::filesystem::path chunkTmpPath(int cx, int cz) const;

    /// Backup file: <root>/chunks/c.<cx>.<cz>.mchk.bak
    [[nodiscard]] std::filesystem::path chunkBakPath(int cx, int cz) const;

    /// Level metadata: <root>/level.json
    [[nodiscard]] std::filesystem::path levelPath() const;

    /// Players directory: <root>/players/
    [[nodiscard]] std::filesystem::path playersDir() const;

    /// Local player file: <root>/players/local.json
    [[nodiscard]] std::filesystem::path localPlayerPath() const;

    /// Ensure the directory structure exists (creates dirs if needed).
    void ensureDirectories() const;

    /// Sanitize a world name for use as a directory name.
    /// Replaces forbidden characters with '_', trims whitespace, defaults to "New World".
    [[nodiscard]] static std::string sanitizeWorldName(const std::string& name);

    /// Check if a chunk file exists on disk.
    [[nodiscard]] bool chunkFileExists(int cx, int cz) const;

private:
    std::filesystem::path m_root;
};

} // namespace save

#endif // MECRAFT_SAVE_PATHS_H
