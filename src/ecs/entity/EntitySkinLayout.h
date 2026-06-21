#ifndef MECRAFT_ECS_ENTITY_SKIN_LAYOUT_H
#define MECRAFT_ECS_ENTITY_SKIN_LAYOUT_H

#include <exception>
#include <optional>
#include <string_view>

namespace ecs {

enum class EntitySkinLayoutKind {
    Steve64x64,
    Classic64x64,
    Classic64x32
};

namespace EntitySkinLayoutIds {
inline constexpr std::string_view STEVE_64X64 = "minecraft:steve_64x64";
inline constexpr std::string_view CLASSIC_64X64 = "minecraft:classic_64x64";
inline constexpr std::string_view CLASSIC_64X32 = "minecraft:classic_64x32";
} // namespace EntitySkinLayoutIds

inline std::optional<EntitySkinLayoutKind> tryParseEntitySkinLayoutId(const std::string_view id) {
    if (id == EntitySkinLayoutIds::STEVE_64X64) {
        return EntitySkinLayoutKind::Steve64x64;
    }
    if (id == EntitySkinLayoutIds::CLASSIC_64X64) {
        return EntitySkinLayoutKind::Classic64x64;
    }
    if (id == EntitySkinLayoutIds::CLASSIC_64X32) {
        return EntitySkinLayoutKind::Classic64x32;
    }
    return std::nullopt;
}

inline std::string_view entitySkinLayoutId(const EntitySkinLayoutKind kind) {
    switch (kind) {
    case EntitySkinLayoutKind::Steve64x64:
        return EntitySkinLayoutIds::STEVE_64X64;
    case EntitySkinLayoutKind::Classic64x64:
        return EntitySkinLayoutIds::CLASSIC_64X64;
    case EntitySkinLayoutKind::Classic64x32:
        return EntitySkinLayoutIds::CLASSIC_64X32;
    }
    std::terminate();
}

inline bool entitySkinLayoutUsesMirroredLeftLimbs(const EntitySkinLayoutKind kind) {
    switch (kind) {
    case EntitySkinLayoutKind::Steve64x64:
        return false;
    case EntitySkinLayoutKind::Classic64x64:
        return true;
    case EntitySkinLayoutKind::Classic64x32:
        return true;
    }
    std::terminate();
}

} // namespace ecs

#endif // MECRAFT_ECS_ENTITY_SKIN_LAYOUT_H
