#include "audio/AudioDecoders.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

int fail(const std::string& message) {
    std::cerr << "[audio_asset_decode_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main(const int argc, char** argv) {
    const std::filesystem::path path =
        argc >= 2 ? std::filesystem::path(argv[1]) : std::filesystem::path("assets/sounds/foo.ogg");

    audio::DecodedAudio decoded;
    std::string error;
    if (!audio::AudioDecoderRegistry::instance().decodeFile(path.string(), decoded, error)) {
        return fail(error);
    }

    const int bytesPerSample = decoded.bitsPerSample / 8;
    const int byteRate = decoded.sampleRate * decoded.channels * bytesPerSample;
    const double duration = byteRate > 0
        ? static_cast<double>(decoded.pcm.size()) / static_cast<double>(byteRate)
        : 0.0;

    std::cout << "[audio_asset_decode_test] PASS: " << path.string() << '\n';
    std::cout << "  channels: " << decoded.channels << '\n';
    std::cout << "  sampleRate: " << decoded.sampleRate << '\n';
    std::cout << "  bitsPerSample: " << decoded.bitsPerSample << '\n';
    std::cout << "  pcmBytes: " << decoded.pcm.size() << '\n';
    std::cout << "  duration: " << duration << "s\n";
    return EXIT_SUCCESS;
}
