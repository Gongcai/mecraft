#ifndef MECRAFT_COMMANDSTATE_H
#define MECRAFT_COMMANDSTATE_H

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <cstdlib>

#include "IGameState.h"
#include "GameStateMachine.h"
#include "CommandStateContext.h"
#include "engine/input/InputContextManager.h"
#include "../../ui/widgets/KeyboardInputBox.h"
#include "../../ui/core/UIRenderer.h"

/// Handles command input and execution using command-specific dependencies.
class CommandState : public IGameState {
public:
    using StateFactory = std::function<std::unique_ptr<IGameState>()>;

    CommandState(CommandStateContext ctx, StateFactory makeCreativeModeState, StateFactory makeSurvivalModeState)
        : m_ctx(ctx),
          m_makeCreativeModeState(std::move(makeCreativeModeState)),
          m_makeSurvivalModeState(std::move(makeSurvivalModeState)),
          m_inputBox(128) {
    }

    void onEnter() override {
        m_ctx.context.pushContext(InputContextType::UI);
        m_ctx.input.captureMouse(false);
        m_inputBox.open("/");
    }

    void onExit() override {
        m_ctx.context.popContext();
        if (m_ctx.context.getCurrentContext() == InputContextType::Gameplay) {
            m_ctx.input.captureMouse(true);
        }
    }

    void update(float dt, const InputSnapshot& snapshot) override {
        const std::string textBeforeUpdate = m_inputBox.getText();
        m_inputBox.update(snapshot, dt, &m_ctx.context);

        bool appliedHistoryNavigation = false;
        if (m_inputBox.consumeHistoryPrev()) {
            recallPreviousCommand();
            appliedHistoryNavigation = true;
        } else if (m_inputBox.consumeHistoryNext()) {
            recallNextCommand();
            appliedHistoryNavigation = true;
        }

        std::string submitted;
        if (m_inputBox.consumeSubmit(&submitted)) {
            commitCommandToHistory(submitted);
            m_ctx.lastSubmittedCommand = submitted;
            m_ctx.uiRenderer.appendCommandLine(submitted);
            const bool handledTransition = executeCommand(submitted);
            if (!handledTransition) {
                m_ctx.fsm.popState();
            }
            return;
        }

        if (!appliedHistoryNavigation && m_historyCursor >= 0 && m_inputBox.getText() != textBeforeUpdate) {
            m_historyCursor = -1;
            m_historyDraft.clear();
        }

        if (m_inputBox.consumeCancel()) {
            m_ctx.fsm.popState();
            return;
        }
    }

    void render() override {
        if (!m_inputBox.isOpen()) {
            return;
        }
        m_ctx.uiRenderer.renderCommandInputBox(m_inputBox.getText());
    }

private:
    bool executeCommand(const std::string& command);

    void switchToCreativeMode();
    void switchToSurvivalMode();

    void commitCommandToHistory(const std::string& command) {
        auto& history = commandHistoryStore();
        if (command.empty()) {
            m_historyCursor = -1;
            m_historyDraft.clear();
            return;
        }

        if (history.empty() || history.back() != command) {
            history.push_back(command);
            if (history.size() > kMaxHistoryEntries) {
                history.erase(history.begin());
            }
        }

        m_historyCursor = -1;
        m_historyDraft.clear();
    }

    void recallPreviousCommand() {
        auto& history = commandHistoryStore();
        if (history.empty()) {
            return;
        }

        if (m_historyCursor < 0) {
            m_historyDraft = m_inputBox.getText();
            m_historyCursor = static_cast<int>(history.size()) - 1;
        } else if (m_historyCursor > 0) {
            --m_historyCursor;
        }

        m_inputBox.setText(history[static_cast<size_t>(m_historyCursor)]);
    }

    void recallNextCommand() {
        auto& history = commandHistoryStore();
        if (m_historyCursor < 0) {
            return;
        }

        const int lastIndex = static_cast<int>(history.size()) - 1;
        if (m_historyCursor < lastIndex) {
            ++m_historyCursor;
            m_inputBox.setText(history[static_cast<size_t>(m_historyCursor)]);
            return;
        }

        m_historyCursor = -1;
        m_inputBox.setText(m_historyDraft);
        m_historyDraft.clear();
    }

    static std::vector<std::string>& commandHistoryStore() {
        static std::vector<std::string> s_history;
        return s_history;
    }

    CommandStateContext m_ctx;
    StateFactory m_makeCreativeModeState;
    StateFactory m_makeSurvivalModeState;
    KeyboardInputBox m_inputBox;
    std::string m_historyDraft;
    int m_historyCursor = -1;

    static constexpr size_t kMaxHistoryEntries = 50;
};

#endif // MECRAFT_COMMANDSTATE_H
