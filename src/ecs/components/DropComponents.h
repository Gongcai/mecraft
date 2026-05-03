#ifndef MECRAFT_ECS_DROP_COMPONENTS_H
#define MECRAFT_ECS_DROP_COMPONENTS_H

#include <cstddef>
#include <cstdint>

#include "../../item/Item.h"

namespace ecs {

struct DropEntityIdComponent {
    std::size_t dropId = 0;
};

struct ItemComponent {
    ItemID itemId = 0;
    uint32_t stackCount = 0;
};

struct LifetimeComponent {
    float ageSeconds = 0.0f;
    float lifeTimeSeconds = 0.0f;
};

struct SpinVisualComponent {
    float yawRadians = 0.0f;
    float spinSpeedRadians = 0.0f;
};

} // namespace ecs

#endif // MECRAFT_ECS_DROP_COMPONENTS_H
