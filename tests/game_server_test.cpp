#include "server/GameServer.h"
#include "client/GameClient.h"
#include "ecs/GameplayRegistry.h"
#include "ecs/components/Components.h"
#include "ecs/components/NetworkComponents.h"
#include "ecs/entity/MobModelFactory.h"
#include "net/InProcessTransport.h"
#include "net/ENetTransport.h"
#include "net/PacketCodec.h"
#include "item/Item.h"
#include "world/World.h"
#include "world/DayNightSystem.h"
#include "world/WeatherSystem.h"
#include "world/block/Block.h"
#include "renderer/mesh/ChunkMesher.h"
#include "thread/ThreadPool.h"
#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <queue>
#include <thread>

class ManualTransport final : public net::ITransportEndpoint {
public:
    void send(net::Packet packet) override {
        sent.push(std::move(packet));
    }

    bool tryReceive(net::Packet& out) override {
        if (inbox.empty()) {
            return false;
        }
        out = std::move(inbox.front());
        inbox.pop();
        return true;
    }

    [[nodiscard]] bool isConnected() const override { return connected; }
    [[nodiscard]] bool hasActiveRemote() const override { return activeRemote; }

    void pushIncoming(net::Packet packet) {
        inbox.push(std::move(packet));
    }

    std::queue<net::Packet> inbox;
    std::queue<net::Packet> sent;
    bool connected = true;
    bool activeRemote = true;
};

struct ServerHarness {
    explicit ServerHarness(int renderDistance = 8) {
        server.init(1234, nullptr, renderDistance);
    }

    server::GameServer server;
};

static float horizontalDistanceSq(const glm::vec3& a, const glm::vec3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return dx * dx + dz * dz;
}

static void require(const bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::abort();
    }
}

static void testServerInit() {
    ServerHarness harness;

    // World should be initialized
    assert(&harness.server.world() != nullptr);
    assert(harness.server.world().getRenderDistance() == 8);
    assert(!harness.server.areSpawnChunksReady());
    assert(harness.server.currentTick() == 0);
    std::printf("[PASS] testServerInit\n");
}

static void testAcceptClient() {
    ServerHarness harness;

    auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
    harness.server.acceptClient(std::move(serverTransport), 1);

    // Server should have accepted the client without crashing
    assert(harness.server.currentTick() == 0);
    std::printf("[PASS] testAcceptClient\n");
}

static void testClientServerHandshake() {
    ServerHarness harness;

    client::GameClient client;

    auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
    harness.server.acceptClient(std::move(serverTransport), 1);
    client.connect(std::move(clientTransport));

    // Process the ClientHello on the server
    harness.server.tick(1.0f / 20.0f);

    // Client should receive ServerHello
    client.receiveMessages();
    assert(client.getClientId() == 1);
    std::printf("[PASS] testClientServerHandshake\n");
}

static void testServerTick() {
    ServerHarness harness;

    auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
    harness.server.acceptClient(std::move(serverTransport), 1);

    // Run several ticks to allow chunk generation
    for (int i = 0; i < 60; ++i) {
        harness.server.tick(1.0f / 20.0f);
    }

    assert(harness.server.currentTick() == 60);
    // Server should have loaded some chunks
    assert(!harness.server.world().getActiveChunks().empty());
    std::printf("[PASS] testServerTick\n");
}

static void testChunkStreamingToClient() {
    ServerHarness harness;

    client::GameClient client;

    auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
    harness.server.acceptClient(std::move(serverTransport), 1);
    client.connect(std::move(clientTransport));

    // Run enough ticks for spawn chunks to generate
    for (int i = 0; i < 120; ++i) {
        harness.server.tick(1.0f / 20.0f);
        client.receiveMessages();
    }

    // Client should have received chunks
    assert(client.clientWorld().loadedChunkCount() > 0);
    std::printf("[PASS] testChunkStreamingToClient\n");
}

static void testInputRoundTrip() {
    ServerHarness harness;

    client::GameClient client;

    auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
    harness.server.acceptClient(std::move(serverTransport), 1);
    client.connect(std::move(clientTransport));

    // Initial handshake
    harness.server.tick(1.0f / 20.0f);
    client.receiveMessages();

    // Client sends input
    client.sendInput(0.016f, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec2(0.0f), false, false, false);

    // Server processes input
    harness.server.tick(1.0f / 20.0f);

    // Client receives snapshot
    client.receiveMessages();

    // The server should have acked the input
    assert(client.lastSnapshot().ackInputSequence >= 1);
    std::printf("[PASS] testInputRoundTrip\n");
}

static void testClientBlockActionRoundTrip() {
    ServerHarness harness;
    client::GameClient client;

    auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
    harness.server.acceptClient(std::move(serverTransport), 1);
    client.connect(std::move(clientTransport));

    const glm::vec3 spawn = harness.server.getSpawnPosition();
    const glm::ivec3 placeBlock(0, Chunk::SIZE_Y - 8, 0);
    for (int tick = 0;
         tick < 240 && !harness.server.world().isChunkLoadedForBlock(placeBlock.x, placeBlock.y, placeBlock.z);
         ++tick) {
        harness.server.tick(1.0f / 20.0f);
        client.receiveMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(harness.server.world().isChunkLoadedForBlock(placeBlock.x, placeBlock.y, placeBlock.z));
    assert(harness.server.world().getBlock(placeBlock.x, placeBlock.y, placeBlock.z) == BlockIds::AIR);

    const glm::vec3 actionPosition = glm::vec3(placeBlock) + glm::vec3(0.5f);
    net::ClientBlockAction place;
    place.sequence = 1;
    place.action = net::ClientBlockActionType::Place;
    place.placeBlock = placeBlock;
    place.playerPosition = actionPosition;
    place.blockState = BlockIds::STONE;
    client.sendBlockAction(place);

    harness.server.tick(1.0f / 20.0f);
    client.receiveMessages();
    assert(harness.server.world().getBlock(placeBlock.x, placeBlock.y, placeBlock.z) == BlockIds::STONE);
    assert(client.clientWorld().getBlock(placeBlock.x, placeBlock.y, placeBlock.z) == BlockIds::STONE);

    net::ClientBlockAction breakAction;
    breakAction.sequence = 2;
    breakAction.action = net::ClientBlockActionType::Break;
    breakAction.targetBlock = placeBlock;
    breakAction.playerPosition = actionPosition;
    client.sendBlockAction(breakAction);

    harness.server.tick(1.0f / 20.0f);
    client.receiveMessages();
    assert(harness.server.world().getBlock(placeBlock.x, placeBlock.y, placeBlock.z) == BlockIds::AIR);
    assert(client.clientWorld().getBlock(placeBlock.x, placeBlock.y, placeBlock.z) == BlockIds::AIR);
    std::printf("[PASS] testClientBlockActionRoundTrip\n");
}

static void testChatBroadcastRoundTrip() {
    ServerHarness harness;
    client::GameClient clientA;
    client::GameClient clientB;

    auto [clientATransport, serverATransport] = net::InProcessTransport::createPair();
    auto [clientBTransport, serverBTransport] = net::InProcessTransport::createPair();
    harness.server.acceptClient(std::move(serverATransport), 1);
    harness.server.acceptClient(std::move(serverBTransport), 2);
    clientA.connect(std::move(clientATransport));
    clientB.connect(std::move(clientBTransport));

    harness.server.tick(1.0f / 20.0f);
    clientA.receiveMessages();
    clientB.receiveMessages();

    bool sawChat = false;
    clientB.setChatMessageCallback([&](const net::ServerChatMessage& msg) {
        sawChat = msg.senderId == 1 && msg.senderName == "Player1" && msg.message == "hello world";
    });

    clientA.sendChatMessage("hello world");
    harness.server.tick(1.0f / 20.0f);
    clientB.receiveMessages();

    assert(sawChat);
    std::printf("[PASS] testChatBroadcastRoundTrip\n");
}

static void testAdminCommandUpdatesWorldState() {
    ServerHarness harness;
    client::GameClient client;

    DayNightSystem dayNight;
    WeatherSystem weather;

    auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
    harness.server.acceptClient(std::move(serverTransport), 1);
    client.connect(std::move(clientTransport));
    client.clientWorld().setDayNightSystem(&dayNight);
    client.clientWorld().setWeatherSystem(&weather);

    harness.server.tick(1.0f / 20.0f);
    client.receiveMessages();

    bool sawTimeResult = false;
    client.setCommandResultCallback([&](const net::CommandResultMessage& result) {
        if (result.success && result.message.find("Time set to 600") != std::string::npos) {
            sawTimeResult = true;
        }
    });

    client.sendCommandRequest("/time set 600");
    harness.server.tick(1.0f / 20.0f);
    client.receiveMessages();

    assert(sawTimeResult);
    assert(static_cast<int>(harness.server.world().getDayNightSystem().getTimeOfDay()) == 600);
    assert(static_cast<int>(dayNight.getTimeOfDay()) == 600);

    client.sendCommandRequest("/weather storm");
    harness.server.tick(1.0f / 20.0f);
    client.receiveMessages();
    assert(harness.server.world().getWeatherSystem().getTargetState().type == WeatherType::Storm);
    assert(weather.getTargetState().type == WeatherType::Storm);
    std::printf("[PASS] testAdminCommandUpdatesWorldState\n");
}

static void testNonAdminCommandDenied() {
    ServerHarness harness;
    client::GameClient clientA;
    client::GameClient clientB;

    auto [clientATransport, serverATransport] = net::InProcessTransport::createPair();
    auto [clientBTransport, serverBTransport] = net::InProcessTransport::createPair();
    harness.server.acceptClient(std::move(serverATransport), 1);
    harness.server.acceptClient(std::move(serverBTransport), 2);
    clientA.connect(std::move(clientATransport));
    clientB.connect(std::move(clientBTransport));

    harness.server.tick(1.0f / 20.0f);
    clientA.receiveMessages();
    clientB.receiveMessages();

    bool denied = false;
    clientB.setCommandResultCallback([&](const net::CommandResultMessage& result) {
        denied = !result.success && result.message.find("permission") != std::string::npos;
    });

    clientB.sendCommandRequest("/weather rain");
    harness.server.tick(1.0f / 20.0f);
    clientB.receiveMessages();

    assert(denied);
    assert(harness.server.world().getWeatherSystem().getTargetState().type == WeatherType::Clear);
    std::printf("[PASS] testNonAdminCommandDenied\n");
}

static void testSummonZombieSpawnsNetworkMob() {
    ServerHarness harness;
    ecs::GameplayRegistry registry;
    harness.server.setEcsRegistry(&registry);

    auto clientTransport = std::make_unique<ManualTransport>();
    ManualTransport* clientPtr = clientTransport.get();

    net::Packet hello;
    hello.type = net::MessageType::ClientHello;
    hello.inProcessPayload = net::ClientHello{};
    clientPtr->pushIncoming(std::move(hello));

    harness.server.acceptClient(std::move(clientTransport), 1);
    harness.server.tick(1.0f / 20.0f);
    while (!clientPtr->sent.empty()) {
        clientPtr->sent.pop();
    }

    net::Packet commandPacket;
    commandPacket.type = net::MessageType::ClientCommandRequest;
    net::ClientCommandRequest command;
    command.sequence = 8;
    command.command = "/summon zombie";
    commandPacket.inProcessPayload = command;
    clientPtr->pushIncoming(std::move(commandPacket));

    harness.server.tick(1.0f / 20.0f);

    bool sawCommandResult = false;
    bool sawMobSpawn = false;
    net::EntityNetId spawnedNetId = 0;
    while (!clientPtr->sent.empty()) {
        net::Packet packet = std::move(clientPtr->sent.front());
        clientPtr->sent.pop();
        if (packet.type == net::MessageType::CommandResult && packet.inProcessPayload.has_value()) {
            const auto& result = std::any_cast<const net::CommandResultMessage&>(packet.inProcessPayload);
            if (result.sequence == 8 && result.success && result.message.find("Summoned zombie") != std::string::npos) {
                sawCommandResult = true;
            }
        }
        if (packet.type == net::MessageType::EntitySpawn && packet.inProcessPayload.has_value()) {
            const auto& spawn = std::any_cast<const net::EntitySpawnMessage&>(packet.inProcessPayload);
            if (spawn.kind == net::EntityKind::Mob) {
                sawMobSpawn = true;
                spawnedNetId = spawn.netId;
            }
        }
    }

    require(sawCommandResult, "external registry summon should return a successful command result");
    require(sawMobSpawn, "external registry summon should send a mob spawn");
    require(spawnedNetId != 0, "external registry summon should assign a mob net id");

    auto view = registry.registry().view<ecs::MobTag,
                                        ecs::HealthComponent,
                                        ecs::NetworkSyncTag,
                                        ecs::EntityNetIdComponent>();
    require(view.begin() != view.end(), "external registry summon should create a networked mob");
    const entt::entity zombie = *view.begin();
    require(registry.registry().get<ecs::EntityNetIdComponent>(zombie).netId == spawnedNetId,
            "external registry mob net id should match spawn packet");
    std::printf("[PASS] testSummonZombieSpawnsNetworkMob\n");
}

static void testSummonZombieUsesOwnedServerEcs() {
    ServerHarness harness;

    auto clientTransport = std::make_unique<ManualTransport>();
    ManualTransport* clientPtr = clientTransport.get();

    net::Packet hello;
    hello.type = net::MessageType::ClientHello;
    hello.inProcessPayload = net::ClientHello{};
    clientPtr->pushIncoming(std::move(hello));

    harness.server.acceptClient(std::move(clientTransport), 1);
    harness.server.tick(1.0f / 20.0f);
    while (!clientPtr->sent.empty()) {
        clientPtr->sent.pop();
    }

    net::Packet commandPacket;
    commandPacket.type = net::MessageType::ClientCommandRequest;
    net::ClientCommandRequest command;
    command.sequence = 18;
    command.command = "/summon zombie";
    commandPacket.inProcessPayload = command;
    clientPtr->pushIncoming(std::move(commandPacket));

    harness.server.tick(1.0f / 20.0f);

    bool sawCommandResult = false;
    bool sawMobSpawn = false;
    while (!clientPtr->sent.empty()) {
        net::Packet packet = std::move(clientPtr->sent.front());
        clientPtr->sent.pop();
        if (packet.type == net::MessageType::CommandResult && packet.inProcessPayload.has_value()) {
            const auto& result = std::any_cast<const net::CommandResultMessage&>(packet.inProcessPayload);
            if (result.sequence == 18 && result.success) {
                sawCommandResult = true;
            }
        }
        if (packet.type == net::MessageType::EntitySpawn && packet.inProcessPayload.has_value()) {
            const auto& spawn = std::any_cast<const net::EntitySpawnMessage&>(packet.inProcessPayload);
            if (spawn.kind == net::EntityKind::Mob && spawn.netId != 0) {
                sawMobSpawn = true;
            }
        }
    }

    assert(sawCommandResult);
    require(sawMobSpawn, "owned ECS pursue test should receive mob spawn");
    std::printf("[PASS] testSummonZombieUsesOwnedServerEcs\n");
}

static void testOwnedServerZombiePursuesPlayer() {
    ServerHarness harness;
    const glm::vec3 playerPosition = harness.server.getSpawnPosition();

    auto clientTransport = std::make_unique<ManualTransport>();
    ManualTransport* clientPtr = clientTransport.get();

    net::Packet hello;
    hello.type = net::MessageType::ClientHello;
    hello.inProcessPayload = net::ClientHello{};
    clientPtr->pushIncoming(std::move(hello));

    harness.server.acceptClient(std::move(clientTransport), 1);
    harness.server.tick(1.0f / 20.0f);
    while (!clientPtr->sent.empty()) {
        clientPtr->sent.pop();
    }

    net::Packet commandPacket;
    commandPacket.type = net::MessageType::ClientCommandRequest;
    net::ClientCommandRequest command;
    command.sequence = 20;
    command.command = "/summon zombie";
    commandPacket.inProcessPayload = command;
    clientPtr->pushIncoming(std::move(commandPacket));

    harness.server.tick(1.0f / 20.0f);

    bool sawMobSpawn = false;
    net::EntityNetId zombieNetId = 0;
    glm::vec3 initialZombiePosition(0.0f);
    while (!clientPtr->sent.empty()) {
        net::Packet packet = std::move(clientPtr->sent.front());
        clientPtr->sent.pop();
        if (packet.type == net::MessageType::EntitySpawn && packet.inProcessPayload.has_value()) {
            const auto& spawn = std::any_cast<const net::EntitySpawnMessage&>(packet.inProcessPayload);
            if (spawn.kind == net::EntityKind::Mob && spawn.netId != 0) {
                sawMobSpawn = true;
                zombieNetId = spawn.netId;
                initialZombiePosition = spawn.position;
            }
        }
    }
    assert(sawMobSpawn);

    glm::vec3 latestZombiePosition = initialZombiePosition;
    bool sawMovingSnapshot = false;
    for (int i = 0; i < 30; ++i) {
        net::Packet inputPacket;
        inputPacket.type = net::MessageType::ClientInput;
        net::ClientInput input;
        input.sequence = static_cast<uint32_t>(i + 1);
        input.playerPosition = playerPosition;
        input.playerVelocity = glm::vec3(0.0f);
        input.yaw = 0.0f;
        inputPacket.inProcessPayload = input;
        clientPtr->pushIncoming(std::move(inputPacket));

        harness.server.tick(1.0f / 20.0f);
        while (!clientPtr->sent.empty()) {
            net::Packet packet = std::move(clientPtr->sent.front());
            clientPtr->sent.pop();
            if (packet.type != net::MessageType::EntitySnapshot || !packet.inProcessPayload.has_value()) {
                continue;
            }

            const auto& snapshot = std::any_cast<const net::EntitySnapshotMessage&>(packet.inProcessPayload);
            for (const auto& entity : snapshot.entities) {
                if (entity.netId == zombieNetId) {
                    latestZombiePosition = entity.position;
                    sawMovingSnapshot = true;
                }
            }
        }
    }

    require(sawMovingSnapshot, "owned ECS zombie should send moving snapshots");
    require(horizontalDistanceSq(latestZombiePosition, playerPosition) <
            horizontalDistanceSq(initialZombiePosition, playerPosition),
            "owned ECS zombie should move closer to the player");
    std::printf("[PASS] testOwnedServerZombiePursuesPlayer\n");
}

static void testOwnedServerPlayerMeleeKillsZombieAndDropsItem() {
    ServerHarness harness;
    const glm::vec3 playerPosition = harness.server.getSpawnPosition() + glm::vec3(0.0f, -1.0f, 0.0f);

    auto clientTransport = std::make_unique<ManualTransport>();
    ManualTransport* clientPtr = clientTransport.get();

    net::Packet hello;
    hello.type = net::MessageType::ClientHello;
    hello.inProcessPayload = net::ClientHello{};
    clientPtr->pushIncoming(std::move(hello));

    harness.server.acceptClient(std::move(clientTransport), 1);
    harness.server.tick(1.0f / 20.0f);
    while (!clientPtr->sent.empty()) {
        clientPtr->sent.pop();
    }

    net::Packet positionPacket;
    positionPacket.type = net::MessageType::ClientInput;
    net::ClientInput positionInput;
    positionInput.sequence = 1;
    positionInput.playerPosition = playerPosition;
    positionInput.playerVelocity = glm::vec3(0.0f);
    positionInput.yaw = 0.0f;
    positionInput.pitch = 0.0f;
    positionPacket.inProcessPayload = positionInput;
    clientPtr->pushIncoming(std::move(positionPacket));
    harness.server.tick(1.0f / 20.0f);
    while (!clientPtr->sent.empty()) {
        clientPtr->sent.pop();
    }

    net::Packet commandPacket;
    commandPacket.type = net::MessageType::ClientCommandRequest;
    net::ClientCommandRequest command;
    command.sequence = 21;
    command.command = "/summon zombie";
    commandPacket.inProcessPayload = command;
    clientPtr->pushIncoming(std::move(commandPacket));

    harness.server.tick(1.0f / 20.0f);

    net::EntityNetId zombieNetId = 0;
    while (!clientPtr->sent.empty()) {
        net::Packet packet = std::move(clientPtr->sent.front());
        clientPtr->sent.pop();
        if (packet.type == net::MessageType::EntitySpawn && packet.inProcessPayload.has_value()) {
            const auto& spawn = std::any_cast<const net::EntitySpawnMessage&>(packet.inProcessPayload);
            if (spawn.kind == net::EntityKind::Mob && spawn.netId != 0) {
                zombieNetId = spawn.netId;
            }
        }
    }
    require(zombieNetId != 0, "melee test should receive zombie net id");

    bool sawZombieDespawn = false;
    bool sawDropSpawn = false;
    uint32_t inputSequence = 100;

    auto drainEntityPackets = [&]() {
        while (!clientPtr->sent.empty()) {
            net::Packet packet = std::move(clientPtr->sent.front());
            clientPtr->sent.pop();
            if (packet.type == net::MessageType::EntityDespawn && packet.inProcessPayload.has_value()) {
                const auto& despawn = std::any_cast<const net::EntityDespawnMessage&>(packet.inProcessPayload);
                sawZombieDespawn = sawZombieDespawn || despawn.netId == zombieNetId;
            }
            if (packet.type == net::MessageType::EntitySpawn && packet.inProcessPayload.has_value()) {
                const auto& spawn = std::any_cast<const net::EntitySpawnMessage&>(packet.inProcessPayload);
                sawDropSpawn = sawDropSpawn || spawn.kind == net::EntityKind::Drop;
            }
        }
    };

    auto pushInput = [&](const uint32_t actions) {
        net::Packet inputPacket;
        inputPacket.type = net::MessageType::ClientInput;
        net::ClientInput input;
        input.sequence = ++inputSequence;
        input.playerPosition = playerPosition;
        input.playerVelocity = glm::vec3(0.0f);
        input.yaw = 0.0f;
        input.pitch = 0.0f;
        input.actions = actions;
        inputPacket.inProcessPayload = input;
        clientPtr->pushIncoming(std::move(inputPacket));
    };

    for (int attempt = 0; attempt < 7 && !sawZombieDespawn; ++attempt) {
        pushInput(net::ClientInputActions::Attack);
        harness.server.tick(1.0f / 20.0f);
        drainEntityPackets();

        for (int cooldownTick = 0; cooldownTick < 10 && !sawZombieDespawn; ++cooldownTick) {
            pushInput(0);
            harness.server.tick(1.0f / 20.0f);
            drainEntityPackets();
        }
    }

    require(sawZombieDespawn, "server melee should despawn the killed zombie");
    require(sawDropSpawn, "server melee zombie death should spawn a drop");
    std::printf("[PASS] testOwnedServerPlayerMeleeKillsZombieAndDropsItem\n");
}

static void testPersistentZombieRestoresFromSave() {
    const std::filesystem::path saveRoot = "test_server_entities_save";
    std::filesystem::remove_all(saveRoot);

    {
        server::GameServer server;
        server.init(1234, nullptr, 2, saveRoot, "Entity Save Test");

        ecs::GameplayRegistry registry;
        server.setEcsRegistry(&registry);
        const entt::entity zombie = ecs::MobModelFactory::createZombie(registry, glm::vec3(4.0f, 65.0f, -2.0f));
        registry.registry().get<ecs::HealthComponent>(zombie).current = 9;
        registry.registry().get<ecs::MobAIComponent>(zombie).yaw = 135.0f;
        registry.registry().get<ecs::PhysicsBodyComponent>(zombie).body.velocity = glm::vec3(0.25f, 0.0f, -0.5f);

        server.savePersistentEntities();
        server.setEcsRegistry(static_cast<ecs::GameplayRegistry*>(nullptr));
        server.shutdown();
    }

    {
        server::GameServer server;
        server.init(1234, nullptr, 2, saveRoot, "Entity Save Test");

        ecs::GameplayRegistry registry;
        server.setEcsRegistry(&registry);
        server.restorePersistentEntities();

        auto view = registry.registry().view<ecs::MobTag,
                                            ecs::TransformComponent,
                                            ecs::HealthComponent,
                                            ecs::MobAIComponent,
                                            ecs::PhysicsBodyComponent,
                                            ecs::NetworkSyncTag>();
        require(view.begin() != view.end(), "persistent zombie restore should create a mob");
        const entt::entity zombie = *view.begin();
        const auto& transform = registry.registry().get<ecs::TransformComponent>(zombie);
        const auto& health = registry.registry().get<ecs::HealthComponent>(zombie);
        const auto& ai = registry.registry().get<ecs::MobAIComponent>(zombie);
        const auto& body = registry.registry().get<ecs::PhysicsBodyComponent>(zombie);
        require(transform.position.x == 4.0f, "persistent zombie restore should keep X position");
        require(transform.position.z == -2.0f, "persistent zombie restore should keep Z position");
        require(health.current == 9, "persistent zombie restore should keep health");
        require(ai.yaw == 135.0f, "persistent zombie restore should keep yaw");
        require(body.body.velocity.z == -0.5f, "persistent zombie restore should keep velocity");

        server.setEcsRegistry(static_cast<ecs::GameplayRegistry*>(nullptr));
        server.shutdown();
    }

    std::filesystem::remove_all(saveRoot);
    std::printf("[PASS] testPersistentZombieRestoresFromSave\n");
}

static void testOwnedServerEcsRestoresPersistentZombie() {
    const std::filesystem::path saveRoot = "test_owned_server_entities_save";
    std::filesystem::remove_all(saveRoot);

    {
        server::GameServer server;
        server.init(1234, nullptr, 2, saveRoot, "Owned Entity Save Test");

        auto clientTransport = std::make_unique<ManualTransport>();
        ManualTransport* clientPtr = clientTransport.get();
        net::Packet hello;
        hello.type = net::MessageType::ClientHello;
        hello.inProcessPayload = net::ClientHello{};
        clientPtr->pushIncoming(std::move(hello));
        server.acceptClient(std::move(clientTransport), 1);
        server.tick(1.0f / 20.0f);
        while (!clientPtr->sent.empty()) {
            clientPtr->sent.pop();
        }

        net::Packet commandPacket;
        commandPacket.type = net::MessageType::ClientCommandRequest;
        net::ClientCommandRequest command;
        command.sequence = 19;
        command.command = "/summon zombie";
        commandPacket.inProcessPayload = command;
        clientPtr->pushIncoming(std::move(commandPacket));
        server.tick(1.0f / 20.0f);

        bool sawMobSpawn = false;
        while (!clientPtr->sent.empty()) {
            net::Packet packet = std::move(clientPtr->sent.front());
            clientPtr->sent.pop();
            if (packet.type == net::MessageType::EntitySpawn && packet.inProcessPayload.has_value()) {
                const auto& spawn = std::any_cast<const net::EntitySpawnMessage&>(packet.inProcessPayload);
                sawMobSpawn = sawMobSpawn || spawn.kind == net::EntityKind::Mob;
            }
        }
        require(sawMobSpawn, "owned ECS save setup should spawn a mob");

        server.savePersistentEntities();
        server.shutdown();
    }

    {
        server::GameServer server;
        server.init(1234, nullptr, 2, saveRoot, "Owned Entity Save Test");

        auto clientTransport = std::make_unique<ManualTransport>();
        ManualTransport* clientPtr = clientTransport.get();
        net::Packet hello;
        hello.type = net::MessageType::ClientHello;
        hello.inProcessPayload = net::ClientHello{};
        clientPtr->pushIncoming(std::move(hello));
        server.acceptClient(std::move(clientTransport), 1);
        server.tick(1.0f / 20.0f);

        bool sawMobSpawn = false;
        while (!clientPtr->sent.empty()) {
            net::Packet packet = std::move(clientPtr->sent.front());
            clientPtr->sent.pop();
            if (packet.type == net::MessageType::EntitySpawn && packet.inProcessPayload.has_value()) {
                const auto& spawn = std::any_cast<const net::EntitySpawnMessage&>(packet.inProcessPayload);
                sawMobSpawn = sawMobSpawn || (spawn.kind == net::EntityKind::Mob && spawn.netId != 0);
            }
        }
        require(sawMobSpawn, "owned ECS restore should send restored mob spawn");
        server.shutdown();
    }

    std::filesystem::remove_all(saveRoot);
    std::printf("[PASS] testOwnedServerEcsRestoresPersistentZombie\n");
}

static void testChatCommandCodecRoundTrip() {
    net::ClientCommandRequest request;
    request.sequence = 42;
    request.command = "/weather rain";
    const auto encodedRequest = net::PacketCodec::encodeClientCommandRequest(request);
    net::ClientCommandRequest decodedRequest;
    assert(net::PacketCodec::decodeClientCommandRequest(encodedRequest.data(), encodedRequest.size(), decodedRequest));
    assert(decodedRequest.sequence == request.sequence);
    assert(decodedRequest.command == request.command);

    net::ServerSystemMessage system;
    system.kind = net::ChatMessageKind::Success;
    system.message = "done";
    const auto encodedSystem = net::PacketCodec::encodeServerSystemMessage(system);
    net::ServerSystemMessage decodedSystem;
    assert(net::PacketCodec::decodeServerSystemMessage(encodedSystem.data(), encodedSystem.size(), decodedSystem));
    assert(decodedSystem.kind == system.kind);
    assert(decodedSystem.message == system.message);
    std::printf("[PASS] testChatCommandCodecRoundTrip\n");
}

static void testServerTickBreaksUnsupportedPlant() {
    ServerHarness harness;

    for (int i = 0; i < 40; ++i) {
        harness.server.tick(1.0f / 20.0f);
    }

    const glm::ivec3 base(0, static_cast<int>(harness.server.getSpawnPosition().y), 0);
    harness.server.world().setBlock(base.x, base.y, base.z, BlockIds::DIRT);
    harness.server.world().setBlock(base.x, base.y + 1, base.z, BlockIds::TALL_GRASS);
    harness.server.world().setBlock(base.x, base.y, base.z, BlockIds::AIR);

    harness.server.tick(1.0f / 20.0f);

    assert(harness.server.world().getBlock(base.x, base.y + 1, base.z) == BlockIds::AIR);
    std::printf("[PASS] testServerTickBreaksUnsupportedPlant\n");
}

static void testDisconnectedPlayerDespawnsForOtherClients() {
    ServerHarness harness;

    auto clientA = std::make_unique<ManualTransport>();
    auto clientB = std::make_unique<ManualTransport>();
    ManualTransport* clientAPtr = clientA.get();
    ManualTransport* clientBPtr = clientB.get();

    net::Packet helloA;
    helloA.type = net::MessageType::ClientHello;
    helloA.inProcessPayload = net::ClientHello{};
    clientAPtr->pushIncoming(std::move(helloA));

    net::Packet helloB;
    helloB.type = net::MessageType::ClientHello;
    helloB.inProcessPayload = net::ClientHello{};
    clientBPtr->pushIncoming(std::move(helloB));

    harness.server.acceptClient(std::move(clientA), 1);
    harness.server.acceptClient(std::move(clientB), 2);
    harness.server.tick(1.0f / 20.0f);
    while (!clientAPtr->sent.empty()) {
        clientAPtr->sent.pop();
    }

    clientBPtr->activeRemote = false;
    clientBPtr->connected = false;
    harness.server.tick(1.0f / 20.0f);

    bool sawDespawn = false;
    bool sawDisconnectedPlayerSnapshot = false;
    while (!clientAPtr->sent.empty()) {
        net::Packet packet = std::move(clientAPtr->sent.front());
        clientAPtr->sent.pop();
        if (packet.type == net::MessageType::EntityDespawn && packet.inProcessPayload.has_value()) {
            const auto& msg = std::any_cast<const net::EntityDespawnMessage&>(packet.inProcessPayload);
            if (msg.netId == (0x80000000u | 2u)) {
                sawDespawn = true;
            }
        }
        if (packet.type == net::MessageType::EntitySnapshot && packet.inProcessPayload.has_value()) {
            const auto& msg = std::any_cast<const net::EntitySnapshotMessage&>(packet.inProcessPayload);
            for (const auto& item : msg.entities) {
                if (item.netId == (0x80000000u | 2u)) {
                    sawDisconnectedPlayerSnapshot = true;
                }
            }
        }
    }

    assert(sawDespawn);
    assert(!sawDisconnectedPlayerSnapshot);
    std::printf("[PASS] testDisconnectedPlayerDespawnsForOtherClients\n");
}

static void testChunkDataDecodeMarksRenderableSubChunks() {
    auto source = std::make_shared<Chunk>(0, 0);
    for (int y = 48; y < 56; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                source->setBlockFast(x, y, z, BlockIds::STONE);
            }
        }
    }
    source->seedInitialLightMap();

    net::ChunkDataMessage message;
    message.chunkX = 0;
    message.chunkZ = 0;
    message.revision = 1;
    message.chunk = source;

    const std::vector<uint8_t> encoded = net::PacketCodec::encodeChunkData(message);

    net::ChunkDataMessage decoded;
    if (!net::PacketCodec::decodeChunkData(encoded.data(), encoded.size(), decoded) || !decoded.chunk) {
        std::fprintf(stderr, "[FAIL] ChunkData decode failed\n");
        std::abort();
    }

    const int scy = Chunk::toSubChunkIndex(48);
    const SubChunk* subChunk = decoded.chunk->getSubChunk(scy);
    if (subChunk == nullptr || subChunk->getType() == SubChunkType::Air || !decoded.chunk->isSubChunkDirty(scy)) {
        std::fprintf(stderr,
                     "[FAIL] Decoded chunk subchunk not renderable dirty scy=%d hasSub=%d type=%d dirty=%d\n",
                     scy,
                     subChunk != nullptr ? 1 : 0,
                     subChunk != nullptr ? static_cast<int>(subChunk->getType()) : -1,
                     decoded.chunk->isSubChunkDirty(scy) ? 1 : 0);
        std::abort();
    }

    const SubChunkMeshingSnapshotPtr snapshot =
        ChunkMesher::captureSubChunkSnapshot(*decoded.chunk, scy, nullptr);
    if (!snapshot) {
        std::fprintf(stderr, "[FAIL] Decoded chunk meshing snapshot was null\n");
        std::abort();
    }
    const ChunkMeshData meshData = ChunkMesher::buildSubChunkMeshData(*snapshot);
    const size_t totalVertices =
        meshData.opaqueVertices.size() +
        meshData.cutoutVertices.size() +
        meshData.cutoutDistanceVertices.size() +
        meshData.transparentVertices.size() +
        meshData.waterVertices.size();
    if (totalVertices == 0) {
        std::fprintf(stderr, "[FAIL] Decoded chunk produced an empty terrain mesh\n");
        std::abort();
    }

    std::printf("[PASS] testChunkDataDecodeMarksRenderableSubChunks\n");
}

static void testBlockUpdateCodecKeepsVariableLightPatch() {
    net::BlockUpdateBatchMessage message;
    net::BlockUpdateEntry entry;
    entry.x = 1;
    entry.y = 64;
    entry.z = -2;
    entry.blockId = BlockIds::TORCH;
    entry.packedLightPatch.resize(5 * 5 * 5);
    for (size_t i = 0; i < entry.packedLightPatch.size(); ++i) {
        entry.packedLightPatch[i] = static_cast<uint8_t>(i & 0x0F);
    }
    message.updates.push_back(entry);

    const std::vector<uint8_t> encoded = net::PacketCodec::encodeBlockUpdateBatch(message);
    net::BlockUpdateBatchMessage decoded;
    if (!net::PacketCodec::decodeBlockUpdateBatch(encoded.data(), encoded.size(), decoded)) {
        std::fprintf(stderr, "[FAIL] BlockUpdateBatch decode failed\n");
        std::abort();
    }
    if (decoded.updates.size() != 1 ||
        decoded.updates[0].packedLightPatch.size() != entry.packedLightPatch.size() ||
        decoded.updates[0].packedLightPatch[124] != entry.packedLightPatch[124]) {
        std::fprintf(stderr, "[FAIL] BlockUpdateBatch variable light patch was not preserved\n");
        std::abort();
    }

    std::printf("[PASS] testBlockUpdateCodecKeepsVariableLightPatch\n");
}

static void testServerEmitsLightPatchAfterTorchPlacement() {
    ThreadPool threadPool(2);
    threadPool.start();

    server::GameServer server;
    server.init(1234, &threadPool, 2);

    auto clientTransport = std::make_unique<ManualTransport>();
    ManualTransport* clientPtr = clientTransport.get();

    net::Packet hello;
    hello.type = net::MessageType::ClientHello;
    hello.inProcessPayload = net::ClientHello{};
    clientPtr->pushIncoming(std::move(hello));

    server.acceptClient(std::move(clientTransport), 1);
    const glm::vec3 spawn = server.getSpawnPosition();
    const glm::ivec3 placeBlock(0, Chunk::SIZE_Y - 8, 0);
    for (int tick = 0;
         tick < 240 && !server.world().isChunkLoadedForBlock(placeBlock.x, placeBlock.y, placeBlock.z);
         ++tick) {
        server.tick(1.0f / 20.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(server.world().isChunkLoadedForBlock(placeBlock.x, placeBlock.y, placeBlock.z));
    assert(server.world().getBlock(placeBlock.x, placeBlock.y, placeBlock.z) == BlockIds::AIR);

    while (!clientPtr->sent.empty()) {
        clientPtr->sent.pop();
    }

    net::Packet actionPacket;
    actionPacket.type = net::MessageType::ClientBlockAction;
    net::ClientBlockAction action;
    action.sequence = 1;
    action.action = net::ClientBlockActionType::Place;
    action.placeBlock = placeBlock;
    action.playerPosition = glm::vec3(placeBlock) + glm::vec3(0.5f);
    action.blockState = BlockIds::TORCH;
    actionPacket.inProcessPayload = action;
    clientPtr->pushIncoming(std::move(actionPacket));

    bool sawTorchBlockUpdate = false;
    bool sawTorchLightPatch = false;
    for (int i = 0; i < 160 && !sawTorchLightPatch; ++i) {
        server.tick(1.0f / 20.0f);
        while (!clientPtr->sent.empty()) {
            net::Packet packet = std::move(clientPtr->sent.front());
            clientPtr->sent.pop();
            if (packet.type != net::MessageType::BlockUpdateBatch || !packet.inProcessPayload.has_value()) {
                continue;
            }
            const auto& batch = std::any_cast<const net::BlockUpdateBatchMessage&>(packet.inProcessPayload);
            for (const auto& update : batch.updates) {
                if (update.x == placeBlock.x &&
                    update.y == placeBlock.y &&
                    update.z == placeBlock.z &&
                    update.blockId == BlockIds::TORCH) {
                    sawTorchBlockUpdate = true;
                }
                for (const uint8_t packed : update.packedLightPatch) {
                    if ((packed & 0x0F) >= 12) {
                        sawTorchLightPatch = true;
                        break;
                    }
                }
            }
        }
    }

    assert(sawTorchBlockUpdate);
    assert(sawTorchLightPatch);
    server.world().setThreadPool(nullptr);
    threadPool.shutdown();
    std::printf("[PASS] testServerEmitsLightPatchAfterTorchPlacement\n");
}

static void testENetChunkStreamingToClient() {
    if (!net::ENetTransport::initialize()) {
        std::fprintf(stderr, "[FAIL] ENet initialize failed\n");
        std::abort();
    }

    ServerHarness harness;

    auto serverTransport = std::make_unique<net::ENetTransport>();
    if (!serverTransport->listen(0, 1, 4)) {
        std::fprintf(stderr, "[FAIL] ENet listen failed\n");
        std::abort();
    }
    const uint16_t port = serverTransport->getLocalPort();
    if (port == 0) {
        std::fprintf(stderr, "[FAIL] ENet local port was not assigned\n");
        std::abort();
    }
    harness.server.acceptClient(std::move(serverTransport), 1);

    auto clientTransport = std::make_unique<net::ENetTransport>();
    std::atomic<bool> connectDone{false};
    std::atomic<bool> connectOk{false};
    std::thread connectThread([&]() {
        connectOk.store(clientTransport->connect("127.0.0.1", port, 4, 2000));
        connectDone.store(true);
    });
    for (int i = 0; i < 250 && !connectDone.load(); ++i) {
        harness.server.tick(1.0f / 20.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    connectThread.join();
    if (!connectOk.load()) {
        std::fprintf(stderr, "[FAIL] ENet connect failed on port %u\n", port);
        std::abort();
    }

    client::GameClient client;
    client.connect(std::move(clientTransport));
    client.sendViewConfig(8);

    for (int i = 0; i < 500 && client.clientWorld().loadedChunkCount() == 0; ++i) {
        harness.server.tick(1.0f / 20.0f);
        client.receiveMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (client.getClientId() != 1 || client.clientWorld().loadedChunkCount() == 0) {
        std::fprintf(stderr,
                     "[FAIL] ENet streaming failed id=%u loaded=%zu active=%zu tick=%u\n",
                     client.getClientId(),
                     client.clientWorld().loadedChunkCount(),
                     harness.server.world().getActiveChunks().size(),
                     harness.server.currentTick());
        std::abort();
    }
    std::printf("[PASS] testENetChunkStreamingToClient\n");

    net::ENetTransport::deinitialize();
}

static void testENetChunkStreamingAfterPreconnectTicks() {
    if (!net::ENetTransport::initialize()) {
        std::fprintf(stderr, "[FAIL] ENet initialize failed\n");
        std::abort();
    }

    ServerHarness harness;

    auto serverTransport = std::make_unique<net::ENetTransport>();
    if (!serverTransport->listen(0, 1, 4)) {
        std::fprintf(stderr, "[FAIL] ENet listen failed\n");
        std::abort();
    }
    const uint16_t port = serverTransport->getLocalPort();
    if (port == 0) {
        std::fprintf(stderr, "[FAIL] ENet local port was not assigned\n");
        std::abort();
    }
    harness.server.acceptClient(std::move(serverTransport), 1);

    for (int i = 0; i < 240; ++i) {
        harness.server.tick(1.0f / 20.0f);
    }
    if (harness.server.world().getActiveChunks().empty()) {
        std::fprintf(stderr, "[FAIL] Server did not load chunks before connect\n");
        std::abort();
    }

    auto clientTransport = std::make_unique<net::ENetTransport>();
    std::atomic<bool> connectDone{false};
    std::atomic<bool> connectOk{false};
    std::thread connectThread([&]() {
        connectOk.store(clientTransport->connect("127.0.0.1", port, 4, 2000));
        connectDone.store(true);
    });
    for (int i = 0; i < 250 && !connectDone.load(); ++i) {
        harness.server.tick(1.0f / 20.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    connectThread.join();
    if (!connectOk.load()) {
        std::fprintf(stderr, "[FAIL] ENet connect failed on port %u\n", port);
        std::abort();
    }

    client::GameClient client;
    client.connect(std::move(clientTransport));
    client.sendViewConfig(8);

    for (int i = 0; i < 500 && client.clientWorld().loadedChunkCount() == 0; ++i) {
        harness.server.tick(1.0f / 20.0f);
        client.receiveMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (client.clientWorld().loadedChunkCount() == 0) {
        std::fprintf(stderr,
                     "[FAIL] ENet preconnect streaming failed loaded=%zu active=%zu tick=%u\n",
                     client.clientWorld().loadedChunkCount(),
                     harness.server.world().getActiveChunks().size(),
                     harness.server.currentTick());
        std::abort();
    }
    std::printf("[PASS] testENetChunkStreamingAfterPreconnectTicks\n");

    net::ENetTransport::deinitialize();
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    BlockRegistry::init(nullptr);
    ItemRegistry::init();

    testServerInit();
    testAcceptClient();
    testClientServerHandshake();
    testServerTick();
    testChunkStreamingToClient();
    testInputRoundTrip();
    testClientBlockActionRoundTrip();
    testChatBroadcastRoundTrip();
    testAdminCommandUpdatesWorldState();
    testNonAdminCommandDenied();
    testSummonZombieSpawnsNetworkMob();
    testSummonZombieUsesOwnedServerEcs();
    testOwnedServerZombiePursuesPlayer();
    testOwnedServerPlayerMeleeKillsZombieAndDropsItem();
    testPersistentZombieRestoresFromSave();
    testOwnedServerEcsRestoresPersistentZombie();
    testChatCommandCodecRoundTrip();
    testServerTickBreaksUnsupportedPlant();
    testDisconnectedPlayerDespawnsForOtherClients();
    testChunkDataDecodeMarksRenderableSubChunks();
    testBlockUpdateCodecKeepsVariableLightPatch();
    testServerEmitsLightPatchAfterTorchPlacement();
    testENetChunkStreamingToClient();
    testENetChunkStreamingAfterPreconnectTicks();
    std::printf("\nAll GameServer integration tests passed!\n");
    return 0;
}
