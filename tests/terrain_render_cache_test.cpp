#include "client/ClientWorld.h"
#include "renderer/mesh/ChunkMeshingService.h"
#include "renderer/mesh/TerrainRenderCache.h"
#include "world/block/Block.h"
#include "world/block/BlockStateRegistry.h"
#include "world/chunk/Chunk.h"
#include "world/World.h"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

int fail(const char* message) {
    std::cerr << "[terrain_render_cache_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    World serverWorld;
    serverWorld.init(20260728);
    serverWorld.setRenderDistance(1);
    if (serverWorld.getChunkLoadProgress(glm::vec3(0.0f)).target != 13) {
        return fail("server load target should include the one-chunk meshing halo");
    }

    client::ClientWorld world;
    auto chunk = std::make_shared<Chunk>(0, 0);
    const int y = 64;
    const int scy = Chunk::toSubChunkIndex(y);
    chunk->setBlock(0, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
    world.addChunk(chunk);

    ChunkMeshingService meshingService;
    TerrainRenderCache cache;
    if (!cache.init()) {
        return fail("CPU-only terrain cache initialization should succeed");
    }
    cache.setChunkMeshingService(&meshingService);

    if (cache.isMeshingSettled(world)) {
        return fail("dirty terrain should keep validation preparation active");
    }

    chunk->setSubChunkMesh(scy, SubChunkMesh{});
    if (!cache.isMeshingSettled(world)) {
        return fail("clean terrain without queued work should be settled");
    }

    cache.shutdown();
    std::cout << "[terrain_render_cache_test] PASS\n";
    return EXIT_SUCCESS;
}
