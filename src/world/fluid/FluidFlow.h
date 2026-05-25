#pragma once

#include <glm/glm.hpp>

#include "FluidRegistry.h"

class World;
struct SubChunkMeshingSnapshot;

glm::vec3 computeFluidFlowVector(const World& world, glm::ivec3 pos, FluidKind kind);
glm::vec3 computeFluidFlowVector(const SubChunkMeshingSnapshot& snapshot, int x, int y, int z, FluidKind kind);
