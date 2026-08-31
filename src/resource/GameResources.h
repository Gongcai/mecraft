#ifndef MECRAFT_GAME_RESOURCES_H
#define MECRAFT_GAME_RESOURCES_H

#include "BlockTextureLibrary.h"
#include "CubemapLibrary.h"
#include "EnvironmentTextureLibrary.h"
#include "Texture2DLibrary.h"
#include "UiTextureAtlasLibrary.h"

class RhiDevice;
class RhiCommandListPool;

/// Aggregate that owns the five texture sub-libraries used by the game.
///
/// Each member library has a single, well-defined responsibility. Consumers depend on
/// the specific library they need and reach it through this aggregate, e.g.
/// `resources.blockTextures.textureArray()` or `resources.texture2D.getHandle(name)`.
///
/// The aggregate deliberately exposes no GPU device or command-list-pool access; those
/// are injected directly into consumers that need them, so the texture system never acts
/// as a service locator for rendering infrastructure.
struct GameResources {
    GameResources() = default;
    GameResources(const GameResources&) = delete;
    GameResources& operator=(const GameResources&) = delete;

    /// Initializes every sub-library against the given device and command list pool.
    void init(RhiDevice& rhiDevice, RhiCommandListPool& commandListPool);

    /// Releases GPU resources held by every sub-library.
    void shutdown();

    Texture2DLibrary texture2D;
    BlockTextureLibrary blockTextures;
    UiTextureAtlasLibrary uiTextures;
    EnvironmentTextureLibrary environmentTextures;
    CubemapLibrary cubemaps;
};

#endif // MECRAFT_GAME_RESOURCES_H
