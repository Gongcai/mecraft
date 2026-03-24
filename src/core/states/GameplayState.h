#ifndef MECRAFT_GAMEPLAYSTATE_H
#define MECRAFT_GAMEPLAYSTATE_H

#include "../IGameState.h"
#include "../GameStateMachine.h"
#include "../InputContextManager.h"
#include "../../player/Player.h"
#include "UIState.h"

class GameplayState : public IGameState {
public:
    GameplayState(GameStateMachine& fsm,
                  Player& player,
                  InputContextManager& ctx,
                  InputManager& input)
            : m_fsm(fsm), m_player(player), m_context(ctx), m_input(input) {}

    void onEnter() override {
        // Ensure we are in Gameplay context
        // This might be redundant if this is the first state, but safe.
        if (m_context.getCurrentContext() != InputContextType::Gameplay) {
             m_context.switchContext(InputContextType::Gameplay);
        }
        // Gameplay requires captured mouse
        m_input.captureMouse(true);
    }

    void update(float dt, const InputSnapshot& snapshot) override {
        // Check for global transitions (e.g. open menu)
        if (m_context.isActionTriggered(Action::Menu)) {
            m_fsm.pushState(std::make_unique<UIState>(m_fsm, m_context, m_input));
            return;
        }

        // Update gameplay entities
        m_player.update(dt, snapshot, m_context);
    }

private:
    GameStateMachine& m_fsm;
    Player& m_player;
    InputContextManager& m_context;
    InputManager& m_input;
};

#endif //MECRAFT_GAMEPLAYSTATE_H

