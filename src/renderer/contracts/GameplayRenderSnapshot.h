#ifndef MECRAFT_GAMEPLAY_RENDER_SNAPSHOT_H
#define MECRAFT_GAMEPLAY_RENDER_SNAPSHOT_H

#include "item/Item.h"
#include "world/block/BlockStateRegistry.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace renderer::contracts {

using GameplayRenderObjectKey = uint64_t;

enum class HumanoidBodyPart : uint8_t { Torso, Head, RightArm, LeftArm, RightLeg, LeftLeg };

enum class HumanoidSkinLayout : uint8_t { Steve64x64, Classic64x64, Classic64x32 };

/// Immutable render data for one humanoid model part.
struct HumanoidPartRenderData final {
    GameplayRenderObjectKey partKey = 0u;
    glm::mat4 model{1.0f};
    HumanoidBodyPart bodyPart = HumanoidBodyPart::Torso;
    std::string modelPartName;
};

/// Immutable render data shared by all parts of one humanoid actor.
struct HumanoidActorRenderData final {
    GameplayRenderObjectKey objectKey = 0u;
    std::size_t firstPart = 0u;
    std::size_t partCount = 0u;
    bool localPlayerModel = false;
    std::string textureKey;
    std::string modelId;
    HumanoidSkinLayout skinLayout = HumanoidSkinLayout::Steve64x64;
    glm::vec3 center{0.0f};
    glm::vec2 light{1.0f, 0.0f};
    float hurtFlash = 0.0f;
};

/// Immutable render data for one falling or piston-moving block.
struct FallingBlockRenderData final {
    GameplayRenderObjectKey objectKey = 0u;
    BlockStateId stateId = NULL_BLOCK_STATE;
    glm::vec3 position{0.0f};
    glm::vec2 light{1.0f, 0.0f};
};

/// Immutable render data for one dropped item or projectile.
struct DropRenderData final {
    GameplayRenderObjectKey objectKey = 0u;
    ItemID itemId = 0;
    glm::vec3 position{0.0f};
    glm::vec3 halfExtents{0.0f};
    float yawRadians = 0.0f;
    glm::vec2 light{1.0f, 0.0f};
};

/// Immutable render data for one billboard particle.
struct ParticleRenderData final {
    glm::vec3 position{0.0f};
    float life = 0.0f;
    float maxLife = 0.0f;
    float size = 0.1f;
    float biomeTintFactor = 0.0f;
    float layer = 0.0f;
    glm::vec2 uvMin{0.0f};
    glm::vec2 uvMax{1.0f};
};

/// Complete ECS-independent actor snapshot consumed by one render frame.
struct GameplayRenderSnapshot final {
    std::vector<HumanoidActorRenderData> humanoidActors;
    std::vector<HumanoidPartRenderData> humanoidParts;
    std::vector<FallingBlockRenderData> fallingBlocks;
    std::vector<DropRenderData> drops;
    std::vector<ParticleRenderData> particles;
    bool hasDynamicShadowCasters = false;
};

} // namespace renderer::contracts

#endif // MECRAFT_GAMEPLAY_RENDER_SNAPSHOT_H
