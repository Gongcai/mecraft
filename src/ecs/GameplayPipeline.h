#ifndef MECRAFT_ECS_GAMEPLAY_PIPELINE_H
#define MECRAFT_ECS_GAMEPLAY_PIPELINE_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <tuple>
#include <typeinfo>
#include <utility>
#include <vector>
#ifdef MECRAFT_DEBUG
#include <array>
#endif

#include "GameplayRegistry.h"
#include "GameplayServices.h"
#include "ISystem.h"

namespace ecs {

enum class GameplayPipelineProfile {
    Client,
    Server
};

struct GameplayPipelineHooks {
    std::function<void(SystemContext&)> afterDamageSystem;
};

class GameplayPipeline {
public:
    enum class FixedUpdateDebugCategory : size_t {
        State = 0,
        Drop = 1,
        Particle = 2,
        Count = 3
    };

#ifdef MECRAFT_DEBUG
    struct FixedUpdateProfile {
        std::array<double, static_cast<size_t>(FixedUpdateDebugCategory::Count)> categoryMs{};

        [[nodiscard]] double stateMs() const { return categoryMs[static_cast<size_t>(FixedUpdateDebugCategory::State)]; }
        [[nodiscard]] double dropMs() const { return categoryMs[static_cast<size_t>(FixedUpdateDebugCategory::Drop)]; }
        [[nodiscard]] double particleMs() const { return categoryMs[static_cast<size_t>(FixedUpdateDebugCategory::Particle)]; }
    };
#endif

    explicit GameplayPipeline(GameplayPipelineProfile profile);

    void runFixedUpdate(GameplayRegistry& registry,
                        GameplayServices& services,
                        float dt,
                        uint64_t tickIndex = 0,
                        const GameplayPipelineHooks* hooks = nullptr);

#ifdef MECRAFT_DEBUG
    [[nodiscard]] FixedUpdateProfile runFixedUpdateProfiled(GameplayRegistry& registry,
                                                            GameplayServices& services,
                                                            float dt,
                                                            uint64_t tickIndex = 0);
#endif

    void runOneTick(GameplayRegistry& registry,
                    GameplayServices& services,
                    float dt,
                    uint64_t tickIndex);

private:
    enum class PostSystemHook : uint8_t {
        None,
        AfterDamageSystem
    };

    struct FixedSystemEntry {
        std::unique_ptr<ISystem> system;
        PostSystemHook postHook = PostSystemHook::None;
    };

    std::vector<FixedSystemEntry> m_fixedUpdateSystems;
    std::vector<std::unique_ptr<ISystem>> m_tickSystems;

    void buildClientFixedUpdateSystems();
    void buildServerFixedUpdateSystems();
    void buildClientTickSystems();
    void runPostHook(PostSystemHook hook, SystemContext& ctx, const GameplayPipelineHooks* hooks);

#ifdef MECRAFT_DEBUG
    struct SystemDepInfo {
        const char* systemName;
        std::vector<uint32_t> required;
        std::vector<uint32_t> written;
    };

    std::vector<SystemDepInfo> m_systemDeps;
    std::vector<FixedUpdateDebugCategory> m_fixedUpdateDebugCategories;

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
        m_systemDeps.push_back(std::move(info));
    }
#endif

    template <typename TSystem>
    void addFixedUpdateSystem(FixedUpdateDebugCategory category = FixedUpdateDebugCategory::State,
                              PostSystemHook postHook = PostSystemHook::None) {
        m_fixedUpdateSystems.push_back(FixedSystemEntry{std::make_unique<TSystem>(), postHook});
#ifdef MECRAFT_DEBUG
        m_fixedUpdateDebugCategories.push_back(category);
        registerSystemDep<TSystem>();
#else
        (void)category;
#endif
    }

    template <typename TSystem>
    void addTickSystem() {
        m_tickSystems.push_back(std::make_unique<TSystem>());
#ifdef MECRAFT_DEBUG
        registerSystemDep<TSystem>();
#endif
    }
};

} // namespace ecs

#endif // MECRAFT_ECS_GAMEPLAY_PIPELINE_H
