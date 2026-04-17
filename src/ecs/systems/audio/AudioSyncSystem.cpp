#include "AudioSyncSystem.h"

#include <algorithm>
#include <unordered_map>

#include "../../util/AudioEventBuffer.h"
#include "../../components/Components.h"
#include "../../../audio/AudioEngine.h"
#include "../../../audio/AudioSource.h"

namespace ecs {

namespace {

struct TrackedAudioSource {
    AudioSource* source = nullptr;
    std::string clipName;
    bool loop = false;
    bool spatial = true;
};

struct AudioSyncRuntimeState {
    std::unordered_map<uint32_t, TrackedAudioSource> trackedSources;
};

AudioSyncRuntimeState& ensureAudioSyncRuntimeState(GameplayRegistry& registry) {
    if (!registry.ctxHas<AudioSyncRuntimeState>()) {
        registry.ctxSet<AudioSyncRuntimeState>();
    }
    return registry.ctxGet<AudioSyncRuntimeState>();
}

void stopTracked(TrackedAudioSource& tracked) {
    if (tracked.source != nullptr) {
        tracked.source->stop();
        tracked.source = nullptr;
    }
}

bool shouldRecreate(const TrackedAudioSource& tracked, const AudioSourceComponent& component) {
    if (tracked.source == nullptr) {
        return true;
    }
    if (tracked.clipName != component.clipName) {
        return true;
    }
    if (tracked.loop != component.loop) {
        return true;
    }
    if (tracked.spatial != component.spatial) {
        return true;
    }
    return false;
}

glm::vec3 resolveAudioPosition(GameplayRegistry& registry,
                               const entt::entity entity,
                               const AudioSourceComponent& component) {
    if (!component.spatial) {
        return glm::vec3(0.0f);
    }
    if (!component.followTransform || !registry.has<TransformComponent>(entity)) {
        return glm::vec3(0.0f);
    }
    return registry.get<TransformComponent>(entity).position;
}

} // namespace

void AudioSyncSystem::update(GameplayRegistry& registry, AudioEngine& audioEngine) {
    auto& eventBuffer = ensureAudioEventBuffer(registry);
    for (const auto& event : eventBuffer.playSoundEvents) {
        if (event.clipName.empty()) {
            continue;
        }
        if (event.spatial) {
            audioEngine.playClip(event.clipName, event.position, false, event.volume, true);
        } else {
            audioEngine.playSound2D(event.clipName, event.volume);
        }
    }
    eventBuffer.playSoundEvents.clear();

    auto& runtime = ensureAudioSyncRuntimeState(registry);
    auto& raw = registry.registry();

    auto view = registry.view<AudioSourceComponent>();
    for (const entt::entity entity : view) {
        auto& component = view.get<AudioSourceComponent>(entity);
        const uint32_t entityId = static_cast<uint32_t>(entt::to_integral(entity));

        auto trackedIt = runtime.trackedSources.find(entityId);
        if (component.clipName.empty() || !component.desiredPlaying) {
            if (trackedIt != runtime.trackedSources.end()) {
                stopTracked(trackedIt->second);
                runtime.trackedSources.erase(trackedIt);
            }
            continue;
        }

        if (trackedIt != runtime.trackedSources.end()
            && (trackedIt->second.source == nullptr || trackedIt->second.source->isStopped())) {
            stopTracked(trackedIt->second);
            if (!component.loop) {
                component.desiredPlaying = false;
                runtime.trackedSources.erase(trackedIt);
                continue;
            }
        }

        if (trackedIt == runtime.trackedSources.end()) {
            trackedIt = runtime.trackedSources.emplace(entityId, TrackedAudioSource{}).first;
        }

        TrackedAudioSource& tracked = trackedIt->second;
        if (shouldRecreate(tracked, component)) {
            stopTracked(tracked);
            const glm::vec3 spawnPos = resolveAudioPosition(registry, entity, component);
            tracked.source = audioEngine.playClip(component.clipName,
                                                  spawnPos,
                                                  component.loop,
                                                  component.volume,
                                                  component.spatial);
            if (tracked.source == nullptr) {
                runtime.trackedSources.erase(trackedIt);
                continue;
            }
            tracked.clipName = component.clipName;
            tracked.loop = component.loop;
            tracked.spatial = component.spatial;
        }

        if (tracked.source == nullptr) {
            runtime.trackedSources.erase(trackedIt);
            continue;
        }

        tracked.source->setVolume(component.volume);
        tracked.source->setPitch(component.pitch);
        if (component.spatial) {
            tracked.source->setReferenceDistance(std::max(0.01f, component.referenceDistance));
            tracked.source->setRolloffFactor(std::max(0.0f, component.rolloff));
            if (component.followTransform) {
                tracked.source->setPosition(resolveAudioPosition(registry, entity, component));
            }
        } else {
            tracked.source->setRolloffFactor(0.0f);
        }
    }

    for (auto it = runtime.trackedSources.begin(); it != runtime.trackedSources.end();) {
        const entt::entity entity = static_cast<entt::entity>(it->first);
        if (!raw.valid(entity) || !raw.all_of<AudioSourceComponent>(entity)) {
            stopTracked(it->second);
            it = runtime.trackedSources.erase(it);
            continue;
        }
        ++it;
    }
}

} // namespace ecs
