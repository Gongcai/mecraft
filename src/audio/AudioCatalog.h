#ifndef MECRAFT_AUDIO_CATALOG_H
#define MECRAFT_AUDIO_CATALOG_H

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace audio {

struct SoundVariant {
    std::filesystem::path filePath;
    float weight = 1.0f;
};

struct SoundEntry {
    std::string id;
    std::string group = "sfx";
    std::vector<SoundVariant> variants;
    float volume = 1.0f;
    bool preload = true;
};

class AudioCatalog {
public:
    [[nodiscard]] bool loadFromFile(const std::filesystem::path& manifestPath,
                                    const std::filesystem::path& rootDirectory, const std::string& defaultGroup,
                                    bool defaultPreload, std::string& error);

    [[nodiscard]] const SoundEntry* find(const std::string& soundId) const;
    [[nodiscard]] const std::vector<std::string>& soundIds() const { return m_order; }
    [[nodiscard]] std::vector<std::string> soundIdsByGroup(const std::string& group) const;
    [[nodiscard]] size_t size() const { return m_entries.size(); }

private:
    std::unordered_map<std::string, SoundEntry> m_entries;
    std::vector<std::string> m_order;
};

} // namespace audio

#endif // MECRAFT_AUDIO_CATALOG_H
