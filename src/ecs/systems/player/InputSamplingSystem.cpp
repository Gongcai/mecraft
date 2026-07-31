#include "InputSamplingSystem.h"

#include "../../util/InputFrameState.h"
#include "../../components/Components.h"
#include "engine/input/InputContextManager.h"

namespace ecs {

void InputSamplingSystem::update(SystemContext& ctx) {
    if (!ctx.services.inputContextManager)
        return;
    auto& registry = ctx.registry;
    const auto& inputCtx = *ctx.services.inputContextManager;

    auto& frame = registry.ctxGet<InputFrameState>();

    // Axes
    frame.verticalAxis = inputCtx.getAxisValue(Axis::Vertical);
    frame.horizontalAxis = inputCtx.getAxisValue(Axis::Horizontal);
    frame.lookX = inputCtx.getAxisValue(Axis::LookX);
    frame.lookY = inputCtx.getAxisValue(Axis::LookY);

    // Actions
    frame.jump = inputCtx.isActionTriggered(Action::Jump);
    frame.jumpDoubleTap = inputCtx.isActionDoubleTapped(Action::Jump);
    frame.sprint = inputCtx.isActionTriggered(Action::Sprint);
    frame.crouch = inputCtx.isActionTriggered(Action::Crouch);
    frame.attack = inputCtx.isActionTriggered(Action::Attack);
    frame.useItem = inputCtx.isActionTriggered(Action::UseItem);
    frame.inventory = inputCtx.isActionTriggered(Action::Inventory);
    frame.menu = inputCtx.isActionTriggered(Action::Menu);
    frame.openCommand = inputCtx.isActionTriggered(Action::OpenCommand);
    frame.toggleViewMode = inputCtx.isActionTriggered(Action::ToggleViewMode);

    // Hotbar
    for (int i = 0; i < 9; ++i) {
        const auto action = static_cast<Action>(static_cast<int>(Action::Hotbar1) + i);
        frame.hotbar[i] = inputCtx.isActionTriggered(action);
    }
    frame.hotbarScrollUp = inputCtx.isActionTriggered(Action::HotbarScrollUp);
    frame.hotbarScrollDown = inputCtx.isActionTriggered(Action::HotbarScrollDown);

    // Context
    frame.gameplayContextActive = (inputCtx.getCurrentContext() == InputContextType::Gameplay);
}

} // namespace ecs
