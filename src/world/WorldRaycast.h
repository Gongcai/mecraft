#ifndef MECRAFT_WORLD_RAYCAST_H
#define MECRAFT_WORLD_RAYCAST_H

#include "IWorldView.h"
#include "../physics/PhysicsInfo.h"

enum class RaycastFluidMode {
    Ignore,
    Include
};

[[nodiscard]] RayHit raycastWorldView(const IWorldView& worldView,
                                      const PhysicsInfo& ray,
                                      float maxDist,
                                      RaycastFluidMode fluidMode = RaycastFluidMode::Ignore);

#endif // MECRAFT_WORLD_RAYCAST_H
