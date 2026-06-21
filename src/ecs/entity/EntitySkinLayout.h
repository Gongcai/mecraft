#ifndef MECRAFT_ECS_ENTITY_SKIN_LAYOUT_H
#define MECRAFT_ECS_ENTITY_SKIN_LAYOUT_H

#include <string>
#include <string_view>

namespace ecs {

namespace EntitySkinLayoutIds {
inline constexpr std::string_view STEVE_64X64 = "minecraft:steve_64x64";
inline constexpr std::string_view CLASSIC_64X32 = "minecraft:classic_64x32";
} // namespace EntitySkinLayoutIds

inline std::string normalizeEntitySkinLayoutId(const std::string_view id) {
    if (id.empty()) {
        return std::string(EntitySkinLayoutIds::STEVE_64X64);
    }
    if (id == "steve_64x64") {
        return std::string(EntitySkinLayoutIds::STEVE_64X64);
    }
    if (id == "classic_64x32") {
        return std::string(EntitySkinLayoutIds::CLASSIC_64X32);
    }
    return std::string(id);
}

inline bool entitySkinLayoutUsesMirroredLeftLimbs(const std::string_view id) {
    return id == EntitySkinLayoutIds::CLASSIC_64X32 ||
           id == "classic_64x32";
}

} // namespace ecs

#endif // MECRAFT_ECS_ENTITY_SKIN_LAYOUT_H
