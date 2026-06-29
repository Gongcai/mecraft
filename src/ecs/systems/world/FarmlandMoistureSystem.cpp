#include "FarmlandMoistureSystem.h"

#include "../../../world/World.h"
#include "../../../world/block/BlockStateRegistry.h"
#include "../../../world/block/FarmlandRules.h"

namespace ecs {

namespace {

struct FarmlandMoistureProperties {
    uint16_t moisture = BlockStateRegistry::INVALID_INDEX;
    uint16_t moisture7 = BlockStateRegistry::INVALID_INDEX;
};

FarmlandMoistureProperties getMoistureProperties() {
    FarmlandMoistureProperties props;
    props.moisture = BlockStateRegistry::getPropertyNameIndex("moisture");
    if (props.moisture != BlockStateRegistry::INVALID_INDEX) {
        props.moisture7 = BlockStateRegistry::getPropertyValueIndex(props.moisture, "7");
    }
    return props;
}

bool paletteContainsFarmland(const Palette& palette, const BlockID farmlandBlock) {
    for (size_t i = 0; i < palette.size(); ++i) {
        const BlockStateId stateId = palette.getStateId(static_cast<uint32_t>(i));
        if (BlockStateRegistry::getBlockId(stateId) == farmlandBlock) {
            return true;
        }
    }
    return false;
}

} // namespace

void FarmlandMoistureSystem::update(SystemContext& ctx) {
    if (!ctx.services.world) {
        return;
    }
    if (ctx.services.gameClient) {
        return;
    }

    hydrateLoadedFarmland(*ctx.services.world);
}

size_t FarmlandMoistureSystem::hydrateLoadedFarmland(World& world) {
    const BlockID farmlandBlock = BlockRegistry::requireIdByName("minecraft:farmland");

    const FarmlandMoistureProperties props = getMoistureProperties();
    if (props.moisture == BlockStateRegistry::INVALID_INDEX ||
        props.moisture7 == BlockStateRegistry::INVALID_INDEX) {
        return 0;
    }

    size_t hydratedCount = 0;
    const auto& chunks = world.getActiveChunks();
    for (const auto& [key, chunkPtr] : chunks) {
        static_cast<void>(key);
        if (!chunkPtr) {
            continue;
        }

        Chunk& chunk = *chunkPtr;
        if (!world.ticketManager().shouldTick(chunk.m_chunkX, chunk.m_chunkZ)) {
            continue;
        }

        const glm::ivec3 chunkOffset = chunk.getWorldOffset();
        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            const SubChunk* subChunk = chunk.getSubChunk(scy);
            if (subChunk == nullptr || !paletteContainsFarmland(subChunk->blockPalette(), farmlandBlock)) {
                continue;
            }

            for (int localY = 0; localY < SubChunk::SIZE; ++localY) {
                const int worldY = scy * SubChunk::SIZE + localY;
                for (int localZ = 0; localZ < SubChunk::SIZE; ++localZ) {
                    for (int localX = 0; localX < SubChunk::SIZE; ++localX) {
                        const BlockStateId state = subChunk->getBlock(localX, localY, localZ);
                        if (BlockStateRegistry::getBlockId(state) != farmlandBlock) {
                            continue;
                        }
                        if (BlockStateRegistry::getPropertyIndex(state, props.moisture) == props.moisture7) {
                            continue;
                        }

                        const glm::ivec3 pos(chunkOffset.x + localX, worldY, chunkOffset.z + localZ);
                        if (!FarmlandRules::hasHydrationWater(world, pos)) {
                            continue;
                        }

                        const BlockStateId hydratedState = BlockStateRegistry::withProperty(
                            state,
                            props.moisture,
                            props.moisture7);
                        if (hydratedState != state) {
                            world.setBlockState(pos.x, pos.y, pos.z, hydratedState);
                            ++hydratedCount;
                        }
                    }
                }
            }
        }
    }

    return hydratedCount;
}

} // namespace ecs
