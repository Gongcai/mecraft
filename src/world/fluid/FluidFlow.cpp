#include "FluidFlow.h"

#include <array>
#include <cmath>

#include "../../renderer/ChunkMesher.h"
#include "FluidState.h"
#include "../World.h"

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

// Get the effective fluid state at a snapshot position (fluid layer or block layer)
BlockID sampleSnapshotFluid(const SubChunkMeshingSnapshot& snapshot, const int x, const int y, const int z) {
    if (x >= 0 && x < SubChunk::SIZE && y >= 0 && y < SubChunk::SIZE && z >= 0 && z < SubChunk::SIZE) {
        const std::size_t idx = static_cast<std::size_t>(x) +
                                static_cast<std::size_t>(z) * SubChunk::SIZE +
                                static_cast<std::size_t>(y) * SubChunk::SIZE * SubChunk::SIZE;
        const BlockID fluidId = snapshot.fluidBlocks[idx];
        if (fluidId != BlockIds::AIR) {
            return fluidId;
        }
    }

    // Fall back to halo or block data for border positions
    if (x < -1 || x > SubChunk::SIZE || y < -1 || y > SubChunk::SIZE || z < -1 || z > SubChunk::SIZE) {
        return BlockIds::AIR;
    }

    const int haloX = x + 1;
    const int haloY = y + 1;
    const int haloZ = z + 1;
    const std::size_t index = static_cast<std::size_t>(haloX) +
                              static_cast<std::size_t>(haloZ) * SC_HALO_SIZE +
                              static_cast<std::size_t>(haloY) * SC_HALO_SIZE * SC_HALO_SIZE;
    const BlockID haloFluid = snapshot.haloFluidBlocks[index];
    if (haloFluid != BlockIds::AIR) {
        return haloFluid;
    }

    // Fallback: block layer may contain pure fluid
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
    const StateID currentFluidState = world.getFluidState(pos.x, pos.y, pos.z);
    const DecodedFluid current = FluidState::decode(currentFluidState);
    if (current.kind != kind) {
        return glm::vec3(0.0f);
    }

    const float currentHeight = surfaceHeightForFluidState(
        currentFluidState, kind, world.getFluidState(pos.x, pos.y + 1, pos.z));

    glm::vec3 flow(0.0f);
    for (const glm::ivec3& offset : kHorizontalOffsets) {
        const glm::ivec3 neighborPos = pos + offset;
        const StateID neighborFluidState = world.getFluidState(neighborPos.x, neighborPos.y, neighborPos.z);
        const DecodedFluid neighborFluid = FluidState::decode(neighborFluidState);
        if (neighborFluid.kind != kind) {
            continue;
        }

        const float neighborHeight = surfaceHeightForFluidState(
            neighborFluidState, kind, world.getFluidState(neighborPos.x, neighborPos.y + 1, neighborPos.z));
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
    const BlockID currentFluidState = sampleSnapshotFluid(snapshot, x, y, z);
    const DecodedFluid current = FluidState::decode(currentFluidState);
    if (current.kind != kind) {
        return glm::vec3(0.0f);
    }

    const float currentHeight = surfaceHeightForFluidState(
        currentFluidState, kind, sampleSnapshotFluid(snapshot, x, y + 1, z));

    glm::vec3 flow(0.0f);
    for (const glm::ivec3& offset : kHorizontalOffsets) {
        const BlockID neighborFluidState = sampleSnapshotFluid(snapshot, x + offset.x, y, z + offset.z);
        const DecodedFluid neighborFluid = FluidState::decode(neighborFluidState);
        if (neighborFluid.kind != kind) {
            continue;
        }

        const float neighborHeight = surfaceHeightForFluidState(
            neighborFluidState, kind, sampleSnapshotFluid(snapshot, x + offset.x, y + 1, z + offset.z));
        flow += glm::vec3(static_cast<float>(offset.x), 0.0f, static_cast<float>(offset.z)) *
                (currentHeight - neighborHeight);
    }

    return finalizeFlowVector(flow, current.falling);
}
