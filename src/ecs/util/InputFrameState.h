#ifndef MECRAFT_ECS_INPUT_FRAME_STATE_H
#define MECRAFT_ECS_INPUT_FRAME_STATE_H

#include "../../player/ActionMap.h"

class InputContextManager;

namespace ecs {

/// Singleton context stored in the registry.
/// Populated each fixed step by InputSamplingSystem before any gameplay systems run.
struct InputFrameState {
    // Raw axis values sampled from InputContextManager
    float verticalAxis   = 0.0f;
    float horizontalAxis = 0.0f;
    float lookX          = 0.0f;
    float lookY          = 0.0f;

    // Action states
    bool jump             = false;
    bool jumpDoubleTap    = false;
    bool sprint           = false;
    bool crouch           = false;
    bool attack           = false;
    bool useItem          = false;
    bool inventory        = false;
    bool menu             = false;
    bool openCommand      = false;
    bool toggleViewMode   = false;
    bool hotbar[9]        = {};
    bool hotbarScrollUp   = false;
    bool hotbarScrollDown = false;

    /// Whether gameplay context is currently active (i.e. not in UI/Pause).
    bool gameplayContextActive = true;
};

} // namespace ecs

#endif // MECRAFT_ECS_INPUT_FRAME_STATE_H
