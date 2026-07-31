#pragma once

#include <memory>
#include <string>

#include <glm/vec3.hpp>

#include "../states/IGameState.h"
#include "InventoryStateContext.h"

class ContainerStateFactory final {
public:
    [[nodiscard]] static std::unique_ptr<IGameState>
    create(InventoryStateContext deps, const std::string& containerUiId, const glm::ivec3& blockPosition);
};
