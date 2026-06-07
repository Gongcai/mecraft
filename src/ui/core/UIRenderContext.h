#pragma once

#include <string>

#include "UITheme.h"

class ResourceMgr;
class LocaleManager;
class Inventory;
class TextRenderer;
class HumanoidRenderer;

struct PlayerStatsData {
    int health = 20;
    int maxHealth = 20;
    int armor = 0;
    int maxArmor = 20;
    int food = 20;
    int maxFood = 20;
    bool showSurvivalStats = true;  // false in creative mode
};

struct UIRenderContext {
    int screenWidth = 0;
    int screenHeight = 0;
    float uiScale = 1.0f;
    float timeSeconds = 0.0f;
    ResourceMgr* resourceMgr = nullptr;
    HumanoidRenderer* humanoidRenderer = nullptr;
    const Inventory* inventory = nullptr;
    const PlayerStatsData* playerStats = nullptr;
    const TextRenderer* textRenderer = nullptr;
    const std::string* commandInputText = nullptr;
    bool commandInputVisible = false;
    float pointerX = 0.0f;
    float pointerY = 0.0f;
    bool hasDraggedItem = false;
    int draggedItemId = 0;
    const UITheme* theme = nullptr;
    const LocaleManager* localeManager = nullptr;
    unsigned int backdropBlurTexture = 0;
    int backdropSourceWidth = 0;
    int backdropSourceHeight = 0;
    int backdropBlurWidth = 0;
    int backdropBlurHeight = 0;
};
