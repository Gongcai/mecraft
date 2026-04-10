#ifndef MECRAFT_GAMEPLAYMODERULES_H
#define MECRAFT_GAMEPLAYMODERULES_H

#include "../../world/Block.h"

enum class GameplayBlockAction {
    None,
    Break,
    Place
};

struct GameplayBlockActionRequest {
    bool hasHit = false;
    bool wantsBreak = false;
    bool wantsPlace = false;
    float placeCooldownRemaining = 0.0f;
    BlockID targetBlock = BlockType::AIR;
    bool playerWouldOverlapPlaceBlock = false;
};

class IGameplayModeRules {
public:
    virtual ~IGameplayModeRules() = default;
    [[nodiscard]] virtual GameplayBlockAction decideBlockAction(const GameplayBlockActionRequest& request) const = 0;
    [[nodiscard]] virtual float placeCooldownSeconds() const = 0;
};

class SurvivalModeRules final : public IGameplayModeRules {
public:
    static const SurvivalModeRules& instance() {
        static SurvivalModeRules s_rules;
        return s_rules;
    }

    [[nodiscard]] GameplayBlockAction decideBlockAction(const GameplayBlockActionRequest& request) const override {
        if (!request.hasHit) {
            return GameplayBlockAction::None;
        }
        if (request.wantsBreak) {
            return GameplayBlockAction::Break;
        }
        if (!request.wantsPlace) {
            return GameplayBlockAction::None;
        }
        if (request.placeCooldownRemaining > 0.0f) {
            return GameplayBlockAction::None;
        }
        if (request.targetBlock != BlockType::AIR && request.targetBlock != BlockType::WATER) {
            return GameplayBlockAction::None;
        }
        if (request.playerWouldOverlapPlaceBlock) {
            return GameplayBlockAction::None;
        }
        return GameplayBlockAction::Place;
    }

    [[nodiscard]] float placeCooldownSeconds() const override {
        return 0.18f;
    }

private:
    SurvivalModeRules() = default;
};

#endif //MECRAFT_GAMEPLAYMODERULES_H


