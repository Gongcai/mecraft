#include "CommandState.h"
#include "GameplayState.h"
#include "CreativeModeState.h"
#include "../../world/World.h"
#include "../../world/DropSystem.h"
#include "../../particle/ParticleSystem.h"
#include "../../audio/AudioEngine.h"
#include "../../physics/PhysicsSystem.h"

void CommandState::switchToCreativeMode() {
    m_deps.fsm.changeState(std::make_unique<CreativeModeState>(m_deps));
    m_deps.uiRenderer.appendSuccessLine(m_deps.localeManager.tr("switched_creative"));
}

void CommandState::switchToSurvivalMode() {
    m_deps.fsm.changeState(std::make_unique<GameplayState>(m_deps));
    m_deps.uiRenderer.appendSuccessLine(m_deps.localeManager.tr("switched_survival"));
}
