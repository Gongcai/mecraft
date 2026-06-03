#include "GameSession.h"
#include "../../resource/ResourceMgr.h"
#include "../../world/World.h"
#include "../../physics/PhysicsSystem.h"
#include "../../ecs/GameplayScene.h"
#include "../../ecs/util/PlayerQuery.h"
#include "../../ecs/entity/SteveModelFactory.h"
#include "../../ecs/entity/MobModelFactory.h"
#include "../../ecs/components/Components.h"
#include "../../ecs/components/PhysicsComponents.h"
#include "../../ecs/components/CameraComponents.h"
#include "../../ecs/components/PlayerStateComponents.h"
#include "../../ecs/components/InputComponents.h"
#include "../../ecs/components/TagComponents.h"
#include "../../world/DropSystem.h"
#include "../../particle/ParticleSystem.h"
#include "../../particle/RainRenderer.h"
#include "../../crafting/CraftingSystem.h"
#include "../camera/CameraController.h"
#include "../presentation/GameplayPresentationBuilder.h"
#include "../states/GameStateMachine.h"
#include "../states/GameplayState.h"
#include "../modes/CreativeModeState.h"
#include "../../ui/core/UIRenderer.h"
#include "../../ui/widgets/ConsoleDisplayBox.h"
#include "../../Paths.h"
#include "../../server/GameServer.h"
#include "../../client/GameClient.h"
#include "../../save/SavePaths.h"
#include "../../save/SaveManager.h"
#include "../../item/Item.h"
#include "../../net/InProcessTransport.h"
#include "../../net/ENetTransport.h"
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <chrono>

GameSession::GameSession() = default;
GameSession::~GameSession() = default;

void GameSession::init(const GameSessionConfig& config, ResourceMgr& resourceMgr, ThreadPool* threadPool) {
    (void)resourceMgr;  // Resource-dependent init deferred to Game::initRenderers()

    m_isMultiplayer = config.isMultiplayer();

    if (m_isMultiplayer) {
        // Multiplayer mode: connect to remote server via ENet
        m_fallbackWorld = std::make_unique<World>();
        m_fallbackWorld->setRenderDistance(config.renderDistance);

        m_client = std::make_unique<client::GameClient>();

        // Create ENet transport and connect to remote server
        auto enetTransport = std::make_unique<net::ENetTransport>();
        if (!enetTransport->connect(config.serverAddress, config.serverPort)) {
            throw std::runtime_error("Failed to connect to multiplayer server " +
                                     config.serverAddress + ":" +
                                     std::to_string(config.serverPort));
        }
        // ENetTransport inherits ITransportEndpoint; transfer ownership
        std::unique_ptr<net::ITransportEndpoint> base(std::move(enetTransport));
        m_client->connect(std::move(base));
        m_client->sendViewConfig(config.renderDistance);

        m_client->clientWorld().setRenderDistance(config.renderDistance);
        m_client->clientWorld().setDayNightSystem(&m_fallbackDayNightSystem);
        m_client->clientWorld().setWeatherSystem(&m_fallbackWeatherSystem);

        bool loggedHandshakeWait = false;
        bool loggedChunkWait = false;
        for (int i = 0; i < 4000 &&
             (!m_client->hasServerHello() || !m_client->areSpawnChunksReady()); ++i) {
            if (!m_client->hasServerHello() && i > 0 && i % 100 == 0) {
                m_client->sendHello();
                m_client->sendViewConfig(config.renderDistance);
                if (!loggedHandshakeWait) {
                    std::printf("[Client] Waiting for ServerHello; resending handshake to %s:%u\n",
                                config.serverAddress.c_str(),
                                static_cast<unsigned>(config.serverPort));
                    std::fflush(stdout);
                    loggedHandshakeWait = true;
                }
            } else if (m_client->hasServerHello() && !m_client->areSpawnChunksReady() && i > 0 && i % 200 == 0) {
                m_client->sendViewConfig(config.renderDistance);
                if (!loggedChunkWait) {
                    std::printf("[Client] Waiting for spawn chunks; refreshing view config\n");
                    std::fflush(stdout);
                    loggedChunkWait = true;
                }
            }
            m_client->receiveMessages();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!m_client->hasServerHello()) {
            throw std::runtime_error("Connected to multiplayer server but did not receive ServerHello from " +
                                     config.serverAddress + ":" +
                                     std::to_string(config.serverPort));
        }
        if (!m_client->areSpawnChunksReady()) {
            std::printf("[Client] Entering gameplay while spawn chunks are still streaming; loaded=%zu\n",
                        m_client->clientWorld().loadedChunkCount());
            std::fflush(stdout);
        }

        m_physicsSystem = std::make_unique<physics::PhysicsSystem>(&m_client->clientWorld());
    } else {
        // Single-player mode: local server + in-process transport
        m_server = std::make_unique<server::GameServer>();

        // Build save path if saving is enabled
        std::filesystem::path worldSavePath;
        if (config.enableSaving && !config.worldName.empty()) {
            worldSavePath = config.saveRoot / save::SavePaths::sanitizeWorldName(config.worldName);
        }

        if (!worldSavePath.empty()) {
            m_server->init(static_cast<uint32_t>(config.seed), threadPool,
                           config.renderDistance, std::move(worldSavePath));
        } else {
            m_server->init(static_cast<uint32_t>(config.seed), threadPool, config.renderDistance);
        }

        m_client = std::make_unique<client::GameClient>();
        auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
        m_server->acceptClient(std::move(serverTransport), 1);
        m_client->connect(std::move(clientTransport));
        m_client->sendViewConfig(config.renderDistance);

        // Wire up ClientWorld with server's weather/day-night for in-process rendering
        m_client->clientWorld().setDayNightSystem(&m_server->world().getDayNightSystem());
        m_client->clientWorld().setWeatherSystem(&m_server->world().getWeatherSystem());
        m_client->clientWorld().setRenderDistance(config.renderDistance);

        m_physicsSystem = std::make_unique<physics::PhysicsSystem>(&m_server->world());
    }

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
    if (m_server) {
        return m_server->world();
    }
    if (m_fallbackWorld) {
        return *m_fallbackWorld;
    }
    throw std::runtime_error("Session world is not initialized.");
}

const World& GameSession::world() const {
    if (m_server) {
        return m_server->world();
    }
    if (m_fallbackWorld) {
        return *m_fallbackWorld;
    }
    throw std::runtime_error("Session world is not initialized.");
}

const IWorldView& GameSession::worldView() const {
    if (m_isMultiplayer) {
        return m_client->clientWorld();
    }
    // When spawn chunks are ready, render from ClientWorld (C/S pipeline active).
    // Before that, render from the server's World directly (loading screen).
    if (m_client && m_client->areSpawnChunksReady()) {
        return m_client->clientWorld();
    }
    return m_server->world();
}

const DayNightSystem& GameSession::dayNightSystem() const {
    if (m_client) {
        if (const DayNightSystem* dns = m_client->clientWorld().dayNightSystem()) {
            return *dns;
        }
    }
    if (m_server) {
        return m_server->world().getDayNightSystem();
    }
    return m_fallbackDayNightSystem;
}

const WeatherSystem& GameSession::weatherSystem() const {
    if (m_client) {
        if (const WeatherSystem* weather = m_client->clientWorld().weatherSystem()) {
            return *weather;
        }
    }
    if (m_server) {
        return m_server->world().getWeatherSystem();
    }
    return m_fallbackWeatherSystem;
}

server::GameServer& GameSession::server() {
    if (!m_server) {
        throw std::runtime_error("Server is not available in this session.");
    }
    return *m_server;
}

const server::GameServer& GameSession::server() const {
    if (!m_server) {
        throw std::runtime_error("Server is not available in this session.");
    }
    return *m_server;
}

client::GameClient& GameSession::client() {
    if (!m_client) {
        throw std::runtime_error("Client is not initialized.");
    }
    return *m_client;
}

const client::GameClient& GameSession::client() const {
    if (!m_client) {
        throw std::runtime_error("Client is not initialized.");
    }
    return *m_client;
}

physics::PhysicsSystem& GameSession::physicsSystem() {
    if (!m_physicsSystem) {
        throw std::runtime_error("Physics system is not initialized.");
    }
    return *m_physicsSystem;
}

ecs::GameplayScene& GameSession::gameplayScene() {
    if (!m_gameplayScene) {
        throw std::runtime_error("Gameplay scene is not initialized.");
    }
    return *m_gameplayScene;
}

const ecs::GameplayScene& GameSession::gameplayScene() const {
    if (!m_gameplayScene) {
        throw std::runtime_error("Gameplay scene is not initialized.");
    }
    return *m_gameplayScene;
}

DropSystem& GameSession::dropSystem() {
    if (!m_dropSystem) {
        throw std::runtime_error("Drop system is not initialized.");
    }
    return *m_dropSystem;
}

ParticleSystem& GameSession::particleSystem() {
    if (!m_particleSystem) {
        throw std::runtime_error("Particle system is not initialized.");
    }
    return *m_particleSystem;
}

RainRenderer& GameSession::rainRenderer() {
    if (!m_rainRenderer) {
        throw std::runtime_error("Rain renderer is not initialized.");
    }
    return *m_rainRenderer;
}

CraftingSystem& GameSession::craftingSystem() {
    if (!m_craftingSystem) {
        throw std::runtime_error("Crafting system is not initialized.");
    }
    return *m_craftingSystem;
}

CameraController& GameSession::cameraController() {
    if (!m_cameraController) {
        throw std::runtime_error("Camera controller is not initialized.");
    }
    return *m_cameraController;
}

GameplayPresentationBuilder& GameSession::presentationBuilder() {
    if (!m_presentationBuilder) {
        throw std::runtime_error("Presentation builder is not initialized.");
    }
    return *m_presentationBuilder;
}

void GameSession::initWorld(int seed) {
    // World initialization is now handled by GameServer::init() in init().
    // This method is kept for API compatibility but is effectively a no-op.
    (void)seed;
}

void GameSession::initECS(const GameSessionDependencies& deps) {
    auto& svc = m_gameplayScene->services();
    svc.world              = m_isMultiplayer ? nullptr : &world();
    svc.worldView          = &worldView();
    svc.gameClient         = m_client.get();
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
    if (m_server) {
        m_server->setEcsRegistry(&reg.registry());
    }
    m_client->initEntityStore(reg, &deps.resourceMgr);
    m_client->setChatMessageCallback([&uiRenderer = deps.uiRenderer](const net::ServerChatMessage& message) {
        uiRenderer.appendOutputLine("<" + message.senderName + "> " + message.message,
                                    ConsoleDisplayBox::MessageType::Normal);
    });
    m_client->setSystemMessageCallback([&uiRenderer = deps.uiRenderer](const net::ServerSystemMessage& message) {
        ConsoleDisplayBox::MessageType type = ConsoleDisplayBox::MessageType::Normal;
        if (message.kind == net::ChatMessageKind::Warning) {
            type = ConsoleDisplayBox::MessageType::Warning;
        } else if (message.kind == net::ChatMessageKind::Success) {
            type = ConsoleDisplayBox::MessageType::Success;
        }
        uiRenderer.appendOutputLine(message.message, type);
    });
    m_client->setCommandResultCallback([&uiRenderer = deps.uiRenderer](const net::CommandResultMessage& result) {
        uiRenderer.appendOutputLine(result.message,
                                    result.success ? ConsoleDisplayBox::MessageType::Success
                                                   : ConsoleDisplayBox::MessageType::Warning);
    });
    m_dropSystem->bindRegistry(reg);
    m_dropSystem->bindServices(svc);
    m_particleSystem->bindRegistry(reg);

    const glm::vec3 spawnPos = m_isMultiplayer
        ? m_client->getAuthoritativePosition()
        : m_server->getSpawnPosition();
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
    m_client->setLocalModeCallback([this, deps](const net::NetworkGameplayMode mode) {
        if (!m_stateMachine) {
            return;
        }
        if (mode == net::NetworkGameplayMode::Creative) {
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
                deps.localeManager,
                client(),
                m_isMultiplayer
            };
            m_stateMachine->changeState(std::make_unique<CreativeModeState>(stateDeps));
            deps.uiRenderer.appendSuccessLine("Switched to creative mode.");
            return;
        }
        m_stateMachine->changeState(createInitialGameplayState(deps));
        deps.uiRenderer.appendSuccessLine("Switched to survival mode.");
    });
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
        deps.localeManager,
        client(),
        m_isMultiplayer
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
    constexpr float kServerTickDt = 1.0f / 20.0f;
    if (!m_isMultiplayer) {
        // Server tick: load chunks, process client messages, send world state
        // For Phase 1, use a fixed dt for server tick (will be decoupled in Phase 3)
        m_server->tick(kServerTickDt);
    }

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
    // Save player state before destroying ECS
    if (!m_isMultiplayer && m_server) {
        saveLocalPlayer();
    }

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

void GameSession::saveLocalPlayer() {
    if (!m_server || !m_gameplayScene) return;

    auto* sm = m_server->saveManager();
    if (!sm) return;

    auto& ecsReg = m_gameplayScene->registry().registry();
    auto view = ecsReg.view<ecs::LocalPlayerTag, ecs::TransformComponent>();
    for (auto e : view) {
        save::PlayerData data;

        // Position
        auto& transform = ecsReg.get<ecs::TransformComponent>(e);
        data.posX = transform.position.x;
        data.posY = transform.position.y;
        data.posZ = transform.position.z;

        // Velocity (from PhysicsBodyComponent)
        if (ecsReg.all_of<ecs::PhysicsBodyComponent>(e)) {
            auto& body = ecsReg.get<ecs::PhysicsBodyComponent>(e);
            data.velX = body.body.velocity.x;
            data.velY = body.body.velocity.y;
            data.velZ = body.body.velocity.z;
        }

        // Camera (yaw, pitch)
        if (ecsReg.all_of<ecs::CameraStateComponent>(e)) {
            auto& cam = ecsReg.get<ecs::CameraStateComponent>(e);
            data.yaw = cam.yaw;
            data.pitch = cam.pitch;
        }

        // Health
        if (ecsReg.all_of<ecs::HealthComponent>(e)) {
            auto& h = ecsReg.get<ecs::HealthComponent>(e);
            data.health = h.current;
            data.healthMax = h.max;
        }

        // Armor
        if (ecsReg.all_of<ecs::ArmorComponent>(e)) {
            auto& a = ecsReg.get<ecs::ArmorComponent>(e);
            data.armor = a.current;
            data.armorMax = a.max;
        }

        // Food
        if (ecsReg.all_of<ecs::FoodComponent>(e)) {
            auto& f = ecsReg.get<ecs::FoodComponent>(e);
            data.food = f.current;
            data.foodMax = f.max;
            data.saturation = f.saturation;
        }

        // Flight
        if (ecsReg.all_of<ecs::FlightStateComponent>(e)) {
            auto& fs = ecsReg.get<ecs::FlightStateComponent>(e);
            data.isFlying = fs.isFlying;
        }

        // Inventory
        if (ecsReg.all_of<ecs::InventoryComponent>(e)) {
            auto& inv = ecsReg.get<ecs::InventoryComponent>(e);
            data.selectedSlot = inv.selectedHotbarSlot;
        }

        if (ecsReg.all_of<ecs::InventoryDataComponent>(e)) {
            auto& invData = ecsReg.get<ecs::InventoryDataComponent>(e);
            const Inventory& inv = invData.inventory;
            data.inventory.resize(Inventory::INVENTORY_SIZE);
            for (int i = 0; i < Inventory::INVENTORY_SIZE; ++i) {
                ItemStack stack = inv.getSlotStack(i);
                if (stack.itemId != 0 && stack.count > 0) {
                    data.inventory[i].item = ItemRegistry::getNamespacedId(stack.itemId).full();
                    data.inventory[i].count = stack.count;
                    data.inventory[i].durability = stack.durability;
                }
            }
        }

        sm->saveLocalPlayer(data);
        return;
    }
}

void GameSession::loadLocalPlayer() {
    if (!m_server || !m_gameplayScene) return;

    auto* sm = m_server->saveManager();
    if (!sm) return;

    save::PlayerData data;
    if (!sm->loadLocalPlayer(data)) return;

    auto& ecsReg = m_gameplayScene->registry().registry();
    auto view = ecsReg.view<ecs::LocalPlayerTag, ecs::TransformComponent>();
    for (auto e : view) {
        // Position
        auto& transform = ecsReg.get<ecs::TransformComponent>(e);
        transform.position = glm::vec3(data.posX, data.posY, data.posZ);

        // Also update physics body position
        if (ecsReg.all_of<ecs::PhysicsBodyComponent>(e)) {
            auto& body = ecsReg.get<ecs::PhysicsBodyComponent>(e);
            body.body.position = transform.position;
            body.body.velocity = glm::vec3(data.velX, data.velY, data.velZ);
        }

        // Camera
        if (ecsReg.all_of<ecs::CameraStateComponent>(e)) {
            auto& cam = ecsReg.get<ecs::CameraStateComponent>(e);
            cam.yaw = data.yaw;
            cam.pitch = data.pitch;
        }

        // Health
        if (ecsReg.all_of<ecs::HealthComponent>(e)) {
            auto& h = ecsReg.get<ecs::HealthComponent>(e);
            h.current = data.health;
            h.max = data.healthMax;
        }

        // Armor
        if (ecsReg.all_of<ecs::ArmorComponent>(e)) {
            auto& a = ecsReg.get<ecs::ArmorComponent>(e);
            a.current = data.armor;
            a.max = data.armorMax;
        }

        // Food
        if (ecsReg.all_of<ecs::FoodComponent>(e)) {
            auto& f = ecsReg.get<ecs::FoodComponent>(e);
            f.current = data.food;
            f.max = data.foodMax;
            f.saturation = data.saturation;
        }

        // Flight
        if (ecsReg.all_of<ecs::FlightStateComponent>(e)) {
            auto& fs = ecsReg.get<ecs::FlightStateComponent>(e);
            fs.isFlying = data.isFlying;
        }

        // Inventory
        if (ecsReg.all_of<ecs::InventoryComponent>(e)) {
            auto& inv = ecsReg.get<ecs::InventoryComponent>(e);
            inv.selectedHotbarSlot = data.selectedSlot;
        }

        if (ecsReg.all_of<ecs::InventoryDataComponent>(e)) {
            auto& invData = ecsReg.get<ecs::InventoryDataComponent>(e);
            Inventory& inv = invData.inventory;
            for (size_t i = 0; i < data.inventory.size() && i < Inventory::INVENTORY_SIZE; ++i) {
                const auto& slot = data.inventory[i];
                if (!slot.item.empty() && slot.count > 0) {
                    ItemID itemId = ItemRegistry::getId(NamespacedId(slot.item));
                    inv.setSlotItem(static_cast<int>(i), itemId, slot.count);
                }
            }
        }

        std::printf("[Save] Loaded local player: pos=(%.1f, %.1f, %.1f)\n",
                     data.posX, data.posY, data.posZ);
        return;
    }
}
