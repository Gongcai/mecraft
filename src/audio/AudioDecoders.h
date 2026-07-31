#ifndef MECRAFT_AUDIO_DECODERS_H
#define MECRAFT_AUDIO_DECODERS_H

#include <memory>
#include <string>
#include <vector>

namespace audio {

struct DecodedAudio {
    std::vector<char> pcm;
    int channels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;
};

class IAudioDecoder {
public:
    virtual ~IAudioDecoder() = default;

    [[nodiscard]] virtual const std::vector<std::string>& extensions() const = 0;
    [[nodiscard]] virtual bool decode(const std::string& filepath, DecodedAudio& out, std::string& error) const = 0;
};

class AudioDecoderRegistry {
public:
    static const AudioDecoderRegistry& instance();

    [[nodiscard]] bool decodeFile(const std::string& filepath, DecodedAudio& out, std::string& error) const;
    [[nodiscard]] bool isSupportedExtension(const std::string& extension) const;
    [[nodiscard]] int extensionPriority(const std::string& extension) const;
    [[nodiscard]] const std::vector<std::string>& supportedExtensions() const { return m_extensions; }

private:
    AudioDecoderRegistry();

    void registerDecoder(std::unique_ptr<IAudioDecoder> decoder);
    [[nodiscard]] const IAudioDecoder* decoderForExtension(const std::string& extension) const;

    std::vector<std::unique_ptr<IAudioDecoder>> m_decoders;
    std::vector<std::string> m_extensions;
};

[[nodiscard]] std::string normalizeAudioExtension(std::string extension);

} // namespace audio

#endif // MECRAFT_AUDIO_DECODERS_H
