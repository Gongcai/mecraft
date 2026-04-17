#include "PlayerFacadeSyncSystem.h"

#include "../components/Components.h"
#include "../../player/Player.h"

namespace ecs {

void PlayerFacadeSyncSystem::update(GameplayRegistry& registry, Player& player, const float dt) {
    auto view = registry.view<LocalPlayerTag,
                              MoveIntentComponent,
                              TransformComponent,
                              PhysicsBodyComponent,
                              CameraStateComponent,
                              InventoryComponent,
                              BlockTargetComponent,
                              BlockBreakComponent,
                              LandingStateComponent>();
    for (auto e : view) {
        const auto& moveIntent = view.get<MoveIntentComponent>(e);
        const auto& transform = view.get<TransformComponent>(e);
        const auto& physicsBody = view.get<PhysicsBodyComponent>(e);
        const auto& camera = view.get<CameraStateComponent>(e);
        const auto& inventory = view.get<InventoryComponent>(e);
        const auto& target = view.get<BlockTargetComponent>(e);
        const auto& blockBreak = view.get<BlockBreakComponent>(e);
        const auto& landing = view.get<LandingStateComponent>(e);

        player.syncFromECS(transform,
                           physicsBody,
                           camera,
                           moveIntent,
                           target,
                           blockBreak,
                           landing,
                           inventory,
                           dt);
    }
}

} // namespace ecs
