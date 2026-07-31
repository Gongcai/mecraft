#ifndef MECRAFT_GAME_RESOURCE_BOOTSTRAPPER_H
#define MECRAFT_GAME_RESOURCE_BOOTSTRAPPER_H

class ResourceMgr;
class RhiDevice;
class RhiCommandListPool;

namespace app {

[[nodiscard]] bool bootstrapGameResources(ResourceMgr& resourceMgr, RhiDevice& rhiDevice,
                                          RhiCommandListPool& commandListPool);

} // namespace app

#endif // MECRAFT_GAME_RESOURCE_BOOTSTRAPPER_H
