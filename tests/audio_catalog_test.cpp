#include "audio/AudioCatalog.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

int fail(const char* message) {
    std::cerr << "[audio_catalog_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void touch(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);
    file << "x";
}

} // namespace

int main() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "mecraft_audio_catalog_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    touch(dir / "step.ogg");
    touch(dir / "step.wav");
    touch(dir / "impact.wav");
    touch(dir / "loose.ogg");

    const std::filesystem::path manifestPath = dir / "sounds.json";
    {
        std::ofstream manifest(manifestPath);
        manifest << R"({
  "version": 1,
  "sounds": {
    "player.step": {
      "variants": [
        { "file": "step", "extension": ".ogg", "weight": 2 },
        { "file": "step", "extension": ".wav", "weight": 1 }
      ],
      "volume": 0.75
    },
    "block.impact": {
      "file": "impact",
      "extension": ".wav"
    },
    "auto.extension": {
      "file": "step"
    },
    "bgm.test": {
      "file": "impact",
      "extension": ".wav",
      "group": "bgm",
      "preload": false
    }
  }
})";
    }

    audio::AudioCatalog catalog;
    std::string error;
    if (!catalog.loadFromFile(manifestPath, dir, "sfx", true, error)) {
        std::cerr << error << '\n';
        std::filesystem::remove_all(dir);
        return fail("catalog should load");
    }
    if (catalog.size() != 4) {
        std::filesystem::remove_all(dir);
        return fail("catalog should contain every declared sound");
    }

    const audio::SoundEntry* step = catalog.find("player.step");
    if (step == nullptr || step->variants.size() != 2) {
        std::filesystem::remove_all(dir);
        return fail("player.step should have two variants");
    }
    if (step->group != "sfx" || !step->preload || step->volume != 0.75f) {
        std::filesystem::remove_all(dir);
        return fail("player.step should inherit defaults and parse volume");
    }
    if (step->variants[0].filePath.filename() != "step.ogg") {
        std::filesystem::remove_all(dir);
        return fail("explicit extension should resolve exact file");
    }

    const audio::SoundEntry* autoExtension = catalog.find("auto.extension");
    if (autoExtension == nullptr || autoExtension->variants[0].filePath.filename() != "step.ogg") {
        std::filesystem::remove_all(dir);
        return fail("extensionless manifest file should use decoder priority");
    }

    const auto bgmIds = catalog.soundIdsByGroup("bgm");
    if (bgmIds.size() != 1 || bgmIds[0] != "bgm.test") {
        std::filesystem::remove_all(dir);
        return fail("catalog should expose sound ids by group");
    }
    const audio::SoundEntry* bgm = catalog.find("bgm.test");
    if (bgm == nullptr || bgm->preload) {
        std::filesystem::remove_all(dir);
        return fail("bgm.test should parse preload=false");
    }
    if (catalog.find("loose") != nullptr) {
        std::filesystem::remove_all(dir);
        return fail("catalog should not expose files that are not declared");
    }

    std::filesystem::remove_all(dir);
    std::cout << "[audio_catalog_test] PASS\n";
    return EXIT_SUCCESS;
}
