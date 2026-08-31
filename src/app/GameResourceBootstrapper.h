#ifndef MECRAFT_GAME_RESOURCE_BOOTSTRAPPER_H
#define MECRAFT_GAME_RESOURCE_BOOTSTRAPPER_H

struct GameResources;
class RhiDevice;
class RhiCommandListPool;

namespace app {

[[nodiscard]] bool bootstrapGameResources(GameResources& resources, RhiDevice& rhiDevice,
                                          RhiCommandListPool& commandListPool);

} // namespace app

#endif // MECRAFT_GAME_RESOURCE_BOOTSTRAPPER_H
