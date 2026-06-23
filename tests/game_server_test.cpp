#include "server/GameServer.h"
#include "client/GameClient.h"
#include "ecs/GameplayRegistry.h"
#include "ecs/components/Components.h"
#include "ecs/components/NetworkComponents.h"
#include "ecs/entity/EntityDefinitionRegistry.h"
#include "ecs/entity/EntityFactory.h"
#include "ecs/entity/MobModelFactory.h"
#include "ecs/systems/world/BlockSupportSystem.h"
#include "game/inventory/ChestInventoryStore.h"
#include "net/InProcessTransport.h"
#include "net/ENetTransport.h"
#include "net/PacketCodec.h"
#include "item/Item.h"
#include "world/World.h"
#include "world/DayNightSystem.h"
#include "world/WeatherSystem.h"
#include "world/block/Block.h"
#include "world/block/BlockStateRegistry.h"
#include "renderer/mesh/ChunkMesher.h"
#include "thread/ThreadPool.h"
#include <cassert>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <queue>
#include <thread>
#include <vector>

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

static float distanceSq(const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 d = a - b;
    return glm::dot(d, d);
}

static void require(const bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::abort();
    }
}

static uint32_t inventoryItemCount(const net::InventorySnapshotMessage& snapshot, const ItemID itemId) {
    uint32_t total = 0;
    const auto encodedItemId = static_cast<uint16_t>(itemId);
    for (const auto& slot : snapshot.slots) {
        if (slot.itemId == encodedItemId) {
            total += slot.stackCount;
        }
    }
    return total;
}

static uint32_t ecsDroppedItemCount(ecs::GameplayRegistry& registry, const ItemID itemId) {
    uint32_t total = 0;
    auto view = registry.view<ecs::DropItemTag, ecs::ItemComponent>();
    for (const entt::entity entity : view) {
        const auto& item = view.get<ecs::ItemComponent>(entity);
        if (item.itemId == itemId) {
            total += item.stackCount;
        }
    }
    return total;
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

static void testClientViewConfigExpandsServerStreamingRadius() {
    ServerHarness harness(8);

    client::GameClient client;

    auto [clientTransport, serverTransport] = net::InProcessTransport::createPair();
    harness.server.acceptClient(std::move(serverTransport), 1);
    client.connect(std::move(clientTransport));
    client.sendViewConfig(16);

    harness.server.tick(1.0f / 20.0f);
    client.receiveMessages();

    require(harness.server.world().getRenderDistance() == 16,
            "client view config should expand server world render distance");
    std::printf("[PASS] testClientViewConfigExpandsServerStreamingRadius\n");
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

static void testClientAppliesPlayerHealthSnapshot() {
    client::GameClient client;
    ecs::GameplayRegistry registry;
    auto& raw = registry.registry();
    const entt::entity player = raw.create();
    raw.emplace<ecs::LocalPlayerTag>(player);
    raw.emplace<ecs::TransformComponent>(player, glm::vec3(5.0f, 6.0f, 7.0f), 1.62f);
    raw.emplace<ecs::VelocityComponent>(player, glm::vec3(1.0f, 0.0f, 0.0f));
    auto& body = raw.emplace<ecs::PhysicsBodyComponent>(player);
    body.body.position = glm::vec3(5.0f, 6.0f, 7.0f);
    body.body.velocity = glm::vec3(1.0f, 0.0f, 0.0f);
    raw.emplace<ecs::HealthComponent>(player);
    raw.emplace<ecs::HurtEffectComponent>(player);
    client.initEntityStore(registry, nullptr);

    auto transport = std::make_unique<ManualTransport>();
    ManualTransport* transportPtr = transport.get();
    client.connect(std::move(transport));

    net::Packet snapshotPacket;
    snapshotPacket.type = net::MessageType::ServerSnapshot;
    net::ServerSnapshot snapshot;
    snapshot.serverTick = 11;
    snapshot.ackInputSequence = 4;
    snapshot.playerHealth = 13;
    snapshot.playerMaxHealth = 20;
    snapshot.playerHurt = true;
    snapshotPacket.inProcessPayload = snapshot;
    transportPtr->pushIncoming(std::move(snapshotPacket));

    client.receiveMessages();

    require(raw.get<ecs::HealthComponent>(player).current == 13, "client should apply server player health");
    require(raw.get<ecs::HealthComponent>(player).max == 20, "client should apply server max health");
    require(raw.get<ecs::HurtEffectComponent>(player).classicHurtEffectPending, "client should apply server hurt event");
    require(client.lastSnapshot().playerHealth == 13, "client should retain health in last snapshot");

    raw.get<ecs::HurtEffectComponent>(player).classicHurtEffectPending = false;
    raw.get<ecs::HurtEffectComponent>(player).flashSecondsRemaining = 0.0f;

    net::Packet duplicateHurtPacket;
    duplicateHurtPacket.type = net::MessageType::ServerSnapshot;
    duplicateHurtPacket.inProcessPayload = snapshot;
    transportPtr->pushIncoming(std::move(duplicateHurtPacket));

    client.receiveMessages();

    require(!raw.get<ecs::HurtEffectComponent>(player).classicHurtEffectPending,
            "duplicate latched player hurt snapshot should not retrigger pending hurt effect");
    require(raw.get<ecs::HurtEffectComponent>(player).flashSecondsRemaining == 0.0f,
            "duplicate latched player hurt snapshot should not refresh visible hurt flash");

    net::Packet lowerHealthPacket;
    lowerHealthPacket.type = net::MessageType::ServerSnapshot;
    snapshot.serverTick = 12;
    snapshot.playerHealth = 10;
    snapshot.playerHurt = false;
    lowerHealthPacket.inProcessPayload = snapshot;
    transportPtr->pushIncoming(std::move(lowerHealthPacket));

    client.receiveMessages();

    require(raw.get<ecs::HealthComponent>(player).current == 10, "client should apply lowered server health");
    require(raw.get<ecs::HurtEffectComponent>(player).classicHurtEffectPending,
            "client should infer hurt event from lowered health");
    require(!client.isPlayerDead(), "client should not mark player dead while health remains above zero");

    net::Packet deathPacket;
    deathPacket.type = net::MessageType::ServerSnapshot;
    snapshot.serverTick = 13;
    snapshot.playerHealth = 0;
    snapshot.playerDead = true;
    snapshot.playerRespawned = false;
    deathPacket.inProcessPayload = snapshot;
    transportPtr->pushIncoming(std::move(deathPacket));

    client.receiveMessages();

    require(raw.get<ecs::HealthComponent>(player).current == 0, "client should apply dead player health");
    require(client.isPlayerDead(), "client should retain dead state from server snapshot");

    net::Packet respawnPacket;
    respawnPacket.type = net::MessageType::ServerSnapshot;
    snapshot.serverTick = 14;
    snapshot.authoritativePosition = glm::vec3(2.0f, 64.0f, -3.0f);
    snapshot.authoritativeVelocity = glm::vec3(0.0f);
    snapshot.playerHealth = 20;
    snapshot.playerRespawned = true;
    snapshot.playerDead = false;
    respawnPacket.inProcessPayload = snapshot;
    transportPtr->pushIncoming(std::move(respawnPacket));

    client.receiveMessages();

    require(raw.get<ecs::HealthComponent>(player).current == 20, "client should apply respawned health");
    require(!client.isPlayerDead(), "client should clear dead state after respawn snapshot");
    require(raw.get<ecs::TransformComponent>(player).position == snapshot.authoritativePosition,
            "client should move local player to respawn position");
    require(raw.get<ecs::VelocityComponent>(player).velocity == snapshot.authoritativeVelocity,
            "client should clear local velocity on respawn");
    require(raw.get<ecs::PhysicsBodyComponent>(player).body.position == snapshot.authoritativePosition,
            "client should move local physics body to respawn position");
    require(raw.get<ecs::PhysicsBodyComponent>(player).body.velocity == snapshot.authoritativeVelocity,
            "client should clear local physics body velocity on respawn");
    std::printf("[PASS] testClientAppliesPlayerHealthSnapshot\n");
}

static void testClientAppliesInventorySnapshot() {
    client::GameClient client;
    ecs::GameplayRegistry registry;
    auto& raw = registry.registry();
    const entt::entity player = raw.create();
    raw.emplace<ecs::LocalPlayerTag>(player);
    raw.emplace<ecs::InventoryComponent>(player);
    auto& inventoryData = raw.emplace<ecs::InventoryDataComponent>(player);
    inventoryData.inventory.initializeDefaultLoadout();
    client.initEntityStore(registry, nullptr);

    auto transport = std::make_unique<ManualTransport>();
    ManualTransport* transportPtr = transport.get();
    client.connect(std::move(transport));

    net::Packet inventoryPacket;
    inventoryPacket.type = net::MessageType::InventorySnapshot;
    net::InventorySnapshotMessage snapshot;
    snapshot.selectedHotbarSlot = 2;
    snapshot.slots.resize(Inventory::INVENTORY_SIZE);
    snapshot.slots[0].itemId = static_cast<uint16_t>(ItemIds::COAL);
    snapshot.slots[0].stackCount = 7;
    inventoryPacket.inProcessPayload = snapshot;
    transportPtr->pushIncoming(std::move(inventoryPacket));

    client.receiveMessages();

    require(raw.get<ecs::InventoryComponent>(player).selectedHotbarSlot == 2,
            "client should apply authoritative selected hotbar slot");
    require(raw.get<ecs::InventoryDataComponent>(player).inventory.getSelectedSlot() == 2,
            "client inventory data should apply selected hotbar slot");
    require(raw.get<ecs::InventoryDataComponent>(player).inventory.getSlotStack(0).itemId == ItemIds::COAL,
            "client should apply inventory snapshot item id");
    require(raw.get<ecs::InventoryDataComponent>(player).inventory.getSlotStack(0).count == 7,
            "client should apply inventory snapshot stack count");
    require(raw.get<ecs::InventoryDataComponent>(player).inventory.getSlotStack(1).isEmpty(),
            "client should clear slots omitted by authoritative snapshot content");

    net::Packet clearPacket;
    clearPacket.type = net::MessageType::InventorySnapshot;
    net::InventorySnapshotMessage clearSnapshot;
    clearSnapshot.selectedHotbarSlot = 0;
    clearSnapshot.slots.resize(Inventory::INVENTORY_SIZE);
    clearPacket.inProcessPayload = clearSnapshot;
    transportPtr->pushIncoming(std::move(clearPacket));

    client.receiveMessages();

    require(raw.get<ecs::InventoryDataComponent>(player).inventory.getSlotStack(0).isEmpty(),
            "client should clear an item when authoritative snapshot slot is empty");
    std::printf("[PASS] testClientAppliesInventorySnapshot\n");
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

static void testGiveCommandAddsRuntimeBlockItem() {
    ServerHarness harness;

    const BlockID cauldronBlock = BlockRegistry::findByName("cauldron");
    require(cauldronBlock != BlockIds::AIR, "cauldron should be registered from block config");
    const ItemID cauldronItem = ItemRegistry::fromBlock(cauldronBlock);
    require(cauldronItem != ItemIds::AIR, "cauldron should have a runtime block item");
    const BlockID anvilBlock = BlockRegistry::findByName("anvil");
    require(anvilBlock != BlockIds::AIR, "anvil should be registered from block config");
    const ItemID anvilItem = ItemRegistry::fromBlock(anvilBlock);
    require(anvilItem != ItemIds::AIR, "anvil should have a runtime block item");
    const BlockID oakFenceBlock = BlockRegistry::findByName("oak_fence");
    require(oakFenceBlock != BlockIds::AIR, "oak_fence should be registered from block config");
    const ItemID oakFenceItem = ItemRegistry::fromBlock(oakFenceBlock);
    require(oakFenceItem != ItemIds::AIR, "oak_fence should have a runtime block item");
    const BlockID cobblestoneWallBlock = BlockRegistry::findByName("cobblestone_wall");
    require(cobblestoneWallBlock != BlockIds::AIR, "cobblestone_wall should be registered from block config");
    const ItemID cobblestoneWallItem = ItemRegistry::fromBlock(cobblestoneWallBlock);
    require(cobblestoneWallItem != ItemIds::AIR, "cobblestone_wall should have a runtime block item");

    auto clientTransport = std::make_unique<ManualTransport>();
    ManualTransport* clientPtr = clientTransport.get();

    net::Packet hello;
    hello.type = net::MessageType::ClientHello;
    hello.inProcessPayload = net::ClientHello{};
    clientPtr->pushIncoming(std::move(hello));

    harness.server.acceptClient(std::move(clientTransport), 1);
    harness.server.tick(1.0f / 20.0f);

    net::InventorySnapshotMessage inventoryBeforeGive;
    bool sawInventoryBeforeGive = false;
    while (!clientPtr->sent.empty()) {
        net::Packet packet = std::move(clientPtr->sent.front());
        clientPtr->sent.pop();
        if (packet.type == net::MessageType::InventorySnapshot && packet.inProcessPayload.has_value()) {
            inventoryBeforeGive = std::any_cast<const net::InventorySnapshotMessage&>(packet.inProcessPayload);
            sawInventoryBeforeGive = true;
        }
    }
    require(sawInventoryBeforeGive, "give test should receive the initial inventory snapshot");
    const uint32_t beforeCauldronCount = inventoryItemCount(inventoryBeforeGive, cauldronItem);
    const uint32_t beforeAnvilCount = inventoryItemCount(inventoryBeforeGive, anvilItem);
    const uint32_t beforeOakFenceCount = inventoryItemCount(inventoryBeforeGive, oakFenceItem);
    const uint32_t beforeCobblestoneWallCount = inventoryItemCount(inventoryBeforeGive, cobblestoneWallItem);

    net::Packet commandPacket;
    commandPacket.type = net::MessageType::ClientCommandRequest;
    net::ClientCommandRequest command;
    command.sequence = 32;
    command.command = "/give cauldron 5";
    commandPacket.inProcessPayload = command;
    clientPtr->pushIncoming(std::move(commandPacket));

    harness.server.tick(1.0f / 20.0f);

    bool sawCommandResult = false;
    bool sawInventoryAfterGive = false;
    while (!clientPtr->sent.empty()) {
        net::Packet packet = std::move(clientPtr->sent.front());
        clientPtr->sent.pop();
        if (packet.type == net::MessageType::CommandResult && packet.inProcessPayload.has_value()) {
            const auto& result = std::any_cast<const net::CommandResultMessage&>(packet.inProcessPayload);
            if (result.sequence == 32 && result.success &&
                result.message.find("minecraft:cauldron") != std::string::npos) {
                sawCommandResult = true;
            }
        }
        if (packet.type == net::MessageType::InventorySnapshot && packet.inProcessPayload.has_value()) {
            const auto& inventory = std::any_cast<const net::InventorySnapshotMessage&>(packet.inProcessPayload);
            const uint32_t afterCount = inventoryItemCount(inventory, cauldronItem);
            if (afterCount == beforeCauldronCount + 5) {
                sawInventoryAfterGive = true;
            }
        }
    }

    require(sawCommandResult, "give command should return a successful command result");
    require(sawInventoryAfterGive, "give command should sync the added runtime block item");

    net::Packet anvilCommandPacket;
    anvilCommandPacket.type = net::MessageType::ClientCommandRequest;
    net::ClientCommandRequest anvilCommand;
    anvilCommand.sequence = 34;
    anvilCommand.command = "/give anvil 2";
    anvilCommandPacket.inProcessPayload = anvilCommand;
    clientPtr->pushIncoming(std::move(anvilCommandPacket));

    harness.server.tick(1.0f / 20.0f);

    bool sawAnvilCommandResult = false;
    bool sawAnvilInventoryAfterGive = false;
    while (!clientPtr->sent.empty()) {
        net::Packet packet = std::move(clientPtr->sent.front());
        clientPtr->sent.pop();
        if (packet.type == net::MessageType::CommandResult && packet.inProcessPayload.has_value()) {
            const auto& result = std::any_cast<const net::CommandResultMessage&>(packet.inProcessPayload);
            if (result.sequence == 34 && result.success &&
                result.message.find("minecraft:anvil") != std::string::npos) {
                sawAnvilCommandResult = true;
            }
        }
        if (packet.type == net::MessageType::InventorySnapshot && packet.inProcessPayload.has_value()) {
            const auto& inventory = std::any_cast<const net::InventorySnapshotMessage&>(packet.inProcessPayload);
            const uint32_t afterCount = inventoryItemCount(inventory, anvilItem);
            if (afterCount == beforeAnvilCount + 2) {
                sawAnvilInventoryAfterGive = true;
            }
        }
    }

    require(sawAnvilCommandResult, "give command should return a successful anvil command result");
    require(sawAnvilInventoryAfterGive, "give command should sync the added anvil runtime block item");

    net::Packet fenceCommandPacket;
    fenceCommandPacket.type = net::MessageType::ClientCommandRequest;
    net::ClientCommandRequest fenceCommand;
    fenceCommand.sequence = 35;
    fenceCommand.command = "/give oak_fence 4";
    fenceCommandPacket.inProcessPayload = fenceCommand;
    clientPtr->pushIncoming(std::move(fenceCommandPacket));

    harness.server.tick(1.0f / 20.0f);

    bool sawFenceCommandResult = false;
    bool sawFenceInventoryAfterGive = false;
    while (!clientPtr->sent.empty()) {
        net::Packet packet = std::move(clientPtr->sent.front());
        clientPtr->sent.pop();
        if (packet.type == net::MessageType::CommandResult && packet.inProcessPayload.has_value()) {
            const auto& result = std::any_cast<const net::CommandResultMessage&>(packet.inProcessPayload);
            if (result.sequence == 35 && result.success &&
                result.message.find("minecraft:oak_fence") != std::string::npos) {
                sawFenceCommandResult = true;
            }
        }
        if (packet.type == net::MessageType::InventorySnapshot && packet.inProcessPayload.has_value()) {
            const auto& inventory = std::any_cast<const net::InventorySnapshotMessage&>(packet.inProcessPayload);
            const uint32_t afterCount = inventoryItemCount(inventory, oakFenceItem);
            if (afterCount == beforeOakFenceCount + 4) {
                sawFenceInventoryAfterGive = true;
            }
        }
    }

    require(sawFenceCommandResult, "give command should return a successful oak fence command result");
    require(sawFenceInventoryAfterGive, "give command should sync the added oak fence runtime block item");

    net::Packet wallCommandPacket;
    wallCommandPacket.type = net::MessageType::ClientCommandRequest;
    net::ClientCommandRequest wallCommand;
    wallCommand.sequence = 36;
    wallCommand.command = "/give cobblestone_wall 3";
    wallCommandPacket.inProcessPayload = wallCommand;
    clientPtr->pushIncoming(std::move(wallCommandPacket));

    harness.server.tick(1.0f / 20.0f);

    bool sawWallCommandResult = false;
    bool sawWallInventoryAfterGive = false;
    while (!clientPtr->sent.empty()) {
        net::Packet packet = std::move(clientPtr->sent.front());
        clientPtr->sent.pop();
        if (packet.type == net::MessageType::CommandResult && packet.inProcessPayload.has_value()) {
            const auto& result = std::any_cast<const net::CommandResultMessage&>(packet.inProcessPayload);
            if (result.sequence == 36 && result.success &&
                result.message.find("minecraft:cobblestone_wall") != std::string::npos) {
                sawWallCommandResult = true;
            }
        }
        if (packet.type == net::MessageType::InventorySnapshot && packet.inProcessPayload.has_value()) {
            const auto& inventory = std::any_cast<const net::InventorySnapshotMessage&>(packet.inProcessPayload);
            const uint32_t afterCount = inventoryItemCount(inventory, cobblestoneWallItem);
            if (afterCount == beforeCobblestoneWallCount + 3) {
                sawWallInventoryAfterGive = true;
            }
        }
    }

    require(sawWallCommandResult, "give command should return a successful cobblestone wall command result");
    require(sawWallInventoryAfterGive, "give command should sync the added cobblestone wall runtime block item");
    std::printf("[PASS] testGiveCommandAddsRuntimeBlockItem\n");
}

static void testGiveCommandRejectsUnknownItem() {
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
    command.sequence = 33;
    command.command = "/give definitely_missing_item 1";
    commandPacket.inProcessPayload = command;
    clientPtr->pushIncoming(std::move(commandPacket));

    harness.server.tick(1.0f / 20.0f);

    bool sawCommandResult = false;
    bool sawInventorySnapshot = false;
    while (!clientPtr->sent.empty()) {
        net::Packet packet = std::move(clientPtr->sent.front());
        clientPtr->sent.pop();
        if (packet.type == net::MessageType::CommandResult && packet.inProcessPayload.has_value()) {
            const auto& result = std::any_cast<const net::CommandResultMessage&>(packet.inProcessPayload);
            if (result.sequence == 33 && !result.success &&
                result.message.find("Unknown item") != std::string::npos) {
                sawCommandResult = true;
            }
        }
        if (packet.type == net::MessageType::InventorySnapshot) {
            sawInventorySnapshot = true;
        }
    }

    require(sawCommandResult, "give command should reject an unknown item");
    require(!sawInventorySnapshot, "failed give command should not sync an inventory change");
    std::printf("[PASS] testGiveCommandRejectsUnknownItem\n");
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
                require(spawn.entityId == "minecraft:zombie",
                        "external registry summon should include mob entity id in spawn packet");
            }
        }
    }

    require(sawCommandResult, "external registry summon should return a successful command result");
    require(sawMobSpawn, "external registry summon should send a mob spawn");
    require(spawnedNetId != 0, "external registry summon should assign a mob net id");

    auto view = registry.registry().view<ecs::MobTag,
                                        ecs::HealthComponent,
                                        ecs::EntityTypeComponent,
                                        ecs::NetworkSyncTag,
                                        ecs::EntityNetIdComponent>();
    require(view.begin() != view.end(), "external registry summon should create a networked mob");
    const entt::entity zombie = *view.begin();
    require(registry.registry().get<ecs::EntityNetIdComponent>(zombie).netId == spawnedNetId,
            "external registry mob net id should match spawn packet");
    require(registry.registry().get<ecs::EntityTypeComponent>(zombie).entityId == "minecraft:zombie",
            "external registry mob should keep configured entity id");
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
    command.command = "/summon minecraft:zombie";
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
                require(spawn.entityId == "minecraft:zombie",
                        "owned ECS summon should include mob entity id in spawn packet");
                sawMobSpawn = true;
            }
        }
    }

    assert(sawCommandResult);
    require(sawMobSpawn, "owned ECS pursue test should receive mob spawn");
    std::printf("[PASS] testSummonZombieUsesOwnedServerEcs\n");
}

static void testSummonHerobrineUsesOwnedServerEcs() {
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
    command.sequence = 21;
    command.command = "/summon herobrine";
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
            if (result.sequence == 21 &&
                result.success &&
                result.message.find("Summoned herobrine") != std::string::npos) {
                sawCommandResult = true;
            }
        }
        if (packet.type == net::MessageType::EntitySpawn && packet.inProcessPayload.has_value()) {
            const auto& spawn = std::any_cast<const net::EntitySpawnMessage&>(packet.inProcessPayload);
            if (spawn.kind == net::EntityKind::Mob && spawn.netId != 0) {
                require(spawn.entityId == "minecraft:herobrine",
                        "owned ECS herobrine summon should include configured entity id in spawn packet");
                sawMobSpawn = true;
            }
        }
    }

    require(sawCommandResult, "owned ECS herobrine summon should return a successful command result");
    require(sawMobSpawn, "owned ECS herobrine summon should send a mob spawn");
    std::printf("[PASS] testSummonHerobrineUsesOwnedServerEcs\n");
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

static void testOwnedServerZombieAttackSyncsPlayerHealth() {
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

    auto pushInput = [&](const uint32_t sequence) {
        net::Packet inputPacket;
        inputPacket.type = net::MessageType::ClientInput;
        net::ClientInput input;
        input.sequence = sequence;
        input.playerPosition = playerPosition;
        input.playerVelocity = glm::vec3(0.0f);
        input.yaw = 0.0f;
        input.pitch = 0.0f;
        inputPacket.inProcessPayload = input;
        clientPtr->pushIncoming(std::move(inputPacket));
    };

    pushInput(1);
    harness.server.tick(1.0f / 20.0f);
    while (!clientPtr->sent.empty()) {
        clientPtr->sent.pop();
    }

    net::Packet commandPacket;
    commandPacket.type = net::MessageType::ClientCommandRequest;
    net::ClientCommandRequest command;
    command.sequence = 22;
    command.command = "/summon zombie";
    commandPacket.inProcessPayload = command;
    clientPtr->pushIncoming(std::move(commandPacket));
    harness.server.tick(1.0f / 20.0f);
    while (!clientPtr->sent.empty()) {
        clientPtr->sent.pop();
    }

    bool sawDamagedSnapshot = false;
    bool sawHurtEvent = false;
    for (uint32_t i = 2; i < 90 && !sawDamagedSnapshot; ++i) {
        pushInput(i);
        harness.server.tick(1.0f / 20.0f);

        while (!clientPtr->sent.empty()) {
            net::Packet packet = std::move(clientPtr->sent.front());
            clientPtr->sent.pop();
            if (packet.type != net::MessageType::ServerSnapshot || !packet.inProcessPayload.has_value()) {
                continue;
            }

            const auto& snapshot = std::any_cast<const net::ServerSnapshot&>(packet.inProcessPayload);
            if (snapshot.playerHealth < snapshot.playerMaxHealth) {
                sawDamagedSnapshot = true;
                sawHurtEvent = snapshot.playerHurt;
            }
        }
    }

    require(sawDamagedSnapshot, "zombie attack should lower player health in server snapshot");
    require(sawHurtEvent, "zombie attack should send a player hurt event");
    std::printf("[PASS] testOwnedServerZombieAttackSyncsPlayerHealth\n");
}

static void testOwnedServerPlayerDiesDropsItemsAndRespawnsOnRequest() {
    ServerHarness harness;
    const glm::vec3 spawnPosition = harness.server.getSpawnPosition();
    const glm::vec3 playerPosition = spawnPosition + glm::vec3(0.0f, -1.0f, 0.0f);
    const glm::vec3 spoofedDeadPosition = playerPosition + glm::vec3(80.0f, 0.0f, 80.0f);

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

    auto pushInput = [&](const uint32_t sequence, const glm::vec3& reportedPosition) {
        net::Packet inputPacket;
        inputPacket.type = net::MessageType::ClientInput;
        net::ClientInput input;
        input.sequence = sequence;
        input.playerPosition = reportedPosition;
        input.playerVelocity = glm::vec3(0.0f);
        input.yaw = 0.0f;
        input.pitch = 0.0f;
        inputPacket.inProcessPayload = input;
        clientPtr->pushIncoming(std::move(inputPacket));
    };

    pushInput(1, playerPosition);
    harness.server.tick(1.0f / 20.0f);
    while (!clientPtr->sent.empty()) {
        clientPtr->sent.pop();
    }

    net::Packet commandPacket;
    commandPacket.type = net::MessageType::ClientCommandRequest;
    net::ClientCommandRequest command;
    command.sequence = 31;
    command.command = "/summon zombie";
    commandPacket.inProcessPayload = command;
    clientPtr->pushIncoming(std::move(commandPacket));
    harness.server.tick(1.0f / 20.0f);
    while (!clientPtr->sent.empty()) {
        clientPtr->sent.pop();
    }

    bool sawDeathSnapshot = false;
    bool sawDeadState = false;
    bool sentSpoofedDeadInput = false;
    bool sawDeadInputIgnored = false;
    bool sawUnexpectedAutoRespawn = false;
    bool sawDropSpawn = false;
    bool sawClearedInventorySnapshot = false;
    uint32_t droppedStackTotal = 0;
    glm::vec3 respawnedPosition(0.0f);

    for (uint32_t i = 2; i < 320 &&
         !(sawDeathSnapshot && sawDeadInputIgnored && sawDropSpawn && sawClearedInventorySnapshot); ++i) {
        const bool spoofThisTick = sawDeathSnapshot;
        pushInput(i, spoofThisTick ? spoofedDeadPosition : playerPosition);
        sentSpoofedDeadInput = sentSpoofedDeadInput || spoofThisTick;
        harness.server.tick(1.0f / 20.0f);

        while (!clientPtr->sent.empty()) {
            net::Packet packet = std::move(clientPtr->sent.front());
            clientPtr->sent.pop();
            if (!packet.inProcessPayload.has_value()) {
                continue;
            }

            if (packet.type == net::MessageType::InventorySnapshot) {
                const auto& inventory = std::any_cast<const net::InventorySnapshotMessage&>(packet.inProcessPayload);
                bool hasItems = false;
                for (const auto& slot : inventory.slots) {
                    if (slot.itemId != 0 && slot.stackCount != 0) {
                        hasItems = true;
                        break;
                    }
                }
                if (sawDeathSnapshot && !hasItems && inventory.slots.size() == Inventory::INVENTORY_SIZE) {
                    sawClearedInventorySnapshot = true;
                }
                continue;
            }

            if (packet.type == net::MessageType::EntitySpawn) {
                const auto& spawn = std::any_cast<const net::EntitySpawnMessage&>(packet.inProcessPayload);
                if (spawn.kind == net::EntityKind::Drop && spawn.stackCount > 0) {
                    sawDropSpawn = true;
                    droppedStackTotal += spawn.stackCount;
                }
                continue;
            }

            if (packet.type != net::MessageType::ServerSnapshot) {
                continue;
            }

            const auto& snapshot = std::any_cast<const net::ServerSnapshot&>(packet.inProcessPayload);
            if (snapshot.playerDead) {
                sawDeadState = true;
                require(snapshot.playerHealth == 0, "dead snapshot should report zero health");
            }
            if (snapshot.playerHealth == 0) {
                sawDeathSnapshot = true;
                if (sentSpoofedDeadInput && distanceSq(snapshot.authoritativePosition, spoofedDeadPosition) > 1.0f) {
                    sawDeadInputIgnored = true;
                }
            }

            if (snapshot.playerRespawned) {
                sawUnexpectedAutoRespawn = true;
            }
        }
    }

    require(sawDeathSnapshot, "zombie should be able to reduce player health to zero");
    require(sawDeadState, "server should mark the player as dead in snapshots");
    require(sawDeadInputIgnored, "server should ignore dead player movement before respawn");
    require(sawDropSpawn, "player death should spawn dropped inventory items");
    require(droppedStackTotal > 0, "player death should drop non-empty item stacks");
    require(sawClearedInventorySnapshot, "player death should sync an empty authoritative inventory");
    require(!sawUnexpectedAutoRespawn, "server should not auto-respawn before a respawn request");

    net::Packet respawnRequestPacket;
    respawnRequestPacket.type = net::MessageType::ClientRespawnRequest;
    net::ClientRespawnRequest respawnRequest;
    respawnRequest.sequence = 1;
    respawnRequestPacket.inProcessPayload = respawnRequest;
    clientPtr->pushIncoming(std::move(respawnRequestPacket));

    bool sawRespawnSnapshot = false;
    for (uint32_t i = 0; i < 20 && !sawRespawnSnapshot; ++i) {
        harness.server.tick(1.0f / 20.0f);

        while (!clientPtr->sent.empty()) {
            net::Packet packet = std::move(clientPtr->sent.front());
            clientPtr->sent.pop();
            if (packet.type != net::MessageType::ServerSnapshot || !packet.inProcessPayload.has_value()) {
                continue;
            }

            const auto& snapshot = std::any_cast<const net::ServerSnapshot&>(packet.inProcessPayload);
            if (!snapshot.playerRespawned) {
                continue;
            }

            sawRespawnSnapshot = true;
            respawnedPosition = snapshot.authoritativePosition;
            require(!snapshot.playerDead, "respawn snapshot should clear dead state");
            require(snapshot.playerHealth == snapshot.playerMaxHealth,
                    "respawn snapshot should restore player health");
            require(snapshot.authoritativeVelocity == glm::vec3(0.0f),
                    "respawn snapshot should clear player velocity");
        }
    }

    require(sawRespawnSnapshot, "server should send a player respawn snapshot after request");
    require(distanceSq(respawnedPosition, spawnPosition) < 0.01f,
            "server should respawn player at world spawn");
    std::printf("[PASS] testOwnedServerPlayerDiesDropsItemsAndRespawnsOnRequest\n");
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
    bool sawZombieDeathImpact = false;
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
            if (packet.type == net::MessageType::EntityImpact && packet.inProcessPayload.has_value()) {
                const auto& impact = std::any_cast<const net::EntityImpactMessage&>(packet.inProcessPayload);
                sawZombieDeathImpact = sawZombieDeathImpact ||
                    (impact.netId == zombieNetId &&
                     impact.particleBlockId == BlockIds::ROSE &&
                     impact.particleCount == 28);
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
    require(sawZombieDeathImpact, "server melee zombie death should emit a final death impact");
    require(sawDropSpawn, "server melee zombie death should spawn a drop");
    std::printf("[PASS] testOwnedServerPlayerMeleeKillsZombieAndDropsItem\n");
}

static void testOwnedServerMobSnapshotCarriesHealthAndHurt() {
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

    auto pushInput = [&](const uint32_t sequence, const uint32_t actions) {
        net::Packet inputPacket;
        inputPacket.type = net::MessageType::ClientInput;
        net::ClientInput input;
        input.sequence = sequence;
        input.playerPosition = playerPosition;
        input.playerVelocity = glm::vec3(0.0f);
        input.yaw = 0.0f;
        input.pitch = 0.0f;
        input.actions = actions;
        inputPacket.inProcessPayload = input;
        clientPtr->pushIncoming(std::move(inputPacket));
    };

    pushInput(1, 0);
    harness.server.tick(1.0f / 20.0f);
    while (!clientPtr->sent.empty()) {
        clientPtr->sent.pop();
    }

    net::Packet commandPacket;
    commandPacket.type = net::MessageType::ClientCommandRequest;
    net::ClientCommandRequest command;
    command.sequence = 23;
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
    require(zombieNetId != 0, "mob health snapshot test should receive zombie net id");

    bool sawDamagedHealth = false;
    bool sawHurtBit = false;
    pushInput(2, net::ClientInputActions::Attack);
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
                sawDamagedHealth = entity.maxHealth == 20 && entity.health < entity.maxHealth;
                sawHurtBit = entity.hurt;
            }
        }
    }

    require(sawDamagedHealth, "mob entity snapshot should carry updated zombie health");
    require(sawHurtBit, "mob entity snapshot should carry one-shot hurt flag");

    bool sawClearedHurtBit = false;
    pushInput(3, 0);
    harness.server.tick(1.0f / 20.0f);
    while (!clientPtr->sent.empty()) {
        net::Packet packet = std::move(clientPtr->sent.front());
        clientPtr->sent.pop();
        if (packet.type != net::MessageType::EntitySnapshot || !packet.inProcessPayload.has_value()) {
            continue;
        }
        const auto& snapshot = std::any_cast<const net::EntitySnapshotMessage&>(packet.inProcessPayload);
        for (const auto& entity : snapshot.entities) {
            if (entity.netId == zombieNetId && entity.maxHealth == 20 && !entity.hurt) {
                sawClearedHurtBit = true;
            }
        }
    }

    require(sawClearedHurtBit, "mob hurt flag should clear after being sent once");
    std::printf("[PASS] testOwnedServerMobSnapshotCarriesHealthAndHurt\n");
}

static void testOwnedServerPlayerPicksUpDropDespawnsAndSyncsInventory() {
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

    bool sawInventoryBeforePickup = false;
    net::InventorySnapshotMessage inventoryBeforePickup;
    auto drainPrePickupPackets = [&]() {
        while (!clientPtr->sent.empty()) {
            net::Packet packet = std::move(clientPtr->sent.front());
            clientPtr->sent.pop();
            if (packet.type == net::MessageType::InventorySnapshot && packet.inProcessPayload.has_value()) {
                inventoryBeforePickup =
                    std::any_cast<const net::InventorySnapshotMessage&>(packet.inProcessPayload);
                sawInventoryBeforePickup = true;
            }
        }
    };
    drainPrePickupPackets();

    auto pushInput = [&](const uint32_t sequence, const glm::vec3& position, const uint32_t actions) {
        net::Packet inputPacket;
        inputPacket.type = net::MessageType::ClientInput;
        net::ClientInput input;
        input.sequence = sequence;
        input.playerPosition = position;
        input.playerVelocity = glm::vec3(0.0f);
        input.yaw = 0.0f;
        input.pitch = 0.0f;
        input.actions = actions;
        inputPacket.inProcessPayload = input;
        clientPtr->pushIncoming(std::move(inputPacket));
    };

    uint32_t inputSequence = 1;
    pushInput(inputSequence++, playerPosition, 0);
    harness.server.tick(1.0f / 20.0f);
    drainPrePickupPackets();

    net::Packet commandPacket;
    commandPacket.type = net::MessageType::ClientCommandRequest;
    net::ClientCommandRequest command;
    command.sequence = 24;
    command.command = "/summon zombie";
    commandPacket.inProcessPayload = command;
    clientPtr->pushIncoming(std::move(commandPacket));

    harness.server.tick(1.0f / 20.0f);

    net::EntityNetId zombieNetId = 0;
    while (!clientPtr->sent.empty()) {
        net::Packet packet = std::move(clientPtr->sent.front());
        clientPtr->sent.pop();
        if (packet.type == net::MessageType::InventorySnapshot && packet.inProcessPayload.has_value()) {
            inventoryBeforePickup =
                std::any_cast<const net::InventorySnapshotMessage&>(packet.inProcessPayload);
            sawInventoryBeforePickup = true;
        }
        if (packet.type == net::MessageType::EntitySpawn && packet.inProcessPayload.has_value()) {
            const auto& spawn = std::any_cast<const net::EntitySpawnMessage&>(packet.inProcessPayload);
            if (spawn.kind == net::EntityKind::Mob && spawn.netId != 0) {
                zombieNetId = spawn.netId;
            }
        }
    }
    require(zombieNetId != 0, "pickup test should receive zombie net id");

    net::EntityNetId dropNetId = 0;
    ItemID dropItemId = 0;
    uint32_t dropStackCount = 0;
    glm::vec3 dropPosition(0.0f);

    auto drainCombatPackets = [&]() {
        while (!clientPtr->sent.empty()) {
            net::Packet packet = std::move(clientPtr->sent.front());
            clientPtr->sent.pop();
            if (packet.type == net::MessageType::InventorySnapshot && packet.inProcessPayload.has_value()) {
                inventoryBeforePickup =
                    std::any_cast<const net::InventorySnapshotMessage&>(packet.inProcessPayload);
                sawInventoryBeforePickup = true;
            }
            if (packet.type != net::MessageType::EntitySpawn || !packet.inProcessPayload.has_value()) {
                continue;
            }

            const auto& spawn = std::any_cast<const net::EntitySpawnMessage&>(packet.inProcessPayload);
            if (spawn.kind == net::EntityKind::Drop && spawn.netId != 0 && spawn.stackCount > 0) {
                dropNetId = spawn.netId;
                dropItemId = spawn.itemId;
                dropStackCount = spawn.stackCount;
                dropPosition = spawn.position;
            }
        }
    };

    for (int attempt = 0; attempt < 7 && dropNetId == 0; ++attempt) {
        pushInput(inputSequence++, playerPosition, net::ClientInputActions::Attack);
        harness.server.tick(1.0f / 20.0f);
        drainCombatPackets();

        for (int cooldownTick = 0; cooldownTick < 10 && dropNetId == 0; ++cooldownTick) {
            pushInput(inputSequence++, playerPosition, 0);
            harness.server.tick(1.0f / 20.0f);
            drainCombatPackets();
        }
    }

    require(sawInventoryBeforePickup, "pickup test should have an initial authoritative inventory");
    require(dropNetId != 0, "pickup test should receive dropped item net id");
    require(dropItemId != 0, "pickup test should receive dropped item id");
    require(dropStackCount > 0, "pickup test should receive dropped item stack count");

    const uint32_t beforeCount = inventoryItemCount(inventoryBeforePickup, dropItemId);
    bool sawDropDespawn = false;
    bool sawInventoryPickup = false;

    for (int i = 0; i < 60 && !(sawDropDespawn && sawInventoryPickup); ++i) {
        pushInput(inputSequence++, dropPosition, 0);
        harness.server.tick(1.0f / 20.0f);

        while (!clientPtr->sent.empty()) {
            net::Packet packet = std::move(clientPtr->sent.front());
            clientPtr->sent.pop();
            if (packet.type == net::MessageType::EntityDespawn && packet.inProcessPayload.has_value()) {
                const auto& despawn = std::any_cast<const net::EntityDespawnMessage&>(packet.inProcessPayload);
                sawDropDespawn = sawDropDespawn || despawn.netId == dropNetId;
            }
            if (packet.type == net::MessageType::InventorySnapshot && packet.inProcessPayload.has_value()) {
                const auto& inventory = std::any_cast<const net::InventorySnapshotMessage&>(packet.inProcessPayload);
                const uint32_t afterCount = inventoryItemCount(inventory, dropItemId);
                if (afterCount >= beforeCount + dropStackCount) {
                    sawInventoryPickup = true;
                }
            }
        }
    }

    require(sawDropDespawn, "server pickup should despawn the collected drop");
    require(sawInventoryPickup, "server pickup should sync the collected item into inventory");
    std::printf("[PASS] testOwnedServerPlayerPicksUpDropDespawnsAndSyncsInventory\n");
}

static void testOwnedServerPlayerThrowsAppleProjectileDamagesZombie() {
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

    bool sawInventoryBeforeThrow = false;
    net::InventorySnapshotMessage inventoryBeforeThrow;
    auto drainBeforeThrowPackets = [&]() {
        while (!clientPtr->sent.empty()) {
            net::Packet packet = std::move(clientPtr->sent.front());
            clientPtr->sent.pop();
            if (packet.type == net::MessageType::InventorySnapshot && packet.inProcessPayload.has_value()) {
                inventoryBeforeThrow =
                    std::any_cast<const net::InventorySnapshotMessage&>(packet.inProcessPayload);
                sawInventoryBeforeThrow = true;
            }
        }
    };
    drainBeforeThrowPackets();

    uint32_t inputSequence = 1;
    auto pushInput = [&](const uint32_t actions) {
        net::Packet inputPacket;
        inputPacket.type = net::MessageType::ClientInput;
        net::ClientInput input;
        input.sequence = inputSequence++;
        input.playerPosition = playerPosition;
        input.playerVelocity = glm::vec3(0.0f);
        input.yaw = 0.0f;
        input.pitch = -15.0f;
        input.selectedHotbarSlot = 7;
        input.actions = actions;
        inputPacket.inProcessPayload = input;
        clientPtr->pushIncoming(std::move(inputPacket));
    };

    pushInput(0);
    harness.server.tick(1.0f / 20.0f);
    drainBeforeThrowPackets();
    require(sawInventoryBeforeThrow, "apple projectile test should receive initial inventory");
    const uint32_t applesBeforeThrow = inventoryItemCount(inventoryBeforeThrow, ItemIds::APPLE);
    require(applesBeforeThrow > 0, "default loadout should include throwable apples");

    net::Packet commandPacket;
    commandPacket.type = net::MessageType::ClientCommandRequest;
    net::ClientCommandRequest command;
    command.sequence = 25;
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
    require(zombieNetId != 0, "apple projectile test should receive zombie net id");

    bool sawProjectileSpawn = false;
    bool sawProjectileImpact = false;
    bool sawProjectileDespawn = false;
    bool sawAppleConsumed = false;
    bool sawZombieDespawn = false;
    std::vector<net::EntityNetId> projectileNetIds;

    auto drainCombatPackets = [&]() {
        while (!clientPtr->sent.empty()) {
            net::Packet packet = std::move(clientPtr->sent.front());
            clientPtr->sent.pop();
            if (packet.type == net::MessageType::EntitySpawn && packet.inProcessPayload.has_value()) {
                const auto& spawn = std::any_cast<const net::EntitySpawnMessage&>(packet.inProcessPayload);
                if (spawn.kind == net::EntityKind::Projectile && spawn.itemId == ItemIds::APPLE && spawn.netId != 0) {
                    sawProjectileSpawn = true;
                    projectileNetIds.push_back(spawn.netId);
                }
            }
            if (packet.type == net::MessageType::EntityDespawn && packet.inProcessPayload.has_value()) {
                const auto& despawn = std::any_cast<const net::EntityDespawnMessage&>(packet.inProcessPayload);
                sawZombieDespawn = sawZombieDespawn || despawn.netId == zombieNetId;
                sawProjectileDespawn =
                    sawProjectileDespawn ||
                    std::find(projectileNetIds.begin(), projectileNetIds.end(), despawn.netId) != projectileNetIds.end();
            }
            if (packet.type == net::MessageType::EntityImpact && packet.inProcessPayload.has_value()) {
                const auto& impact = std::any_cast<const net::EntityImpactMessage&>(packet.inProcessPayload);
                sawProjectileImpact =
                    sawProjectileImpact ||
                    (impact.particleBlockId != 0 &&
                     impact.particleCount == 14 &&
                     std::find(projectileNetIds.begin(), projectileNetIds.end(), impact.netId) != projectileNetIds.end());
            }
            if (packet.type == net::MessageType::InventorySnapshot && packet.inProcessPayload.has_value()) {
                const auto& inventory = std::any_cast<const net::InventorySnapshotMessage&>(packet.inProcessPayload);
                sawAppleConsumed = sawAppleConsumed ||
                    inventoryItemCount(inventory, ItemIds::APPLE) < applesBeforeThrow;
            }
        }
    };

    for (int throwAttempt = 0; throwAttempt < 4 && !sawZombieDespawn; ++throwAttempt) {
        pushInput(net::ClientInputActions::UseItem);
        harness.server.tick(1.0f / 20.0f);
        drainCombatPackets();

        for (int cooldownTick = 0; cooldownTick < 14 && !sawZombieDespawn; ++cooldownTick) {
            pushInput(0);
            harness.server.tick(1.0f / 20.0f);
            drainCombatPackets();
        }
    }

    require(sawProjectileSpawn, "using a selected apple should spawn a projectile entity");
    require(sawProjectileImpact, "apple projectile impact should be broadcast before despawn");
    require(sawProjectileDespawn, "apple projectile should despawn after impact");
    require(sawAppleConsumed, "throwing an apple should consume it from authoritative inventory");
    require(sawZombieDespawn, "apple projectiles should damage and eventually kill the zombie");
    std::printf("[PASS] testOwnedServerPlayerThrowsAppleProjectileDamagesZombie\n");
}

static void testCreativeAppleProjectileDoesNotConsumeInventory() {
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

    bool sawInitialInventory = false;
    net::InventorySnapshotMessage initialInventory;
    auto drainInitialPackets = [&]() {
        while (!clientPtr->sent.empty()) {
            net::Packet packet = std::move(clientPtr->sent.front());
            clientPtr->sent.pop();
            if (packet.type == net::MessageType::InventorySnapshot && packet.inProcessPayload.has_value()) {
                initialInventory = std::any_cast<const net::InventorySnapshotMessage&>(packet.inProcessPayload);
                sawInitialInventory = true;
            }
        }
    };
    drainInitialPackets();
    require(sawInitialInventory, "creative projectile test should receive initial inventory");
    const uint32_t applesBeforeThrow = inventoryItemCount(initialInventory, ItemIds::APPLE);
    require(applesBeforeThrow > 0, "creative projectile test needs throwable apples");

    net::Packet commandPacket;
    commandPacket.type = net::MessageType::ClientCommandRequest;
    net::ClientCommandRequest command;
    command.sequence = 41;
    command.command = "/gamemode creative";
    commandPacket.inProcessPayload = command;
    clientPtr->pushIncoming(std::move(commandPacket));
    harness.server.tick(1.0f / 20.0f);
    while (!clientPtr->sent.empty()) {
        clientPtr->sent.pop();
    }

    uint32_t inputSequence = 1;
    auto pushInput = [&](const uint32_t actions, const uint8_t selectedSlot) {
        net::Packet inputPacket;
        inputPacket.type = net::MessageType::ClientInput;
        net::ClientInput input;
        input.sequence = inputSequence++;
        input.playerPosition = playerPosition;
        input.playerVelocity = glm::vec3(0.0f);
        input.yaw = 0.0f;
        input.pitch = -10.0f;
        input.selectedHotbarSlot = selectedSlot;
        input.actions = actions;
        inputPacket.inProcessPayload = input;
        clientPtr->pushIncoming(std::move(inputPacket));
    };

    bool sawProjectileSpawn = false;
    bool sawInventoryAfterThrow = false;
    net::InventorySnapshotMessage inventoryAfterThrow;
    auto drainThrowPackets = [&]() {
        while (!clientPtr->sent.empty()) {
            net::Packet packet = std::move(clientPtr->sent.front());
            clientPtr->sent.pop();
            if (packet.type == net::MessageType::EntitySpawn && packet.inProcessPayload.has_value()) {
                const auto& spawn = std::any_cast<const net::EntitySpawnMessage&>(packet.inProcessPayload);
                sawProjectileSpawn = sawProjectileSpawn ||
                    (spawn.kind == net::EntityKind::Projectile && spawn.itemId == ItemIds::APPLE);
            }
            if (packet.type == net::MessageType::InventorySnapshot && packet.inProcessPayload.has_value()) {
                inventoryAfterThrow = std::any_cast<const net::InventorySnapshotMessage&>(packet.inProcessPayload);
                sawInventoryAfterThrow = true;
            }
        }
    };

    pushInput(net::ClientInputActions::UseItem, 7);
    harness.server.tick(1.0f / 20.0f);
    drainThrowPackets();

    pushInput(0, 0);
    harness.server.tick(1.0f / 20.0f);
    drainThrowPackets();

    require(sawProjectileSpawn, "creative apple use should still spawn a projectile");
    require(sawInventoryAfterThrow, "slot change should sync inventory after creative throw");
    require(inventoryItemCount(inventoryAfterThrow, ItemIds::APPLE) == applesBeforeThrow,
            "creative apple projectile should not consume authoritative inventory");
    std::printf("[PASS] testCreativeAppleProjectileDoesNotConsumeInventory\n");
}

static void testEntityFactoryCreatesConfiguredZombie() {
    ecs::EntityDefinitionRegistry::instance().clear();
    std::string error;
    require(ecs::EntityDefinitionRegistry::instance().ensureLoaded(&error),
            error.empty() ? "entity definitions should load" : error.c_str());

    ecs::GameplayRegistry registry;
    const entt::entity zombie =
        ecs::EntityFactory::createMob(registry, "minecraft:zombie", glm::vec3(1.0f, 64.0f, 2.0f));
    require(zombie != entt::null, "EntityFactory should create configured zombie");

    auto& raw = registry.registry();
    require(raw.all_of<ecs::MobTag,
                       ecs::TransformComponent,
                       ecs::MobAIComponent,
                       ecs::HealthComponent,
                       ecs::HurtEffectComponent,
                       ecs::MobVisualComponent,
                       ecs::PhysicsBodyComponent,
                       ecs::DropTableComponent,
                       ecs::DeathEffectComponent,
                       ecs::EntityTypeComponent,
                       ecs::NetworkSyncTag>(zombie),
            "configured zombie should have gameplay mob components");

    const auto& transform = raw.get<ecs::TransformComponent>(zombie);
    const auto& ai = raw.get<ecs::MobAIComponent>(zombie);
    const auto& health = raw.get<ecs::HealthComponent>(zombie);
    const auto& body = raw.get<ecs::PhysicsBodyComponent>(zombie);
    const auto& drops = raw.get<ecs::DropTableComponent>(zombie);
    const auto& hurtEffect = raw.get<ecs::HurtEffectComponent>(zombie);
    const auto& deathEffect = raw.get<ecs::DeathEffectComponent>(zombie);
    const auto& type = raw.get<ecs::EntityTypeComponent>(zombie);
    const auto& visual = raw.get<ecs::MobVisualComponent>(zombie);

    require(type.entityId == "minecraft:zombie", "configured zombie should keep entity definition id");
    require(visual.model == "humanoid" &&
            visual.textureKey == "zombie" &&
            ecs::entitySkinLayoutId(visual.skinLayout) == "minecraft:classic_64x64" &&
            std::fabs(visual.scale - 1.0f) < 0.001f,
            "configured zombie should apply visual data");
    require(transform.eyeHeight == 1.62f, "configured zombie should apply eye height");
    require(health.current == 20 && health.max == 20, "configured zombie should apply health");
    require(ai.attackDamage == 3 && ai.pursueSpeed == 0.85f,
            "configured zombie should apply AI tuning");
    require(body.body.halfExtents.y == 0.9f && body.body.colliderOffset.y == 0.9f,
            "configured zombie should apply physics bounds");
    require(drops.itemId == ItemIds::COAL && drops.minCount == 1 && drops.maxCount == 1,
            "configured zombie should apply drop table");
    require(hurtEffect.soundId == "mob.zombie.hurt" &&
            hurtEffect.soundVolume == 1.0f &&
            std::fabs(hurtEffect.flashDurationSeconds - 0.18f) < 0.001f,
            "configured zombie should apply hurt effect");
    require(deathEffect.particleBlock == BlockIds::ROSE &&
            deathEffect.particleCount == 28 &&
            deathEffect.soundId == "mob.zombie.death",
            "configured zombie should apply death effect");

    std::printf("[PASS] testEntityFactoryCreatesConfiguredZombie\n");
}

static void testEntityFactoryCreatesConfiguredHerobrine() {
    ecs::EntityDefinitionRegistry::instance().clear();
    std::string error;
    require(ecs::EntityDefinitionRegistry::instance().ensureLoaded(&error),
            error.empty() ? "entity definitions should load" : error.c_str());

    ecs::GameplayRegistry registry;
    const entt::entity herobrine =
        ecs::EntityFactory::createMob(registry, "minecraft:herobrine", glm::vec3(2.0f, 64.0f, 1.0f));
    require(herobrine != entt::null, "EntityFactory should create configured herobrine");

    auto& raw = registry.registry();
    require(raw.all_of<ecs::MobTag,
                       ecs::TransformComponent,
                       ecs::MobAIComponent,
                       ecs::HealthComponent,
                       ecs::MobVisualComponent,
                       ecs::PhysicsBodyComponent,
                       ecs::DropTableComponent,
                       ecs::DeathEffectComponent,
                       ecs::EntityTypeComponent,
                       ecs::NetworkSyncTag>(herobrine),
            "configured herobrine should have gameplay mob components");

    const auto& ai = raw.get<ecs::MobAIComponent>(herobrine);
    const auto& health = raw.get<ecs::HealthComponent>(herobrine);
    const auto& drops = raw.get<ecs::DropTableComponent>(herobrine);
    const auto& deathEffect = raw.get<ecs::DeathEffectComponent>(herobrine);
    const auto& type = raw.get<ecs::EntityTypeComponent>(herobrine);
    const auto& visual = raw.get<ecs::MobVisualComponent>(herobrine);

    require(type.entityId == "minecraft:herobrine", "configured herobrine should keep entity definition id");
    require(visual.model == "humanoid" &&
            visual.textureKey == "herobrine" &&
            ecs::entitySkinLayoutId(visual.skinLayout) == "minecraft:steve_64x64" &&
            std::fabs(visual.scale - 1.0f) < 0.001f,
            "configured herobrine should apply visual data");
    require(health.current == 40 && health.max == 40,
            "configured herobrine should apply health");
    require(ai.attackDamage == 6 && std::fabs(ai.pursueSpeed - 1.05f) < 0.001f,
            "configured herobrine should apply AI tuning");
    require(drops.itemId == ItemRegistry::findByName("minecraft:diamond") &&
            drops.minCount == 1 &&
            drops.maxCount == 1,
            "configured herobrine should apply drop table");
    require(deathEffect.particleBlock == BlockIds::DIAMOND_ORE &&
            deathEffect.particleCount == 36 &&
            deathEffect.soundId == "mob.zombie.death",
            "configured herobrine should apply death effect");

    std::printf("[PASS] testEntityFactoryCreatesConfiguredHerobrine\n");
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
                                            ecs::EntityTypeComponent,
                                            ecs::NetworkSyncTag>();
        require(view.begin() != view.end(), "persistent zombie restore should create a mob");
        const entt::entity zombie = *view.begin();
        const auto& transform = registry.registry().get<ecs::TransformComponent>(zombie);
        const auto& health = registry.registry().get<ecs::HealthComponent>(zombie);
        const auto& ai = registry.registry().get<ecs::MobAIComponent>(zombie);
        const auto& body = registry.registry().get<ecs::PhysicsBodyComponent>(zombie);
        const auto& type = registry.registry().get<ecs::EntityTypeComponent>(zombie);
        require(transform.position.x == 4.0f, "persistent zombie restore should keep X position");
        require(transform.position.z == -2.0f, "persistent zombie restore should keep Z position");
        require(type.entityId == "minecraft:zombie", "persistent zombie restore should keep entity id");
        require(health.current == 9, "persistent zombie restore should keep health");
        require(ai.yaw == 135.0f, "persistent zombie restore should keep yaw");
        require(body.body.velocity.z == -0.5f, "persistent zombie restore should keep velocity");

        server.setEcsRegistry(static_cast<ecs::GameplayRegistry*>(nullptr));
        server.shutdown();
    }

    std::filesystem::remove_all(saveRoot);
    std::printf("[PASS] testPersistentZombieRestoresFromSave\n");
}

static void testPersistentDropRestoresFromSave() {
    const std::filesystem::path saveRoot = "test_server_drop_entities_save";
    std::filesystem::remove_all(saveRoot);

    {
        server::GameServer server;
        server.init(1234, nullptr, 2, saveRoot, "Drop Entity Save Test");

        ecs::GameplayRegistry registry;
        server.setEcsRegistry(&registry);

        ecs::ItemDropSpawnParams params;
        params.itemId = ItemIds::COAL;
        params.stackCount = 5;
        params.position = glm::vec3(6.5f, 66.25f, -4.0f);
        params.velocity = glm::vec3(0.25f, 0.5f, -0.75f);
        params.halfExtents = glm::vec3(0.2f, 0.21f, 0.22f);
        params.yawRadians = 1.5f;
        params.spinSpeedRadians = 2.25f;
        params.ageSeconds = 3.0f;
        params.lifeTimeSeconds = 24.0f;
        params.grounded = true;
        params.dropId = 77;
        ecs::EntityFactory::createItemDrop(registry, params);

        server.savePersistentEntities();
        server.setEcsRegistry(static_cast<ecs::GameplayRegistry*>(nullptr));
        server.shutdown();
    }

    {
        server::GameServer server;
        server.init(1234, nullptr, 2, saveRoot, "Drop Entity Save Test");

        ecs::GameplayRegistry registry;
        server.setEcsRegistry(&registry);
        server.restorePersistentEntities();

        auto view = registry.registry().view<ecs::DropItemTag,
                                            ecs::DropEntityIdComponent,
                                            ecs::TransformComponent,
                                            ecs::ItemComponent,
                                            ecs::VelocityComponent,
                                            ecs::BoundsComponent,
                                            ecs::LifetimeComponent,
                                            ecs::SpinVisualComponent,
                                            ecs::GroundedStateComponent,
                                            ecs::NetworkSyncTag>();
        require(view.begin() != view.end(), "persistent drop restore should create a synced drop");
        const entt::entity drop = *view.begin();
        const auto& id = registry.registry().get<ecs::DropEntityIdComponent>(drop);
        const auto& transform = registry.registry().get<ecs::TransformComponent>(drop);
        const auto& item = registry.registry().get<ecs::ItemComponent>(drop);
        const auto& velocity = registry.registry().get<ecs::VelocityComponent>(drop);
        const auto& bounds = registry.registry().get<ecs::BoundsComponent>(drop);
        const auto& lifetime = registry.registry().get<ecs::LifetimeComponent>(drop);
        const auto& spin = registry.registry().get<ecs::SpinVisualComponent>(drop);
        const auto& grounded = registry.registry().get<ecs::GroundedStateComponent>(drop);
        require(id.dropId == 77, "persistent drop restore should keep drop id");
        require(item.itemId == ItemIds::COAL && item.stackCount == 5,
                "persistent drop restore should keep item stack");
        require(transform.position.x == 6.5f && transform.position.z == -4.0f,
                "persistent drop restore should keep position");
        require(velocity.velocity.z == -0.75f, "persistent drop restore should keep velocity");
        require(bounds.halfExtents.y == 0.21f, "persistent drop restore should keep bounds");
        require(lifetime.ageSeconds == 3.0f && lifetime.lifeTimeSeconds == 24.0f,
                "persistent drop restore should keep lifetime");
        require(spin.yawRadians == 1.5f && spin.spinSpeedRadians == 2.25f,
                "persistent drop restore should keep spin");
        require(grounded.grounded, "persistent drop restore should keep grounded state");

        server.setEcsRegistry(static_cast<ecs::GameplayRegistry*>(nullptr));
        server.shutdown();
    }

    std::filesystem::remove_all(saveRoot);
    std::printf("[PASS] testPersistentDropRestoresFromSave\n");
}

static void testPersistentChestInventoryRestoresFromSave() {
    const std::filesystem::path saveRoot = "test_server_chest_block_entities_save";
    std::filesystem::remove_all(saveRoot);
    const glm::ivec3 chestPos(-3, 70, 8);

    {
        server::GameServer server;
        server.init(1234, nullptr, 2, saveRoot, "Chest Block Entity Save Test");

        ecs::GameplayRegistry registry;
        server.setEcsRegistry(&registry);

        ChestInventoryStore& store = registry.ctxSet<ChestInventoryStore>();
        ChestInventory& chest = store.getOrCreate(chestPos);
        chest.setSlotItem(0, ItemIds::APPLE, 6);

        ItemStack pickaxe;
        pickaxe.itemId = ItemIds::IRON_PICKAXE;
        pickaxe.count = 1;
        pickaxe.durability = 44;
        chest.setSlotStack(17, pickaxe);

        server.saveBlockEntities();
        server.setEcsRegistry(static_cast<ecs::GameplayRegistry*>(nullptr));
        server.shutdown();
    }

    {
        server::GameServer server;
        server.init(1234, nullptr, 2, saveRoot, "Chest Block Entity Save Test");

        ecs::GameplayRegistry registry;
        server.setEcsRegistry(&registry);

        require(registry.ctxHas<ChestInventoryStore>(),
                "persistent chest restore should create a chest inventory store");
        const ChestInventoryStore& store = registry.ctxGet<ChestInventoryStore>();
        const ChestInventory* chest = store.find(chestPos);
        require(chest != nullptr, "persistent chest restore should recreate the chest inventory");

        const ItemStack apples = chest->getSlotStack(0);
        require(apples.itemId == ItemIds::APPLE && apples.count == 6,
                "persistent chest restore should keep stack item and count");

        const ItemStack pickaxe = chest->getSlotStack(17);
        require(pickaxe.itemId == ItemIds::IRON_PICKAXE && pickaxe.count == 1 && pickaxe.durability == 44,
                "persistent chest restore should keep tool durability");

        server.setEcsRegistry(static_cast<ecs::GameplayRegistry*>(nullptr));
        server.shutdown();
    }

    std::filesystem::remove_all(saveRoot);
    std::printf("[PASS] testPersistentChestInventoryRestoresFromSave\n");
}

static void testServerBlockActionBreaksChestLifecycle() {
    ServerHarness harness(2);
    ecs::GameplayRegistry registry;
    harness.server.setEcsRegistry(&registry);

    auto clientTransport = std::make_unique<ManualTransport>();
    ManualTransport* clientPtr = clientTransport.get();

    net::Packet hello;
    hello.type = net::MessageType::ClientHello;
    hello.inProcessPayload = net::ClientHello{};
    clientPtr->pushIncoming(std::move(hello));

    harness.server.acceptClient(std::move(clientTransport), 1);

    const glm::ivec3 chestPos(0, Chunk::SIZE_Y - 8, 0);
    for (int tick = 0;
         tick < 240 && !harness.server.world().isChunkLoadedForBlock(chestPos.x, chestPos.y, chestPos.z);
         ++tick) {
        harness.server.tick(1.0f / 20.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(harness.server.world().isChunkLoadedForBlock(chestPos.x, chestPos.y, chestPos.z),
            "test setup should load the target chest chunk");

    const StateID chestState = BlockStateRegistry::getDefaultState(BlockIds::CHEST);
    harness.server.world().setBlock(chestPos.x, chestPos.y, chestPos.z, chestState);

    ChestInventoryStore& store = registry.ctxSet<ChestInventoryStore>();
    ChestInventory& chest = store.getOrCreate(chestPos);
    chest.setSlotItem(0, ItemIds::APPLE, 3);
    chest.setSlotItem(5, ItemIds::COAL, 2);

    while (!clientPtr->sent.empty()) {
        clientPtr->sent.pop();
    }

    net::Packet actionPacket;
    actionPacket.type = net::MessageType::ClientBlockAction;
    net::ClientBlockAction action;
    action.sequence = 2;
    action.action = net::ClientBlockActionType::Break;
    action.targetBlock = chestPos;
    action.playerPosition = glm::vec3(chestPos) + glm::vec3(0.5f);
    actionPacket.inProcessPayload = action;
    clientPtr->pushIncoming(std::move(actionPacket));

    harness.server.tick(1.0f / 20.0f);

    require(harness.server.world().getBlock(chestPos.x, chestPos.y, chestPos.z) == BlockIds::AIR,
            "server block break should remove the chest block");
    require(store.find(chestPos) == nullptr,
            "server block break should erase the chest inventory store entry");
    require(ecsDroppedItemCount(registry, ItemIds::APPLE) == 3,
            "server chest break should spawn stored apple drops");
    require(ecsDroppedItemCount(registry, ItemIds::COAL) == 2,
            "server chest break should spawn stored coal drops");
    require(ecsDroppedItemCount(registry, ItemRegistry::fromBlock(BlockIds::CHEST)) == 1,
            "server survival chest break should spawn the chest item drop");

    bool sawAirBlockUpdate = false;
    uint32_t sawDropSpawns = 0;
    while (!clientPtr->sent.empty()) {
        net::Packet packet = std::move(clientPtr->sent.front());
        clientPtr->sent.pop();
        if (packet.type == net::MessageType::BlockUpdateBatch && packet.inProcessPayload.has_value()) {
            const auto& batch = std::any_cast<const net::BlockUpdateBatchMessage&>(packet.inProcessPayload);
            for (const auto& update : batch.updates) {
                if (update.x == chestPos.x &&
                    update.y == chestPos.y &&
                    update.z == chestPos.z &&
                    update.blockId == BlockIds::AIR) {
                    sawAirBlockUpdate = true;
                }
            }
        }
        if (packet.type == net::MessageType::EntitySpawn && packet.inProcessPayload.has_value()) {
            const auto& spawn = std::any_cast<const net::EntitySpawnMessage&>(packet.inProcessPayload);
            if (spawn.kind == net::EntityKind::Drop &&
                (spawn.itemId == ItemIds::APPLE ||
                 spawn.itemId == ItemIds::COAL ||
                 spawn.itemId == ItemRegistry::fromBlock(BlockIds::CHEST))) {
                ++sawDropSpawns;
            }
        }
    }

    require(sawAirBlockUpdate, "server chest break should sync the removed block");
    require(sawDropSpawns >= 3, "server chest break should sync spawned chest drops");

    harness.server.setEcsRegistry(static_cast<ecs::GameplayRegistry*>(nullptr));
    std::printf("[PASS] testServerBlockActionBreaksChestLifecycle\n");
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

static void testOwnedServerEcsRestoresPersistentDrop() {
    const std::filesystem::path saveRoot = "test_owned_server_drop_entities_save";
    std::filesystem::remove_all(saveRoot);

    {
        server::GameServer server;
        server.init(1234, nullptr, 2, saveRoot, "Owned Drop Entity Save Test");

        ecs::GameplayRegistry registry;
        server.setEcsRegistry(&registry);

        ecs::ItemDropSpawnParams params;
        params.itemId = ItemIds::COAL;
        params.stackCount = 3;
        params.position = glm::vec3(20.5f, 66.0f, 20.5f);
        params.velocity = glm::vec3(0.0f);
        params.yawRadians = 0.75f;
        params.spinSpeedRadians = 2.0f;
        params.dropId = 99;
        ecs::EntityFactory::createItemDrop(registry, params);

        server.savePersistentEntities();
        server.setEcsRegistry(static_cast<ecs::GameplayRegistry*>(nullptr));
        server.shutdown();
    }

    {
        server::GameServer server;
        server.init(1234, nullptr, 2, saveRoot, "Owned Drop Entity Save Test");

        auto clientTransport = std::make_unique<ManualTransport>();
        ManualTransport* clientPtr = clientTransport.get();
        net::Packet hello;
        hello.type = net::MessageType::ClientHello;
        hello.inProcessPayload = net::ClientHello{};
        clientPtr->pushIncoming(std::move(hello));
        server.acceptClient(std::move(clientTransport), 1);
        server.tick(1.0f / 20.0f);

        bool sawDropSpawn = false;
        while (!clientPtr->sent.empty()) {
            net::Packet packet = std::move(clientPtr->sent.front());
            clientPtr->sent.pop();
            if (packet.type == net::MessageType::EntitySpawn && packet.inProcessPayload.has_value()) {
                const auto& spawn = std::any_cast<const net::EntitySpawnMessage&>(packet.inProcessPayload);
                sawDropSpawn = sawDropSpawn ||
                    (spawn.kind == net::EntityKind::Drop &&
                     spawn.itemId == ItemIds::COAL &&
                     spawn.stackCount == 3 &&
                     spawn.netId != 0);
            }
        }
        require(sawDropSpawn, "owned ECS restore should send restored drop spawn");
        server.shutdown();
    }

    std::filesystem::remove_all(saveRoot);
    std::printf("[PASS] testOwnedServerEcsRestoresPersistentDrop\n");
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

    net::ClientRespawnRequest respawnRequest;
    respawnRequest.sequence = 7;
    const auto encodedRespawn = net::PacketCodec::encodeClientRespawnRequest(respawnRequest);
    net::ClientRespawnRequest decodedRespawn;
    assert(net::PacketCodec::decodeClientRespawnRequest(encodedRespawn.data(), encodedRespawn.size(), decodedRespawn));
    assert(decodedRespawn.sequence == respawnRequest.sequence);

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

static void testClientInputCodecCarriesSelectedHotbarSlot() {
    net::ClientInput input;
    input.sequence = 44;
    input.dt = 0.05f;
    input.playerPosition = glm::vec3(1.0f, 2.0f, 3.0f);
    input.playerVelocity = glm::vec3(0.0f, 1.0f, 0.0f);
    input.yaw = 15.0f;
    input.pitch = -10.0f;
    input.actions = net::ClientInputActions::UseItem;
    input.selectedHotbarSlot = 7;

    const auto encoded = net::PacketCodec::encodeClientInput(input);
    net::ClientInput decoded;
    require(net::PacketCodec::decodeClientInput(encoded.data(), encoded.size(), decoded),
            "client input codec should decode selected hotbar payload");
    require(decoded.selectedHotbarSlot == 7,
            "client input codec should keep selected hotbar slot");
    require((decoded.actions & net::ClientInputActions::UseItem) != 0,
            "client input codec should keep use item action");

    net::ClientInput legacyDecoded;
    require(net::PacketCodec::decodeClientInput(encoded.data(), encoded.size() - 1, legacyDecoded),
            "client input codec should accept payloads without selected slot");
    require(legacyDecoded.selectedHotbarSlot == 0,
            "legacy client input payload should default selected slot to zero");
    std::printf("[PASS] testClientInputCodecCarriesSelectedHotbarSlot\n");
}

static void testEntitySpawnCodecCarriesEntityId() {
    net::EntitySpawnMessage spawn;
    spawn.netId = 123;
    spawn.kind = net::EntityKind::Mob;
    spawn.position = glm::vec3(1.0f, 64.0f, -2.0f);
    spawn.velocity = glm::vec3(0.25f, 0.0f, -0.5f);
    spawn.yaw = 45.0f;
    spawn.pitch = 0.0f;
    spawn.entityId = "minecraft:zombie";

    const auto encoded = net::PacketCodec::encodeEntitySpawn(spawn);

    net::EntitySpawnMessage decoded;
    require(net::PacketCodec::decodeEntitySpawn(encoded.data(), encoded.size(), decoded),
            "entity spawn codec should decode payload");
    require(decoded.netId == spawn.netId, "entity spawn codec should keep net id");
    require(decoded.kind == net::EntityKind::Mob, "entity spawn codec should keep kind");
    require(decoded.position.z == spawn.position.z, "entity spawn codec should keep position");
    require(decoded.velocity.x == spawn.velocity.x, "entity spawn codec should keep velocity");
    require(decoded.yaw == spawn.yaw, "entity spawn codec should keep yaw");
    require(decoded.entityId == "minecraft:zombie", "entity spawn codec should keep entity id");

    net::EntitySpawnMessage legacyDecoded;
    require(net::PacketCodec::decodeEntitySpawn(encoded.data(), 41, legacyDecoded),
            "entity spawn codec should decode legacy payload without entity id");
    require(legacyDecoded.entityId.empty(), "legacy entity spawn payload should default entity id empty");

    net::EntitySpawnMessage truncated;
    require(!net::PacketCodec::decodeEntitySpawn(encoded.data(), encoded.size() - 1, truncated),
            "entity spawn codec should reject truncated entity id payload");
    std::printf("[PASS] testEntitySpawnCodecCarriesEntityId\n");
}

static void testEntityImpactCodecRoundTrip() {
    net::EntityImpactMessage impact;
    impact.netId = 123;
    impact.position = glm::vec3(1.25f, 64.5f, -3.75f);
    impact.particleBlockId = static_cast<uint16_t>(BlockIds::STONE);
    impact.particleCount = 28;

    const auto encoded = net::PacketCodec::encodeEntityImpact(impact);
    require(encoded.size() == 20, "entity impact codec should write net id, position, particle block, and count");

    net::EntityImpactMessage decoded;
    require(net::PacketCodec::decodeEntityImpact(encoded.data(), encoded.size(), decoded),
            "entity impact codec should decode payload");
    require(decoded.netId == impact.netId, "entity impact codec should keep net id");
    require(decoded.position.x == impact.position.x &&
            decoded.position.y == impact.position.y &&
            decoded.position.z == impact.position.z,
            "entity impact codec should keep impact position");
    require(decoded.particleBlockId == impact.particleBlockId,
            "entity impact codec should keep particle block id");
    require(decoded.particleCount == impact.particleCount,
            "entity impact codec should keep particle count");

    net::EntityImpactMessage legacyDecoded;
    require(net::PacketCodec::decodeEntityImpact(encoded.data(), 18, legacyDecoded),
            "entity impact codec should decode legacy impact payload");
    require(legacyDecoded.particleBlockId == impact.particleBlockId,
            "legacy entity impact payload should keep particle block id");
    require(legacyDecoded.particleCount == 14,
            "legacy entity impact payload should default particle count");

    net::EntityImpactMessage truncated;
    require(!net::PacketCodec::decodeEntityImpact(encoded.data(), encoded.size() - 1, truncated),
            "entity impact codec should reject truncated payload");
    std::printf("[PASS] testEntityImpactCodecRoundTrip\n");
}

static void testEntitySnapshotCodecCarriesHealthAndHurt() {
    net::EntitySnapshotMessage snapshot;
    snapshot.serverTick = 77;
    net::EntitySnapshotItem entity;
    entity.netId = 321;
    entity.position = glm::vec3(1.0f, 64.0f, -2.0f);
    entity.velocity = glm::vec3(0.25f, 0.0f, -0.5f);
    entity.yaw = 45.0f;
    entity.pitch = 5.0f;
    entity.health = 16;
    entity.maxHealth = 20;
    entity.hurt = true;
    snapshot.entities.push_back(entity);

    const auto encoded = net::PacketCodec::encodeEntitySnapshot(snapshot);
    require(encoded.size() == 49, "entity snapshot codec should append health/max/hurt state");

    net::EntitySnapshotMessage decoded;
    require(net::PacketCodec::decodeEntitySnapshot(encoded.data(), encoded.size(), decoded),
            "entity snapshot codec should decode payload");
    require(decoded.serverTick == snapshot.serverTick, "entity snapshot codec should keep server tick");
    require(decoded.entities.size() == 1, "entity snapshot codec should keep entity count");
    require(decoded.entities.front().netId == entity.netId, "entity snapshot codec should keep net id");
    require(decoded.entities.front().position.y == entity.position.y,
            "entity snapshot codec should keep position");
    require(decoded.entities.front().velocity.z == entity.velocity.z,
            "entity snapshot codec should keep velocity");
    require(decoded.entities.front().yaw == entity.yaw &&
            decoded.entities.front().pitch == entity.pitch,
            "entity snapshot codec should keep rotation");
    require(decoded.entities.front().health == 16 &&
            decoded.entities.front().maxHealth == 20 &&
            decoded.entities.front().hurt,
            "entity snapshot codec should keep synced health and hurt state");

    net::EntitySnapshotMessage legacyDecoded;
    require(net::PacketCodec::decodeEntitySnapshot(encoded.data(), 44, legacyDecoded),
            "entity snapshot codec should decode legacy pitch payload");
    require(legacyDecoded.entities.size() == 1, "legacy entity snapshot should keep entity count");
    require(legacyDecoded.entities.front().health == 0 &&
            legacyDecoded.entities.front().maxHealth == 0 &&
            !legacyDecoded.entities.front().hurt,
            "legacy entity snapshot should default synced health and hurt state");

    net::EntitySnapshotMessage truncated;
    require(!net::PacketCodec::decodeEntitySnapshot(encoded.data(), encoded.size() - 1, truncated),
            "entity snapshot codec should reject truncated health/hurt payload");
    std::printf("[PASS] testEntitySnapshotCodecCarriesHealthAndHurt\n");
}

static void testServerSnapshotCodecCarriesPlayerHealth() {
    net::ServerSnapshot snapshot;
    snapshot.serverTick = 17;
    snapshot.ackInputSequence = 9;
    snapshot.authoritativePosition = glm::vec3(1.0f, 2.0f, 3.0f);
    snapshot.authoritativeVelocity = glm::vec3(0.25f, 0.0f, -0.5f);
    snapshot.playerHealth = 7;
    snapshot.playerMaxHealth = 20;
    snapshot.playerHurt = true;
    snapshot.playerRespawned = true;
    snapshot.playerDead = true;

    const auto encoded = net::PacketCodec::encodeServerSnapshot(snapshot);
    require(encoded.size() == 39, "server snapshot codec should include health, respawn, and dead payload bytes");
    require(encoded[32] == 7 && encoded[33] == 0, "server snapshot codec should write player health after base payload");
    require(encoded[34] == 20 && encoded[35] == 0, "server snapshot codec should write max health after health");
    require(encoded[36] == 1, "server snapshot codec should write hurt flag after health values");
    require(encoded[37] == 1, "server snapshot codec should write respawn flag after hurt flag");
    require(encoded[38] == 1, "server snapshot codec should write dead flag after respawn flag");
    net::ServerSnapshot decoded;
    require(net::PacketCodec::decodeServerSnapshot(encoded.data(), encoded.size(), decoded),
            "server snapshot codec should decode health payload");
    require(decoded.serverTick == 17, "server snapshot codec should keep tick");
    require(decoded.ackInputSequence == 9, "server snapshot codec should keep ack");
    require(decoded.playerHealth == 7, "server snapshot codec should keep player health");
    require(decoded.playerMaxHealth == 20, "server snapshot codec should keep max health");
    require(decoded.playerHurt, "server snapshot codec should keep hurt event");
    require(decoded.playerRespawned, "server snapshot codec should keep respawn event");
    require(decoded.playerDead, "server snapshot codec should keep dead state");

    net::ServerSnapshot legacyDecoded;
    require(net::PacketCodec::decodeServerSnapshot(encoded.data(), 38, legacyDecoded),
            "server snapshot codec should decode legacy respawn payload");
    require(legacyDecoded.playerHealth == 7, "legacy health payload should keep player health");
    require(legacyDecoded.playerHurt, "legacy health payload should keep hurt event");
    require(legacyDecoded.playerRespawned, "legacy respawn payload should keep respawn event");
    require(!legacyDecoded.playerDead, "legacy respawn payload should default dead state off");

    net::ServerSnapshot legacyHealthDecoded;
    require(net::PacketCodec::decodeServerSnapshot(encoded.data(), 37, legacyHealthDecoded),
            "server snapshot codec should decode legacy health-only payload");
    require(legacyHealthDecoded.playerHealth == 7, "legacy health payload should keep player health");
    require(legacyHealthDecoded.playerHurt, "legacy health payload should keep hurt event");
    require(!legacyHealthDecoded.playerRespawned, "legacy health payload should default respawn event off");
    require(!legacyHealthDecoded.playerDead, "legacy health payload should default dead state off");
    std::printf("[PASS] testServerSnapshotCodecCarriesPlayerHealth\n");
}

static void testInventorySnapshotCodecRoundTrip() {
    net::InventorySnapshotMessage snapshot;
    snapshot.selectedHotbarSlot = 4;
    snapshot.slots.resize(3);
    snapshot.slots[0].itemId = static_cast<uint16_t>(ItemIds::COAL);
    snapshot.slots[0].stackCount = 12;
    snapshot.slots[2].itemId = static_cast<uint16_t>(ItemIds::IRON_PICKAXE);
    snapshot.slots[2].stackCount = 1;

    const auto encoded = net::PacketCodec::encodeInventorySnapshot(snapshot);
    require(encoded.size() == 14, "inventory snapshot codec should write selected slot, count, and slots");

    net::InventorySnapshotMessage decoded;
    require(net::PacketCodec::decodeInventorySnapshot(encoded.data(), encoded.size(), decoded),
            "inventory snapshot codec should decode payload");
    require(decoded.selectedHotbarSlot == 4, "inventory snapshot codec should keep selected slot");
    require(decoded.slots.size() == 3, "inventory snapshot codec should keep slot count");
    require(decoded.slots[0].itemId == ItemIds::COAL && decoded.slots[0].stackCount == 12,
            "inventory snapshot codec should keep first slot");
    require(decoded.slots[1].itemId == 0 && decoded.slots[1].stackCount == 0,
            "inventory snapshot codec should keep empty slot");
    require(decoded.slots[2].itemId == ItemIds::IRON_PICKAXE && decoded.slots[2].stackCount == 1,
            "inventory snapshot codec should keep later slot");

    net::InventorySnapshotMessage truncated;
    require(!net::PacketCodec::decodeInventorySnapshot(encoded.data(), encoded.size() - 1, truncated),
            "inventory snapshot codec should reject truncated slot payload");
    std::printf("[PASS] testInventorySnapshotCodecRoundTrip\n");
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

static void testPlacedUnsupportedSandQueuesItself() {
    ServerHarness harness;

    for (int i = 0; i < 40; ++i) {
        harness.server.tick(1.0f / 20.0f);
    }

    const int surfaceY = static_cast<int>(harness.server.getSpawnPosition().y) - 2;
    const glm::ivec3 sandPos(0, surfaceY + 4, 0);
    harness.server.world().setBlock(sandPos.x, sandPos.y - 1, sandPos.z, BlockIds::AIR);
    harness.server.world().setBlock(sandPos.x, sandPos.y, sandPos.z, BlockIds::SAND);

    ecs::BlockSupportSystem::processWorldQueue(harness.server.world(), 1024);

    assert(harness.server.world().getBlock(sandPos.x, sandPos.y, sandPos.z) == BlockIds::AIR);
    std::printf("[PASS] testPlacedUnsupportedSandQueuesItself\n");
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

    bool lightSettled = false;
    for (int tick = 0; tick < 240; ++tick) {
        const LightFrameStats stats = server.world().getLightFrameStats();
        if (stats.queued == 0 &&
            stats.dirty == 0 &&
            stats.inFlight == 0 &&
            stats.pendingCompleted == 0) {
            lightSettled = true;
            break;
        }
        server.tick(1.0f / 20.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(lightSettled);

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
    int ticksUntilLightPatch = -1;
    for (int i = 0; i < 4 && !sawTorchLightPatch; ++i) {
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
                        ticksUntilLightPatch = i + 1;
                        break;
                    }
                }
            }
        }
    }

    assert(sawTorchBlockUpdate);
    assert(sawTorchLightPatch);
    assert(ticksUntilLightPatch == 1);
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
    BlockDropTable::init();

    testServerInit();
    testAcceptClient();
    testClientServerHandshake();
    testServerTick();
    testChunkStreamingToClient();
    testClientViewConfigExpandsServerStreamingRadius();
    testInputRoundTrip();
    testClientAppliesPlayerHealthSnapshot();
    testClientAppliesInventorySnapshot();
    testClientBlockActionRoundTrip();
    testChatBroadcastRoundTrip();
    testAdminCommandUpdatesWorldState();
    testNonAdminCommandDenied();
    testGiveCommandAddsRuntimeBlockItem();
    testGiveCommandRejectsUnknownItem();
    testSummonZombieSpawnsNetworkMob();
    testSummonZombieUsesOwnedServerEcs();
    testSummonHerobrineUsesOwnedServerEcs();
    testOwnedServerZombiePursuesPlayer();
    testOwnedServerZombieAttackSyncsPlayerHealth();
    testOwnedServerPlayerDiesDropsItemsAndRespawnsOnRequest();
    testOwnedServerPlayerMeleeKillsZombieAndDropsItem();
    testOwnedServerMobSnapshotCarriesHealthAndHurt();
    testOwnedServerPlayerPicksUpDropDespawnsAndSyncsInventory();
    testOwnedServerPlayerThrowsAppleProjectileDamagesZombie();
    testCreativeAppleProjectileDoesNotConsumeInventory();
    testEntityFactoryCreatesConfiguredZombie();
    testEntityFactoryCreatesConfiguredHerobrine();
    testPersistentZombieRestoresFromSave();
    testPersistentDropRestoresFromSave();
    testPersistentChestInventoryRestoresFromSave();
    testServerBlockActionBreaksChestLifecycle();
    testOwnedServerEcsRestoresPersistentZombie();
    testOwnedServerEcsRestoresPersistentDrop();
    testChatCommandCodecRoundTrip();
    testClientInputCodecCarriesSelectedHotbarSlot();
    testEntitySpawnCodecCarriesEntityId();
    testEntityImpactCodecRoundTrip();
    testEntitySnapshotCodecCarriesHealthAndHurt();
    testServerSnapshotCodecCarriesPlayerHealth();
    testInventorySnapshotCodecRoundTrip();
    testServerTickBreaksUnsupportedPlant();
    testPlacedUnsupportedSandQueuesItself();
    testDisconnectedPlayerDespawnsForOtherClients();
    testChunkDataDecodeMarksRenderableSubChunks();
    testBlockUpdateCodecKeepsVariableLightPatch();
    testServerEmitsLightPatchAfterTorchPlacement();
    testENetChunkStreamingToClient();
    testENetChunkStreamingAfterPreconnectTicks();
    std::printf("\nAll GameServer integration tests passed!\n");
    return 0;
}
