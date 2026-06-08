#ifndef MECRAFT_GAMEPLAY_SCENE_H
#define MECRAFT_GAMEPLAY_SCENE_H

#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include "GameplayPipeline.h"
#include "GameplayRegistry.h"
#include "GameplayServices.h"
#include "util/GameTickClock.h"

namespace ecs {

class GameplayScene {
public:
#ifdef MECRAFT_DEBUG
    using FixedUpdateProfile = GameplayPipeline::FixedUpdateProfile;
#endif

    GameplayScene();

    GameplayRegistry& registry() { return m_registry; }
    const GameplayRegistry& registry() const { return m_registry; }

    GameplayServices& services() { return m_services; }
    const GameplayServices& services() const { return m_services; }

    GameTickClock& tickClock() { return m_tickClock; }
    const GameTickClock& tickClock() const { return m_tickClock; }

    /// Create the local player entity and attach intent components.
    void initLocalPlayer(const glm::vec3& spawnPos);

    /// Drive the 60 Hz fixed-step systems.
    void runFixedUpdate(float dt);

#ifdef MECRAFT_DEBUG
    /// Drive the 60 Hz fixed-step systems and collect debug timing by system category.
    [[nodiscard]] FixedUpdateProfile runFixedUpdateProfiled(float dt);
#endif

    /// Drive one 20 TPS tick.
    void runOneTick();

private:
    GameplayRegistry m_registry;
    GameplayServices m_services;
    GameTickClock    m_tickClock;
    GameplayPipeline m_pipeline;
    entt::entity     m_localPlayer = entt::null;
};

} // namespace ecs

#endif // MECRAFT_GAMEPLAY_SCENE_H
