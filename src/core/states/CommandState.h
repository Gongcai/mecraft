#ifndef MECRAFT_COMMANDSTATE_H
#define MECRAFT_COMMANDSTATE_H

#include <string>
#include <sstream>
#include <vector>

#include "../IGameState.h"
#include "../GameStateMachine.h"
#include "../InputContextManager.h"
#include "StateDependencies.h"
#include "../../ui/widgets/KeyboardInputBox.h"
#include "../../ui/core/UIRenderer.h"
#include "../../world/World.h"
#include "../../world/block/Block.h"
#include "../../world/DropSystem.h"
#include "../../particle/ParticleSystem.h"
#include "../../audio/AudioEngine.h"
#include "../../ecs/GameplayRegistry.h"
#include "../../ecs/util/PlayerQuery.h"
#include "../../ecs/entity/MobModelFactory.h"
#include "../../locale/LocaleManager.h"
#include <cstdlib>
namespace physics {
class PhysicsSystem;
}

class CommandState : public IGameState {
public:
    explicit CommandState(StateDependencies deps)
        : m_deps(deps),
          m_inputBox(128) {
    }

    void onEnter() override {
        m_deps.context.pushContext(InputContextType::UI);
        m_deps.input.captureMouse(false);
        m_inputBox.open("/");
    }

    void onExit() override {
        m_deps.context.popContext();
        if (m_deps.context.getCurrentContext() == InputContextType::Gameplay) {
            m_deps.input.captureMouse(true);
        }
    }

    void update(float dt, const InputSnapshot& snapshot) override {
        const std::string textBeforeUpdate = m_inputBox.getText();
        m_inputBox.update(snapshot, dt, &m_deps.context);

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
            m_deps.lastSubmittedCommand = submitted;
            m_deps.uiRenderer.appendCommandLine(submitted);
            const bool handledTransition = executeCommand(submitted);
            if (!handledTransition) {
                m_deps.fsm.popState();
            }
            return;
        }

        if (!appliedHistoryNavigation && m_historyCursor >= 0 && m_inputBox.getText() != textBeforeUpdate) {
            m_historyCursor = -1;
            m_historyDraft.clear();
        }

        if (m_inputBox.consumeCancel()) {
            m_deps.fsm.popState();
            return;
        }
    }

    void render() override {
        if (!m_inputBox.isOpen()) {
            return;
        }
        m_deps.uiRenderer.renderCommandInputBox(m_inputBox.getText());
    }

private:
    bool executeCommand(const std::string& command) {
        if (command.empty() || command[0] != '/') {
            return false;
        }

        std::istringstream iss(command.substr(1));
        std::string primary;
        iss >> primary;

        if (primary == "gamemode") {
            std::string secondary;
            iss >> secondary;
            if (secondary == "creative" || secondary == "1") {
                switchToCreativeMode();
                return true;
            } else if (secondary == "survival" || secondary == "0") {
                switchToSurvivalMode();
                return true;
            } else {
                m_deps.uiRenderer.appendWarningLine(m_deps.localeManager.tr("usage_gamemode"));
                return false;
            }
        }
        if (primary == "time") {
            std::string secondary;
            iss >> secondary;
            if (secondary == "set") {
                std::string valueStr;
                iss >> valueStr;
                if (valueStr.empty()) {
                    m_deps.uiRenderer.appendWarningLine(m_deps.localeManager.tr("usage_time_set"));
                    return false;
                }
                char* endPtr = nullptr;
                const float value = std::strtof(valueStr.c_str(), &endPtr);
                if (endPtr == valueStr.c_str() || value < 0.0f || value > 1200.0f) {
                    m_deps.uiRenderer.appendWarningLine(m_deps.localeManager.tr("usage_time_set"));
                    return false;
                }
                m_deps.world.getDayNightSystem().setTimeOfDay(value);
                m_deps.uiRenderer.appendCommandLine(m_deps.localeManager.tr("time_set_to") + std::to_string(static_cast<int>(value)));
                return false;
            } else {
                m_deps.uiRenderer.appendWarningLine(m_deps.localeManager.tr("usage_time_set"));
                return false;
            }
        }
        if (primary == "spawn") {
            std::string mobType;
            iss >> mobType;
            if (mobType == "zombie") {
                ecs::PlayerQuery query(m_deps.ecsRegistry);
                glm::vec3 playerPos = query.getPosition();
                ecs::MobModelFactory::createZombie(m_deps.ecsRegistry, playerPos);
                m_deps.uiRenderer.appendCommandLine(m_deps.localeManager.tr("spawned_zombie"));
                return false;
            }
            m_deps.uiRenderer.appendWarningLine(m_deps.localeManager.tr("unknown_mob") + mobType);
            return false;
        }
        m_deps.uiRenderer.appendWarningLine(m_deps.localeManager.tr("unknown_command") + primary);
        return false;


    }

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

    StateDependencies m_deps;
    KeyboardInputBox m_inputBox;
    std::string m_historyDraft;
    int m_historyCursor = -1;

    static constexpr size_t kMaxHistoryEntries = 50;
};

#endif // MECRAFT_COMMANDSTATE_H
