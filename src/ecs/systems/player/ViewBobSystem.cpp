#include "ViewBobSystem.h"

#include <algorithm>
#include <cmath>

#include "../../components/Components.h"
#include "../../../core/Time.h"

namespace ecs {

void ViewBobSystem::update(GameplayRegistry& registry, const float dt) {
    auto view = registry.view<LocalPlayerTag,
                              MoveIntentComponent,
                              PhysicsBodyComponent,
                              CameraStateComponent,
                              ViewBobComponent>();
    for (auto e : view) {
        const auto& moveIntent = view.get<MoveIntentComponent>(e);
        const auto& physicsBody = view.get<PhysicsBodyComponent>(e);
        auto& viewBob = view.get<ViewBobComponent>(e);

        const bool hasMoveInput = (moveIntent.move.x != 0.0f || moveIntent.move.y != 0.0f);
        const bool shouldBob = hasMoveInput && physicsBody.body.isGrounded && !moveIntent.wantsCrouch;

        const float targetBlend = shouldBob ? 1.0f : 0.0f;
        const float blendSpeed = shouldBob ? viewBob.fadeInSpeed : viewBob.fadeOutSpeed;
        viewBob.blend = std::clamp(viewBob.blend + (targetBlend - viewBob.blend) * dt * blendSpeed, 0.0f, 1.0f);

        const float phase = static_cast<float>(Time::getGameTime()) * viewBob.frequency;

        const float verticalBob = viewBob.amplitude * static_cast<float>(std::sin(phase)) * viewBob.blend;
        viewBob.verticalOffset = verticalBob * verticalBob;

        viewBob.horizontalOffset = viewBob.horizontalAmplitude *
            static_cast<float>(std::cos(phase + viewBob.phaseOffset)) * viewBob.blend;
    }
}

} // namespace ecs
