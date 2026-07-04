#ifndef MECRAFT_ENTITY_TEXTURE_PRELOADER_H
#define MECRAFT_ENTITY_TEXTURE_PRELOADER_H

#include <string>

class ResourceMgr;

namespace resource {

[[nodiscard]] bool preloadEntityTexturesFromConfig(ResourceMgr& resourceMgr, const std::string& entitiesConfigPath);

} // namespace resource

#endif // MECRAFT_ENTITY_TEXTURE_PRELOADER_H
