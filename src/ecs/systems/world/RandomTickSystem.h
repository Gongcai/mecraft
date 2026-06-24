#ifndef MECRAFT_ECS_RANDOM_TICK_SYSTEM_H
#define MECRAFT_ECS_RANDOM_TICK_SYSTEM_H

#include <cstddef>
#include <cstdint>

#include "../../ISystem.h"

class World;

namespace ecs {

/// Tick-rate system that samples loaded sub-chunks and dispatches block random tick behaviors.
class RandomTickSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;

    static size_t processWorld(World& world, uint64_t tickIndex, uint32_t ticksPerSubChunk = 3);
};

} // namespace ecs

#endif // MECRAFT_ECS_RANDOM_TICK_SYSTEM_H
