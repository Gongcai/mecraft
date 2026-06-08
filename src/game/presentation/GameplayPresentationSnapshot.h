#ifndef MECRAFT_GAMEPLAY_PRESENTATION_SNAPSHOT_H
#define MECRAFT_GAMEPLAY_PRESENTATION_SNAPSHOT_H

#include "../../engine/camera/Camera.h"
#include <cstdint>
#include <glm/glm.hpp>

class Inventory;

/// Block target data for overlay rendering (duplicated here to avoid renderer header dependency).
struct SnapBlockTargetData {
    bool hasTarget = false;
    glm::ivec3 targetBlock{};
};

/// Block break data for overlay rendering (duplicated here to avoid renderer header dependency).
struct SnapBlockBreakData {
    bool active = false;
    float progress01 = 0.0f;
    glm::ivec3 blockPos{};
};

/// Held item motion data for first-person rendering.
struct SnapHeldItemMotion {
    bool moving = false;
    bool sprinting = false;
    float bobFrequency = 6.0f;
    float bobPhaseOffset = 0.0f;
    float cameraYawDegrees = -90.0f;
    float cameraPitchDegrees = 0.0f;
};

/// Player stats for HUD display.
struct SnapPlayerStats {
    int health = 20;
    int maxHealth = 20;
    int armor = 0;
    int maxArmor = 20;
    int food = 20;
    int maxFood = 20;
    bool showSurvivalStats = true;
    bool isDead = false;
};

/// Immutable snapshot of gameplay state needed for rendering, UI, and audio.
/// Built once per frame by GameplayPresentationBuilder; consumed by render/UI/audio systems.
/// This decouples Game::renderFrame() from direct ECS queries.
struct GameplayPresentationSnapshot {
    // Camera state
    Camera renderCamera;
    glm::vec3 eyePosition{0.0f};
    bool renderLocalPlayerModel = false;

    // Player state
    bool eyeInWater = false;
    float fallRollRadians = 0.0f;
    int heldBlockLightLevel = 0;

    // Block interaction
    SnapBlockTargetData blockTarget;
    SnapBlockBreakData blockBreak;

    // Held item motion
    SnapHeldItemMotion heldItemMotion;
    uint32_t heldItemSwingSequence = 0;

    // Player stats (for HUD)
    SnapPlayerStats playerStats;

    // Inventory reference (non-owning, valid for the frame)
    const Inventory* inventory = nullptr;

    // Camera controller flags
    bool shouldRenderPlayerModel = false;
};

#endif // MECRAFT_GAMEPLAY_PRESENTATION_SNAPSHOT_H
