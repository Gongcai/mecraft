#include "RandomTickSystem.h"

#include "../../../world/World.h"
#include "../../../world/block/BlockRandomTick.h"
#include "../../../world/block/BlockStateRegistry.h"

namespace ecs {

namespace {

uint64_t mixRandomTickSeed(const uint64_t value) {
    uint64_t z = value + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30u)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27u)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31u);
}

uint64_t makeSubChunkSeed(const World& world,
                          const Chunk& chunk,
                          const int scy,
                          const uint64_t tickIndex) {
    uint64_t seed = static_cast<uint64_t>(world.getSeed());
    seed ^= tickIndex * 0xD1B54A32D192ED03ull;
    seed ^= static_cast<uint64_t>(static_cast<uint32_t>(chunk.m_chunkX)) * 0x9E3779B185EBCA87ull;
    seed ^= static_cast<uint64_t>(static_cast<uint32_t>(chunk.m_chunkZ)) * 0xC2B2AE3D27D4EB4Full;
    seed ^= static_cast<uint64_t>(static_cast<uint32_t>(scy)) * 0x165667B19E3779F9ull;
    return mixRandomTickSeed(seed);
}

uint32_t nextRandomBits(uint64_t& state) {
    state = mixRandomTickSeed(state);
    return static_cast<uint32_t>(state >> 32u);
}

bool paletteHasRandomTickBlock(const Palette& palette) {
    for (size_t i = 0; i < palette.size(); ++i) {
        const BlockID blockId = BlockStateRegistry::getBlockId(palette.getRuntimeId(static_cast<uint32_t>(i)));
        if (BlockRegistry::getFast(blockId).randomTick.enabled) {
            return true;
        }
    }
    return false;
}

} // namespace

void RandomTickSystem::update(SystemContext& ctx) {
    if (!ctx.services.world) {
        return;
    }
    if (ctx.services.gameClient) {
        return;
    }

    processWorld(*ctx.services.world, ctx.tickIndex);
}

size_t RandomTickSystem::processWorld(World& world, const uint64_t tickIndex, const uint32_t ticksPerSubChunk) {
    if (ticksPerSubChunk == 0) {
        return 0;
    }

    size_t changedCount = 0;
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
            if (subChunk == nullptr || !paletteHasRandomTickBlock(subChunk->blockPalette())) {
                continue;
            }

            uint64_t randomState = makeSubChunkSeed(world, chunk, scy, tickIndex);
            for (uint32_t tick = 0; tick < ticksPerSubChunk; ++tick) {
                const uint32_t xBits = nextRandomBits(randomState);
                const uint32_t yBits = nextRandomBits(randomState);
                const uint32_t zBits = nextRandomBits(randomState);
                const int localX = static_cast<int>(xBits & 0x0Fu);
                const int localY = static_cast<int>(yBits & 0x0Fu);
                const int localZ = static_cast<int>(zBits & 0x0Fu);

                const StateID state = subChunk->getBlock(localX, localY, localZ);
                if (state == RUNTIME_ID_NULL) {
                    continue;
                }

                const BlockID blockId = BlockStateRegistry::getBlockId(state);
                const BlockDef& def = BlockRegistry::getFast(blockId);
                if (!def.randomTick.enabled) {
                    continue;
                }

                BlockRandomTickContext randomTickCtx{
                    world,
                    glm::ivec3(chunkOffset.x + localX, scy * SubChunk::SIZE + localY, chunkOffset.z + localZ),
                    state,
                    tickIndex,
                    nextRandomBits(randomState)
                };
                if (BlockRandomTick::dispatch(def.randomTick, randomTickCtx)) {
                    ++changedCount;
                }
            }
        }
    }

    return changedCount;
}

} // namespace ecs
