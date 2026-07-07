#include "HudControl.h"

#include <glad/glad.h>

#include <algorithm>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../../renderer/core/Shader.h"
#include "../../renderer/rhi/gl/GlRhiTextureRegistry.h"
#include "../../resource/ResourceMgr.h"
#include "../layout/UILayout.h"
#include "../core/UIRenderUtils.h"

void HudControl::init(ResourceMgr& resourceMgr)
{
    m_inventoryShader = resourceMgr.getShader("inventory");
    m_resourceMgr = &resourceMgr;

    m_heartFull = resourceMgr.getHudIconIndex("heart_full");
    m_heartHalf = resourceMgr.getHudIconIndex("heart_half");
    m_armorFull = resourceMgr.getHudIconIndex("armor_full");
    m_armorHalf = resourceMgr.getHudIconIndex("armor_half");
    m_foodFull  = resourceMgr.getHudIconIndex("food_full");
    m_foodHalf  = resourceMgr.getHudIconIndex("food_half");

    initMesh();
}

void HudControl::shutdown()
{
    cleanupMesh();
    m_inventoryShader = nullptr;
    m_resourceMgr = nullptr;
}

void HudControl::initMesh()
{
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // Worst case: 10 hearts + 10 armor + 10 food = 30 quads = 180 vertices * 4 floats
    glBufferData(GL_ARRAY_BUFFER, 30 * 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void HudControl::cleanupMesh()
{
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
}

void HudControl::appendIconRow(std::vector<float>& verts,
                                const TextureAtlas& atlas,
                                const float startX, const float startY,
                                const int current, const int max,
                                const int fullIndex, const int halfIndex,
                                const float iconSize) const
{
    if (fullIndex < 0) {
        return;
    }

    const int fullIcons = std::clamp(current / 2, 0, max / 2);
    const bool hasHalf = (current % 2 != 0) && (current < max);
    const int emptyIcons = max / 2 - fullIcons - (hasHalf ? 1 : 0);
    static_cast<void>(emptyIcons);

    const auto fullUV = atlas.getUV(fullIndex);
    const auto halfUV = halfIndex >= 0 ? atlas.getUV(halfIndex) : fullUV;

    for (int i = 0; i < fullIcons; ++i) {
        const float x0 = startX + static_cast<float>(i) * iconSize;
        const float y0 = startY;
        UIRenderUtils::pushTexturedQuad(verts, x0, y0, x0 + iconSize, y0 + iconSize,
                                        fullUV.first.x, fullUV.first.y, fullUV.second.x, fullUV.second.y);
    }

    if (hasHalf) {
        const float x0 = startX + static_cast<float>(fullIcons) * iconSize;
        const float y0 = startY;
        UIRenderUtils::pushTexturedQuad(verts, x0, y0, x0 + iconSize, y0 + iconSize,
                                        halfUV.first.x, halfUV.first.y, halfUV.second.x, halfUV.second.y);
    }
}

void HudControl::renderSelf(const UIRenderContext& context) const
{
    if (!visible || !context.playerStats || !m_inventoryShader || !m_resourceMgr) {
        return;
    }

    const TextureAtlas& atlas = m_resourceMgr->getHudIconAtlas();
    const uint32_t atlasTextureId = renderer::rhi::gl::textureId(atlas.texture);
    if (atlasTextureId == 0) {
        return;
    }

    const PlayerStatsData& stats = *context.playerStats;

    // In creative mode, hide health/food/armor bars
    if (!stats.showSurvivalStats) {
        m_cachedVertCount = 0;
        m_dirty = true;
        return;
    }

    // Check if stats changed since last build
    if (!m_dirty &&
        m_cachedHealth == stats.health && m_cachedMaxHealth == stats.maxHealth &&
        m_cachedFood == stats.food && m_cachedMaxFood == stats.maxFood &&
        m_cachedArmor == stats.armor && m_cachedMaxArmor == stats.maxArmor &&
        m_cachedScreenW == context.screenWidth && m_cachedScreenH == context.screenHeight) {
        // Nothing changed — just re-draw cached vertices
        if (m_cachedVertCount > 0) {
            const UIRenderUtils::GLStateGuard glState;
            m_inventoryShader->use();
            m_inventoryShader->setVec2("uScreenSize", glm::vec2(static_cast<float>(context.screenWidth),
                                                                static_cast<float>(context.screenHeight)));
            m_inventoryShader->setVec4("uTintColor", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
            glActiveTexture(GL_TEXTURE0);
            m_inventoryShader->setInt("uAtlas", 0);
            glBindTexture(GL_TEXTURE_2D, atlasTextureId);
            glBindVertexArray(m_vao);
            glDrawArrays(GL_TRIANGLES, 0, m_cachedVertCount);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        return;
    }

    const float screenW = static_cast<float>(context.screenWidth);
    const float screenH = static_cast<float>(context.screenHeight);

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

    std::vector<float> verts;
    verts.reserve(30 * 6 * 4);

    // Health row
    appendIconRow(verts, atlas, heartStartX, hudBaseY,
                  stats.health, heartMax, m_heartFull, m_heartHalf, iconSize);

    // Food row (right side)
    appendIconRow(verts, atlas, foodStartX, hudBaseY,
                  stats.food, foodMax, m_foodFull, m_foodHalf, iconSize);

    // Armor row (above hearts, only when armor > 0)
    const int armorVal = stats.armor;
    if (armorVal > 0) {
        const int armorMax = stats.maxArmor;
        const float armorY = hudBaseY + iconSize + 2.0f;
        appendIconRow(verts, atlas, heartStartX, armorY,
                      armorVal, armorMax, m_armorFull, m_armorHalf, iconSize);
    }

    // Update cache
    m_cachedHealth = stats.health;
    m_cachedMaxHealth = stats.maxHealth;
    m_cachedFood = stats.food;
    m_cachedMaxFood = stats.maxFood;
    m_cachedArmor = stats.armor;
    m_cachedMaxArmor = stats.maxArmor;
    m_cachedScreenW = context.screenWidth;
    m_cachedScreenH = context.screenHeight;
    m_dirty = false;

    if (verts.empty()) {
        m_cachedVertCount = 0;
        return;
    }

    m_cachedVertCount = static_cast<int>(verts.size() / 4);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    const UIRenderUtils::GLStateGuard glState;

    m_inventoryShader->use();
    m_inventoryShader->setVec2("uScreenSize", glm::vec2(screenW, screenH));
    m_inventoryShader->setVec4("uTintColor", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

    glActiveTexture(GL_TEXTURE0);
    m_inventoryShader->setInt("uAtlas", 0);
    glBindTexture(GL_TEXTURE_2D, atlasTextureId);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, m_cachedVertCount);
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_2D, 0);
}
