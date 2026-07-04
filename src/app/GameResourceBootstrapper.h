#ifndef MECRAFT_GAME_RESOURCE_BOOTSTRAPPER_H
#define MECRAFT_GAME_RESOURCE_BOOTSTRAPPER_H

class ResourceMgr;

namespace app {

[[nodiscard]] bool bootstrapGameResources(ResourceMgr& resourceMgr);

} // namespace app

#endif // MECRAFT_GAME_RESOURCE_BOOTSTRAPPER_H
