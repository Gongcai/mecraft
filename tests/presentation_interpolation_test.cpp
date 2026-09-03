#include "ecs/GameplayRegistry.h"
#include "ecs/components/Components.h"
#include "game/camera/CameraController.h"
#include "game/presentation/GameplayPresentationBuilder.h"
#include "world/IWorldView.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

#include <glm/gtc/matrix_transform.hpp>

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

renderer::contracts::GameplayRenderObjectKey objectKey(const entt::entity entity) {
    return static_cast<renderer::contracts::GameplayRenderObjectKey>(entt::to_integral(entity));
}

const renderer::contracts::HumanoidActorRenderData&
findActor(const renderer::contracts::GameplayRenderSnapshot& snapshot,
          const renderer::contracts::GameplayRenderObjectKey key) {
    for (const auto& actor : snapshot.humanoidActors) {
        if (actor.objectKey == key) {
            return actor;
        }
    }
    require(false, "expected humanoid actor should exist");
    std::abort();
}

const renderer::contracts::FallingBlockRenderData&
findFallingBlock(const renderer::contracts::GameplayRenderSnapshot& snapshot,
                 const renderer::contracts::GameplayRenderObjectKey key) {
    for (const auto& block : snapshot.fallingBlocks) {
        if (block.objectKey == key) {
            return block;
        }
    }
    require(false, "expected falling block should exist");
    std::abort();
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

    const entt::entity localSteve = raw.create();
    raw.emplace<ecs::SteveTag>(localSteve);
    raw.emplace<ecs::ChildrenComponent>(localSteve);
    auto& localSteveHurt = raw.emplace<ecs::HurtEffectComponent>(localSteve);
    localSteveHurt.flashSecondsRemaining = 0.1f;
    localSteveHurt.flashDurationSeconds = 0.4f;

    const entt::entity localTorso = raw.create();
    raw.emplace<ecs::StevePartComponent>(localTorso, ecs::StevePartType::Torso);
    auto& localTorsoTransform = raw.emplace<ecs::WorldTransformComponent>(localTorso);
    localTorsoTransform.worldMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 5.0f, 7.0f));
    auto& localTorsoChildren = raw.emplace<ecs::ChildrenComponent>(localTorso);
    raw.get<ecs::ChildrenComponent>(localSteve).children.push_back(localTorso);

    const entt::entity localHead = raw.create();
    raw.emplace<ecs::StevePartComponent>(localHead, ecs::StevePartType::Head);
    auto& localHeadTransform = raw.emplace<ecs::WorldTransformComponent>(localHead);
    localHeadTransform.worldMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 5.5f, 7.0f));
    localTorsoChildren.children.push_back(localHead);

    const entt::entity mob = raw.create();
    raw.emplace<ecs::MobTag>(mob);
    auto& mobChildren = raw.emplace<ecs::ChildrenComponent>(mob);
    auto& mobVisual = raw.emplace<ecs::MobVisualComponent>(mob);
    mobVisual.textureKey = "zombie";
    mobVisual.skinLayout = ecs::EntitySkinLayoutKind::Classic64x64;
    mobVisual.scale = 2.0f;
    raw.emplace<ecs::TransformComponent>(mob, glm::vec3(1.0f), 2.0f);
    raw.emplace<ecs::EntityModelComponent>(mob, "test_model", "idle", "head");
    auto& mobHurt = raw.emplace<ecs::HurtEffectComponent>(mob);
    mobHurt.flashSecondsRemaining = 0.25f;
    mobHurt.flashDurationSeconds = 0.5f;

    const entt::entity mobPart = raw.create();
    raw.emplace<ecs::EntityModelPartComponent>(mobPart, "body");
    auto& mobPartTransform = raw.emplace<ecs::WorldTransformComponent>(mobPart);
    mobPartTransform.worldMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 2.0f, 1.0f));
    mobChildren.children.push_back(mobPart);

    const BlockID sand = BlockRegistry::requireIdByName("minecraft:sand");
    const BlockStateId sandState = BlockStateRegistry::getDefaultState(sand);
    const entt::entity fallingBlock = raw.create();
    raw.emplace<ecs::FallingBlockTag>(fallingBlock);
    raw.emplace<ecs::FallingBlockComponent>(fallingBlock, sand, glm::ivec3(2), glm::ivec3(2), 0.0f);
    raw.emplace<ecs::TransformComponent>(fallingBlock, glm::vec3(2.5f), 0.0f);
    raw.emplace<ecs::DropEntityIdComponent>(fallingBlock, 301u);

    const entt::entity movingBlock = raw.create();
    raw.emplace<ecs::MovingBlockTag>(movingBlock);
    raw.emplace<ecs::MovingBlockComponent>(movingBlock, sandState, glm::ivec3(3), glm::ivec3(4, 3, 3),
                                           glm::ivec3(1, 0, 0), 0.05f, 0.1f, true);
    raw.emplace<ecs::TransformComponent>(movingBlock, glm::vec3(3.5f), 0.0f);
    raw.emplace<ecs::DropEntityIdComponent>(movingBlock, 302u);

    const entt::entity drop = raw.create();
    raw.emplace<ecs::DropItemTag>(drop);
    raw.emplace<ecs::DropEntityIdComponent>(drop, 202u);
    raw.emplace<ecs::TransformComponent>(drop, glm::vec3(8.0f, 9.0f, 10.0f), 0.0f);
    raw.emplace<ecs::ItemComponent>(drop, static_cast<ItemID>(42u), 3u);
    raw.emplace<ecs::BoundsComponent>(drop, glm::vec3(0.2f));
    raw.emplace<ecs::SpinVisualComponent>(drop, 0.75f, 1.0f);

    const entt::entity projectile = raw.create();
    raw.emplace<ecs::ProjectileTag>(projectile);
    raw.emplace<ecs::DropEntityIdComponent>(projectile, 201u);
    raw.emplace<ecs::TransformComponent>(projectile, glm::vec3(11.0f, 12.0f, 13.0f), 0.0f);
    raw.emplace<ecs::ItemComponent>(projectile, static_cast<ItemID>(43u), 1u);
    raw.emplace<ecs::BoundsComponent>(projectile, glm::vec3(0.15f));
    raw.emplace<ecs::SpinVisualComponent>(projectile, 1.25f, 2.0f);

    const entt::entity particle = raw.create();
    raw.emplace<ecs::ParticleTag>(particle);
    raw.emplace<ecs::TransformComponent>(particle, glm::vec3(14.0f, 15.0f, 16.0f), 0.0f);
    raw.emplace<ecs::ParticleComponent>(particle, 0.5f, 1.0f, 0.25f, 0.75f, 4.0f, glm::vec2(0.1f, 0.2f),
                                        glm::vec2(0.3f, 0.4f));

    EmptyWorldView worldView;
    CameraController cameraController;
    GameplayPresentationBuilder builder;
    const GameplayPresentationSnapshot snap = builder.build(registry, cameraController, worldView, 0.25f);

    require(near(snap.eyePosition.x, 1.0f), "eye position should interpolate x");
    require(near(snap.eyePosition.y, 12.5f), "eye position should interpolate position and eye height");
    require(near(snap.renderCamera.getYaw(), 355.0f), "yaw should interpolate across zero degrees");
    require(near(snap.renderCamera.getPitch(), 5.0f), "pitch should interpolate linearly");
    require(near(snap.renderCamera.getFOV(), 80.0f), "FOV should interpolate linearly");

    static_assert(std::is_same_v<decltype(snap.renderScene), renderer::contracts::GameplayRenderSnapshot>);
    const auto& renderSnapshot = snap.renderScene;
    require(renderSnapshot.hasDynamicShadowCasters, "renderable dynamic actors should invalidate cached local shadows");
    require(renderSnapshot.humanoidActors.size() == 2u, "Steve and mob roots should become render actors");
    require(renderSnapshot.humanoidParts.size() == 3u, "humanoid parts should use one flattened storage array");

    const auto& steveActor = findActor(renderSnapshot, objectKey(localSteve));
    require(steveActor.localPlayerModel, "Steve root without a network id should be the local model");
    require(steveActor.textureKey == "steve", "Steve actor should select the Steve texture");
    require(steveActor.partCount == 2u, "Steve actor should reference direct and nested body parts");
    require(near(steveActor.center.x, 2.0f) && near(steveActor.center.y, 5.0f) && near(steveActor.center.z, 7.0f),
            "Steve actor center should come from the torso world matrix");
    require(near(steveActor.hurtFlash, 0.25f), "Steve hurt flash should be normalized");
    require(near(steveActor.light.x, 1.0f) && near(steveActor.light.y, 0.0f),
            "missing streamed light data should use the explicit render fallback");

    const auto& mobActor = findActor(renderSnapshot, objectKey(mob));
    require(!mobActor.localPlayerModel, "mob actors should never be classified as the local player model");
    require(mobActor.textureKey == "zombie", "mob texture key should be copied into the snapshot");
    require(mobActor.modelId == "test_model", "custom mob model id should be copied into the snapshot");
    require(mobActor.skinLayout == renderer::contracts::HumanoidSkinLayout::Classic64x64,
            "mob skin layout should use the renderer contract enum");
    require(mobActor.partCount == 1u, "custom mob should reference its model part");
    require(near(mobActor.hurtFlash, 0.5f), "mob hurt flash should be normalized");
    const auto& mobRenderPart = renderSnapshot.humanoidParts[mobActor.firstPart];
    require(mobRenderPart.partKey == objectKey(mobPart), "mob part should preserve its opaque object key");
    require(mobRenderPart.modelPartName == "body", "custom model part name should be copied");
    require(near(mobRenderPart.model[3].x, 5.0f) && near(mobRenderPart.model[3].y, 3.0f) &&
                near(mobRenderPart.model[3].z, 1.0f),
            "mob visual scale should be baked around the root pivot");

    require(renderSnapshot.fallingBlocks.size() == 2u,
            "falling and piston-moving blocks should share the render snapshot");
    const auto& fallingRender = findFallingBlock(renderSnapshot, 301u);
    const auto& movingRender = findFallingBlock(renderSnapshot, 302u);
    require(fallingRender.stateId == sandState, "falling block should resolve its default block state");
    require(movingRender.stateId == sandState, "moving block should preserve its concrete block state");
    require(near(fallingRender.position.x, 2.5f) && near(movingRender.position.x, 3.5f),
            "block actor positions should be copied into the snapshot");

    require(renderSnapshot.drops.size() == 2u, "drops and projectiles should share dropped-object render data");
    require(renderSnapshot.drops[0].objectKey == 201u && renderSnapshot.drops[1].objectKey == 202u,
            "dropped-object snapshot should be deterministically sorted by object key");
    require(renderSnapshot.drops[0].itemId == static_cast<ItemID>(43u),
            "projectile item id should be copied into render data");
    require(near(renderSnapshot.drops[1].halfExtents.x, 0.2f) && near(renderSnapshot.drops[1].yawRadians, 0.75f),
            "drop bounds and spin state should be copied into render data");

    require(renderSnapshot.particles.size() == 1u, "particle components should become billboard render data");
    const auto& particleRender = renderSnapshot.particles.front();
    require(near(particleRender.position.x, 14.0f) && near(particleRender.life, 0.5f) &&
                near(particleRender.maxLife, 1.0f) && near(particleRender.size, 0.25f) &&
                near(particleRender.biomeTintFactor, 0.75f) && near(particleRender.layer, 4.0f) &&
                near(particleRender.uvMin.x, 0.1f) && near(particleRender.uvMax.y, 0.4f),
            "particle render fields should be copied without renderer-side ECS queries");

    raw.destroy(localSteve);
    raw.destroy(mob);
    raw.destroy(fallingBlock);
    raw.destroy(movingBlock);
    raw.destroy(drop);
    const GameplayPresentationSnapshot projectileSnap = builder.build(registry, cameraController, worldView, 0.25f);
    require(projectileSnap.renderScene.hasDynamicShadowCasters,
            "projectiles rendered as drops should invalidate cached local shadows");

    raw.destroy(projectile);
    const GameplayPresentationSnapshot particleOnlySnap = builder.build(registry, cameraController, worldView, 0.25f);
    require(!particleOnlySnap.renderScene.hasDynamicShadowCasters,
            "particles alone should not invalidate cached local shadows");

    std::printf("[presentation_interpolation_test] PASS\n");
    return EXIT_SUCCESS;
}
