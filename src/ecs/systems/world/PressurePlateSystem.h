#ifndef MECRAFT_ECS_PRESSURE_PLATE_SYSTEM_H
#define MECRAFT_ECS_PRESSURE_PLATE_SYSTEM_H

#include "../../ISystem.h"

#include <cstddef>

class World;

namespace ecs {

class GameplayRegistry;

/// Updates pressure plate powered states from nearby entity contact boxes.
class PressurePlateSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;

    /// Evaluate entity contact boxes and apply pressure plate powered states.
    /// @param world  Mutable world containing pressure plate block states.
    /// @param registry  Gameplay entity registry containing entity transforms and bounds.
    /// @return Number of pressure plate block states changed.
    static size_t processWorldEntities(World& world, GameplayRegistry& registry);
};

} // namespace ecs

#endif // MECRAFT_ECS_PRESSURE_PLATE_SYSTEM_H
