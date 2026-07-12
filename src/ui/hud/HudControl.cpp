#include "HudControl.h"

#include <algorithm>
#include <cmath>

#include <glm/vec4.hpp>

#include "../../renderer/rhi/RhiCommandList.h"
#include "../../resource/ResourceMgr.h"
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

[[nodiscard]] RhiRect2D hudScissor(const UIRenderContext& context)
{
    if (context.hasScissor) {
        return context.scissor;
    }
    return {
        0,
        0,
        static_cast<uint32_t>(std::max(1.0f,
            std::round(static_cast<float>(context.screenWidth) * context.pixelScale()))),
        static_cast<uint32_t>(std::max(1.0f,
            std::round(static_cast<float>(context.screenHeight) * context.pixelScale())))
    };
}

} // namespace

void HudControl::init(ResourceMgr& resourceMgr)
{
    UIWidget::init(resourceMgr);
    m_resourceMgr = &resourceMgr;

    m_heartFull = resourceMgr.getHudIconIndex("heart_full");
    m_heartHalf = resourceMgr.getHudIconIndex("heart_half");
    m_armorFull = resourceMgr.getHudIconIndex("armor_full");
    m_armorHalf = resourceMgr.getHudIconIndex("armor_half");
    m_foodFull  = resourceMgr.getHudIconIndex("food_full");
    m_foodHalf  = resourceMgr.getHudIconIndex("food_half");
}

void HudControl::shutdown()
{
    m_resourceMgr = nullptr;
    UIWidget::shutdown();
}

void HudControl::drawIconRow(const UIRenderContext& context,
                                const TextureAtlas& atlas,
                                const float startX, const float startY,
                                const int current, const int max,
                                const int fullIndex, const int halfIndex,
                                const float iconSize) const
{
    if (fullIndex < 0 || context.commandList == nullptr || context.uiRenderer == nullptr) {
        return;
    }

    const int fullIcons = std::clamp(current / 2, 0, max / 2);
    const bool hasHalf = (current % 2 != 0) && (current < max);
    const auto fullUV = atlas.getUV(fullIndex);
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
            glm::vec4(static_cast<float>(context.screenWidth),
                      static_cast<float>(context.screenHeight), x, startY),
            glm::vec4(iconSize, iconSize, 0.0f, 0.0f),
            glm::vec4(uv.first.x, uv.first.y, uv.second.x, uv.second.y),
            glm::vec4(1.0f)
        };
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Vertex) |
                                  rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(6u, 1u, 0u, 0u);
    };

    for (int i = 0; i < fullIcons; ++i) {
        drawIcon(startX + static_cast<float>(i) * iconSize, fullUV);
    }

    if (hasHalf && halfIndex >= 0) {
        const auto halfUV = atlas.getUV(halfIndex);
        drawIcon(startX + static_cast<float>(fullIcons) * iconSize, halfUV);
    }
}

void HudControl::renderSelf(const UIRenderContext& context) const
{
    if (!visible || !context.playerStats || !m_resourceMgr ||
        context.commandList == nullptr || context.uiRenderer == nullptr ||
        !context.panelQuadVertexBuffer.isValid() ||
        !context.imageTexturePipeline.isValid() ||
        context.screenWidth <= 0 || context.screenHeight <= 0) {
        return;
    }

    const TextureAtlas& atlas = m_resourceMgr->getHudIconAtlas();
    if (!atlas.texture.isValid()) {
        return;
    }

    const PlayerStatsData& stats = *context.playerStats;

    // In creative mode, hide health/food/armor bars
    if (!stats.showSurvivalStats) {
        return;
    }

    const float screenW = static_cast<float>(context.screenWidth);

    constexpr float kIconNativeSize = 8.0f;
    constexpr float kScale = 2.0f;
    const float iconSize = kIconNativeSize * kScale; // 16px

    const float hudBaseY = HotbarLayout::kBottomMargin + HotbarLayout::kHeight + 4.0f;

    const int heartMax = stats.maxHealth;
    const int foodMax = stats.maxFood;

    // Align with hotbar edges
    const float hotbarLeftX = (screenW - HotbarLayout::kWidth) * 0.5f;
    const float hotbarRightX = hotbarLeftX + HotbarLayout::kWidth;

    // Hearts: left-aligned to hotbar left edge
    const float heartStartX = hotbarLeftX;
    // Food: right-aligned to hotbar right edge
    const float foodStartX = hotbarRightX - static_cast<float>(foodMax) * 0.5f * iconSize;

    // Health row
    drawIconRow(context, atlas, heartStartX, hudBaseY,
                  stats.health, heartMax, m_heartFull, m_heartHalf, iconSize);

    // Food row (right side)
    drawIconRow(context, atlas, foodStartX, hudBaseY,
                  stats.food, foodMax, m_foodFull, m_foodHalf, iconSize);

    // Armor row (above hearts, only when armor > 0)
    const int armorVal = stats.armor;
    if (armorVal > 0) {
        const int armorMax = stats.maxArmor;
        const float armorY = hudBaseY + iconSize + 2.0f;
        drawIconRow(context, atlas, heartStartX, armorY,
                      armorVal, armorMax, m_armorFull, m_armorHalf, iconSize);
    }

}
