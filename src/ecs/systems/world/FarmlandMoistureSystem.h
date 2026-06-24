#ifndef MECRAFT_ECS_FARMLAND_MOISTURE_SYSTEM_H
#define MECRAFT_ECS_FARMLAND_MOISTURE_SYSTEM_H

#include "../../ISystem.h"

class World;

namespace ecs {

/// Tick-rate system that hydrates farmland near water.
class FarmlandMoistureSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;

    static size_t hydrateLoadedFarmland(World& world);
};

} // namespace ecs

#endif // MECRAFT_ECS_FARMLAND_MOISTURE_SYSTEM_H
