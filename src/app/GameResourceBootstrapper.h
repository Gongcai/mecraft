#ifndef MECRAFT_GAME_RESOURCE_BOOTSTRAPPER_H
#define MECRAFT_GAME_RESOURCE_BOOTSTRAPPER_H

class ResourceMgr;
class RhiDevice;

namespace app {

[[nodiscard]] bool bootstrapGameResources(ResourceMgr& resourceMgr, RhiDevice& rhiDevice);

} // namespace app

#endif // MECRAFT_GAME_RESOURCE_BOOTSTRAPPER_H
