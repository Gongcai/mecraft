#pragma once

#include <vector>

#include <glm/vec3.hpp>

#include "BlockStateRegistry.h"

struct BlockCollisionBox {
    glm::vec3 min{0.0f};
    glm::vec3 max{1.0f};
};

namespace BlockCollision {

[[nodiscard]] std::vector<BlockCollisionBox> getBoxes(StateID stateId);
[[nodiscard]] bool intersects(StateID stateId,
                              const glm::ivec3& blockPos,
                              const glm::vec3& queryMin,
                              const glm::vec3& queryMax);
[[nodiscard]] bool containsPoint(StateID stateId,
                                 const glm::ivec3& blockPos,
                                 const glm::vec3& point);

} // namespace BlockCollision
