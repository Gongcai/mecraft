#include "FluidFlow.h"

#include <array>
#include <cmath>

#include "../renderer/ChunkMesher.h"
#include "FluidState.h"
#include "World.h"

namespace {
constexpr std::array<glm::ivec3, 4> kHorizontalOffsets = {{
    {1, 0, 0},
    {-1, 0, 0},
    {0, 0, 1},
    {0, 0, -1}
}};

BlockID sampleSnapshotBlock(const SubChunkMeshingSnapshot& snapshot, const int x, const int y, const int z) {
    if (x < -1 || x > SubChunk::SIZE || y < -1 || y > SubChunk::SIZE || z < -1 || z > SubChunk::SIZE) {
        return BlockIds::AIR;
    }

    const int haloX = x + 1;
    const int haloY = y + 1;
    const int haloZ = z + 1;
    const std::size_t index = static_cast<std::size_t>(haloX) +
                              static_cast<std::size_t>(haloZ) * SC_HALO_SIZE +
                              static_cast<std::size_t>(haloY) * SC_HALO_SIZE * SC_HALO_SIZE;
    return snapshot.haloBlocks[index];
}

float surfaceHeightForFluidState(const BlockID stateId, const FluidKind kind, const BlockID aboveState) {
    const DecodedFluid fluid = FluidState::decode(stateId);
    if (fluid.kind != kind) {
        return 0.0f;
    }

    if (FluidState::decode(aboveState).kind == kind) {
        return 1.0f;
    }

    return FluidState::surfaceHeight(stateId);
}

glm::vec3 finalizeFlowVector(glm::vec3 flow, const bool falling) {
    if (falling) {
        flow.y -= 1.0f;
    }

    const float len = glm::length(flow);
    if (len <= 0.0001f) {
        return glm::vec3(0.0f);
    }
    return flow / len;
}
}

glm::vec3 computeFluidFlowVector(const World& world, const glm::ivec3 pos, const FluidKind kind) {
    const BlockID currentState = world.getBlockState(pos.x, pos.y, pos.z);
    const DecodedFluid current = FluidState::decode(currentState);
    if (current.kind != kind) {
        return glm::vec3(0.0f);
    }

    const float currentHeight = surfaceHeightForFluidState(
        currentState, kind, world.getBlockState(pos.x, pos.y + 1, pos.z));

    glm::vec3 flow(0.0f);
    for (const glm::ivec3& offset : kHorizontalOffsets) {
        const glm::ivec3 neighborPos = pos + offset;
        const BlockID neighborState = world.getBlockState(neighborPos.x, neighborPos.y, neighborPos.z);
        const DecodedFluid neighborFluid = FluidState::decode(neighborState);
        if (neighborFluid.kind != kind) {
            continue;
        }

        const float neighborHeight = surfaceHeightForFluidState(
            neighborState, kind, world.getBlockState(neighborPos.x, neighborPos.y + 1, neighborPos.z));
        flow += glm::vec3(static_cast<float>(offset.x), 0.0f, static_cast<float>(offset.z)) *
                (currentHeight - neighborHeight);
    }

    return finalizeFlowVector(flow, current.falling);
}

glm::vec3 computeFluidFlowVector(const SubChunkMeshingSnapshot& snapshot,
                                 const int x,
                                 const int y,
                                 const int z,
                                 const FluidKind kind) {
    const BlockID currentState = sampleSnapshotBlock(snapshot, x, y, z);
    const DecodedFluid current = FluidState::decode(currentState);
    if (current.kind != kind) {
        return glm::vec3(0.0f);
    }

    const float currentHeight = surfaceHeightForFluidState(
        currentState, kind, sampleSnapshotBlock(snapshot, x, y + 1, z));

    glm::vec3 flow(0.0f);
    for (const glm::ivec3& offset : kHorizontalOffsets) {
        const BlockID neighborState = sampleSnapshotBlock(snapshot, x + offset.x, y, z + offset.z);
        const DecodedFluid neighborFluid = FluidState::decode(neighborState);
        if (neighborFluid.kind != kind) {
            continue;
        }

        const float neighborHeight = surfaceHeightForFluidState(
            neighborState, kind, sampleSnapshotBlock(snapshot, x + offset.x, y + 1, z + offset.z));
        flow += glm::vec3(static_cast<float>(offset.x), 0.0f, static_cast<float>(offset.z)) *
                (currentHeight - neighborHeight);
    }

    return finalizeFlowVector(flow, current.falling);
}
