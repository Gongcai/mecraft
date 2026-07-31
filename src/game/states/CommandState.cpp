#include "CommandState.h"
#include <sstream>
#include "../../world/World.h"
#include "../../ecs/util/PlayerQuery.h"
#include "../../ecs/entity/EntityFactory.h"
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
            m_ctx.uiRenderer.appendWarningLine(m_ctx.localeManager.tr("usage_gamemode"));
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
                m_ctx.uiRenderer.appendWarningLine(m_ctx.localeManager.tr("usage_time_set"));
                return false;
            }
            char* endPtr = nullptr;
            const float value = std::strtof(valueStr.c_str(), &endPtr);
            if (endPtr == valueStr.c_str() || value < 0.0f || value > 1200.0f) {
                m_ctx.uiRenderer.appendWarningLine(m_ctx.localeManager.tr("usage_time_set"));
                return false;
            }
            m_ctx.world.getDayNightSystem().setTimeOfDay(value);
            m_ctx.uiRenderer.appendCommandLine(m_ctx.localeManager.tr("time_set_to") +
                                               std::to_string(static_cast<int>(value)));
            return false;
        } else {
            m_ctx.uiRenderer.appendWarningLine(m_ctx.localeManager.tr("usage_time_set"));
            return false;
        }
    }
    if (primary == "spawn") {
        std::string mobType;
        iss >> mobType;
        const std::string entityId = mobType.find(':') == std::string::npos ? "minecraft:" + mobType : mobType;
        ecs::PlayerQuery query(m_ctx.ecsRegistry);
        glm::vec3 playerPos = query.getPosition();
        if (ecs::EntityFactory::createMob(m_ctx.ecsRegistry, entityId, playerPos) != entt::null) {
            m_ctx.uiRenderer.appendCommandLine(m_ctx.localeManager.tr("spawned_mob") + mobType);
            return false;
        }
        m_ctx.uiRenderer.appendWarningLine(m_ctx.localeManager.tr("unknown_mob") + mobType);
        return false;
    }
    m_ctx.uiRenderer.appendWarningLine(m_ctx.localeManager.tr("unknown_command") + primary);
    return false;
}

void CommandState::switchToCreativeMode() {
    if (m_makeCreativeModeState) {
        m_ctx.fsm.changeState(m_makeCreativeModeState());
    }
    m_ctx.uiRenderer.appendSuccessLine(m_ctx.localeManager.tr("switched_creative"));
}

void CommandState::switchToSurvivalMode() {
    if (m_makeSurvivalModeState) {
        m_ctx.fsm.changeState(m_makeSurvivalModeState());
    }
    m_ctx.uiRenderer.appendSuccessLine(m_ctx.localeManager.tr("switched_survival"));
}
