#include "GameSession.h"
#include "../../resource/ResourceMgr.h"
#include "../../world/World.h"
#include "../../physics/PhysicsSystem.h"
#include "../../ecs/GameplayScene.h"
#include "../../ecs/util/PlayerQuery.h"
#include "../../ecs/entity/SteveModelFactory.h"
#include "../../ecs/entity/MobModelFactory.h"
#include "../../ecs/components/Components.h"
#include "../../world/DropSystem.h"
#include "../../particle/ParticleSystem.h"
#include "../../particle/RainRenderer.h"
#include "../../crafting/CraftingSystem.h"
#include "../camera/CameraController.h"
#include "../presentation/GameplayPresentationBuilder.h"
#include "../states/GameStateMachine.h"
#include "../states/GameplayState.h"
#include "../../ui/core/UIRenderer.h"
#include "../../Paths.h"
#include "../../server/GameServer.h"
#include "../../client/GameClient.h"
#include "../../net/InProcessTransport.h"

GameSession::GameSession() = default;
GameSession::~GameSession() = default;

void GameSession::init(const GameSessionConfig& config, ResourceMgr& resourceMgr, ThreadPool* threadPool) {
    (void)resourceMgr;  // Resource-dependent init deferred to Game::initRenderers()

    // Create server (owns the authoritative World)
    m_server = std::make_unique<server::GameServer>();
    m_server->init(static_cast<uint32_t>(config.seed), threadPool, config.renderDistance);

    // Create client and connect via in-process transport
    m_client = std::make_unique<client::GameClient>();
    auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
    m_server->acceptClient(std::move(serverTransport), 1);
    m_client->connect(std::move(clientTransport));
    m_client->sendViewConfig(config.renderDistance);

    // Wire up ClientWorld with server's weather/day-night for in-process rendering
    m_client->clientWorld().setDayNightSystem(&m_server->world().getDayNightSystem());
    m_client->clientWorld().setWeatherSystem(&m_server->world().getWeatherSystem());
    m_client->clientWorld().setRenderDistance(config.renderDistance);

    // Create physics (references server's authoritative World)
    m_physicsSystem = std::make_unique<physics::PhysicsSystem>(&m_server->world());
    m_gameplayScene = std::make_unique<ecs::GameplayScene>();
    m_dropSystem = std::make_unique<DropSystem>();
    m_craftingSystem = std::make_unique<CraftingSystem>();
    m_cameraController = std::make_unique<CameraController>();
    m_presentationBuilder = std::make_unique<GameplayPresentationBuilder>();

    // Create particle and rain systems (actual init happens in Game::initRenderers)
    m_particleSystem = std::make_unique<ParticleSystem>();
    m_rainRenderer = std::make_unique<RainRenderer>();
}

World& GameSession::world() {
    return m_server->world();
}

const World& GameSession::world() const {
    return m_server->world();
}

const IWorldView& GameSession::worldView() const {
    // When spawn chunks are ready, render from ClientWorld (C/S pipeline active).
    // Before that, render from the server's World directly (loading screen).
    if (m_client && m_client->areSpawnChunksReady()) {
        return m_client->clientWorld();
    }
    return m_server->world();
}

void GameSession::initWorld(int seed) {
    // World initialization is now handled by GameServer::init() in init().
    // This method is kept for API compatibility but is effectively a no-op.
    (void)seed;
}

void GameSession::initECS(const GameSessionDependencies& deps) {
    auto& svc = m_gameplayScene->services();
    svc.world              = &m_server->world();
    svc.audioEngine        = &deps.audioEngine;
    svc.inputContextManager = &deps.contextManager;
    svc.resourceMgr        = &deps.resourceMgr;
    svc.dropSystem         = m_dropSystem.get();
    svc.particleSystem     = m_particleSystem.get();
    svc.uiRenderer         = &deps.uiRenderer;
    svc.physicsSystem      = m_physicsSystem.get();
    svc.cameraController   = m_cameraController.get();

    // Load recipes
    m_craftingSystem->loadRecipes(RECIPES_CONFIG_PATH);
    deps.uiRenderer.setCraftingSystem(m_craftingSystem.get());

    auto& reg = m_gameplayScene->registry();
    m_server->setEcsRegistry(&reg.registry());
    m_client->initEntityStore(reg.registry(), &deps.resourceMgr);
    m_dropSystem->bindRegistry(reg);
    m_dropSystem->bindServices(svc);
    m_particleSystem->bindRegistry(reg);

    const glm::vec3 spawnPos = m_server->getSpawnPosition();
    m_gameplayScene->initLocalPlayer(spawnPos);

    ecs::PlayerQuery query(reg);
    auto steveRoot = ecs::SteveModelFactory::createSteve(reg, query.getPosition());
    reg.emplace<ecs::SkinTypeComponent>(steveRoot, ecs::SkinTypeComponent::Type::Player);
    auto playerView = reg.view<ecs::LocalPlayerTag, ecs::TransformComponent>();
    for (auto e : playerView) {
        auto& playerTransform = reg.get<ecs::TransformComponent>(e);
        auto& steveAnim = reg.get<ecs::SteveAnimationStateComponent>(steveRoot);
        steveAnim.lastPosition = playerTransform.position;
    }

#ifdef MECRAFT_DEBUG
    constexpr float kTestMobOffsetX = 5.0f;
    glm::vec3 playerPos = query.getPosition();
    ecs::MobModelFactory::createZombie(reg, glm::vec3(playerPos.x + kTestMobOffsetX, playerPos.y, playerPos.z));
#endif
}

void GameSession::initStateMachine(const GameSessionDependencies& deps) {
    m_stateMachine = std::make_unique<GameStateMachine>();
    m_stateMachine->pushState(createInitialGameplayState(deps));
}

std::unique_ptr<IGameState> GameSession::createInitialGameplayState(const GameSessionDependencies& deps) {
    StateDependencies stateDeps{
        *m_stateMachine,
        getPlayerInventory(),
        deps.contextManager,
        deps.input,
        deps.uiRenderer,
        m_lastSubmittedCommand,
        physicsSystem(),
        world(),
        deps.audioEngine,
        particleSystem(),
        dropSystem(),
        gameplayScene().registry(),
        deps.localeManager
    };
    return std::make_unique<GameplayState>(stateDeps);
}

GameStateMachine& GameSession::stateMachine() {
    return *m_stateMachine;
}

const GameStateMachine& GameSession::stateMachine() const {
    return *m_stateMachine;
}

void GameSession::updateWorldAroundLocalPlayer() {
    // Server tick: load chunks, process client messages, send world state
    // For Phase 1, use a fixed dt for server tick (will be decoupled in Phase 3)
    constexpr float kServerTickDt = 1.0f / 20.0f;
    m_server->tick(kServerTickDt);

    // Client: receive messages from server (chunk data, snapshots)
    m_client->receiveMessages();
}

Inventory& GameSession::getPlayerInventory() {
    auto& reg = m_gameplayScene->registry();
    auto view = reg.view<ecs::LocalPlayerTag, ecs::InventoryDataComponent>();
    for (auto e : view) {
        return view.get<ecs::InventoryDataComponent>(e).inventory;
    }
    throw std::runtime_error("Local player inventory is not initialized.");
}

void GameSession::shutdown() {
    m_stateMachine.reset();
    // Cleanup in reverse order of initialization
    m_presentationBuilder.reset();
    m_cameraController.reset();
    m_rainRenderer.reset();
    m_particleSystem.reset();
    m_craftingSystem.reset();
    m_dropSystem.reset();
    m_gameplayScene.reset();
    m_physicsSystem.reset();
    m_client.reset();
    m_server.reset();
}
