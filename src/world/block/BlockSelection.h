#pragma once

#include <glm/vec3.hpp>

#include "BlockStateRegistry.h"

struct BlockSelectionBox {
    glm::vec3 min{0.0f};
    glm::vec3 max{1.0f};
};

namespace BlockSelection {

[[nodiscard]] BlockSelectionBox getBox(BlockStateId stateId);

} // namespace BlockSelection
