#ifndef MECRAFT_ENTITY_TEXTURE_PRELOADER_H
#define MECRAFT_ENTITY_TEXTURE_PRELOADER_H

#include <string>

struct GameResources;

namespace resource {

[[nodiscard]] bool preloadEntityTexturesFromConfig(GameResources& resources, const std::string& entitiesConfigPath);

} // namespace resource

#endif // MECRAFT_ENTITY_TEXTURE_PRELOADER_H
