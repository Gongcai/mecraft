#pragma once

#include <string>
#include <unordered_map>
#include <vector>

enum class BlockInteractionActionKind { ToggleBooleanProperty, SetPropertyOnce, CycleProperty };

enum class BlockInteractionPartnerSync { None, DoorOpen };

struct BlockInteractionDef {
    std::string id;
    BlockInteractionActionKind action = BlockInteractionActionKind::ToggleBooleanProperty;
    std::string property;
    std::string falseValue;
    std::string trueValue;
    std::string setValue;
    std::vector<std::string> cycleValues;
    BlockInteractionPartnerSync partnerSync = BlockInteractionPartnerSync::None;
};

class BlockInteractionRegistry final {
public:
    [[nodiscard]] static bool init();
    [[nodiscard]] static bool ensureInitialized();
    [[nodiscard]] static const BlockInteractionDef& require(const std::string& id);
    [[nodiscard]] static const std::unordered_map<std::string, BlockInteractionDef>& all();

private:
    static bool s_initialized;
    static std::unordered_map<std::string, BlockInteractionDef> s_defs;
};
