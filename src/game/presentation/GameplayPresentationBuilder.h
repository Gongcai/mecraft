#ifndef MECRAFT_GAMEPLAY_PRESENTATION_BUILDER_H
#define MECRAFT_GAMEPLAY_PRESENTATION_BUILDER_H

#include "GameplayPresentationSnapshot.h"

namespace ecs { class GameplayRegistry; }

class World;
class CameraController;

/// Builds a GameplayPresentationSnapshot from ECS state.
/// Extracts all presentation-related queries from Game::renderFrame() into a single builder.
/// This decouples the render/UI/audio paths from direct ECS component access.
class GameplayPresentationBuilder {
public:
    /// Build a snapshot from current ECS state.
    /// @param reg The gameplay ECS registry
    /// @param cameraController The camera controller for final camera computation
    /// @return An immutable snapshot for the current frame
    [[nodiscard]] GameplayPresentationSnapshot build(ecs::GameplayRegistry& reg,
                                                      const CameraController& cameraController);
};

#endif // MECRAFT_GAMEPLAY_PRESENTATION_BUILDER_H
