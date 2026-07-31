#include "audio/AudioDecoders.h"
#include "audio/AudioFileDiscovery.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

int fail(const char* message) {
    std::cerr << "[audio_decoder_registry_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void touch(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);
    file << "x";
}

} // namespace

int main() {
    const auto& registry = audio::AudioDecoderRegistry::instance();
    if (!registry.isSupportedExtension(".wav")) {
        return fail("WAV extension should be supported");
    }
    if (!registry.isSupportedExtension(".ogg")) {
        return fail("OGG extension should be supported");
    }
    if (registry.extensionPriority(".ogg") >= registry.extensionPriority(".wav")) {
        return fail("OGG should be preferred over WAV when both files exist");
    }

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "mecraft_audio_discovery_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    touch(dir / "impact.wav");
    touch(dir / "impact.ogg");
    touch(dir / "step.wav");
    touch(dir / "block.break.ogg");
    touch(dir / "readme.txt");

    const auto impact = audio::findAudioFileByStem(dir, "impact");
    if (!impact.has_value() || impact->extension() != ".ogg") {
        std::filesystem::remove_all(dir);
        return fail("finder should prefer impact.ogg over impact.wav");
    }

    const auto explicitWav = audio::findAudioFileByStem(dir, "impact.wav");
    if (!explicitWav.has_value() || explicitWav->extension() != ".wav") {
        std::filesystem::remove_all(dir);
        return fail("finder should honor explicit supported extensions");
    }

    const auto dottedStem = audio::findAudioFileByStem(dir, "block.break");
    if (!dottedStem.has_value() || dottedStem->filename() != "block.break.ogg") {
        std::filesystem::remove_all(dir);
        return fail("finder should treat unsupported dotted suffixes as part of the stem");
    }

    const auto files = audio::listSupportedAudioFiles(dir);
    if (files.size() != 3) {
        std::filesystem::remove_all(dir);
        return fail("listing should include supported files and collapse duplicate stems");
    }
    if (files[0].stem() != "block.break" || files[0].extension() != ".ogg") {
        std::filesystem::remove_all(dir);
        return fail("listing should include dotted stems");
    }
    if (files[1].stem() != "impact" || files[1].extension() != ".ogg") {
        std::filesystem::remove_all(dir);
        return fail("listing should keep preferred OGG duplicate");
    }
    if (files[2].stem() != "step" || files[2].extension() != ".wav") {
        std::filesystem::remove_all(dir);
        return fail("listing should keep WAV when no OGG duplicate exists");
    }

    std::filesystem::remove_all(dir);
    std::cout << "[audio_decoder_registry_test] PASS\n";
    return EXIT_SUCCESS;
}
