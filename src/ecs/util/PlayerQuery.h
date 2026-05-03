#ifndef MECRAFT_ECS_PLAYER_QUERY_H
#define MECRAFT_ECS_PLAYER_QUERY_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

class Inventory;

namespace ecs {
class GameplayRegistry;

/// Lightweight read-only query utility for LocalPlayer state.
/// Reads directly from ECS components, providing a unified interface
/// to replace direct Player& dependencies for read-only access patterns.
class PlayerQuery {
public:
    explicit PlayerQuery(const GameplayRegistry& registry);

    [[nodiscard]] bool isValid() const;

    // ── Position & Transform ──
    [[nodiscard]] glm::vec3 getPosition() const;
    [[nodiscard]] glm::vec3 getEyePosition() const;
    [[nodiscard]] float getEyeHeight() const;

    // ── Physics ──
    [[nodiscard]] bool isOnGround() const;
    [[nodiscard]] bool isFullySubmerged() const;
    [[nodiscard]] bool isEyesInWater() const;
    [[nodiscard]] glm::vec3 getVelocity() const;

    // ── Camera ──
    [[nodiscard]] glm::vec3 getCameraFront() const;
    [[nodiscard]] glm::vec3 getCameraRight() const;
    [[nodiscard]] glm::vec3 getCameraUp() const;
    [[nodiscard]] float getCameraYaw() const;
    [[nodiscard]] float getCameraPitch() const;
    [[nodiscard]] float getCameraFOV() const;

    // ── Movement State ──
    [[nodiscard]] bool isMoving() const;
    [[nodiscard]] bool isSprinting() const;
    [[nodiscard]] bool isCrouching() const;
    [[nodiscard]] bool isJustLanded() const;
    [[nodiscard]] float getLandingImpactSpeed() const;

    // ── Block Interaction ──
    [[nodiscard]] bool hasTargetBlock() const;
    [[nodiscard]] glm::ivec3 getTargetBlock() const;
    [[nodiscard]] bool hasBlockBreakProgress() const;
    [[nodiscard]] float getBlockBreakProgress() const;
    [[nodiscard]] glm::ivec3 getBreakTargetBlock() const;

    // ── Stats ──
    [[nodiscard]] int getHealth() const;
    [[nodiscard]] int getMaxHealth() const;
    [[nodiscard]] int getArmor() const;
    [[nodiscard]] int getMaxArmor() const;
    [[nodiscard]] int getFood() const;
    [[nodiscard]] int getMaxFood() const;

    // ── View Bob ──
    [[nodiscard]] float getEyeBobAmplitude() const;
    [[nodiscard]] float getEyeBobHorizontalAmplitude() const;
    [[nodiscard]] float getEyeBobFrequency() const;
    [[nodiscard]] float getEyeBobPhaseOffset() const;
    [[nodiscard]] float getEyeBobBlend() const;
    [[nodiscard]] float getEyeBobVerticalOffset() const;
    [[nodiscard]] float getEyeBobHorizontalOffset() const;

    // ── Hurt Effect ──
    [[nodiscard]] bool hasClassicHurtEffectPending() const;

    // ── Inventory ──
    [[nodiscard]] const Inventory& getInventory() const;

private:
    entt::entity findPlayer() const;

    const GameplayRegistry& m_registry;
};

} // namespace ecs

#endif // MECRAFT_ECS_PLAYER_QUERY_H
