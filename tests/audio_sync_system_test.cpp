#include <cstdlib>
#include <iostream>

#include <glm/vec3.hpp>

#include "../src/audio/AudioEngine.h"
#include "../src/ecs/util/AudioEventBuffer.h"
#include "../src/ecs/systems/audio/AudioSyncSystem.h"
#include "../src/ecs/components/Components.h"
#include "../src/ecs/GameplayRegistry.h"

namespace {

int fail(const char* message) {
    std::cerr << "[audio_sync_system_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main() {
    AudioEngine audioEngine;
    audioEngine.init();

    ecs::GameplayRegistry registry;

    auto& events = ecs::ensureAudioEventBuffer(registry);
    events.playSoundEvents.push_back({"walk_grass1", glm::vec3(0.0f), false, 0.2f});
    events.playSoundEvents.push_back({"", glm::vec3(0.0f), false, 1.0f});
    ecs::AudioSyncSystem::update(registry, audioEngine);
    if (!events.playSoundEvents.empty()) {
        audioEngine.shutdown();
        return fail("AudioSyncSystem should clear playSoundEvents after dispatch");
    }

    const entt::entity sourceEntity = registry.create();
    auto& source = registry.emplace<ecs::AudioSourceComponent>(sourceEntity);
    source.clipName = "walk_grass";
    source.loop = true;
    source.volume = 0.12f;
    source.pitch = 0.9f;
    source.spatial = false;
    source.followTransform = false;
    source.desiredPlaying = true;

    ecs::AudioSyncSystem::update(registry, audioEngine);

    source.pitch = 1.1f;
    ecs::AudioSyncSystem::update(registry, audioEngine);

    source.desiredPlaying = false;
    ecs::AudioSyncSystem::update(registry, audioEngine);

    registry.destroy(sourceEntity);
    ecs::AudioSyncSystem::update(registry, audioEngine);

    audioEngine.shutdown();
    std::cout << "[audio_sync_system_test] PASS\n";
    return EXIT_SUCCESS;
}
