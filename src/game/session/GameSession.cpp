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

GameSession::GameSession() = default;
GameSession::~GameSession() = default;

void GameSession::init(const GameSessionConfig& config, ResourceMgr& resourceMgr, ThreadPool* threadPool) {
    (void)resourceMgr;  // Resource-dependent init deferred to Game::initRenderers()

    // Create core systems
    m_world = std::make_unique<World>();
    m_physicsSystem = std::make_unique<physics::PhysicsSystem>(m_world.get());
    m_gameplayScene = std::make_unique<ecs::GameplayScene>();
    m_dropSystem = std::make_unique<DropSystem>();
    m_craftingSystem = std::make_unique<CraftingSystem>();
    m_cameraController = std::make_unique<CameraController>();
    m_presentationBuilder = std::make_unique<GameplayPresentationBuilder>();

    // Configure world (actual init deferred to initWorld() called by Game)
    m_world->setRenderDistance(config.renderDistance);
    m_world->setThreadPool(threadPool);

    // Create particle and rain systems (actual init happens in Game::initRenderers)
    m_particleSystem = std::make_unique<ParticleSystem>();
    m_rainRenderer = std::make_unique<RainRenderer>();
}

void GameSession::initWorld(int seed) {
    m_world->init(seed);
}

void GameSession::initECS(const GameSessionDependencies& deps) {
    auto& svc = m_gameplayScene->services();
    svc.world              = m_world.get();
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
    m_dropSystem->bindRegistry(reg);
    m_dropSystem->bindServices(svc);
    m_particleSystem->bindRegistry(reg);

    constexpr float kSpawnHeightOffset = 2.0f;
    const glm::vec3 spawnPos(0.0f,
        static_cast<float>(m_world->getSurfaceY(0, 0) + kSpawnHeightOffset), 0.0f);
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

std::unique_ptr<IGameState> GameSession::createInitialGameplayState(GameStateMachine& stateMachine,
                                                                    const GameSessionDependencies& deps,
                                                                    std::string& lastSubmittedCommand) {
    StateDependencies stateDeps{
        stateMachine,
        getPlayerInventory(),
        deps.contextManager,
        deps.input,
        deps.uiRenderer,
        lastSubmittedCommand,
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

void GameSession::updateWorldAroundLocalPlayer() {
    ecs::PlayerQuery query(m_gameplayScene->registry());
    m_world->update(query.getPosition());
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
    // Cleanup in reverse order of initialization
    m_presentationBuilder.reset();
    m_cameraController.reset();
    m_rainRenderer.reset();
    m_particleSystem.reset();
    m_craftingSystem.reset();
    m_dropSystem.reset();
    m_gameplayScene.reset();
    m_physicsSystem.reset();
    m_world.reset();
}
