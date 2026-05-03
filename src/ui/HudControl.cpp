#include "HudControl.h"

#include <algorithm>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../player/Player.h"
#include "../renderer/Shader.h"
#include "../resource/ResourceMgr.h"
#include "UILayout.h"

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

void HudControl::setVisible(bool visible)
{
    m_visible = visible;
}

bool HudControl::isVisible() const
{
    return m_visible;
}

UIEventResult HudControl::onInput(const UIInputEvent&)
{
    return UIEventResult::Ignored;
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
        const float x1 = x0 + iconSize;
        const float y1 = y0 + iconSize;
        verts.push_back(x0); verts.push_back(y0); verts.push_back(fullUV.first.x);  verts.push_back(fullUV.first.y);
        verts.push_back(x1); verts.push_back(y0); verts.push_back(fullUV.second.x); verts.push_back(fullUV.first.y);
        verts.push_back(x1); verts.push_back(y1); verts.push_back(fullUV.second.x); verts.push_back(fullUV.second.y);
        verts.push_back(x0); verts.push_back(y0); verts.push_back(fullUV.first.x);  verts.push_back(fullUV.first.y);
        verts.push_back(x1); verts.push_back(y1); verts.push_back(fullUV.second.x); verts.push_back(fullUV.second.y);
        verts.push_back(x0); verts.push_back(y1); verts.push_back(fullUV.first.x);  verts.push_back(fullUV.second.y);
    }

    if (hasHalf) {
        const float x0 = startX + static_cast<float>(fullIcons) * iconSize;
        const float y0 = startY;
        const float x1 = x0 + iconSize;
        const float y1 = y0 + iconSize;
        verts.push_back(x0); verts.push_back(y0); verts.push_back(halfUV.first.x);  verts.push_back(halfUV.first.y);
        verts.push_back(x1); verts.push_back(y0); verts.push_back(halfUV.second.x); verts.push_back(halfUV.first.y);
        verts.push_back(x1); verts.push_back(y1); verts.push_back(halfUV.second.x); verts.push_back(halfUV.second.y);
        verts.push_back(x0); verts.push_back(y0); verts.push_back(halfUV.first.x);  verts.push_back(halfUV.first.y);
        verts.push_back(x1); verts.push_back(y1); verts.push_back(halfUV.second.x); verts.push_back(halfUV.second.y);
        verts.push_back(x0); verts.push_back(y1); verts.push_back(halfUV.first.x);  verts.push_back(halfUV.second.y);
    }
}

void HudControl::render(const UIRenderContext& context) const
{
    if (!m_visible || !context.player || !m_inventoryShader || !m_resourceMgr) {
        return;
    }

    const TextureAtlas& atlas = m_resourceMgr->getHudIconAtlas();
    if (atlas.textureID == 0) {
        return;
    }

    const Player& player = *context.player;
    const float screenW = static_cast<float>(context.screenWidth);
    const float screenH = static_cast<float>(context.screenHeight);

    constexpr float kIconNativeSize = 8.0f;
    constexpr float kScale = 2.0f;
    const float iconSize = kIconNativeSize * kScale; // 16px

    const float hudBaseY = HotbarLayout::kBottomMargin + HotbarLayout::kHeight + 4.0f;

    const int heartMax = player.getMaxHealth();
    const int foodMax = player.getMaxFood();

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
                  player.getHealth(), heartMax, m_heartFull, m_heartHalf, iconSize);

    // Food row (right side)
    appendIconRow(verts, atlas, foodStartX, hudBaseY,
                  player.getFood(), foodMax, m_foodFull, m_foodHalf, iconSize);

    // Armor row (above hearts, only when armor > 0)
    const int armorVal = player.getArmor();
    if (armorVal > 0) {
        const int armorMax = player.getMaxArmor();
        const float armorY = hudBaseY + iconSize + 2.0f;
        appendIconRow(verts, atlas, heartStartX, armorY,
                      armorVal, armorMax, m_armorFull, m_armorHalf, iconSize);
    }

    if (verts.empty()) {
        return;
    }

    const int vertCount = static_cast<int>(verts.size() / 4);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_inventoryShader->use();
    m_inventoryShader->setVec2("uScreenSize", glm::vec2(screenW, screenH));
    m_inventoryShader->setVec4("uTintColor", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

    glActiveTexture(GL_TEXTURE0);
    m_inventoryShader->setInt("uAtlas", 0);
    glBindTexture(GL_TEXTURE_2D, atlas.textureID);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, vertCount);
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
