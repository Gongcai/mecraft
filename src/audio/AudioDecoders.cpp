#include "AudioDecoders.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <vorbis/vorbisfile.h>

namespace audio {
namespace {

class WavPcmDecoder final : public IAudioDecoder {
public:
    [[nodiscard]] const std::vector<std::string>& extensions() const override {
        static const std::vector<std::string> kExtensions{".wav"};
        return kExtensions;
    }

    [[nodiscard]] bool decode(const std::string& filepath,
                              DecodedAudio& out,
                              std::string& error) const override {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            error = "failed to open file";
            return false;
        }

        char riff[4] = {};
        uint32_t fileSize = 0;
        char wave[4] = {};
        file.read(riff, sizeof(riff));
        file.read(reinterpret_cast<char*>(&fileSize), sizeof(fileSize));
        file.read(wave, sizeof(wave));
        static_cast<void>(fileSize);

        if (!file ||
            std::strncmp(riff, "RIFF", 4) != 0 ||
            std::strncmp(wave, "WAVE", 4) != 0) {
            error = "invalid RIFF/WAVE header";
            return false;
        }

        bool sawFormat = false;
        bool sawData = false;
        uint16_t audioFormat = 0;
        uint16_t channels = 0;
        uint32_t sampleRate = 0;
        uint32_t byteRate = 0;
        uint16_t blockAlign = 0;
        uint16_t bitsPerSample = 0;
        std::vector<char> pcm;

        while (file) {
            char chunkId[4] = {};
            uint32_t chunkSize = 0;
            file.read(chunkId, sizeof(chunkId));
            file.read(reinterpret_cast<char*>(&chunkSize), sizeof(chunkSize));
            if (!file) {
                break;
            }

            const std::streamoff paddedChunkSize =
                static_cast<std::streamoff>(chunkSize + (chunkSize & 1u));

            if (std::strncmp(chunkId, "fmt ", 4) == 0) {
                if (chunkSize < 16) {
                    error = "invalid fmt chunk";
                    return false;
                }

                file.read(reinterpret_cast<char*>(&audioFormat), sizeof(audioFormat));
                file.read(reinterpret_cast<char*>(&channels), sizeof(channels));
                file.read(reinterpret_cast<char*>(&sampleRate), sizeof(sampleRate));
                file.read(reinterpret_cast<char*>(&byteRate), sizeof(byteRate));
                file.read(reinterpret_cast<char*>(&blockAlign), sizeof(blockAlign));
                file.read(reinterpret_cast<char*>(&bitsPerSample), sizeof(bitsPerSample));
                if (!file) {
                    error = "truncated fmt chunk";
                    return false;
                }

                if (chunkSize > 16) {
                    file.seekg(static_cast<std::streamoff>(chunkSize - 16), std::ios::cur);
                }
                if (chunkSize & 1u) {
                    file.seekg(1, std::ios::cur);
                }
                sawFormat = true;
                continue;
            }

            if (std::strncmp(chunkId, "data", 4) == 0) {
                if (chunkSize > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
                    error = "WAV data chunk is too large";
                    return false;
                }

                pcm.resize(static_cast<size_t>(chunkSize));
                file.read(pcm.data(), static_cast<std::streamsize>(chunkSize));
                if (!file) {
                    error = "truncated data chunk";
                    return false;
                }
                if (chunkSize & 1u) {
                    file.seekg(1, std::ios::cur);
                }
                sawData = true;
                continue;
            }

            file.seekg(paddedChunkSize, std::ios::cur);
        }

        if (!sawFormat) {
            error = "missing fmt chunk";
            return false;
        }
        if (!sawData) {
            error = "missing data chunk";
            return false;
        }
        if (audioFormat != 1) {
            error = "only PCM WAV is supported";
            return false;
        }
        if ((channels != 1 && channels != 2) || (bitsPerSample != 8 && bitsPerSample != 16)) {
            error = "unsupported WAV channel count or bit depth";
            return false;
        }
        if (sampleRate == 0 || byteRate == 0 || blockAlign == 0 || pcm.empty()) {
            error = "invalid WAV format values";
            return false;
        }

        out.pcm = std::move(pcm);
        out.channels = channels;
        out.sampleRate = static_cast<int>(sampleRate);
        out.bitsPerSample = bitsPerSample;
        return true;
    }
};

class OggVorbisDecoder final : public IAudioDecoder {
public:
    [[nodiscard]] const std::vector<std::string>& extensions() const override {
        static const std::vector<std::string> kExtensions{".ogg"};
        return kExtensions;
    }

    [[nodiscard]] bool decode(const std::string& filepath,
                              DecodedAudio& out,
                              std::string& error) const override {
        OggVorbis_File vorbisFile{};
        const int openResult = ov_fopen(filepath.c_str(), &vorbisFile);
        if (openResult < 0) {
            error = "failed to open OGG/Vorbis stream";
            return false;
        }

        vorbis_info* info = ov_info(&vorbisFile, -1);
        if (info == nullptr) {
            ov_clear(&vorbisFile);
            error = "missing OGG/Vorbis stream info";
            return false;
        }

        const int channels = info->channels;
        const int sampleRate = static_cast<int>(info->rate);
        if ((channels != 1 && channels != 2) || sampleRate <= 0) {
            ov_clear(&vorbisFile);
            error = "unsupported OGG/Vorbis channel count or sample rate";
            return false;
        }

        std::vector<char> pcm;
        constexpr size_t kMaxDecodedPcmBytes =
            static_cast<size_t>(std::numeric_limits<int32_t>::max());
        const ogg_int64_t totalSamples = ov_pcm_total(&vorbisFile, -1);
        if (totalSamples > 0) {
            const ogg_int64_t reserveBytes = totalSamples * channels * static_cast<ogg_int64_t>(sizeof(int16_t));
            if (reserveBytes > 0 && reserveBytes <= static_cast<ogg_int64_t>(kMaxDecodedPcmBytes)) {
                pcm.reserve(static_cast<size_t>(reserveBytes));
            }
        }

        std::array<char, 32768> buffer{};
        int currentSection = 0;
        while (true) {
            const long bytesRead = ov_read(&vorbisFile,
                                           buffer.data(),
                                           static_cast<int>(buffer.size()),
                                           0,
                                           2,
                                           1,
                                           &currentSection);
            if (bytesRead == 0) {
                break;
            }
            if (bytesRead == OV_HOLE) {
                continue;
            }
            if (bytesRead < 0) {
                ov_clear(&vorbisFile);
                error = "failed while decoding OGG/Vorbis stream";
                return false;
            }
            if (pcm.size() > kMaxDecodedPcmBytes - static_cast<size_t>(bytesRead)) {
                ov_clear(&vorbisFile);
                error = "decoded OGG/Vorbis stream is too large";
                return false;
            }
            pcm.insert(pcm.end(), buffer.data(), buffer.data() + bytesRead);
        }

        ov_clear(&vorbisFile);
        if (pcm.empty()) {
            error = "empty OGG/Vorbis stream";
            return false;
        }

        out.pcm = std::move(pcm);
        out.channels = channels;
        out.sampleRate = sampleRate;
        out.bitsPerSample = 16;
        return true;
    }
};

} // namespace

std::string normalizeAudioExtension(std::string extension) {
    if (extension.empty()) {
        return extension;
    }
    std::transform(extension.begin(),
                   extension.end(),
                   extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension.front() != '.') {
        extension.insert(extension.begin(), '.');
    }
    return extension;
}

const AudioDecoderRegistry& AudioDecoderRegistry::instance() {
    static const AudioDecoderRegistry registry;
    return registry;
}

AudioDecoderRegistry::AudioDecoderRegistry() {
    registerDecoder(std::make_unique<OggVorbisDecoder>());
    registerDecoder(std::make_unique<WavPcmDecoder>());
}

void AudioDecoderRegistry::registerDecoder(std::unique_ptr<IAudioDecoder> decoder) {
    if (!decoder) {
        return;
    }
    for (const std::string& extension : decoder->extensions()) {
        m_extensions.push_back(normalizeAudioExtension(extension));
    }
    m_decoders.push_back(std::move(decoder));
}

const IAudioDecoder* AudioDecoderRegistry::decoderForExtension(const std::string& extension) const {
    const std::string normalized = normalizeAudioExtension(extension);
    for (const auto& decoder : m_decoders) {
        for (const std::string& decoderExtension : decoder->extensions()) {
            if (normalizeAudioExtension(decoderExtension) == normalized) {
                return decoder.get();
            }
        }
    }
    return nullptr;
}

bool AudioDecoderRegistry::decodeFile(const std::string& filepath,
                                      DecodedAudio& out,
                                      std::string& error) const {
    const std::string extension =
        normalizeAudioExtension(std::filesystem::path(filepath).extension().string());
    const IAudioDecoder* decoder = decoderForExtension(extension);
    if (decoder == nullptr) {
        error = "unsupported audio extension: " + extension;
        return false;
    }
    return decoder->decode(filepath, out, error);
}

bool AudioDecoderRegistry::isSupportedExtension(const std::string& extension) const {
    return decoderForExtension(extension) != nullptr;
}

int AudioDecoderRegistry::extensionPriority(const std::string& extension) const {
    const std::string normalized = normalizeAudioExtension(extension);
    for (size_t i = 0; i < m_extensions.size(); ++i) {
        if (m_extensions[i] == normalized) {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(m_extensions.size());
}

} // namespace audio
