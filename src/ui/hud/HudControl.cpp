#include "HudControl.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>

#include <glm/vec4.hpp>

#include "../../renderer/rhi/RhiCommandList.h"
#include "../../resource/GameResources.h"
#include "../layout/UILayout.h"
#include "../core/UIRenderer.h"

namespace {

struct HudImagePushConstants {
    glm::vec4 screenRect;
    glm::vec4 extent;
    glm::vec4 uvRect;
    glm::vec4 tint;
};

static_assert(sizeof(HudImagePushConstants) == 64u);

[[nodiscard]] RhiRect2D hudScissor(const UIRenderContext& context) {
    return context.fullFramebufferScissor();
}

} // namespace

void HudControl::init(GameResources& resources, RhiDevice& rhiDevice) {
    UIWidget::init(resources, rhiDevice);
    m_resources = &resources;

    m_heartFull = resources.uiTextures.hudIconIndex("heart_full");
    m_heartHalf = resources.uiTextures.hudIconIndex("heart_half");
    m_heartContainer = resources.uiTextures.hudIconIndex("heart_container");
    m_armorFull = resources.uiTextures.hudIconIndex("armor_full");
    m_armorHalf = resources.uiTextures.hudIconIndex("armor_half");
    m_armorEmpty = resources.uiTextures.hudIconIndex("armor_empty");
    m_foodFull = resources.uiTextures.hudIconIndex("food_full");
    m_foodHalf = resources.uiTextures.hudIconIndex("food_half");
    m_foodEmpty = resources.uiTextures.hudIconIndex("food_empty");
    m_airFull = resources.uiTextures.hudIconIndex("air");
    m_airEmpty = resources.uiTextures.hudIconIndex("air_empty");

    const std::array<std::pair<const char*, int>, 10> requiredIcons{{
        {"heart_full", m_heartFull}, {"heart_half", m_heartHalf}, {"heart_container", m_heartContainer},
        {"armor_full", m_armorFull}, {"armor_half", m_armorHalf}, {"armor_empty", m_armorEmpty},
        {"food_full", m_foodFull},   {"food_half", m_foodHalf},   {"food_empty", m_foodEmpty},
        {"air", m_airFull},
    }};
    for (const auto& [name, index] : requiredIcons) {
        if (index < 0) {
            std::fprintf(stderr, "[HUD] Missing required HUD sprite: %s\n", name);
        }
    }
    if (m_airEmpty < 0) {
        std::fprintf(stderr, "[HUD] Missing required HUD sprite: air_empty\n");
    }
}

void HudControl::shutdown() {
    m_resources = nullptr;
    UIWidget::shutdown();
}

void HudControl::drawMeterRow(const UIRenderContext& context, const TextureAtlas& atlas, const float startX,
                              const float startY, const int current, const int max, const int fullIndex,
                              const int halfIndex, const int emptyIndex, const float iconSize,
                              const float iconStride) const {
    if (fullIndex < 0 || emptyIndex < 0 || context.commandList == nullptr || context.uiRenderer == nullptr ||
        max <= 0) {
        return;
    }

    const int slots = (max + 1) / 2;
    const int value = std::clamp(current, 0, max);
    const auto fullUV = atlas.getUV(fullIndex);
    const auto emptyUV = atlas.getUV(emptyIndex);
    const auto halfUV = halfIndex >= 0 ? atlas.getUV(halfIndex) : fullUV;
    const RhiBindGroupHandle bindGroup = context.uiRenderer->resolveImageBindGroup(atlas.texture);
    if (!bindGroup.isValid()) {
        return;
    }

    RhiCommandList& commandList = *context.commandList;
    commandList.setGraphicsPipeline(context.imageTexturePipeline);
    commandList.setVertexBuffer(0u, context.panelQuadVertexBuffer, 0u);
    commandList.setBindGroup(0u, bindGroup);
    commandList.setScissor(hudScissor(context));

    const auto drawIcon = [&](const float x, const auto& uv) {
        const HudImagePushConstants pushConstants{
            glm::vec4(static_cast<float>(context.uiWidth), static_cast<float>(context.uiHeight), x, startY),
            glm::vec4(iconSize, iconSize, 0.0f, 0.0f), glm::vec4(uv.first.x, uv.first.y, uv.second.x, uv.second.y),
            glm::vec4(1.0f)};
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(6u, 1u, 0u, 0u);
    };

    for (int i = 0; i < slots; ++i) {
        const float x = startX + static_cast<float>(i) * iconStride;
        drawIcon(x, emptyUV);
        const int slotValue = std::clamp(value - i * 2, 0, 2);
        if (slotValue == 2) {
            drawIcon(x, fullUV);
        } else if (slotValue == 1) {
            drawIcon(x, halfUV);
        }
    }
}

void HudControl::drawAirRow(const UIRenderContext& context, const TextureAtlas& atlas, const float startX,
                            const float startY, const int current, const int max, const float iconSize,
                            const float iconStride) const {
    if (m_airFull < 0 || m_airEmpty < 0 || context.commandList == nullptr || context.uiRenderer == nullptr ||
        max <= 0) {
        return;
    }

    constexpr int kAirPerBubble = 30;
    const int slots = (max + kAirPerBubble - 1) / kAirPerBubble;
    const int filledSlots = (std::clamp(current, 0, max) + kAirPerBubble - 1) / kAirPerBubble;
    const auto fullUV = atlas.getUV(m_airFull);
    const auto emptyUV = atlas.getUV(m_airEmpty);
    const RhiBindGroupHandle bindGroup = context.uiRenderer->resolveImageBindGroup(atlas.texture);
    if (!bindGroup.isValid()) {
        return;
    }

    RhiCommandList& commandList = *context.commandList;
    commandList.setGraphicsPipeline(context.imageTexturePipeline);
    commandList.setVertexBuffer(0u, context.panelQuadVertexBuffer, 0u);
    commandList.setBindGroup(0u, bindGroup);
    commandList.setScissor(hudScissor(context));

    for (int i = 0; i < slots; ++i) {
        const float x = startX + static_cast<float>(i) * iconStride;
        const auto& uv = i < filledSlots ? fullUV : emptyUV;
        const HudImagePushConstants pushConstants{
            glm::vec4(static_cast<float>(context.uiWidth), static_cast<float>(context.uiHeight), x, startY),
            glm::vec4(iconSize, iconSize, 0.0f, 0.0f), glm::vec4(uv.first.x, uv.first.y, uv.second.x, uv.second.y),
            glm::vec4(1.0f)};
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(6u, 1u, 0u, 0u);
    }
}

void HudControl::renderSelf(const UIRenderContext& context) const {
    if (!visible || !context.playerStats || !m_resources || context.commandList == nullptr ||
        context.uiRenderer == nullptr || !context.panelQuadVertexBuffer.isValid() ||
        !context.imageTexturePipeline.isValid() || context.uiWidth <= 0 || context.uiHeight <= 0) {
        return;
    }

    const TextureAtlas& atlas = m_resources->uiTextures.hudIconAtlas();
    if (!atlas.texture.isValid()) {
        return;
    }

    const PlayerStatsData& stats = *context.playerStats;

    // In creative mode, hide health/food/armor bars
    if (!stats.showSurvivalStats) {
        return;
    }

    const float screenW = static_cast<float>(context.uiWidth);

    constexpr float kIconNativeSize = 9.0f;
    constexpr float kScale = 2.0f;
    constexpr float kIconNativeStride = 8.0f;
    const float iconSize = kIconNativeSize * kScale;
    const float iconStride = kIconNativeStride * kScale;

    const float hudBaseY = HotbarLayout::kBottomMargin + HotbarLayout::kHeight + 4.0f;

    const int heartMax = stats.maxHealth;
    const int foodMax = stats.maxFood;

    // Align with hotbar edges
    const float hotbarLeftX = (screenW - HotbarLayout::kWidth) * 0.5f;
    const float hotbarRightX = hotbarLeftX + HotbarLayout::kWidth;

    // Hearts: left-aligned to hotbar left edge
    const float heartStartX = hotbarLeftX;
    // Food: right-aligned to hotbar right edge
    const int foodSlots = std::max(1, (foodMax + 1) / 2);
    const float foodStartX = hotbarRightX - static_cast<float>(foodSlots - 1) * iconStride - iconSize;

    // Health row
    drawMeterRow(context, atlas, heartStartX, hudBaseY, stats.health, heartMax, m_heartFull, m_heartHalf,
                 m_heartContainer, iconSize, iconStride);

    // Food row (right side)
    drawMeterRow(context, atlas, foodStartX, hudBaseY, stats.food, foodMax, m_foodFull, m_foodHalf, m_foodEmpty,
                 iconSize, iconStride);

    if (stats.eyesInWater) {
        const int airSlots = std::max(1, (stats.maxAir + 29) / 30);
        const float airStartX = hotbarRightX - static_cast<float>(airSlots - 1) * iconStride - iconSize;
        drawAirRow(context, atlas, airStartX, hudBaseY + iconSize + 2.0f, stats.air, stats.maxAir, iconSize,
                   iconStride);
    }

    // Armor row (above hearts, only when armor > 0)
    const int armorVal = stats.armor;
    if (armorVal > 0) {
        const int armorMax = stats.maxArmor;
        const float armorY = hudBaseY + iconSize + 2.0f;
        drawMeterRow(context, atlas, heartStartX, armorY, armorVal, armorMax, m_armorFull, m_armorHalf,
                      m_armorEmpty, iconSize, iconStride);
    }
}
