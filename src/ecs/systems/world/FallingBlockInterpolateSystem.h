#ifndef MECRAFT_ECS_FALLING_BLOCK_INTERPOLATE_SYSTEM_H
#define MECRAFT_ECS_FALLING_BLOCK_INTERPOLATE_SYSTEM_H

#include "../../ISystem.h"

namespace ecs {

/// Fixed-update (60 Hz) system that smooths the rendered position of each
/// falling-block entity between two tick grid cells. Minecraft's falling
/// blocks advance one cell per tick (20 TPS); this system lerps
/// TransformComponent.position from prevGridPosition toward gridPosition
/// using the time elapsed since the last tick.
class FallingBlockInterpolateSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_FALLING_BLOCK_INTERPOLATE_SYSTEM_H
