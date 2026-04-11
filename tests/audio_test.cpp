#include <cstdlib>
#include <iostream>
#include <cmath>
#include <thread>
#include <chrono>

#include <glm/vec3.hpp>

#include "../src/audio/AudioEngine.h"
#include "../src/audio/AudioClip.h"
#include "../src/audio/AudioSource.h"
#include "../src/audio/AudioListener.h"

namespace {

int fail(const char* message) {
    std::cerr << "[audio_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main() {
    std::cout << "[audio_test] Starting audio system tests...\n";

    AudioEngine audio;
    audio.init();

    AudioClip* walkClip = audio.getClip("walk_grass");
    if (!walkClip) {
        walkClip = audio.getClip("walk_grass1");
    }

    if (!walkClip) {
        std::cout << "[audio_test] SKIP: No audio files found in assets/sounds/\n";
        audio.shutdown();
        return EXIT_SUCCESS;
    }

    if (!walkClip->isValid()) {
        return fail("AudioClip should be valid");
    }
    if (walkClip->getBufferID() == 0) {
        return fail("AudioClip buffer ID should not be zero");
    }
    if (walkClip->getDuration() <= 0.0f) {
        return fail("AudioClip duration should be positive");
    }

    std::cout << "[audio_test] Clip '" << walkClip->getName()
              << "' loaded, duration: " << walkClip->getDuration() << "s\n";

    // 循环播放测试
    {
        AudioSource source;
        if (!source.isValid()) {
            return fail("AudioSource should be valid after creation");
        }

        source.setClip(walkClip);
        source.setPosition(glm::vec3(0.0f, 0.0f, 0.0f));
        source.setVolume(0.5f);
        source.setPitch(1.0f);
        source.setLooping(true);  // 循环播放
        source.setRolloffFactor(1.0f);
        source.setReferenceDistance(1.0f);

        AudioListener::setPosition(glm::vec3(0.0f, 0.0f, 0.0f));
        AudioListener::setOrientation(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        AudioListener::setVelocity(glm::vec3(0.0f, 0.0f, 0.0f));
        AudioListener::setGain(1.0f);

        source.play();
        std::cout << "[audio_test] Playing loop sound for 5 seconds... (press Ctrl+C to stop)\n";

        // 等待 5 秒让用户听到效果
        std::this_thread::sleep_for(std::chrono::seconds(5));

        source.stop();
    }  // source 在这里析构，此时 clip 还存在

    // 清理
    audio.shutdown();

    std::cout << "[audio_test] PASS\n";
    return EXIT_SUCCESS;
}
