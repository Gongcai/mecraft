#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace ui {

enum class ContainerSlotGroupKind {
    Container,
    PlayerInventory,
    CraftingInput,
    CraftingResult
};

enum class ContainerProgressKind {
    Burn,
    Cook
};

struct ContainerSlotGroupDef {
    std::string id;
    ContainerSlotGroupKind kind = ContainerSlotGroupKind::Container;
    int firstSlot = 0;
    int columns = 1;
    int rows = 1;
    float x = 0.0f;
    float y = 0.0f;
    float slotSize = 18.0f;
    float columnGap = 0.0f;
    float rowGap = 0.0f;
    float row4ExtraGap = 0.0f;
};

struct ContainerProgressDef {
    std::string id;
    ContainerProgressKind kind = ContainerProgressKind::Burn;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float textureX = 0.0f;
    float textureY = 0.0f;
    std::string direction = "right";
};

struct ContainerUiDef {
    std::string id;
    std::string behavior;
    std::string backgroundTexture;
    std::string backgroundTexturePath;
    float width = 0.0f;
    float height = 0.0f;
    float textureWidth = 0.0f;
    float textureHeight = 0.0f;
    float anchorX = 0.5f;
    float anchorY = 0.5f;
    float pivotX = 0.5f;
    float pivotY = 0.5f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float scale = 1.0f;
    float fitPadding = 8.0f;
    bool showPlayerPreview = false;
    std::vector<ContainerSlotGroupDef> slotGroups;
    std::vector<ContainerProgressDef> progressBars;
};

class ContainerUiRegistry {
public:
    [[nodiscard]] static bool init();
    [[nodiscard]] static bool ensureInitialized();
    [[nodiscard]] static const ContainerUiDef& require(const std::string& id);
    [[nodiscard]] static bool tryGet(const std::string& id, const ContainerUiDef*& outDef);
    [[nodiscard]] static const std::unordered_map<std::string, ContainerUiDef>& all();

private:
    static bool s_initialized;
    static std::unordered_map<std::string, ContainerUiDef> s_defs;
};

} // namespace ui
