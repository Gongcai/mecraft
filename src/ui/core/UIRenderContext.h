#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include <glm/vec2.hpp>

#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiResources.h"
#include "UITheme.h"
#include "UIScaleConfig.h"
#include "../layout/UILayout.h"

struct GameResources;
class LocaleManager;
class Inventory;
class TextRenderer;
class HumanoidRenderer;
class RhiDevice;
class RhiCommandList;
class UIRenderer;

enum class UIRenderPhase { CollectText, Record };

struct PlayerStatsData {
    int health = 20;
    int maxHealth = 20;
    int armor = 0;
    int maxArmor = 20;
    int food = 20;
    int maxFood = 20;
    int air = 300;
    int maxAir = 300;
    bool eyesInWater = false;
    bool showSurvivalStats = true; // false in creative mode
    bool isDead = false;
};

struct UIRenderContext {
    UIRenderPhase phase = UIRenderPhase::Record;
    int windowWidth = 0; // Logical GLFW window dimensions.
    int windowHeight = 0;
    int framebufferWidth = 0; // Physical framebuffer dimensions.
    int framebufferHeight = 0;
    int uiWidth = 0; // Bottom-left-origin UI reference dimensions.
    int uiHeight = 0;

    // Unified scale configuration
    UIScaleConfig scaleConfig;

    float timeSeconds = 0.0f;
    GameResources* resources = nullptr;
    RhiDevice* rhiDevice = nullptr;
    HumanoidRenderer* humanoidRenderer = nullptr;
    const Inventory* inventory = nullptr;
    const PlayerStatsData* playerStats = nullptr;
    bool playerDead = false;
    const TextRenderer* textRenderer = nullptr;
    const std::string* commandInputText = nullptr;
    bool commandInputVisible = false;
    float pointerX = 0.0f; // Bottom-left-origin UI reference coordinates.
    float pointerY = 0.0f;
    bool hasDraggedItem = false;
    int draggedItemId = 0;
    const UITheme* theme = nullptr;
    const LocaleManager* localeManager = nullptr;
    RhiTextureHandle backdropBlur;
    RhiTextureViewHandle backdropBlurView;
    RhiBufferHandle panelQuadVertexBuffer;
    RhiPipelineHandle panelSolidPipeline;
    RhiPipelineHandle panelGlassPipeline;
    RhiBindGroupHandle panelGlassBindGroup;
    RhiPipelineHandle imageTexturePipeline;
    const UIRenderer* uiRenderer = nullptr;
    RhiCommandList* commandList = nullptr;
    bool hasScissor = false;
    RhiRect2D scissor; // Bottom-left-origin physical framebuffer pixels.
    bool backdropBlurPrepared = false;
    int backdropSourceWidth = 0;
    int backdropSourceHeight = 0;
    int backdropBlurWidth = 0;
    int backdropBlurHeight = 0;

    // Converts a GLFW top-left-origin window cursor position into UI reference coordinates.
    [[nodiscard]] glm::vec2 windowToUi(const glm::vec2& point) const {
        const float safeWindowWidth = static_cast<float>(std::max(1, windowWidth));
        const float safeWindowHeight = static_cast<float>(std::max(1, windowHeight));
        const float width = static_cast<float>(std::max(1, uiWidth));
        const float height = static_cast<float>(std::max(1, uiHeight));
        return {point.x * width / safeWindowWidth, height - point.y * height / safeWindowHeight};
    }

    [[nodiscard]] float uiToFramebufferScaleX() const {
        return uiWidth > 0 ? static_cast<float>(framebufferWidth) / static_cast<float>(uiWidth) : 1.0f;
    }

    [[nodiscard]] float uiToFramebufferScaleY() const {
        return uiHeight > 0 ? static_cast<float>(framebufferHeight) / static_cast<float>(uiHeight) : 1.0f;
    }

    [[nodiscard]] RhiRect2D fullFramebufferScissor() const {
        if (hasScissor) {
            return scissor;
        }
        return {0, 0, static_cast<uint32_t>(std::max(1, framebufferWidth)),
                static_cast<uint32_t>(std::max(1, framebufferHeight))};
    }

    // Converts a bottom-left-origin UI rectangle into a clipped framebuffer scissor.
    [[nodiscard]] RhiRect2D uiRectToFramebufferScissor(float x, float y, float width, float height) const {
        const float scaleX = uiToFramebufferScaleX();
        const float scaleY = uiToFramebufferScaleY();
        const int32_t x0 = static_cast<int32_t>(std::floor(x * scaleX));
        const int32_t y0 = static_cast<int32_t>(std::floor(y * scaleY));
        const int32_t x1 = static_cast<int32_t>(std::ceil((x + width) * scaleX));
        const int32_t y1 = static_cast<int32_t>(std::ceil((y + height) * scaleY));
        const RhiRect2D rect{x0, y0, static_cast<uint32_t>(std::max(0, x1 - x0)),
                             static_cast<uint32_t>(std::max(0, y1 - y0))};
        const RhiRect2D clip = fullFramebufferScissor();
        const int32_t clipX0 = std::max(rect.x, clip.x);
        const int32_t clipY0 = std::max(rect.y, clip.y);
        const int32_t clipX1 = std::min(rect.x + static_cast<int32_t>(rect.width),
                                        clip.x + static_cast<int32_t>(clip.width));
        const int32_t clipY1 = std::min(rect.y + static_cast<int32_t>(rect.height),
                                        clip.y + static_cast<int32_t>(clip.height));
        return {clipX0, clipY0, static_cast<uint32_t>(std::max(0, clipX1 - clipX0)),
                static_cast<uint32_t>(std::max(0, clipY1 - clipY0))};
    }

    // Helper: Get anchor position in UI coordinates.
    [[nodiscard]] glm::vec2 getAnchorPosition(Anchor anchor) const {
        const float w = static_cast<float>(uiWidth);
        const float h = static_cast<float>(uiHeight);

        switch (anchor) {
        case Anchor::TopLeft: return {0.0f, h};
        case Anchor::TopCenter: return {w * 0.5f, h};
        case Anchor::TopRight: return {w, h};
        case Anchor::CenterLeft: return {0.0f, h * 0.5f};
        case Anchor::Center: return {w * 0.5f, h * 0.5f};
        case Anchor::CenterRight: return {w, h * 0.5f};
        case Anchor::BottomLeft: return {0.0f, 0.0f};
        case Anchor::BottomCenter: return {w * 0.5f, 0.0f};
        case Anchor::BottomRight: return {w, 0.0f};
        }
        return {0.0f, 0.0f};
    }

    // Helper: Get scale for a specific strategy
    [[nodiscard]] float getScaleForStrategy(UIScaleStrategy strategy) const {
        return scaleConfig.getScaleForStrategy(strategy);
    }

};
