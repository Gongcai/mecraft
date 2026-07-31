#ifndef MECRAFT_ECS_INPUT_COMPONENTS_H
#define MECRAFT_ECS_INPUT_COMPONENTS_H

#include <glm/glm.hpp>

namespace ecs {

struct MoveIntentComponent {
    glm::vec2 move{0.0f}; // world-space X/Z wish direction
    bool wantsJump = false;
    bool wantsSprint = false;
    bool wantsCrouch = false;
    bool toggleFlightMode = false;
};

struct LookIntentComponent {
    float deltaX = 0.0f; // horizontal mouse delta
    float deltaY = 0.0f; // vertical mouse delta
};

struct HotbarIntentComponent {
    bool slotSelected[9] = {}; // Hotbar1~9 triggered
    bool scrollUp = false;
    bool scrollDown = false;
};

struct BlockActionIntentComponent {
    bool wantsBreak = false;
    bool wantsPlace = false;
};

} // namespace ecs

#endif // MECRAFT_ECS_INPUT_COMPONENTS_H
