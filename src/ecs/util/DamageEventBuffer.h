#ifndef MECRAFT_ECS_DAMAGE_EVENT_BUFFER_H
#define MECRAFT_ECS_DAMAGE_EVENT_BUFFER_H

#include "../components/CombatComponents.h"
#include "EventBus.h"

namespace ecs {

using DamageEventBus = EventBus<DamageEvent>;

inline DamageEventBus& ensureDamageEventBus(GameplayRegistry& registry) {
    return ensureEventBus<DamageEvent>(registry);
}

} // namespace ecs

#endif // MECRAFT_ECS_DAMAGE_EVENT_BUFFER_H
