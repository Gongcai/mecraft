#include "FallingBlockTickSystem.h"

#include <cmath>
#include <vector>

#include "../../util/AudioEventBuffer.h"
#include "../../util/ParticleEventBuffer.h"
#include "../../components/Components.h"
#include "../../../world/World.h"
#include "../../../world/block/Block.h"
#include "../../../world/block/BlockStateRegistry.h"
#include "../../../world/chunk/Chunk.h"

namespace ecs {

namespace {

/// True if the cell at `pos` cannot support a falling block (air or fluid-like).
/// Solid blocks return false; air/non-solid return true.
bool isPassable(const World& world, const glm::ivec3& pos) {
    if (pos.y < 0 || pos.y >= Chunk::SIZE_Y) {
        return false;  // world bottom / top — treat as non-passable (landing surface)
    }
    const BlockStateId stateId = world.getBlockState(pos.x, pos.y, pos.z);
    if (stateId == NULL_BLOCK_STATE) return true;  // air
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return !BlockRegistry::getFast(blockId).isSolid;
}

/// Place the block back into the world at `pos` if the cell is currently
/// passable (air or fluid). Returns true if the block was placed.
bool tryLandBlock(World& world, const glm::ivec3& pos, BlockID blockId) {
    if (pos.y < 0 || pos.y >= Chunk::SIZE_Y) {
        return false;
    }
    if (!isPassable(world, pos)) {
        return false;  // occupied — caller will drop as item instead
    }
    world.setBlock(pos.x, pos.y, pos.z, blockId);
    return true;
}

bool isBlockPositionTicking(const World& world, const glm::ivec3& position) {
    const int chunkX = static_cast<int>(std::floor(static_cast<float>(position.x) /
                                                   static_cast<float>(Chunk::SIZE_X)));
    const int chunkZ = static_cast<int>(std::floor(static_cast<float>(position.z) /
                                                   static_cast<float>(Chunk::SIZE_Z)));
    return world.ticketManager().shouldTick(chunkX, chunkZ);
}

} // namespace

void FallingBlockTickSystem::update(SystemContext& ctx) {
    if (!ctx.services.world) return;
    if (ctx.services.gameClient) return;
    tickWorld(*ctx.services.world, ctx.registry);
}

size_t FallingBlockTickSystem::tickWorld(World& world, GameplayRegistry& registry) {
    auto& reg = registry.registry();

    auto& particleBus = ensureParticleEventBus(registry);
    auto& audioBus = ensureAudioEventBus(registry);

    std::vector<entt::entity> destroyList;

    auto view = reg.view<FallingBlockTag, FallingBlockComponent>();
    for (const entt::entity entity : view) {
        auto& block = view.get<FallingBlockComponent>(entity);
        if (!isBlockPositionTicking(world, block.gridPosition)) {
            continue;
        }

        // Snapshot current grid cell as the interpolation start for this tick.
        block.prevGridPosition = block.gridPosition;
        block.tickAccumulator = 0.0f;

        const glm::ivec3 below = block.gridPosition + glm::ivec3(0, -1, 0);

        // Chunk not loaded at the target — freeze the entity this tick
        // (avoid writing into unloaded regions; matches ProjectileSystem guard).
        if (!world.isChunkLoadedForBlock(below.x, below.y, below.z)) {
            continue;
        }

        if (isPassable(world, below)) {
            // Descend one cell.
            block.gridPosition = below;
            continue;
        }

        // Cannot descend: land at the current grid cell.
        const bool placed = tryLandBlock(world, block.gridPosition, block.blockId);
        if (placed) {
            particleBus.push({block.gridPosition, block.blockId});
            audioBus.push({"block.sand.fall", glm::vec3(block.gridPosition), true, 0.8f});
        }
        destroyList.push_back(entity);
    }

    for (const entt::entity entity : destroyList) {
        if (reg.valid(entity)) {
            registry.destroy(entity);
        }
    }

    return destroyList.size();
}

} // namespace ecs
