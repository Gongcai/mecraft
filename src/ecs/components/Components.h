#ifndef MECRAFT_ECS_COMPONENTS_H
#define MECRAFT_ECS_COMPONENTS_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <glm/glm.hpp>
#include "../../physics/PhysicsInfo.h"
#include "../../item/Item.h"

namespace ecs {

// ── Tags ──────────────────────────────────────────────

struct LocalPlayerTag {};

// ── Input Intent Components (written by InputSamplingSystem / PlayerIntentBuildSystem) ──

struct MoveIntentComponent {
    glm::vec2 move{0.0f};     // world-space X/Z wish direction
    bool wantsJump   = false;
    bool wantsSprint = false;
    bool wantsCrouch = false;
    bool toggleFlightMode = false;
};

struct LookIntentComponent {
    float deltaX = 0.0f;      // horizontal mouse delta
    float deltaY = 0.0f;      // vertical mouse delta
};

struct HotbarIntentComponent {
    bool slotSelected[9] = {};      // Hotbar1~9 triggered
    bool scrollUp   = false;
    bool scrollDown = false;
};

struct BlockActionIntentComponent {
    bool wantsBreak = false;
    bool wantsPlace = false;
};

// ── Milestone 2 Runtime Components (skeleton) ───────────────────────────────

struct TransformComponent {
    glm::vec3 position{0.0f};
    float eyeHeight = 1.62f;
};

struct PhysicsBodyComponent {
    PhysicsBody body{};
};

struct CharacterControllerComponent {
    PhysicsTuning tuning{};
    float standEyeHeight = 1.62f;
    float crouchEyeHeight = 1.0f;
    float eyeHeightLerpSpeed = 15.0f;
    bool crouchChangesEyeHeight = true;
};

struct FlightStateComponent {
    bool isFlying = false;
};

struct CameraStateComponent {
    float yaw = -90.0f;
    float pitch = 0.0f;
    float fov = 75.0f;
    float sensitivity = 0.1f;
    glm::vec3 front{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
};

struct SprintFovComponent {
    float walkFov = 75.0f;
    float sprintFov = 90.0f;
    float lerpSpeed = 10.0f;
};

struct InventoryComponent {
    int selectedHotbarSlot = 0;
};

struct BlockTargetComponent {
    bool hasTarget = false;
    glm::ivec3 targetBlock{};
};

struct BlockBreakComponent {
    bool active = false;
    glm::ivec3 blockPos{};
    float progress01 = 0.0f;
};

struct BlockInteractionRuntimeComponent {
    float placeCooldownRemaining = 0.0f;
    float creativeBreakCooldownRemaining = 0.0f;
    bool breakActive = false;
    glm::ivec3 breakBlockPos{};
    float breakElapsedMs = 0.0f;
    float breakRequiredMs = 0.0f;
};

struct FootstepStateComponent {
    float timer = 0.0f;
    int clipIndex = 0;
};

struct LandingStateComponent {
    bool justLanded = false;
    float impactSpeed = 0.0f;
};

// ── Milestone 3 Drop ECS Components (bootstrap mirror) ──────────────────────

struct DropItemTag {};

struct DropEntityIdComponent {
    std::size_t dropId = 0;
};

struct ItemComponent {
    ItemID itemId = 0;
    uint32_t stackCount = 0;
};

struct VelocityComponent {
    glm::vec3 velocity{0.0f};
};

struct BoundsComponent {
    glm::vec3 halfExtents{0.0f};
};

struct LifetimeComponent {
    float ageSeconds = 0.0f;
    float lifeTimeSeconds = 0.0f;
};

struct SpinVisualComponent {
    float yawRadians = 0.0f;
    float spinSpeedRadians = 0.0f;
};

struct GroundedStateComponent {
    bool grounded = false;
};

// ── Milestone 4 Particle ECS Components ──────────────────────────────────────

struct ParticleTag {};

struct ParticleComponent {
    float life = 0.0f;
    float maxLife = 0.0f;
    float size = 0.1f;
    float grassTintFactor = 0.0f;
    float layer = 0.0f;
    glm::vec2 uvMin{0.0f};
    glm::vec2 uvMax{1.0f};
};

// ── Milestone 5 Audio ECS Components (bootstrap) ─────────────────────────────

struct AudioSourceComponent {
    std::string clipName;
    bool loop = false;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool spatial = true;
    float referenceDistance = 8.0f;
    float rolloff = 1.0f;
    bool desiredPlaying = false;
    bool followTransform = true;
};

} // namespace ecs

#endif // MECRAFT_ECS_COMPONENTS_H
