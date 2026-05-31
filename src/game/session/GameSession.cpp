#include "GameSession.h"
#include "../../resource/ResourceMgr.h"
#include "../../world/World.h"
#include "../../physics/PhysicsSystem.h"
#include "../../ecs/GameplayScene.h"
#include "../../world/DropSystem.h"
#include "../../particle/ParticleSystem.h"
#include "../../particle/RainRenderer.h"
#include "../../crafting/CraftingSystem.h"
#include "../camera/CameraController.h"
#include "../presentation/GameplayPresentationBuilder.h"

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
