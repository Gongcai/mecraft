#include "renderer/lighting/VoxelLightRegistry.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "world/IWorldView.h"
#include "world/block/Block.h"
#include "world/block/BlockStateRegistry.h"
#include "world/block/PropIndices.h"
#include "world/chunk/Chunk.h"

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[voxel_light_registry_test] FAIL: " << message << '\n';
        return false;
    }
    return true;
}

class TestWorldView final : public IWorldView {
public:
    [[nodiscard]] const ChunkMap& getActiveChunks() const override { return chunks; }
    [[nodiscard]] uint64_t getActiveChunkRevision() const override { return activeRevision; }
    [[nodiscard]] uint64_t getBlockContentRevision() const override { return contentRevision; }
    [[nodiscard]] BlockStateId getBlock(const int x, const int y, const int z) const override {
        if (y < 0 || y >= Chunk::SIZE_Y) {
            return NULL_BLOCK_STATE;
        }
        const glm::ivec2 coordinates = getChunkCoords(x, z);
        const auto found = chunks.find(IWorldView::chunkKey(coordinates.x, coordinates.y));
        if (found == chunks.end()) {
            return NULL_BLOCK_STATE;
        }
        return found->second->getBlock(x - coordinates.x * Chunk::SIZE_X, y, z - coordinates.y * Chunk::SIZE_Z);
    }
    [[nodiscard]] uint8_t getPackedLight(int, int, int) const override { return 0u; }
    [[nodiscard]] BlockStateId getBlockState(const int x, const int y, const int z) const override {
        return getBlock(x, y, z);
    }
    [[nodiscard]] BlockStateId getFluidState(int, int, int) const override { return NULL_BLOCK_STATE; }
    [[nodiscard]] bool isChunkLoadedForBlock(const int x, int, const int z) const override {
        const glm::ivec2 coordinates = getChunkCoords(x, z);
        return chunks.find(IWorldView::chunkKey(coordinates.x, coordinates.y)) != chunks.end();
    }
    [[nodiscard]] int getRenderDistance() const override { return 2; }
    [[nodiscard]] glm::ivec2 getChunkCoords(const int worldX, const int worldZ) const override {
        return {static_cast<int>(std::floor(static_cast<double>(worldX) / Chunk::SIZE_X)),
                static_cast<int>(std::floor(static_cast<double>(worldZ) / Chunk::SIZE_Z))};
    }
    [[nodiscard]] TerrainBiome getBiome(int, int) const override { return TerrainBiome::Temperate; }

    std::shared_ptr<Chunk> addChunk(const int chunkX, const int chunkZ) {
        auto chunk = std::make_shared<Chunk>(chunkX, chunkZ);
        chunks[IWorldView::chunkKey(chunkX, chunkZ)] = chunk;
        ++activeRevision;
        ++contentRevision;
        return chunk;
    }

    void removeChunk(const int chunkX, const int chunkZ) {
        chunks.erase(IWorldView::chunkKey(chunkX, chunkZ));
        ++activeRevision;
        ++contentRevision;
    }

    void notifyBlockChange() { ++contentRevision; }

private:
    ChunkMap chunks;
    uint64_t activeRevision = 1u;
    uint64_t contentRevision = 1u;
};

bool testIncrementalVoxelLights() {
    BlockRegistry::init(nullptr);
    const BlockID torchId = BlockRegistry::requireIdByName("minecraft:torch");
    const BlockID redstoneTorchId = BlockRegistry::requireIdByName("minecraft:redstone_torch");
    const BlockID redstoneLampId = BlockRegistry::requireIdByName("minecraft:redstone_lamp");
    const BlockID glowLichenId = BlockRegistry::requireIdByName("minecraft:glow_lichen");
    const BlockID glowstoneId = BlockRegistry::requireIdByName("minecraft:glowstone");
    const BlockID magmaId = BlockRegistry::requireIdByName("minecraft:magma_block");
    const BlockID stoneId = BlockRegistry::requireIdByName("minecraft:stone");

    const BlockStateId torch = BlockStateRegistry::getDefaultState(torchId);
    const BlockStateId redstoneTorchOn = BlockStateRegistry::getDefaultState(redstoneTorchId);
    const BlockStateId redstoneTorchOff =
        BlockStateRegistry::withProperty(redstoneTorchOn, PropIndices::LIT, PropIndices::LIT_FALSE);
    const BlockStateId redstoneLampOff = BlockStateRegistry::getDefaultState(redstoneLampId);
    const BlockStateId redstoneLampOn =
        BlockStateRegistry::withProperty(redstoneLampOff, PropIndices::LIT, PropIndices::LIT_TRUE);
    const BlockStateId glowLichen = BlockStateRegistry::getDefaultState(glowLichenId);
    const BlockStateId glowstone = BlockStateRegistry::getDefaultState(glowstoneId);
    const BlockStateId magma = BlockStateRegistry::getDefaultState(magmaId);
    const BlockStateId stone = BlockStateRegistry::getDefaultState(stoneId);

    if (!requireTrue(BlockRegistry::getFast(redstoneLampId).analyticLight.has_value(),
                     "redstone lamps must declare an analytic light template") ||
        !requireTrue(
            BlockRegistry::getFast(redstoneLampId).analyticLight->enabledStatePropertyIndex == PropIndices::LIT &&
                BlockRegistry::getFast(redstoneLampId).analyticLight->enabledStateValueIndex == PropIndices::LIT_TRUE,
            "analytic light state activation must resolve to hot indices")) {
        return false;
    }

    TestWorldView world;
    const std::shared_ptr<Chunk> center = world.addChunk(0, 0);
    const std::shared_ptr<Chunk> west = world.addChunk(-1, 0);
    center->setBlock(1, 64, 2, torch);
    center->setBlock(2, 64, 2, redstoneTorchOff);
    center->setBlock(3, 64, 2, redstoneLampOn);
    center->setBlock(5, 64, 2, glowLichen);
    west->setBlock(15, 64, 2, glowstone);
    world.notifyBlockChange();

    renderer::lighting::VoxelLightRegistry registry;
    std::vector<renderer::contracts::SceneLight> lights;
    if (!requireTrue(registry.buildSceneLights(world, glm::vec3(0.0f), lights),
                     "initial voxel light snapshot must build") ||
        !requireTrue(lights.size() == 4u && registry.sourceCount() == 4u,
                     "every configured Vulkan emitter must publish an analytic light") ||
        !requireTrue(lights[0].light.positionAndRange.x == -0.5f && lights[1].light.positionAndRange.x == 1.5f &&
                         lights[2].light.positionAndRange.x == 3.5f &&
                         lights[3].light.positionAndRange.x == 5.5f,
                     "voxel lights must use deterministic chunk and block order") ||
        !requireTrue(lights[0].requestedShadowPolicy == renderer::contracts::GpuLightShadowPolicy::None &&
                         lights[1].requestedShadowPolicy == renderer::contracts::GpuLightShadowPolicy::RasterCached &&
                         lights[2].requestedShadowPolicy == renderer::contracts::GpuLightShadowPolicy::RasterCached &&
                         lights[3].requestedShadowPolicy == renderer::contracts::GpuLightShadowPolicy::None &&
                         lights[1].light.classificationAndIdentity.z ==
                             static_cast<uint32_t>(renderer::contracts::GpuLightShadowPolicy::None) &&
                         lights[1].light.classificationAndIdentity.w ==
                             renderer::contracts::kGpuLightInvalidResourceIndex,
                     "voxel shadow policies must remain explicit unallocated requests")) {
        return false;
    }
    const uint32_t westLightId = lights[0].light.classificationAndIdentity.y;
    const uint32_t torchLightId = lights[1].light.classificationAndIdentity.y;
    const uint64_t initialRevision = registry.lightRevision();

    if (!requireTrue(registry.buildSceneLights(world, glm::vec3(10.0f, 0.0f, 0.0f), lights),
                     "camera movement must rebuild relative records from cached sources") ||
        !requireTrue(lights[0].light.positionAndRange.x == -10.5f &&
                         lights[0].light.classificationAndIdentity.y == westLightId &&
                         lights[1].light.classificationAndIdentity.y == torchLightId,
                     "camera movement must preserve stable light IDs") ||
        !requireTrue(registry.lightRevision() == initialRevision,
                     "camera-relative movement must not change source revision")) {
        return false;
    }

    const BlockStateId wallTorch =
        BlockStateRegistry::withProperty(torch, PropIndices::FACING, PropIndices::FACING_EAST);
    center->setBlock(1, 64, 2, wallTorch);
    world.notifyBlockChange();
    if (!requireTrue(registry.buildSceneLights(world, glm::vec3(0.0f), lights),
                     "wall torch state changes must rebuild its light source") ||
        !requireTrue(lights[1].light.classificationAndIdentity.y == torchLightId &&
                         lights[1].light.positionAndRange.x < 1.4f &&
                         lights[1].light.positionAndRange.y > 64.75f &&
                         lights[1].light.positionAndRange.z == 2.5f,
                     "wall torches must place their light at the oriented rendered flame") ||
        !requireTrue(registry.lightRevision() > initialRevision,
                     "torch-facing state changes must advance the light revision")) {
        return false;
    }

    center->setBlock(1, 64, 2, stone);
    world.notifyBlockChange();
    if (!requireTrue(registry.buildSceneLights(world, glm::vec3(0.0f), lights) && lights.size() == 3u,
                     "removing an emissive block must update only its changed chunk") ||
        !requireTrue(registry.lightRevision() > initialRevision, "source edits must advance the light revision")) {
        return false;
    }

    center->setBlock(3, 64, 2, redstoneLampOff);
    center->setBlock(2, 64, 2, redstoneTorchOn);
    world.notifyBlockChange();
    if (!requireTrue(registry.buildSceneLights(world, glm::vec3(0.0f), lights) && lights.size() == 3u,
                     "state-controlled lights must track their enabled property") ||
        !requireTrue(lights[1].light.colorAndIntensity.x == 1.0f && lights[1].light.colorAndIntensity.y == 0.025f,
                     "enabled redstone torches must use their configured color")) {
        return false;
    }

    west->setBlock(15, 64, 2, magma);
    world.notifyBlockChange();
    if (!requireTrue(registry.buildSceneLights(world, glm::vec3(0.0f), lights),
                     "replacing one analytic block with another must rebuild") ||
        !requireTrue(lights[0].light.classificationAndIdentity.y == westLightId,
                     "parameter changes at one persistent source position must retain ID")) {
        return false;
    }

    world.removeChunk(-1, 0);
    if (!requireTrue(registry.buildSceneLights(world, glm::vec3(0.0f), lights) && lights.size() == 2u,
                     "unloaded chunks must retire their cached light sources")) {
        return false;
    }

    const uint64_t fastRevision = center->getBlockContentRevision();
    center->setBlockFast(4, 64, 2, glowstone);
    return requireTrue(center->getBlockContentRevision() > fastRevision,
                       "fast block writes must advance the chunk content revision");
}

} // namespace

int main() {
    if (!testIncrementalVoxelLights()) {
        return EXIT_FAILURE;
    }
    std::cout << "[voxel_light_registry_test] PASS\n";
    return EXIT_SUCCESS;
}
