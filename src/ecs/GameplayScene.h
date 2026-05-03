#ifndef MECRAFT_GAMEPLAY_SCENE_H
#define MECRAFT_GAMEPLAY_SCENE_H

#include <memory>
#include <vector>
#include <typeinfo>
#include <glm/glm.hpp>
#include <entt/entt.hpp>
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

    /// Tick-rate pipeline — runs at 20 TPS.
    std::vector<std::unique_ptr<ISystem>> m_tickSystems;

#ifndef NDEBUG
    struct SystemDepInfo {
        const char* systemName;
        std::vector<uint32_t> required;
        std::vector<uint32_t> written;
    };
    std::vector<SystemDepInfo> m_systemDeps;
    void validateSystemOrder();

    template <typename Tuple, std::size_t... Is>
    std::vector<uint32_t> getComponentHashesImpl(std::index_sequence<Is...>) {
        return { entt::type_hash<std::tuple_element_t<Is, Tuple>>::value()... };
    }

    template <typename Tuple>
    std::vector<uint32_t> getComponentHashes() {
        return getComponentHashesImpl<Tuple>(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
    }

    template <typename TSystem>
    void registerSystemDep() {
        SystemDepInfo info;
        info.systemName = typeid(TSystem).name();
        info.required = getComponentHashes<typename TSystem::Dependencies::Required>();
        info.written = getComponentHashes<typename TSystem::Dependencies::Written>();
        m_systemDeps.push_back(info);
    }
#endif

    template <typename TSystem>
    void addFixedUpdateSystem() {
        m_fixedUpdateSystems.push_back(std::make_unique<TSystem>());
#ifndef NDEBUG
        registerSystemDep<TSystem>();
#endif
    }

    template <typename TSystem>
    void addTickSystem() {
        m_tickSystems.push_back(std::make_unique<TSystem>());
#ifndef NDEBUG
        registerSystemDep<TSystem>();
#endif
    }
};

} // namespace ecs

#endif // MECRAFT_GAMEPLAY_SCENE_H
