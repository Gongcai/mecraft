#pragma once

#include <glm/vec3.hpp>

#include "Block.h"

class World;

namespace FarmlandRules {

constexpr int HYDRATION_HORIZONTAL_RADIUS = 4;
constexpr int HYDRATION_VERTICAL_SEARCH_TOP = 1;

bool hasHydrationWater(const World& world, const glm::ivec3& farmlandPos);
bool hasCropAbove(const World& world, const glm::ivec3& farmlandPos);

} // namespace FarmlandRules
