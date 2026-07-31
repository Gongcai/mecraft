#include "ecs/GameplayRegistry.h"
#include "ecs/components/Components.h"
#include "game/camera/CameraController.h"
#include "game/presentation/GameplayPresentationBuilder.h"
#include "world/IWorldView.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

class EmptyWorldView final : public IWorldView {
public:
    [[nodiscard]] const ChunkMap& getActiveChunks() const override { return m_chunks; }

    [[nodiscard]] uint64_t getActiveChunkRevision() const override { return 1; }

    [[nodiscard]] uint64_t getBlockContentRevision() const override { return 1; }

    [[nodiscard]] BlockStateId getBlock(int, int, int) const override { return NULL_BLOCK_STATE; }

    [[nodiscard]] uint8_t getPackedLight(int, int, int) const override { return 0; }

    [[nodiscard]] BlockStateId getBlockState(int, int, int) const override { return NULL_BLOCK_STATE; }

    [[nodiscard]] BlockStateId getFluidState(int, int, int) const override { return NULL_BLOCK_STATE; }

    [[nodiscard]] bool isChunkLoadedForBlock(int, int, int) const override { return true; }

    [[nodiscard]] int getRenderDistance() const override { return 8; }

    [[nodiscard]] glm::ivec2 getChunkCoords(const int worldX, const int worldZ) const override {
        return {worldX >> 4, worldZ >> 4};
    }

    [[nodiscard]] TerrainBiome getBiome(int, int) const override { return TerrainBiome::Temperate; }

private:
    ChunkMap m_chunks;
};

void require(const bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "[presentation_interpolation_test] FAIL: %s\n", message);
        std::abort();
    }
}

bool near(const float a, const float b, const float epsilon = 0.001f) {
    return std::abs(a - b) <= epsilon;
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    ecs::GameplayRegistry registry;
    auto& raw = registry.registry();
    const entt::entity player = raw.create();
    raw.emplace<ecs::LocalPlayerTag>(player);
    auto& transform = raw.emplace<ecs::TransformComponent>(player);
    transform.position = glm::vec3(4.0f, 14.0f, 0.0f);
    transform.eyeHeight = 3.0f;
    auto& transformInterpolation = raw.emplace<ecs::TransformInterpolationComponent>(player);
    transformInterpolation.previousPosition = glm::vec3(0.0f, 10.0f, 0.0f);
    transformInterpolation.previousEyeHeight = 1.0f;
    transformInterpolation.initialized = true;

    auto& camera = raw.emplace<ecs::CameraStateComponent>(player);
    camera.yaw = 10.0f;
    camera.pitch = 20.0f;
    camera.fov = 95.0f;
    auto& cameraInterpolation = raw.emplace<ecs::CameraInterpolationComponent>(player);
    cameraInterpolation.previousYaw = 350.0f;
    cameraInterpolation.previousPitch = 0.0f;
    cameraInterpolation.previousFov = 75.0f;
    cameraInterpolation.initialized = true;

    raw.emplace<ecs::ViewBobComponent>(player);
    raw.emplace<ecs::PhysicsBodyComponent>(player);
    raw.emplace<ecs::MoveIntentComponent>(player);
    raw.emplace<ecs::InventoryDataComponent>(player);
    raw.emplace<ecs::InventoryComponent>(player);

    EmptyWorldView worldView;
    CameraController cameraController;
    GameplayPresentationBuilder builder;
    const GameplayPresentationSnapshot snap = builder.build(registry, cameraController, worldView, 0.25f);

    require(near(snap.eyePosition.x, 1.0f), "eye position should interpolate x");
    require(near(snap.eyePosition.y, 12.5f), "eye position should interpolate position and eye height");
    require(near(snap.renderCamera.getYaw(), 355.0f), "yaw should interpolate across zero degrees");
    require(near(snap.renderCamera.getPitch(), 5.0f), "pitch should interpolate linearly");
    require(near(snap.renderCamera.getFOV(), 80.0f), "FOV should interpolate linearly");

    std::printf("[presentation_interpolation_test] PASS\n");
    return EXIT_SUCCESS;
}
