#ifndef MECRAFT_COMMANDSTATE_H
#define MECRAFT_COMMANDSTATE_H

#include <string>

#include "../IGameState.h"
#include "../GameStateMachine.h"
#include "../InputContextManager.h"
#include "../../ui/KeyboardInputBox.h"
#include "../../ui/UIRenderer.h"

class CommandState : public IGameState {
public:
    CommandState(GameStateMachine& fsm,
                 InputContextManager& context,
                 InputManager& input,
                 UIRenderer& uiRenderer,
                 std::string& lastSubmittedCommand)
        : m_fsm(fsm),
          m_context(context),
          m_input(input),
          m_uiRenderer(uiRenderer),
          m_lastSubmittedCommand(lastSubmittedCommand),
          m_inputBox(128) {
    }

    void onEnter() override {
        m_context.pushContext(InputContextType::UI);
        m_input.captureMouse(false);
        m_inputBox.open("/");
    }

    void onExit() override {
        m_context.popContext();
        if (m_context.getCurrentContext() == InputContextType::Gameplay) {
            m_input.captureMouse(true);
        }
    }

    void update(float dt, const InputSnapshot& snapshot) override {
        m_inputBox.update(snapshot, dt, &m_context);

        std::string submitted;
        if (m_inputBox.consumeSubmit(&submitted)) {
            m_lastSubmittedCommand = submitted;
            m_uiRenderer.appendCommandLine(submitted);
            m_fsm.popState();
            return;
        }

        if (m_inputBox.consumeCancel()) {
            m_fsm.popState();
            return;
        }
    }

    void render() override {
        m_uiRenderer.renderCommandInputBox(m_inputBox.getText());
    }

private:
    GameStateMachine& m_fsm;
    InputContextManager& m_context;
    InputManager& m_input;
    UIRenderer& m_uiRenderer;
    std::string& m_lastSubmittedCommand;
    KeyboardInputBox m_inputBox;
};

#endif // MECRAFT_COMMANDSTATE_H

