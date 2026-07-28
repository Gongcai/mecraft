#ifndef MECRAFT_GAME_SESSION_H
#define MECRAFT_GAME_SESSION_H

#include "GameSessionConfig.h"
#include "../../world/IWorldView.h"
#include "../../world/DayNightSystem.h"
#include "../../world/WeatherSystem.h"
#include "../../save/PlayerSerializer.h"
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
class RenderScene;

/// Aggregates gameplay session objects: World, ECS, physics, crafting, particles, etc.
/// Extracted from Game to reduce its responsibilities and coupling.
/// Lifetime: one gameplay session (from world load to return to menu).
class GameSession {
public:
    struct InitialLoadProgress {
        int serverLoaded = 0;
        int clientLoaded = 0;
        int target = 0;
        int inFlight = 0;
        bool lightingSettled = false;
        bool complete = false;
    };

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
    void updateWorldAroundLocalPlayer(float dt);
    void receiveWorldMessages();
    void pumpInitialChunkLoad(float dt);
    [[nodiscard]] InitialLoadProgress getInitialLoadProgress() const;
    [[nodiscard]] bool isInitialChunkLoadComplete() const;
    [[nodiscard]] glm::vec3 getLocalPlayerPosition() const;
    [[nodiscard]] bool stabilizeLocalPlayerAfterInitialLoad();

    /// Get the local player's inventory from ECS.
    /// Aborts if inventory is not initialized.
    [[nodiscard]] Inventory& getPlayerInventory();

    /// Shutdown and release all session resources.
    void shutdown();

    /// Save local player state to disk (called during shutdown).
    void saveLocalPlayer();

    /// Load saved local player state and apply to ECS entity (called after initECS).
    void loadLocalPlayer();

    /// Mirror the active gameplay mode (from the state machine's base state) into
    /// the local player's PlayerModeComponent, so client-side systems read the
    /// correct creative/survival flag. Called after load and on every mode change.
    void syncLocalPlayerMode();

    // Accessors (valid only after init())
    // world() returns the server's authoritative World for ECS/physics use.
    [[nodiscard]] World& world();
    [[nodiscard]] const World& world() const;
    // worldView() returns the appropriate IWorldView for rendering.
    [[nodiscard]] const IWorldView& worldView() const;
    [[nodiscard]] const DayNightSystem& dayNightSystem() const;
    [[nodiscard]] const WeatherSystem& weatherSystem() const;
    // C/S components
    [[nodiscard]] server::GameServer& server();
    [[nodiscard]] const server::GameServer& server() const;
    [[nodiscard]] client::GameClient& client();
    [[nodiscard]] const client::GameClient& client() const;
    [[nodiscard]] physics::PhysicsSystem& physicsSystem();
    [[nodiscard]] ecs::GameplayScene& gameplayScene();
    [[nodiscard]] const ecs::GameplayScene& gameplayScene() const;
    [[nodiscard]] DropSystem& dropSystem();
    [[nodiscard]] ParticleSystem& particleSystem();
    [[nodiscard]] RainRenderer& rainRenderer();
    [[nodiscard]] CraftingSystem& craftingSystem();
    [[nodiscard]] CameraController& cameraController();
    [[nodiscard]] GameplayPresentationBuilder& presentationBuilder();
    [[nodiscard]] GameStateMachine& stateMachine();
    [[nodiscard]] const GameStateMachine& stateMachine() const;
    [[nodiscard]] std::string& lastSubmittedCommand() { return m_lastSubmittedCommand; }
    [[nodiscard]] const std::string& lastSubmittedCommand() const { return m_lastSubmittedCommand; }
    [[nodiscard]] bool isMultiplayer() const { return m_isMultiplayer; }

    /// Set the render scene for in-game settings access. Called after RenderRuntime init.
    void setRenderScene(RenderScene* rs) { m_renderScene = rs; }

private:
    // C/S architecture: server owns authoritative World, client owns ClientWorld
    std::unique_ptr<server::GameServer> m_server;
    std::unique_ptr<client::GameClient> m_client;
    std::unique_ptr<World> m_fallbackWorld;
    DayNightSystem m_fallbackDayNightSystem;
    WeatherSystem m_fallbackWeatherSystem;

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

    // Render settings access (non-owning, injected after RenderRuntime init)
    RenderScene* m_renderScene = nullptr;
};

#endif // MECRAFT_GAME_SESSION_H
