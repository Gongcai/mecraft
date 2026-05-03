#pragma once

#include <string>

class ResourceMgr;
class Inventory;
class TextRenderer;

struct HeldItemPreviewMotion {
    bool moving = false;
    bool sprinting = false;
    float bobFrequency = 6.0f;
    float bobPhaseOffset = 0.0f;
};

struct PlayerStatsData {
    int health = 20;
    int maxHealth = 20;
    int armor = 0;
    int maxArmor = 20;
    int food = 20;
    int maxFood = 20;
};

struct UIRenderContext {
    int screenWidth = 0;
    int screenHeight = 0;
    float timeSeconds = 0.0f;
    ResourceMgr* resourceMgr = nullptr;
    const Inventory* inventory = nullptr;
    const PlayerStatsData* playerStats = nullptr;
    const TextRenderer* textRenderer = nullptr;
    const std::string* commandInputText = nullptr;
    bool commandInputVisible = false;
    float pointerX = 0.0f;
    float pointerY = 0.0f;
    bool hasDraggedItem = false;
    int draggedItemId = 0;
    HeldItemPreviewMotion heldItemPreviewMotion;
};
