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

    if (argc > 1) port = static_cast<uint16_t>(std::atoi(argv[1]));
    if (argc > 2) seed = static_cast<uint32_t>(std::atoi(argv[2]));
    if (argc > 3) renderDistance = std::atoi(argv[3]);

    std::printf("=== Mecraft Dedicated Server ===\n");
    std::printf("Port: %d\n", port);
    std::printf("Seed: %u\n", seed);
    std::printf("Render Distance: %d\n", renderDistance);

    // Initialize ENet
    if (!net::ENetTransport::initialize()) {
        std::fprintf(stderr, "Failed to initialize ENet\n");
        return 1;
    }

    BlockRegistry::init(nullptr);

    // Create thread pool
    ThreadPool threadPool(4);

    // Create and initialize server
    server::GameServer server;
    server.init(seed, &threadPool, renderDistance);

    // Create ENet transport for listening
    auto listenTransport = std::make_unique<net::ENetTransport>();
    std::printf("Attempting to listen on port %d...\n", port);
    if (!listenTransport->listen(port, 32, 4)) {
        std::fprintf(stderr, "Failed to listen on port %d. Port may be in use or ENet failed.\n", port);
        net::ENetTransport::deinitialize();
        return 1;
    }
    server.acceptClient(std::move(listenTransport), 1);

    std::printf("Server listening on port %d\n", port);
    std::printf("Press Ctrl+C to stop\n\n");

    // Set up signal handler
#ifdef _WIN32
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
#else
    std::signal(SIGINT, signalHandler);
#endif

    // Server loop
    constexpr float kServerTickDt = 1.0f / 20.0f;
    constexpr auto kTickInterval = std::chrono::milliseconds(50);  // 20 TPS

    while (g_running) {
        const auto tickStart = std::chrono::steady_clock::now();

        // Run server tick
        server.tick(kServerTickDt);

        // Print status periodically
        static int statusCounter = 0;
        if (++statusCounter >= 200) {  // Every 10 seconds
            statusCounter = 0;
            std::printf("[Status] Tick: %u, Chunks: %zu\n",
                       server.currentTick(),
                       server.world().getActiveChunks().size());
        }

        // Sleep to maintain tick rate
        const auto tickEnd = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(tickEnd - tickStart);
        if (elapsed < kTickInterval) {
            std::this_thread::sleep_for(kTickInterval - elapsed);
        }
    }

    std::printf("\nShutting down...\n");

    // Cleanup
    net::ENetTransport::deinitialize();

    std::printf("Server stopped.\n");
    return 0;
}
