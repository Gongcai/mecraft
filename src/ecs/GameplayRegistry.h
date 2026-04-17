#ifndef MECRAFT_GAMEPLAY_REGISTRY_H
#define MECRAFT_GAMEPLAY_REGISTRY_H

#include <entt/entt.hpp>

namespace ecs {

class GameplayRegistry {
public:
    entt::registry& registry() { return m_registry; }
    const entt::registry& registry() const { return m_registry; }

    entt::entity create() { return m_registry.create(); }
    void destroy(entt::entity e) { m_registry.destroy(e); }

    template<typename T, typename... Args>
    decltype(auto) emplace(entt::entity e, Args&&... args) {
        return m_registry.emplace<T>(e, std::forward<Args>(args)...);
    }

    template<typename T>
    T& get(entt::entity e) { return m_registry.get<T>(e); }

    template<typename T>
    const T& get(entt::entity e) const { return m_registry.get<T>(e); }

    template<typename T>
    bool has(entt::entity e) const { return m_registry.all_of<T>(e); }

    template<typename T>
    T* try_get(entt::entity e) { return m_registry.try_get<T>(e); }

    template<typename T>
    const T* try_get(entt::entity e) const { return m_registry.try_get<T>(e); }

    template<typename... Components>
    auto view() { return m_registry.view<Components...>(); }

    template<typename... Components>
    auto view() const { return m_registry.view<Components...>(); }

    // Context / singleton
    template<typename T, typename... Args>
    T& ctxSet(Args&&... args) {
        return m_registry.ctx().emplace<T>(std::forward<Args>(args)...);
    }

    template<typename T>
    T& ctxGet() { return m_registry.ctx().get<T>(); }

    template<typename T>
    const T& ctxGet() const { return m_registry.ctx().get<T>(); }

    template<typename T>
    bool ctxHas() const { return m_registry.ctx().contains<T>(); }

private:
    entt::registry m_registry;
};

} // namespace ecs

#endif // MECRAFT_GAMEPLAY_REGISTRY_H
