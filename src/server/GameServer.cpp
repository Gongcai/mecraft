#include "GameServer.h"
#include "../world/World.h"
#include "../world/WeatherSystem.h"
#include "../world/block/Block.h"
#include "../thread/ThreadPool.h"
#include "../save/SaveManager.h"
#include "../ecs/systems/world/BlockSupportSystem.h"
#include "../ecs/components/Components.h"
#include "../ecs/components/NetworkComponents.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <sstream>
#include <string>

namespace server {
namespace {
constexpr net::EntityNetId kPlayerNetIdBase = 0x80000000u;
constexpr uint16_t kLightOnlyBlockUpdate = 0xFFFFu;

std::string playerName(const net::ClientId id) {
    return "Player" + std::to_string(id);
}

std::string trimCommand(std::string command) {
    while (!command.empty() && (command.front() == ' ' || command.front() == '\t')) {
        command.erase(command.begin());
    }
    while (!command.empty() && (command.back() == ' ' || command.back() == '\t' ||
                                command.back() == '\r' || command.back() == '\n')) {
        command.pop_back();
    }
    return command;
}

net::NetworkWeatherType toNetworkWeather(const WeatherType type) {
    switch (type) {
    case WeatherType::Rain: return net::NetworkWeatherType::Rain;
    case WeatherType::Storm: return net::NetworkWeatherType::Storm;
    case WeatherType::Snow: return net::NetworkWeatherType::Snow;
    case WeatherType::Clear:
    default:
        return net::NetworkWeatherType::Clear;
    }
}

bool parseWeatherType(const std::string& value, WeatherType& out) {
    if (value == "clear") {
        out = WeatherType::Clear;
        return true;
    }
    if (value == "rain") {
        out = WeatherType::Rain;
        return true;
    }
    if (value == "storm" || value == "thunder") {
        out = WeatherType::Storm;
        return true;
    }
    if (value == "snow") {
        out = WeatherType::Snow;
        return true;
    }
    return false;
}

bool parseGameplayMode(const std::string& value, net::NetworkGameplayMode& out) {
    if (value == "creative" || value == "1" || value == "c") {
        out = net::NetworkGameplayMode::Creative;
        return true;
    }
    if (value == "survival" || value == "0" || value == "s") {
        out = net::NetworkGameplayMode::Survival;
        return true;
    }
    return false;
}

const char* modeName(const net::NetworkGameplayMode mode) {
    return mode == net::NetworkGameplayMode::Creative ? "creative" : "survival";
}

const char* weatherName(const WeatherType type) {
    switch (type) {
    case WeatherType::Rain: return "rain";
    case WeatherType::Storm: return "storm";
    case WeatherType::Snow: return "snow";
    case WeatherType::Clear:
    default:
        return "clear";
    }
}
}

GameServer::GameServer() = default;

GameServer::~GameServer() {
    shutdown();
}

void GameServer::init(uint32_t seed, ThreadPool* threadPool, int renderDistance) {
    m_world.setRenderDistance(renderDistance);
    m_world.setThreadPool(threadPool);
    m_world.init(seed);

    // Register block change callback to collect dirty blocks for BlockUpdateBatch
    m_world.setBlockChangeCallback([this](int x, int y, int z, BlockID newBlockId) {
        m_pendingBlockUpdates.push_back(makeBlockOnlyUpdateEntry(x, y, z, newBlockId));
    });

    m_world.setLightChangeCallback([this](int64_t chunkKey, uint32_t dirtySubChunkMask) {
        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            if ((dirtySubChunkMask & (1u << scy)) == 0u) {
                continue;
            }
            net::BlockUpdateEntry entry = makeSubChunkLightUpdateEntry(chunkKey, scy);
            if (!entry.packedLightPatch.empty()) {
                m_pendingBlockUpdates.push_back(std::move(entry));
            }
        }
    });

    // Compute spawn position from world surface
    constexpr float kSpawnHeightOffset = 2.0f;
    const int surfaceY = m_world.getSurfaceY(0, 0);
    m_spawnPosition = glm::vec3(0.0f, static_cast<float>(surfaceY + kSpawnHeightOffset), 0.0f);
    std::printf("[Server] World initialized seed=%u renderDistance=%d spawn=(%.1f, %.1f, %.1f)\n",
                seed,
                renderDistance,
                m_spawnPosition.x,
                m_spawnPosition.y,
                m_spawnPosition.z);
    std::fflush(stdout);
}

void GameServer::init(uint32_t seed, ThreadPool* threadPool, int renderDistance,
                      std::filesystem::path savePath) {
    // Create save manager if path is provided
    if (!savePath.empty()) {
        m_saveManager = std::make_unique<save::SaveManager>(std::move(savePath));
        m_saveManager->setThreadPool(threadPool);
        m_saveManager->paths().ensureDirectories();

        // Try to load existing level metadata
        save::LevelMeta meta;
        meta.seed = seed;
        if (m_saveManager->loadLevelMeta(meta)) {
            seed = meta.seed;
            // Restore time and weather after world init
            m_loadedMeta = meta;
            m_hasLoadedMeta = true;
            std::printf("[Server] Loaded existing world (seed=%u)\n", seed);
        } else {
            meta.seed = seed;
            m_saveManager->saveLevelMeta(meta);
            std::printf("[Server] Created new world (seed=%u)\n", seed);
        }

        m_world.setSaveManager(m_saveManager.get());
    }

    // Delegate to the base init for world setup
    init(seed, threadPool, renderDistance);

    // Restore time and weather from loaded metadata
    if (m_hasLoadedMeta) {
        m_world.getDayNightSystem().setTimeOfDay(m_loadedMeta.timeOfDay);
        m_world.getWeatherSystem().setDebugWeatherPresetInstant(
            weatherTypeFromString(m_loadedMeta.weatherType));
        m_spawnPosition = glm::vec3(m_loadedMeta.spawnX, m_loadedMeta.spawnY, m_loadedMeta.spawnZ);
    }
}

void GameServer::shutdown() {
    if (m_shutdownDone) return;
    m_shutdownDone = true;

    // Flush all pending chunk saves
    m_world.flushSaves();

    // Save level metadata with current state
    if (m_saveManager) {
        save::LevelMeta meta;
        meta.seed = m_world.getSeed();
        meta.spawnX = m_spawnPosition.x;
        meta.spawnY = m_spawnPosition.y;
        meta.spawnZ = m_spawnPosition.z;
        meta.timeOfDay = m_world.getDayNightSystem().getTimeOfDay();
        meta.totalGameTime = m_world.getDayNightSystem().getTotalGameTime();
        meta.elapsedDays = m_world.getDayNightSystem().getElapsedDays();

        const auto& weather = m_world.getWeatherSystem().getTargetState();
        meta.weatherType = weatherTypeToString(weather.type);
        meta.weatherWetness = weather.wetness;
        meta.weatherStorm = weather.storm;
        meta.weatherAerialReduction = weather.aerialReduction;

        m_saveManager->saveLevelMeta(meta);
    }
}

void GameServer::saveLevelMeta() {
    if (!m_saveManager) return;

    save::LevelMeta meta;
    meta.seed = m_world.getSeed();
    meta.spawnX = m_spawnPosition.x;
    meta.spawnY = m_spawnPosition.y;
    meta.spawnZ = m_spawnPosition.z;
    meta.timeOfDay = m_world.getDayNightSystem().getTimeOfDay();
    meta.totalGameTime = m_world.getDayNightSystem().getTotalGameTime();
    meta.elapsedDays = m_world.getDayNightSystem().getElapsedDays();

    const auto& weather = m_world.getWeatherSystem().getTargetState();
    meta.weatherType = weatherTypeToString(weather.type);
    meta.weatherWetness = weather.wetness;
    meta.weatherStorm = weather.storm;
    meta.weatherAerialReduction = weather.aerialReduction;

    m_saveManager->saveLevelMeta(meta);
}

void GameServer::acceptClient(std::unique_ptr<net::ITransportEndpoint> transport, net::ClientId id) {
    ConnectedClient client;
    client.id = id;
    client.transport = std::move(transport);
    client.lastPosition = m_spawnPosition;
    client.playerNetId = kPlayerNetIdBase | id;
    client.isAdmin = id == 1;
    m_clients.push_back(std::move(client));
    std::printf("[Server] Accepted transport slot for client %u admin=%d\n", id, id == 1 ? 1 : 0);
    std::fflush(stdout);
}

void GameServer::tick(float dt) {
    (void)dt;

    // Process incoming client messages first so block edits and player poses
    // participate in this tick's world/light update before snapshots are sent.
    processClientMessages();
    cleanupDisconnectedClients();
    tickWorldSystems();

    // Periodic autosave
    if (m_saveManager) {
        m_autosaveTimer += dt;
        if (m_autosaveTimer >= AUTOSAVE_INTERVAL_SECONDS) {
            m_autosaveTimer = 0.0f;
            m_world.flushSaves();
            saveLevelMeta();
        }
    }

    glm::vec3 loadCenter = m_spawnPosition;
    for (const auto& client : m_clients) {
        if (client.receivedHello) {
            loadCenter = client.lastPosition;
            break;
        }
    }
    m_world.update(loadCenter);

    // Send new chunks to clients
    sendNewChunksToClients();

    // Send authoritative snapshots to clients
    sendSnapshotsToClients();

    // Sync entities (spawn/despawn/snapshot)
    syncEntitiesToClients();
    syncPlayersToClients();

    // Send pending block updates to clients
    sendBlockUpdatesToClients();

    // Check if spawn chunks are ready
    if (!m_spawnChunksReady) {
        checkSpawnChunksReady();
    }

    ++m_currentTick;
}

void GameServer::tickWorldSystems() {
    m_world.fluidSystem().processScheduledBlockTicks(m_currentTick, 4096);
    ecs::BlockSupportSystem::processWorldQueue(m_world, 1024);
}

void GameServer::processClientMessages() {
    for (auto& client : m_clients) {
        net::Packet packet;
        while (client.transport->tryReceive(packet)) {
            switch (packet.type) {
            case net::MessageType::ClientHello: {
                client.receivedHello = true;
                client.helloTick = m_currentTick;
                client.sentChunks.clear();
                client.chunkSendLogCount = 0;
                client.totalChunksSent = 0;
                if (packet.inProcessPayload.has_value()) {
                    const auto& hello = std::any_cast<const net::ClientHello&>(packet.inProcessPayload);
                    std::printf("[Server] ClientHello client=%u protocol=%u\n",
                                client.id,
                                hello.protocolVersion);
                } else {
                    std::printf("[Server] ClientHello client=%u without decoded payload\n", client.id);
                }
                std::fflush(stdout);

                // Respond with ServerHello
                net::Packet response;
                response.channel = net::PacketChannel::ReliableControl;
                response.type = net::MessageType::ServerHello;
                net::ServerHello hello;
                hello.protocolVersion = 1;
                hello.assignedId = client.id;
                hello.spawnPosition = m_spawnPosition;
                response.inProcessPayload = hello;
                client.transport->send(std::move(response));
                sendSystemMessage(client,
                                  "Connected as " + playerName(client.id) +
                                      (client.isAdmin ? " (admin)" : ""),
                                  net::ChatMessageKind::Success);
                broadcastWorldState();
                broadcastPlayerMode(client.id, client.gameplayMode);
                break;
            }
            case net::MessageType::ClientInput: {
                if (packet.inProcessPayload.has_value()) {
                    const auto& input = std::any_cast<const net::ClientInput&>(packet.inProcessPayload);
                    client.lastAckedInput = input.sequence;
                    client.lastPosition = input.playerPosition;
                    client.lastVelocity = input.playerVelocity;
                    client.lastYaw = input.yaw;
                    client.lastPitch = input.pitch;
                }
                break;
            }
            case net::MessageType::ClientReady:
                // Client is ready to receive world data; nothing special needed.
                break;
            case net::MessageType::ClientViewConfig: {
                client.receivedViewConfig = true;
                if (packet.inProcessPayload.has_value()) {
                    const auto& config = std::any_cast<const net::ClientViewConfig&>(packet.inProcessPayload);
                    client.viewDistance = std::max(1, config.renderDistance);
                    std::printf("[Server] ClientViewConfig client=%u renderDistance=%d\n",
                                client.id,
                                client.viewDistance);
                    std::fflush(stdout);
                }
                break;
            }
            case net::MessageType::ClientBlockAction: {
                if (packet.inProcessPayload.has_value()) {
                    const auto& action = std::any_cast<const net::ClientBlockAction&>(packet.inProcessPayload);
                    handleClientBlockAction(client, action);
                }
                break;
            }
            case net::MessageType::ClientChatMessage: {
                if (packet.inProcessPayload.has_value()) {
                    const auto& message = std::any_cast<const net::ClientChatMessage&>(packet.inProcessPayload);
                    handleClientChatMessage(client, message);
                }
                break;
            }
            case net::MessageType::ClientCommandRequest: {
                if (packet.inProcessPayload.has_value()) {
                    const auto& request = std::any_cast<const net::ClientCommandRequest&>(packet.inProcessPayload);
                    handleClientCommandRequest(client, request);
                }
                break;
            }
            default:
                break;
            }
        }
    }
}

void GameServer::broadcastPlayerDespawn(const net::EntityNetId playerNetId, const net::ClientId exceptClientId) {
    if (playerNetId == 0) {
        return;
    }

    for (auto& client : m_clients) {
        client.spawnedPlayerNetIds.erase(playerNetId);
    }

    for (auto& client : m_clients) {
        if (client.id == exceptClientId || !client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
            continue;
        }

        net::Packet packet;
        packet.channel = net::PacketChannel::ReliableWorld;
        packet.type = net::MessageType::EntityDespawn;
        net::EntityDespawnMessage msg;
        msg.netId = playerNetId;
        packet.inProcessPayload = msg;
        client.transport->send(std::move(packet));
    }
}

void GameServer::cleanupDisconnectedClients() {
    for (auto& client : m_clients) {
        if (!client.transport || client.transport->isConnected()) {
            continue;
        }

        if (client.receivedHello) {
            std::printf("[Server] Removing disconnected client %u netId=%u\n",
                        client.id,
                        client.playerNetId);
            std::fflush(stdout);
            broadcastPlayerDespawn(client.playerNetId, client.id);
        }
        client.receivedHello = false;
    }

    m_clients.erase(std::remove_if(m_clients.begin(), m_clients.end(), [](const ConnectedClient& client) {
        return !client.transport || !client.transport->isConnected();
    }), m_clients.end());

    for (auto& client : m_clients) {
        if (!client.receivedHello || !client.transport || client.transport->hasActiveRemote()) {
            continue;
        }

        std::printf("[Server] Client %u lost remote peer netId=%u\n",
                    client.id,
                    client.playerNetId);
        std::fflush(stdout);
        broadcastPlayerDespawn(client.playerNetId, client.id);
        client.receivedHello = false;
        client.receivedViewConfig = false;
        client.lastAckedInput = 0;
        client.helloTick = 0;
        client.sentChunks.clear();
        client.spawnedPlayerNetIds.clear();
        client.chunkSendLogCount = 0;
        client.totalChunksSent = 0;
        client.lastPosition = m_spawnPosition;
        client.lastVelocity = glm::vec3(0.0f);
        client.lastYaw = 0.0f;
        client.lastPitch = 0.0f;
    }
}

ConnectedClient* GameServer::findClient(const net::ClientId id) {
    for (auto& client : m_clients) {
        if (client.id == id) {
            return &client;
        }
    }
    return nullptr;
}

void GameServer::handleClientChatMessage(ConnectedClient& client, const net::ClientChatMessage& message) {
    std::string text = trimCommand(message.message);
    if (text.empty()) {
        return;
    }
    if (text.size() > 240) {
        text.resize(240);
    }

    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableChat;
    packet.type = net::MessageType::ServerChatMessage;
    net::ServerChatMessage out;
    out.senderId = client.id;
    out.senderName = playerName(client.id);
    out.message = text;
    packet.inProcessPayload = std::move(out);

    for (auto& receiver : m_clients) {
        if (!receiver.receivedHello || !receiver.transport || !receiver.transport->hasActiveRemote()) {
            continue;
        }
        net::Packet copy = packet;
        receiver.transport->send(std::move(copy));
    }
}

void GameServer::handleClientCommandRequest(ConnectedClient& client, const net::ClientCommandRequest& request) {
    executeServerCommand(client, request);
}

void GameServer::executeServerCommand(ConnectedClient& client, const net::ClientCommandRequest& request) {
    std::string command = trimCommand(request.command);
    if (!command.empty() && command.front() == '/') {
        command.erase(command.begin());
    }
    std::istringstream iss(command);
    std::string primary;
    iss >> primary;

    if (primary.empty()) {
        sendCommandResult(client, request.sequence, false, "Empty command.");
        return;
    }

    if (primary == "help") {
        sendCommandResult(client,
                          request.sequence,
                          true,
                          "Commands: /help, /list, /gamemode <survival|creative> [clientId], /time set <0..1200>, /weather <clear|rain|storm|snow>");
        return;
    }

    if (primary == "list") {
        std::string list = "Online players:";
        for (const auto& online : m_clients) {
            if (online.receivedHello && online.transport && online.transport->hasActiveRemote()) {
                list += " " + playerName(online.id) + "(" + std::to_string(online.id) + ")";
            }
        }
        sendCommandResult(client, request.sequence, true, list);
        return;
    }

    if (!client.isAdmin) {
        sendCommandResult(client, request.sequence, false, "You do not have permission to use this command.");
        return;
    }

    if (primary == "gamemode") {
        std::string modeStr;
        iss >> modeStr;
        net::NetworkGameplayMode mode = net::NetworkGameplayMode::Survival;
        if (!parseGameplayMode(modeStr, mode)) {
            sendCommandResult(client, request.sequence, false, "Usage: /gamemode <survival|creative> [clientId]");
            return;
        }

        net::ClientId targetId = client.id;
        std::string idStr;
        iss >> idStr;
        if (!idStr.empty()) {
            char* endPtr = nullptr;
            const unsigned long parsed = std::strtoul(idStr.c_str(), &endPtr, 10);
            if (endPtr == idStr.c_str() || *endPtr != '\0') {
                sendCommandResult(client, request.sequence, false, "Invalid client id.");
                return;
            }
            targetId = static_cast<net::ClientId>(parsed);
        }

        ConnectedClient* target = findClient(targetId);
        if (!target || !target->receivedHello) {
            sendCommandResult(client, request.sequence, false, "Target client is not online.");
            return;
        }

        target->gameplayMode = mode;
        broadcastPlayerMode(target->id, mode);
        const std::string message = "Set " + playerName(target->id) + " to " + modeName(mode) + " mode.";
        sendCommandResult(client, request.sequence, true, message);
        broadcastSystemMessage(message, net::ChatMessageKind::Success);
        return;
    }

    if (primary == "time") {
        std::string secondary;
        iss >> secondary;
        if (secondary != "set") {
            sendCommandResult(client, request.sequence, false, "Usage: /time set <0..1200>");
            return;
        }
        std::string valueStr;
        iss >> valueStr;
        char* endPtr = nullptr;
        const float value = std::strtof(valueStr.c_str(), &endPtr);
        if (valueStr.empty() || endPtr == valueStr.c_str() || *endPtr != '\0' || value < 0.0f || value > 1200.0f) {
            sendCommandResult(client, request.sequence, false, "Usage: /time set <0..1200>");
            return;
        }

        m_world.getDayNightSystem().setTimeOfDay(value);
        broadcastWorldState();
        const std::string message = "Time set to " + std::to_string(static_cast<int>(value)) + ".";
        sendCommandResult(client, request.sequence, true, message);
        broadcastSystemMessage(message, net::ChatMessageKind::Success);
        return;
    }

    if (primary == "weather") {
        std::string value;
        iss >> value;
        WeatherType weather = WeatherType::Clear;
        if (!parseWeatherType(value, weather)) {
            sendCommandResult(client, request.sequence, false, "Usage: /weather <clear|rain|storm|snow>");
            return;
        }

        m_world.getWeatherSystem().setDebugWeatherPresetInstant(weather);
        broadcastWorldState();
        const std::string message = std::string("Weather set to ") + weatherName(weather) + ".";
        sendCommandResult(client, request.sequence, true, message);
        broadcastSystemMessage(message, net::ChatMessageKind::Success);
        return;
    }

    sendCommandResult(client, request.sequence, false, "Unknown command: " + primary);
}

void GameServer::sendCommandResult(ConnectedClient& client,
                                   const uint32_t sequence,
                                   const bool success,
                                   const std::string& message) {
    if (!client.transport || !client.transport->hasActiveRemote()) {
        return;
    }
    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableChat;
    packet.type = net::MessageType::CommandResult;
    net::CommandResultMessage result;
    result.sequence = sequence;
    result.success = success;
    result.message = message;
    packet.inProcessPayload = std::move(result);
    client.transport->send(std::move(packet));
}

void GameServer::sendSystemMessage(ConnectedClient& client,
                                   const std::string& message,
                                   const net::ChatMessageKind kind) {
    if (!client.transport || !client.transport->hasActiveRemote()) {
        return;
    }
    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableChat;
    packet.type = net::MessageType::ServerSystemMessage;
    net::ServerSystemMessage system;
    system.kind = kind;
    system.message = message;
    packet.inProcessPayload = std::move(system);
    client.transport->send(std::move(packet));
}

void GameServer::broadcastSystemMessage(const std::string& message, const net::ChatMessageKind kind) {
    for (auto& client : m_clients) {
        if (!client.receivedHello) {
            continue;
        }
        sendSystemMessage(client, message, kind);
    }
}

void GameServer::broadcastWorldState() {
    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableWorld;
    packet.type = net::MessageType::WorldStateSnapshot;
    net::WorldStateSnapshotMessage snapshot;
    snapshot.timeOfDay = m_world.getDayNightSystem().getTimeOfDay();
    snapshot.weather = toNetworkWeather(m_world.getWeatherSystem().getTargetState().type);
    packet.inProcessPayload = snapshot;

    for (auto& client : m_clients) {
        if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
            continue;
        }
        net::Packet copy = packet;
        client.transport->send(std::move(copy));
    }
}

void GameServer::broadcastPlayerMode(const net::ClientId clientId, const net::NetworkGameplayMode mode) {
    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableWorld;
    packet.type = net::MessageType::PlayerModeUpdate;
    net::PlayerModeUpdateMessage update;
    update.clientId = clientId;
    update.mode = mode;
    packet.inProcessPayload = update;

    for (auto& client : m_clients) {
        if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
            continue;
        }
        net::Packet copy = packet;
        client.transport->send(std::move(copy));
    }
}

void GameServer::handleClientBlockAction(ConnectedClient& client, const net::ClientBlockAction& action) {
    constexpr float kMaxActionDistance = 6.5f;
    const glm::ivec3 actionBlock = action.action == net::ClientBlockActionType::Place
        ? action.placeBlock
        : action.targetBlock;
    const glm::vec3 blockCenter = glm::vec3(actionBlock) + glm::vec3(0.5f);
    const glm::vec3 diff = action.playerPosition - blockCenter;
    const float distSq = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
    if (distSq > kMaxActionDistance * kMaxActionDistance) {
        return;
    }

    client.lastPosition = action.playerPosition;

    if (action.action == net::ClientBlockActionType::Break) {
        const BlockID target = m_world.getBlock(action.targetBlock.x, action.targetBlock.y, action.targetBlock.z);
        if (target == BlockIds::AIR || !BlockRegistry::get(target).isSelectable) {
            return;
        }
        m_world.setBlock(action.targetBlock.x, action.targetBlock.y, action.targetBlock.z, BlockIds::AIR);
        std::printf("[Server] ClientBlockAction break client=%u block=(%d,%d,%d)\n",
                    client.id,
                    action.targetBlock.x,
                    action.targetBlock.y,
                    action.targetBlock.z);
        std::fflush(stdout);
        return;
    }

    if (action.blockState == BlockIds::AIR) {
        return;
    }
    if (m_world.getBlock(action.placeBlock.x, action.placeBlock.y, action.placeBlock.z) != BlockIds::AIR) {
        return;
    }

    m_world.setBlock(action.placeBlock.x,
                     action.placeBlock.y,
                     action.placeBlock.z,
                     static_cast<BlockID>(action.blockState));
    std::printf("[Server] ClientBlockAction place client=%u block=(%d,%d,%d) state=%u\n",
                client.id,
                action.placeBlock.x,
                action.placeBlock.y,
                action.placeBlock.z,
                static_cast<unsigned>(action.blockState));
    std::fflush(stdout);
}

void GameServer::sendNewChunksToClients() {
    for (auto& client : m_clients) {
        if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote() ||
            client.helloTick == m_currentTick) {
            continue;
        }

        const auto& activeChunks = m_world.getActiveChunks();

        // Build a temporary ticket manager for this client's view distance
        ChunkTicketManager clientTicketMgr;
        clientTicketMgr.setViewRadius(client.viewDistance);
        clientTicketMgr.setSimulationRadius(m_world.ticketManager().simulationRadius());
        const int playerChunkX = static_cast<int>(std::floor(client.lastPosition.x / 16.0f));
        const int playerChunkZ = static_cast<int>(std::floor(client.lastPosition.z / 16.0f));
        clientTicketMgr.updatePlayerPosition(playerChunkX, playerChunkZ);

        // Send initial spawn chunks aggressively so the client can enable
        // physics only after enough terrain is present.
        const int maxChunkSendsPerTick = client.totalChunksSent < 81 ? 96 : 24;
        int sent = 0;

        // Get prioritized chunks to send
        const auto chunksToSend = clientTicketMgr.getChunksToLoad(
            maxChunkSendsPerTick * 2, client.sentChunks);

        for (const auto& pos : chunksToSend) {
            if (sent >= maxChunkSendsPerTick) break;

            const int64_t key = ChunkTicketManager::chunkKey(pos.x, pos.y);
            auto it = activeChunks.find(key);
            if (it != activeChunks.end() && it->second) {
                sendChunkDataToClient(client, pos.x, pos.y);
                client.sentChunks.insert(key);
                ++sent;
            }
        }

        // Unload chunks outside the client's unload radius
        std::vector<int64_t> toUnload;
        for (const int64_t key : client.sentChunks) {
            const int cx = static_cast<int>(key >> 32);
            const int cz = static_cast<int>(static_cast<int32_t>(key & 0xFFFFFFFF));
            if (clientTicketMgr.shouldUnload(cx, cz)) {
                toUnload.push_back(key);
            }
        }

        for (const int64_t key : toUnload) {
            const int cx = static_cast<int>(key >> 32);
            const int cz = static_cast<int>(static_cast<int32_t>(key & 0xFFFFFFFF));

            net::Packet packet;
            packet.channel = net::PacketChannel::ReliableWorld;
            packet.type = net::MessageType::ChunkUnload;
            net::ChunkUnloadMessage unloadMsg;
            unloadMsg.chunkX = cx;
            unloadMsg.chunkZ = cz;
            packet.inProcessPayload = unloadMsg;
            client.transport->send(std::move(packet));

            client.sentChunks.erase(key);
        }
    }
}

void GameServer::sendSnapshotsToClients() {
    for (auto& client : m_clients) {
        if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
            continue;
        }
        net::Packet packet;
        packet.channel = net::PacketChannel::UnreliableState;
        packet.type = net::MessageType::ServerSnapshot;
        net::ServerSnapshot snapshot;
        snapshot.serverTick = m_currentTick;
        snapshot.ackInputSequence = client.lastAckedInput;
        snapshot.authoritativePosition = m_spawnPosition;
        snapshot.authoritativeVelocity = glm::vec3(0.0f);
        packet.inProcessPayload = snapshot;
        client.transport->send(std::move(packet));
    }
}

void GameServer::sendBlockUpdatesToClients() {
    if (m_pendingBlockUpdates.empty()) {
        return;
    }

    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableWorld;
    packet.type = net::MessageType::BlockUpdateBatch;
    net::BlockUpdateBatchMessage batch;
    batch.updates = std::move(m_pendingBlockUpdates);
    packet.inProcessPayload = std::move(batch);

    for (auto& client : m_clients) {
        if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
            continue;
        }
        // Send a copy to each client
        net::Packet clientPacket = packet;
        client.transport->send(std::move(clientPacket));
    }

    m_pendingBlockUpdates.clear();
}

net::BlockUpdateEntry GameServer::makeBlockOnlyUpdateEntry(const int x, const int y, const int z, const BlockID blockId) const {
    net::BlockUpdateEntry entry;
    entry.x = x;
    entry.y = y;
    entry.z = z;
    entry.blockId = static_cast<uint16_t>(blockId);
    return entry;
}

net::BlockUpdateEntry GameServer::makeSubChunkLightUpdateEntry(const int64_t chunkKey, const int scy) const {
    net::BlockUpdateEntry entry;
    const auto chunkIt = m_world.getActiveChunks().find(chunkKey);
    if (scy < 0 || scy >= Chunk::NUM_SUB_CHUNKS ||
        chunkIt == m_world.getActiveChunks().end() || !chunkIt->second) {
        return entry;
    }

    const Chunk& chunk = *chunkIt->second;
    entry.x = chunk.m_chunkX * Chunk::SIZE_X;
    entry.y = scy * SubChunk::SIZE;
    entry.z = chunk.m_chunkZ * Chunk::SIZE_Z;
    entry.blockId = kLightOnlyBlockUpdate;
    entry.packedLightPatch.resize(SubChunk::BLOCK_COUNT);
    size_t index = 0;
    const int yBase = scy * SubChunk::SIZE;
    for (int ly = 0; ly < SubChunk::SIZE; ++ly) {
        const int yPos = yBase + ly;
        for (int zPos = 0; zPos < Chunk::SIZE_Z; ++zPos) {
            for (int xPos = 0; xPos < Chunk::SIZE_X; ++xPos) {
                entry.packedLightPatch[index++] =
                    chunk.getPackedLight(xPos, yPos, zPos);
            }
        }
    }
    return entry;
}

net::BlockUpdateEntry GameServer::makeBlockUpdateEntry(const int x,
                                                       const int y,
                                                       const int z,
                                                       const BlockID blockId,
                                                       const int lightPatchRadius) const {
    net::BlockUpdateEntry entry;
    entry.x = x;
    entry.y = y;
    entry.z = z;
    entry.blockId = static_cast<uint16_t>(blockId);
    if (lightPatchRadius < 0) {
        const glm::ivec2 chunkCoords = m_world.getChunkCoords(x, z);
        return makeSubChunkLightUpdateEntry(World::chunkKey(chunkCoords.x, chunkCoords.y),
                                            Chunk::toSubChunkIndex(y));
    }
    const int patchSide = lightPatchRadius * 2 + 1;
    entry.packedLightPatch.reserve(static_cast<size_t>(patchSide * patchSide * patchSide));
    for (int dy = -lightPatchRadius; dy <= lightPatchRadius; ++dy) {
        for (int dz = -lightPatchRadius; dz <= lightPatchRadius; ++dz) {
            for (int dx = -lightPatchRadius; dx <= lightPatchRadius; ++dx) {
                entry.packedLightPatch.push_back(m_world.getPackedLight(x + dx, y + dy, z + dz));
            }
        }
    }
    return entry;
}

void GameServer::syncPlayersToClients() {
    if (m_clients.size() < 2) {
        return;
    }

    for (auto& receiver : m_clients) {
        if (!receiver.receivedHello || !receiver.transport || !receiver.transport->hasActiveRemote()) {
            continue;
        }

        for (auto& other : m_clients) {
            if (other.id == receiver.id || !other.receivedHello || !other.transport || !other.transport->hasActiveRemote()) {
                continue;
            }

            if (receiver.spawnedPlayerNetIds.insert(other.playerNetId).second) {
                net::Packet spawnPacket;
                spawnPacket.channel = net::PacketChannel::ReliableWorld;
                spawnPacket.type = net::MessageType::EntitySpawn;
                net::EntitySpawnMessage spawn;
                spawn.netId = other.playerNetId;
                spawn.kind = net::EntityKind::Player;
                spawn.position = other.lastPosition;
                spawn.velocity = other.lastVelocity;
                spawn.yaw = other.lastYaw;
                spawn.pitch = other.lastPitch;
                spawnPacket.inProcessPayload = spawn;
                receiver.transport->send(std::move(spawnPacket));
                std::printf("[Server] Sent PlayerSpawn receiver=%u sourceClient=%u netId=%u\n",
                            receiver.id,
                            other.id,
                            other.playerNetId);
                std::fflush(stdout);
            }
        }
    }

    net::EntitySnapshotMessage snapshot;
    snapshot.serverTick = m_currentTick;
    for (const auto& client : m_clients) {
        if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
            continue;
        }
        net::EntitySnapshotItem item;
        item.netId = client.playerNetId;
        item.position = client.lastPosition;
        item.velocity = client.lastVelocity;
        item.yaw = client.lastYaw;
        item.pitch = client.lastPitch;
        snapshot.entities.push_back(item);
    }
    if (snapshot.entities.empty()) {
        return;
    }

    for (auto& receiver : m_clients) {
        if (!receiver.receivedHello || !receiver.transport || !receiver.transport->hasActiveRemote()) {
            continue;
        }
        net::EntitySnapshotMessage filtered;
        filtered.serverTick = snapshot.serverTick;
        for (const auto& item : snapshot.entities) {
            if (item.netId != receiver.playerNetId) {
                filtered.entities.push_back(item);
            }
        }
        if (filtered.entities.empty()) {
            continue;
        }
        net::Packet packet;
        packet.channel = net::PacketChannel::UnreliableState;
        packet.type = net::MessageType::EntitySnapshot;
        packet.inProcessPayload = std::move(filtered);
        receiver.transport->send(std::move(packet));
    }
}

void GameServer::syncEntitiesToClients() {
    if (!m_ecsRegistry || m_clients.empty()) {
        return;
    }

    auto& reg = *m_ecsRegistry;

    // 1. Detect new entities with NetworkSyncTag that don't have EntityNetId yet
    auto newSyncView = reg.view<ecs::NetworkSyncTag>(entt::exclude<ecs::EntityNetIdComponent>);
    for (auto entity : newSyncView) {
        reg.emplace<ecs::EntityNetIdComponent>(entity, m_nextNetId);
        m_syncedEntities[m_nextNetId] = entity;
        ++m_nextNetId;
    }

    // 2. Detect despawned entities (entities in m_syncedEntities that are no longer valid)
    std::vector<ecs::EntityNetId> toDespawn;
    for (const auto& [netId, entity] : m_syncedEntities) {
        if (!reg.valid(entity)) {
            toDespawn.push_back(netId);
        }
    }

    // Send despawn messages and clean up tracking
    for (const ecs::EntityNetId netId : toDespawn) {
        for (auto& client : m_clients) {
            if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
                continue;
            }
            net::Packet packet;
            packet.channel = net::PacketChannel::ReliableWorld;
            packet.type = net::MessageType::EntityDespawn;
            net::EntityDespawnMessage msg;
            msg.netId = netId;
            packet.inProcessPayload = msg;
            client.transport->send(std::move(packet));
        }
        m_syncedEntities.erase(netId);
    }

    // 3. Build and send entity snapshots (batch of all synced entities)
    net::EntitySnapshotMessage snapshot;
    snapshot.serverTick = m_currentTick;

    for (const auto& [netId, entity] : m_syncedEntities) {
        if (!reg.valid(entity)) continue;

        auto* transform = reg.try_get<ecs::TransformComponent>(entity);
        if (!transform) continue;

        net::EntitySnapshotItem item;
        item.netId = netId;
        item.position = transform->position;

        auto* velocity = reg.try_get<ecs::VelocityComponent>(entity);
        if (velocity) {
            item.velocity = velocity->velocity;
        }

        auto* spin = reg.try_get<ecs::SpinVisualComponent>(entity);
        if (spin) {
            item.yaw = spin->yawRadians;
        }

        snapshot.entities.push_back(item);
    }

    if (!snapshot.entities.empty()) {
        net::Packet packet;
        packet.channel = net::PacketChannel::UnreliableState;
        packet.type = net::MessageType::EntitySnapshot;
        packet.inProcessPayload = std::move(snapshot);

        for (auto& client : m_clients) {
            if (!client.receivedHello || !client.transport || !client.transport->hasActiveRemote()) {
                continue;
            }
            net::Packet clientPacket = packet;
            client.transport->send(std::move(clientPacket));
        }
    }
}

void GameServer::sendChunkDataToClient(ConnectedClient& client, int cx, int cz) {
    const int64_t key = World::chunkKey(cx, cz);
    const auto& activeChunks = m_world.getActiveChunks();
    auto it = activeChunks.find(key);
    if (it == activeChunks.end() || !it->second) {
        return;
    }

    net::Packet packet;
    packet.channel = net::PacketChannel::ReliableWorld;
    packet.type = net::MessageType::ChunkData;
    net::ChunkDataMessage data;
    data.chunkX = cx;
    data.chunkZ = cz;
    data.revision = static_cast<uint32_t>(m_world.getActiveChunkRevision());
    data.chunk = it->second;  // Zero-copy: share the Chunk pointer
    packet.inProcessPayload = std::move(data);
    client.transport->send(std::move(packet));

    ++client.totalChunksSent;
    if (client.chunkSendLogCount < 12 || client.totalChunksSent % 25 == 0) {
        std::printf("[Server] Sent ChunkData client=%u chunk=(%d,%d) total=%d active=%zu\n",
                    client.id,
                    cx,
                    cz,
                    client.totalChunksSent,
                    m_world.getActiveChunks().size());
        std::fflush(stdout);
        ++client.chunkSendLogCount;
    }
}

void GameServer::checkSpawnChunksReady() {
    // Consider spawn chunks ready when a 5x5 area around (0,0) is loaded
    constexpr int kSpawnRadius = 2;
    int loadedCount = 0;
    const int totalNeeded = (2 * kSpawnRadius + 1) * (2 * kSpawnRadius + 1);

    for (int dx = -kSpawnRadius; dx <= kSpawnRadius; ++dx) {
        for (int dz = -kSpawnRadius; dz <= kSpawnRadius; ++dz) {
            const int64_t key = World::chunkKey(dx, dz);
            if (m_world.getActiveChunks().count(key) > 0) {
                ++loadedCount;
            }
        }
    }

    if (loadedCount >= totalNeeded) {
        m_spawnChunksReady = true;
    }
}

} // namespace server
