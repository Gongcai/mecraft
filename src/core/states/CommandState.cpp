#include "CommandState.h"
#include "GameplayState.h"
#include "CreativeModeState.h"
#include "../../player/Player.h"
#include "../../world/World.h"
#include "../../world/DropSystem.h"
#include "../../particle/ParticleSystem.h"
#include "../../audio/AudioEngine.h"
#include "../../physics/PhysicsSystem.h"

void CommandState::switchToCreativeMode() {
    m_fsm.changeState(std::make_unique<CreativeModeState>(
        m_fsm,
        m_player,
        m_context,
        m_input,
        m_uiRenderer,
        m_lastSubmittedCommand,
        m_physicsSystem,
        m_world,
        m_audioEngine,
        m_particleSystem,
        m_dropSystem
    ));
    m_uiRenderer.appendSuccessLine("Switched to Creative Mode");
}

void CommandState::switchToSurvivalMode() {
    m_fsm.changeState(std::make_unique<GameplayState>(
        m_fsm,
        m_player,
        m_context,
        m_input,
        m_uiRenderer,
        m_lastSubmittedCommand,
        m_physicsSystem,
        m_world,
        m_audioEngine,
        m_particleSystem,
        m_dropSystem
    ));
    m_uiRenderer.appendSuccessLine("Switched to Survive Mode");

}
