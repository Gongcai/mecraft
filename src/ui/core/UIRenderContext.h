#pragma once

#include <cstdint>
#include <string>

#include <glm/vec2.hpp>

#include "renderer/rhi/RhiHandles.h"
#include "UITheme.h"
#include "UIScaleConfig.h"
#include "../layout/UILayout.h"

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
    bool isDead = false;
};

struct UIRenderContext {
    // Screen dimensions (physical pixels)
    int screenWidth = 0;
    int screenHeight = 0;

    // Unified scale configuration
    UIScaleConfig scaleConfig;

    float timeSeconds = 0.0f;
    ResourceMgr* resourceMgr = nullptr;
    HumanoidRenderer* humanoidRenderer = nullptr;
    const Inventory* inventory = nullptr;
    const PlayerStatsData* playerStats = nullptr;
    bool playerDead = false;
    const TextRenderer* textRenderer = nullptr;
    const std::string* commandInputText = nullptr;
    bool commandInputVisible = false;
    float pointerX = 0.0f;
    float pointerY = 0.0f;
    bool hasDraggedItem = false;
    int draggedItemId = 0;
    const UITheme* theme = nullptr;
    const LocaleManager* localeManager = nullptr;
    RhiTextureHandle backdropBlur;
    int backdropSourceWidth = 0;
    int backdropSourceHeight = 0;
    int backdropBlurWidth = 0;
    int backdropBlurHeight = 0;

    // Helper: Get anchor position in virtual coordinates
    [[nodiscard]] glm::vec2 getAnchorPosition(Anchor anchor) const {
        const float w = static_cast<float>(scaleConfig.virtualWidth);
        const float h = static_cast<float>(scaleConfig.virtualHeight);

        switch (anchor) {
            case Anchor::TopLeft:      return {0.0f, h};
            case Anchor::TopCenter:    return {w * 0.5f, h};
            case Anchor::TopRight:     return {w, h};
            case Anchor::CenterLeft:   return {0.0f, h * 0.5f};
            case Anchor::Center:       return {w * 0.5f, h * 0.5f};
            case Anchor::CenterRight:  return {w, h * 0.5f};
            case Anchor::BottomLeft:   return {0.0f, 0.0f};
            case Anchor::BottomCenter: return {w * 0.5f, 0.0f};
            case Anchor::BottomRight:  return {w, 0.0f};
        }
        return {0.0f, 0.0f};
    }

    // Helper: Get scale for a specific strategy
    [[nodiscard]] float getScaleForStrategy(UIScaleStrategy strategy) const {
        return scaleConfig.getScaleForStrategy(strategy);
    }

    [[nodiscard]] float pixelScale() const {
        return scaleConfig.effectiveScale > 0.0f ? scaleConfig.effectiveScale : 1.0f;
    }
};
