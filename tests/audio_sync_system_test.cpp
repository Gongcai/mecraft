#include <cstdlib>
#include <iostream>

#include <glm/vec3.hpp>

#include "../src/audio/AudioEngine.h"
#include "../src/ecs/components/Components.h"
#include "../src/ecs/GameplayServices.h"
#include "../src/ecs/GameplayRegistry.h"
#include "../src/ecs/SystemContext.h"
#include "../src/ecs/systems/audio/AudioSyncSystem.h"
#include "../src/ecs/util/AudioEventBuffer.h"

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
    ecs::GameplayServices services;
    services.audioEngine = &audioEngine;
    ecs::SystemContext ctx{registry, services, 1.0f / 60.0f, 0};
    ecs::AudioSyncSystem system;

    auto& events = ecs::ensureAudioEventBus(registry);
    events.push({"player.step.grass", glm::vec3(0.0f), false, 0.2f});
    events.push({"", glm::vec3(0.0f), false, 1.0f});
    system.update(ctx);
    if (!events.empty()) {
        audioEngine.shutdown();
        return fail("AudioSyncSystem should clear audio events after dispatch");
    }

    const entt::entity sourceEntity = registry.create();
    auto& source = registry.emplace<ecs::AudioSourceComponent>(sourceEntity);
    source.clipName = "player.step.grass";
    source.loop = true;
    source.volume = 0.12f;
    source.pitch = 0.9f;
    source.spatial = false;
    source.followTransform = false;
    source.desiredPlaying = true;

    system.update(ctx);

    source.pitch = 1.1f;
    system.update(ctx);

    source.desiredPlaying = false;
    system.update(ctx);

    registry.destroy(sourceEntity);
    system.update(ctx);

    audioEngine.shutdown();
    std::cout << "[audio_sync_system_test] PASS\n";
    return EXIT_SUCCESS;
}
