#ifndef MECRAFT_GAMEPLAYMODERULES_H
#define MECRAFT_GAMEPLAYMODERULES_H

#include <string>
#include <random>
#include <algorithm>
#include "../../world/block/Block.h"
#include "../../world/block/BlockStateRegistry.h"
#include "../../world/fluid/FluidState.h"

namespace gameplay_state_detail {
    inline std::string getRandomName(const std::string& name, int maxRandomLength) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(1, maxRandomLength);
        return name + std::to_string(dist(gen));
    }
}

enum class GameplayBlockAction {
    None,
    Break,
    Place
};

enum class GameplayMode {
    Survival,
    Creative
};

struct GameplayBlockActionRequest {
    bool hasHit = false;
    bool wantsBreak = false;
    bool wantsPlace = false;
    float placeCooldownRemaining = 0.0f;
    BlockStateId targetBlock = NULL_BLOCK_STATE;
    bool playerWouldOverlapPlaceBlock = false;
    bool placementReplacesTarget = false;
};

namespace gameplay_mode_rules_detail {
    inline GameplayBlockAction decideDefaultBlockAction(const GameplayBlockActionRequest& request) {
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
        if (request.targetBlock != NULL_BLOCK_STATE &&
            !FluidState::isWater(request.targetBlock) &&
            !request.placementReplacesTarget) {
            return GameplayBlockAction::None;
        }
        if (request.playerWouldOverlapPlaceBlock) {
            return GameplayBlockAction::None;
        }
        return GameplayBlockAction::Place;
    }
}

class IGameplayModeRules {
public:
    virtual ~IGameplayModeRules() = default;
    [[nodiscard]] virtual GameplayBlockAction decideBlockAction(const GameplayBlockActionRequest& request) const = 0;
    [[nodiscard]] virtual float placeCooldownSeconds() const = 0;
    [[nodiscard]] virtual float breakDurationMs(BlockID targetBlock) const = 0;
    [[nodiscard]] virtual bool shouldReportBreakProgress() const = 0;
};

class SurvivalModeRules final : public IGameplayModeRules {
public:
    static const SurvivalModeRules& instance() {
        static SurvivalModeRules s_rules;
        return s_rules;
    }

    [[nodiscard]] GameplayBlockAction decideBlockAction(const GameplayBlockActionRequest& request) const override {
        return gameplay_mode_rules_detail::decideDefaultBlockAction(request);
    }

    [[nodiscard]] float placeCooldownSeconds() const override {
        return 0.18f;
    }

    [[nodiscard]] float breakDurationMs(BlockID targetBlock) const override {
        return std::max(1.0f, static_cast<float>(BlockRegistry::get(targetBlock).timeToBreak));
    }

    [[nodiscard]] bool shouldReportBreakProgress() const override {
        return true;
    }

private:
    SurvivalModeRules() = default;
};

class CreativeModeRules final : public IGameplayModeRules {
public:
    static const CreativeModeRules& instance() {
        static CreativeModeRules s_rules;
        return s_rules;
    }

    [[nodiscard]] GameplayBlockAction decideBlockAction(const GameplayBlockActionRequest& request) const override {
        return gameplay_mode_rules_detail::decideDefaultBlockAction(request);
    }

    [[nodiscard]] float placeCooldownSeconds() const override {
        return 0.18f;
    }

    [[nodiscard]] float breakDurationMs(BlockID /*targetBlock*/) const override {
        // Creative uses a fixed break speed and ignores per-block hardness.
        return 180.0f;
    }

    [[nodiscard]] bool shouldReportBreakProgress() const override {
        return false;
    }

private:
    CreativeModeRules() = default;
};

#endif //MECRAFT_GAMEPLAYMODERULES_H


