#ifndef MECRAFT_AUDIO_FILE_DISCOVERY_H
#define MECRAFT_AUDIO_FILE_DISCOVERY_H

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace audio {

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path);

[[nodiscard]] std::optional<std::filesystem::path> findAudioFileByStem(
    const std::filesystem::path& directory,
    const std::filesystem::path& stemOrFilename);

[[nodiscard]] std::vector<std::filesystem::path> listSupportedAudioFiles(
    const std::filesystem::path& directory);

} // namespace audio

#endif // MECRAFT_AUDIO_FILE_DISCOVERY_H
