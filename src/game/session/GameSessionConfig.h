#ifndef MECRAFT_GAME_SESSION_CONFIG_H
#define MECRAFT_GAME_SESSION_CONFIG_H

#include <glm/glm.hpp>
#include <filesystem>
#include <string>

class Window;
class InputManager;
class ActionMap;
class InputContextManager;
class ResourceMgr;
class AudioEngine;
class BgmSystem;
class UIRenderer;
class LocaleManager;
class ThreadPool;

/// Configuration for a gameplay session (seed, render distance, etc.)
struct GameSessionConfig {
    int seed = 1234;
    int renderDistance = 16;
    glm::vec3 debugMobOffset = glm::vec3(5.0f, 0.0f, 0.0f);

    /// Multiplayer settings. Empty serverAddress means single-player (local server).
    std::string serverAddress;
    uint16_t serverPort = 25565;
    bool isMultiplayer() const { return !serverAddress.empty(); }

    /// Save system settings.
    std::string worldName;              // Empty = no save (ephemeral world)
    std::filesystem::path saveRoot;     // Root directory for all saves (e.g. "saves/")
    bool enableSaving = true;
};

/// External service dependencies required by a gameplay session.
/// All references are non-owning; lifetime is managed by the caller (GameManager).
struct GameSessionDependencies {
    Window& window;
    InputManager& input;
    ActionMap& actionMap;
    InputContextManager& contextManager;
    ResourceMgr& resourceMgr;
    AudioEngine& audioEngine;
    BgmSystem& bgmSystem;
    UIRenderer& uiRenderer;
    LocaleManager& localeManager;
    ThreadPool& threadPool;
};

#endif // MECRAFT_GAME_SESSION_CONFIG_H
