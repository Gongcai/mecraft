#ifndef MECRAFT_ECS_PLAYER_STATE_COMPONENTS_H
#define MECRAFT_ECS_PLAYER_STATE_COMPONENTS_H

#include <string>

namespace ecs {

struct FootstepStateComponent {
    float timer = 0.0f;
    int clipIndex = 0;
};

struct LandingStateComponent {
    bool justLanded = false;
    float impactSpeed = 0.0f;
};

struct FallRollComponent {
    bool active = false;
    float elapsed = 0.0f;
    float currentRadians = 0.0f;
    static constexpr float kMaxRadians = 0.06f;
    static constexpr float kDurationSeconds = 0.24f;
    static constexpr float kPeakRatio = 0.35f;
};

struct HealthComponent {
    int current = 20;   // 0-20 (half-hearts)
    int max = 20;
};

struct PlayerModeComponent {
    bool creative = false;
};

struct ArmorComponent {
    int current = 0;    // 0-20
    int max = 20;
};

struct FoodComponent {
    int current = 20;   // 0-20 (half-drumsticks)
    int max = 20;
    int saturation = 5;
    double lastHungerTick = 0.0;  // last game-time (seconds) when hunger was depleted
};

struct HurtEffectComponent {
    bool classicHurtEffectPending = false;
    float flashSecondsRemaining = 0.0f;
    float flashDurationSeconds = 0.18f;
    std::string soundId;
    float soundVolume = 1.0f;

    void triggerClassicHurt() {
        classicHurtEffectPending = true;
        flashSecondsRemaining = flashDurationSeconds;
    }
};

} // namespace ecs

#endif // MECRAFT_ECS_PLAYER_STATE_COMPONENTS_H
