#ifndef MECRAFT_GAMEPLAY_SCENE_H
#define MECRAFT_GAMEPLAY_SCENE_H

#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "GameplayRegistry.h"
#include "GameplayServices.h"
#include "ISystem.h"
#include "util/GameTickClock.h"

namespace ecs {

class GameplayScene {
public:
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

    /// Drive one 20 TPS tick.
    void runOneTick();

private:
    GameplayRegistry m_registry;
    GameplayServices m_services;
    GameTickClock    m_tickClock;
    entt::entity     m_localPlayer = entt::null;

    /// Fixed-update pipeline — execution order matches declaration order.
    std::vector<std::unique_ptr<ISystem>> m_fixedUpdateSystems;
};

} // namespace ecs

#endif // MECRAFT_GAMEPLAY_SCENE_H
