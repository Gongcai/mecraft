#include "CommandState.h"
#include "GameplayState.h"
#include <sstream>
#include "../modes/CreativeModeState.h"
#include "../../world/World.h"
#include "../../world/DropSystem.h"
#include "../../particle/ParticleSystem.h"
#include "../../audio/AudioEngine.h"
#include "../../physics/PhysicsSystem.h"
#include "../../ecs/util/PlayerQuery.h"
#include "../../ecs/entity/MobModelFactory.h"
#include "../../ecs/GameplayRegistry.h"
#include "../../locale/LocaleManager.h"

bool CommandState::executeCommand(const std::string& command) {
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

void CommandState::switchToCreativeMode() {
    m_deps.fsm.changeState(std::make_unique<CreativeModeState>(m_deps));
    m_deps.uiRenderer.appendSuccessLine(m_deps.localeManager.tr("switched_creative"));
}

void CommandState::switchToSurvivalMode() {
    m_deps.fsm.changeState(std::make_unique<GameplayState>(m_deps));
    m_deps.uiRenderer.appendSuccessLine(m_deps.localeManager.tr("switched_survival"));
}
