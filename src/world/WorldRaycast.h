#ifndef MECRAFT_WORLD_RAYCAST_H
#define MECRAFT_WORLD_RAYCAST_H

#include "IWorldView.h"
#include "../physics/PhysicsInfo.h"

[[nodiscard]] RayHit raycastWorldView(const IWorldView& worldView,
                                      const PhysicsInfo& ray,
                                      float maxDist);

#endif // MECRAFT_WORLD_RAYCAST_H
