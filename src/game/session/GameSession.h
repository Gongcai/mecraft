#ifndef MECRAFT_GAME_SESSION_H
#define MECRAFT_GAME_SESSION_H

#include "GameSessionConfig.h"
#include "../../world/IWorldView.h"
#include <memory>
#include <string>

// Forward declarations
class World;
class ResourceMgr;
class ThreadPool;
namespace physics { class PhysicsSystem; }
namespace ecs { class GameplayScene; }
namespace server { class GameServer; }
namespace client { class GameClient; }
class DropSystem;
class ParticleSystem;
class RainRenderer;
class CraftingSystem;
class CameraController;
class GameplayPresentationBuilder;
class GameStateMachine;
class IGameState;

class Inventory;  // Forward declaration for getPlayerInventory()

/// Aggregates gameplay session objects: World, ECS, physics, crafting, particles, etc.
/// Extracted from Game to reduce its responsibilities and coupling.
/// Lifetime: one gameplay session (from world load to return to menu).
class GameSession {
public:
    GameSession();
    ~GameSession();

    /// Initialize the session with config and external dependencies.
    void init(const GameSessionConfig& config, ResourceMgr& resourceMgr, ThreadPool* threadPool);

    /// Initialize world (seed, spawn).
    void initWorld(int seed);

    /// Initialize ECS: bind services, create player entity, spawn test mob.
    /// @param deps External services not owned by GameSession
    void initECS(const GameSessionDependencies& deps);

    /// Initialize the state machine and push the initial gameplay state.
    void initStateMachine(const GameSessionDependencies& deps);

    /// Create the initial gameplay state using session-owned gameplay services.
    [[nodiscard]] std::unique_ptr<IGameState> createInitialGameplayState(const GameSessionDependencies& deps);

    /// Update world streaming and simulation center from the local player position.
    void updateWorldAroundLocalPlayer();

    /// Get the local player's inventory from ECS.
    /// @throws std::runtime_error if inventory is not initialized
    [[nodiscard]] Inventory& getPlayerInventory();

    /// Shutdown and release all session resources.
    void shutdown();

    // Accessors (valid only after init())
    // world() returns the server's authoritative World for ECS/physics use.
    [[nodiscard]] World& world();
    [[nodiscard]] const World& world() const;
    // worldView() returns the appropriate IWorldView for rendering.
    [[nodiscard]] const IWorldView& worldView() const;
    // C/S components
    [[nodiscard]] server::GameServer& server() { return *m_server; }
    [[nodiscard]] const server::GameServer& server() const { return *m_server; }
    [[nodiscard]] client::GameClient& client() { return *m_client; }
    [[nodiscard]] const client::GameClient& client() const { return *m_client; }
    [[nodiscard]] physics::PhysicsSystem& physicsSystem() { return *m_physicsSystem; }
    [[nodiscard]] ecs::GameplayScene& gameplayScene() { return *m_gameplayScene; }
    [[nodiscard]] const ecs::GameplayScene& gameplayScene() const { return *m_gameplayScene; }
    [[nodiscard]] DropSystem& dropSystem() { return *m_dropSystem; }
    [[nodiscard]] ParticleSystem& particleSystem() { return *m_particleSystem; }
    [[nodiscard]] RainRenderer& rainRenderer() { return *m_rainRenderer; }
    [[nodiscard]] CraftingSystem& craftingSystem() { return *m_craftingSystem; }
    [[nodiscard]] CameraController& cameraController() { return *m_cameraController; }
    [[nodiscard]] GameplayPresentationBuilder& presentationBuilder() { return *m_presentationBuilder; }
    [[nodiscard]] GameStateMachine& stateMachine();
    [[nodiscard]] const GameStateMachine& stateMachine() const;
    [[nodiscard]] std::string& lastSubmittedCommand() { return m_lastSubmittedCommand; }
    [[nodiscard]] const std::string& lastSubmittedCommand() const { return m_lastSubmittedCommand; }
    [[nodiscard]] bool isMultiplayer() const { return m_isMultiplayer; }

private:
    // C/S architecture: server owns authoritative World, client owns ClientWorld
    std::unique_ptr<server::GameServer> m_server;
    std::unique_ptr<client::GameClient> m_client;

    // Physics (references server's World)
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

    // State machine
    std::unique_ptr<GameStateMachine> m_stateMachine;
    std::string m_lastSubmittedCommand;
    bool m_isMultiplayer = false;
};

#endif // MECRAFT_GAME_SESSION_H
