#ifndef MECRAFT_ECS_COMBAT_COMPONENTS_H
#define MECRAFT_ECS_COMBAT_COMPONENTS_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <entt/entity/entity.hpp>

#include "../../item/Item.h"
#include "../../world/block/Block.h"

namespace ecs {

struct DropTableEntry {
    ItemID itemId = 0;
    uint32_t minCount = 1;
    uint32_t maxCount = 1;
};

struct DropTableComponent {
    ItemID itemId = 0;
    uint32_t minCount = 1;
    uint32_t maxCount = 1;
    std::vector<DropTableEntry> entries;

    DropTableComponent() = default;
    DropTableComponent(const ItemID itemId, const uint32_t minCount = 1, const uint32_t maxCount = 1)
        : itemId(itemId),
          minCount(minCount),
          maxCount(maxCount) {}
};

struct MeleeAttackComponent {
    float cooldownRemaining = 0.0f;
    float cooldownSeconds = 0.45f;
    float reach = 3.25f;
    int damage = 4;
};

struct DamageEvent {
    entt::entity target = entt::null;
    entt::entity source = entt::null;
    int amount = 0;
};

struct DeathEffectComponent {
    DeathEffectComponent() = default;
    DeathEffectComponent(const BlockID particleBlock,
                         const int particleCount,
                         std::string soundId,
                         const float volume = 1.0f)
        : particleBlock(particleBlock),
          particleCount(particleCount),
          soundId(std::move(soundId)),
          volume(volume) {}

    BlockID particleBlock = 0;
    int particleCount = 24;
    std::string soundId;
    float volume = 1.0f;
};

} // namespace ecs

#endif // MECRAFT_ECS_COMBAT_COMPONENTS_H
