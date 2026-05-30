#ifndef MECRAFT_GAME_SESSION_H
#define MECRAFT_GAME_SESSION_H

#include "GameSessionConfig.h"
#include <memory>

// Forward declarations
class World;
class ResourceMgr;
class ThreadPool;
namespace physics { class PhysicsSystem; }
namespace ecs { class GameplayScene; }
class DropSystem;
class ParticleSystem;
class RainRenderer;
class CraftingSystem;
class CameraController;
class GameplayPresentationBuilder;

/// Aggregates gameplay session objects: World, ECS, physics, crafting, particles, etc.
/// Extracted from Game to reduce its responsibilities and coupling.
/// Lifetime: one gameplay session (from world load to return to menu).
class GameSession {
public:
    GameSession();
    ~GameSession();

    /// Initialize the session with config and external dependencies.
    void init(const GameSessionConfig& config, ResourceMgr& resourceMgr, ThreadPool* threadPool);

    /// Shutdown and release all session resources.
    void shutdown();

    // Accessors (valid only after init())
    [[nodiscard]] World& world() { return *m_world; }
    [[nodiscard]] const World& world() const { return *m_world; }
    [[nodiscard]] physics::PhysicsSystem& physicsSystem() { return *m_physicsSystem; }
    [[nodiscard]] ecs::GameplayScene& gameplayScene() { return *m_gameplayScene; }
    [[nodiscard]] const ecs::GameplayScene& gameplayScene() const { return *m_gameplayScene; }
    [[nodiscard]] DropSystem& dropSystem() { return *m_dropSystem; }
    [[nodiscard]] ParticleSystem& particleSystem() { return *m_particleSystem; }
    [[nodiscard]] RainRenderer& rainRenderer() { return *m_rainRenderer; }
    [[nodiscard]] CraftingSystem& craftingSystem() { return *m_craftingSystem; }
    [[nodiscard]] CameraController& cameraController() { return *m_cameraController; }
    [[nodiscard]] GameplayPresentationBuilder& presentationBuilder() { return *m_presentationBuilder; }

private:
    // Core world and physics (created in init())
    std::unique_ptr<World> m_world;
    std::unique_ptr<physics::PhysicsSystem> m_physicsSystem;

    // ECS
    std::unique_ptr<ecs::GameplayScene> m_gameplayScene;

    // Gameplay systems
    std::unique_ptr<DropSystem> m_dropSystem;
    std::unique_ptr<CraftingSystem> m_craftingSystem;

    // Particles
    std::unique_ptr<ParticleSystem> m_particleSystem;
    std::unique_ptr<RainRenderer> m_rainRenderer;

    // Camera
    std::unique_ptr<CameraController> m_cameraController;

    // Presentation
    std::unique_ptr<GameplayPresentationBuilder> m_presentationBuilder;
};

#endif // MECRAFT_GAME_SESSION_H
