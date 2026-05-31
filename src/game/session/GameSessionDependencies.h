#ifndef MECRAFT_GAME_SESSION_DEPENDENCIES_H
#define MECRAFT_GAME_SESSION_DEPENDENCIES_H

class AudioEngine;
class InputContextManager;
class ResourceMgr;
class UIRenderer;
class LocaleManager;

/// External service pointers needed by GameSession for ECS initialization.
/// All pointers are non-owning; lifetime is managed by the caller (GameManager/Game).
struct ExternalEcsServices {
    AudioEngine* audioEngine = nullptr;
    InputContextManager* inputContextManager = nullptr;
    ResourceMgr* resourceMgr = nullptr;
    UIRenderer* uiRenderer = nullptr;
    LocaleManager* localeManager = nullptr;
};

#endif // MECRAFT_GAME_SESSION_DEPENDENCIES_H
