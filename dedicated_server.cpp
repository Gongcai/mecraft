/// Mecraft Dedicated Server
/// Runs a headless GameServer that clients can connect to via ENet.
///
/// Usage: mecraft_server [port] [seed] [render_distance]
///   port:           Listen port (default 25565)
///   seed:           World seed (default 1234)
///   render_distance: Server render distance (default 8)

#include "src/server/GameServer.h"
#include "src/net/ENetTransport.h"
#include "src/thread/ThreadPool.h"
#include "src/world/block/Block.h"
#include "src/Diagnostics.h"
#include <cstdio>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
static volatile bool g_running = true;
static BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    (void)ctrlType;
    g_running = false;
    return TRUE;
}
#else
#include <csignal>
static volatile bool g_running = true;
static void signalHandler(int sig) {
    (void)sig;
    g_running = false;
}
#endif

int main(int argc, char* argv[]) {
    // Parse arguments
    uint16_t port = 25565;
    uint32_t seed = 1234;
    int renderDistance = 8;

    if (argc > 1)
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    if (argc > 2)
        seed = static_cast<uint32_t>(std::atoi(argv[2]));
    if (argc > 3)
        renderDistance = std::atoi(argv[3]);

    MECRAFT_LOG_PRINTF("=== Mecraft Dedicated Server ===\n");
    MECRAFT_LOG_PRINTF("Port: %d\n", port);
    MECRAFT_LOG_PRINTF("Seed: %u\n", seed);
    MECRAFT_LOG_PRINTF("Render Distance: %d\n", renderDistance);

    // Initialize ENet
    if (!net::ENetTransport::initialize()) {
        MECRAFT_LOG_FPRINTF(stderr, "Failed to initialize ENet\n");
        return 1;
    }

    BlockRegistry::init(nullptr);

    // Create thread pool
    ThreadPool threadPool(4);
    threadPool.start();

    // Create ENet transport for listening
    auto listenTransport = std::make_unique<net::ENetTransport>();
    MECRAFT_LOG_PRINTF("Attempting to listen on port %d...\n", port);
    if (!listenTransport->listen(port, 32, 4)) {
        MECRAFT_LOG_FPRINTF(stderr, "Failed to listen on port %d. Port may be in use or ENet failed.\n", port);
        net::ENetTransport::deinitialize();
        return 1;
    }

    // Create and initialize server after the listener so peer endpoints are
    // destroyed before the shared ENet host on shutdown.
    server::GameServer server;
    server.init(seed, &threadPool, renderDistance);
    net::ClientId nextClientId = 1;

    MECRAFT_LOG_PRINTF("Server listening on port %d\n", port);
    MECRAFT_LOG_PRINTF("Press Ctrl+C to stop\n\n");

    // Set up signal handler
#ifdef _WIN32
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
#else
    std::signal(SIGINT, signalHandler);
#endif

    // Server loop
    constexpr float kServerTickDt = 1.0f / 20.0f;
    constexpr auto kTickInterval = std::chrono::milliseconds(50); // 20 TPS

    while (g_running) {
        const auto tickStart = std::chrono::steady_clock::now();

        while (auto endpoint = listenTransport->takeAcceptedEndpoint()) {
            server.acceptClient(std::move(endpoint), nextClientId++);
        }

        // Run server tick
        server.tick(kServerTickDt);

        // Print status periodically
        static int statusCounter = 0;
        if (++statusCounter >= 200) { // Every 10 seconds
            statusCounter = 0;
            MECRAFT_LOG_PRINTF("[Status] Tick: %u, Chunks: %zu\n", server.currentTick(),
                               server.world().getActiveChunks().size());
        }

        // Sleep to maintain tick rate
        const auto tickEnd = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(tickEnd - tickStart);
        if (elapsed < kTickInterval) {
            std::this_thread::sleep_for(kTickInterval - elapsed);
        }
    }

    MECRAFT_LOG_PRINTF("\nShutting down...\n");

    // Cleanup
    net::ENetTransport::deinitialize();

    MECRAFT_LOG_PRINTF("Server stopped.\n");
    return 0;
}
