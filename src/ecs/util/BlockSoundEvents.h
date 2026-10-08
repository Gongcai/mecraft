#ifndef MECRAFT_ECS_BLOCK_SOUND_EVENTS_H
#define MECRAFT_ECS_BLOCK_SOUND_EVENTS_H

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "../../world/block/Block.h"

namespace ecs {

/// Return the vanilla sound group for a block's broad material category.
/// @param materialKind The material category stored by the block definition.
/// @return The Minecraft 1.21 sound group name used for break and place events.
inline std::string_view blockSoundGroup(const uint8_t materialKind) {
    switch (materialKind) {
    case BlockMaterialKinds::DIRT: return "gravel";
    case BlockMaterialKinds::GRASS:
    case BlockMaterialKinds::LEAVES:
    case BlockMaterialKinds::PLANT: return "grass";
    case BlockMaterialKinds::WOOD: return "wood";
    case BlockMaterialKinds::SAND: return "sand";
    case BlockMaterialKinds::GLASS:
    case BlockMaterialKinds::ICE:
    case BlockMaterialKinds::STAINED_GLASS: return "glass";
    case BlockMaterialKinds::METAL: return "metal";
    case BlockMaterialKinds::WOOL: return "wool";
    case BlockMaterialKinds::DEFAULT:
    case BlockMaterialKinds::STONE:
    case BlockMaterialKinds::WATER:
    case BlockMaterialKinds::ORE:
    case BlockMaterialKinds::EMISSIVE: return "stone";
    default:
        std::cerr << "[Audio] Unsupported block material sound category: " << static_cast<int>(materialKind)
                  << std::endl;
        std::abort();
    }
}

/// Build a registered block sound event identifier from a block and action.
/// @param blockId The runtime block identifier whose material selects the sound group.
/// @param action The event action, such as "break" or "place".
/// @return A vanilla-style event identifier in the form "block.<group>.<action>".
inline std::string blockSoundEventId(const BlockID blockId, const std::string_view action) {
    const std::string_view group = blockSoundGroup(BlockRegistry::get(blockId).materialKind);
    std::string eventId;
    eventId.reserve(7 + group.size() + action.size());
    eventId.append("block.");
    eventId.append(group);
    eventId.push_back('.');
    eventId.append(action);
    return eventId;
}

} // namespace ecs

#endif // MECRAFT_ECS_BLOCK_SOUND_EVENTS_H
