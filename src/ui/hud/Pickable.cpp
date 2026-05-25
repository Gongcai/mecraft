#include "Pickable.h"
#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include "../../resource/ResourceMgr.h"
#include "../../renderer/core/Shader.h"
#include "../../item/Item.h"
#include "../font/TextRenderer.h"
#include "../core/UIRenderUtils.h"

void Pickable::initMesh(MeshHandles& mesh)
{
    if (mesh.vao != 0 || mesh.vbo != 0) {
        return;
    }

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);

    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    // Pre-allocate for one quad (hover background) to allow glBufferSubData
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void Pickable::shutdownMesh(MeshHandles& mesh)
{
    if (mesh.vao != 0) {
        glDeleteVertexArrays(1, &mesh.vao);
        mesh.vao = 0;
    }
    if (mesh.vbo != 0) {
        glDeleteBuffers(1, &mesh.vbo);
        mesh.vbo = 0;
    }
}

int Pickable::hitTest(const SlotInfo* slots, int count,
                      float mouseX, float mouseY)
{
    for (int i = 0; i < count; ++i)
    {
        if (mouseX >= slots[i].x &&
            mouseX <  slots[i].x + slots[i].size &&
            mouseY >= slots[i].y &&
            mouseY <  slots[i].y + slots[i].size)
        {
            return i;
        }
    }
    return -1;
}

void Pickable::render(const SlotInfo* slots, int count,
                      int hoveredIndex,
                      int screenW, int screenH,
                      const RenderParams& params,
                      Shader* crosshairShader,
                      Shader* inventoryShader,
                      const MeshHandles& mesh,
                      const ResourceMgr& resourceMgr,
                      const TextureAtlas& itemIconAtlas,
                      const TextureAtlas& itemTextureAtlas,
                      const TextRenderer* textRenderer)
{
    if (count <= 0 || mesh.vao == 0 || mesh.vbo == 0)
        return;

    const bool hasBakedItemIcons = (itemIconAtlas.textureID != 0 && itemIconAtlas.tilesPerRow > 0);
    const bool hasItemTextures = (itemTextureAtlas.textureID != 0 && itemTextureAtlas.tilesPerRow > 0);

    // ── Collect icon vertices (pass 2) ──
    std::vector<float> iconVerts;
    std::vector<float> fallbackIconVerts;
    for (int i = 0; i < count; ++i)
    {
        if (slots[i].itemId == 0)
            continue;

        const ItemID itemId = static_cast<ItemID>(slots[i].itemId);
        const ItemDef& itemDef = ItemRegistry::get(itemId);
        bool pushed = false;
        const auto& s = slots[i];
        // inventory shader expects bottom-left pixel coordinates.
        const float x0 = static_cast<float>(s.x);
        const float y0 = static_cast<float>(screenH - (s.y + s.size));
        const float x1 = static_cast<float>(s.x + s.size);
        const float y1 = static_cast<float>(screenH - s.y);

        if (hasItemTextures) {
            const int tileIndex = resourceMgr.getItemTextureIndex(itemDef.iconTextureName);
            if (tileIndex >= 0) {
                const auto uv = itemTextureAtlas.getUV(tileIndex);
                iconVerts.push_back(x0); iconVerts.push_back(y0); iconVerts.push_back(uv.first.x);  iconVerts.push_back(uv.first.y);
                iconVerts.push_back(x1); iconVerts.push_back(y0); iconVerts.push_back(uv.second.x); iconVerts.push_back(uv.first.y);
                iconVerts.push_back(x1); iconVerts.push_back(y1); iconVerts.push_back(uv.second.x); iconVerts.push_back(uv.second.y);
                iconVerts.push_back(x0); iconVerts.push_back(y0); iconVerts.push_back(uv.first.x);  iconVerts.push_back(uv.first.y);
                iconVerts.push_back(x1); iconVerts.push_back(y1); iconVerts.push_back(uv.second.x); iconVerts.push_back(uv.second.y);
                iconVerts.push_back(x0); iconVerts.push_back(y1); iconVerts.push_back(uv.first.x);  iconVerts.push_back(uv.second.y);
                pushed = true;
            }
        }

        if (!pushed && hasBakedItemIcons) {
            const auto uv = itemIconAtlas.getUV(static_cast<int>(itemDef.renderBlock));
            fallbackIconVerts.push_back(x0); fallbackIconVerts.push_back(y0); fallbackIconVerts.push_back(uv.first.x);  fallbackIconVerts.push_back(uv.first.y);
            fallbackIconVerts.push_back(x1); fallbackIconVerts.push_back(y0); fallbackIconVerts.push_back(uv.second.x); fallbackIconVerts.push_back(uv.first.y);
            fallbackIconVerts.push_back(x1); fallbackIconVerts.push_back(y1); fallbackIconVerts.push_back(uv.second.x); fallbackIconVerts.push_back(uv.second.y);
            fallbackIconVerts.push_back(x0); fallbackIconVerts.push_back(y0); fallbackIconVerts.push_back(uv.first.x);  fallbackIconVerts.push_back(uv.first.y);
            fallbackIconVerts.push_back(x1); fallbackIconVerts.push_back(y1); fallbackIconVerts.push_back(uv.second.x); fallbackIconVerts.push_back(uv.second.y);
            fallbackIconVerts.push_back(x0); fallbackIconVerts.push_back(y1); fallbackIconVerts.push_back(uv.first.x);  fallbackIconVerts.push_back(uv.second.y);
        }
    }

    const bool hasBg = (hoveredIndex >= 0 && hoveredIndex < count);
    const bool hasIcons = !iconVerts.empty() || !fallbackIconVerts.empty();
    if (!hasBg && !hasIcons)
        return;

    {
        const UIRenderUtils::GLStateGuard glState;

        // ── Pass 1: Hovered background (solid color, crosshair shader) ──
        if (hasBg && crosshairShader)
        {
            const auto& s = slots[hoveredIndex];
            // Bottom-left origin (same as inventory/text/ui_color shaders)
            const float x0 = static_cast<float>(s.x);
            const float y0 = static_cast<float>(screenH - (s.y + s.size));
            const float x1 = static_cast<float>(s.x + s.size);
            const float y1 = static_cast<float>(screenH - s.y);

            const float bgVerts[] = {
                x0, y0, 0.0f, 0.0f,
                x1, y0, 0.0f, 0.0f,
                x1, y1, 0.0f, 0.0f,
                x0, y0, 0.0f, 0.0f,
                x1, y1, 0.0f, 0.0f,
                x0, y1, 0.0f, 0.0f,
            };

            crosshairShader->use();
            crosshairShader->setVec2("uScreenSize", glm::vec2(static_cast<float>(screenW), static_cast<float>(screenH)));
            crosshairShader->setVec2("uOffset", glm::vec2(0.0f, 0.0f));
            crosshairShader->setVec4("uColor", glm::vec4(params.hoverBgColor[0], params.hoverBgColor[1],
                                                          params.hoverBgColor[2], params.hoverBgColor[3]));

            glBindVertexArray(mesh.vao);
            glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(bgVerts), bgVerts);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);
        }

        // ── Pass 2: Item icons (textured, inventory shader) ──
        if (hasIcons && inventoryShader)
        {
            inventoryShader->use();
            inventoryShader->setVec2("uScreenSize", glm::vec2(static_cast<float>(screenW), static_cast<float>(screenH)));
            inventoryShader->setVec4("uTintColor", glm::vec4(params.iconTintColor[0], params.iconTintColor[1],
                                                              params.iconTintColor[2], params.iconTintColor[3]));
            inventoryShader->setInt("uAtlas", 0);

            glActiveTexture(GL_TEXTURE0);
            glBindVertexArray(mesh.vao);
            glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);

            if (!iconVerts.empty()) {
                glBindTexture(GL_TEXTURE_2D, itemTextureAtlas.textureID);
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(iconVerts.size() * sizeof(float)),
                             iconVerts.data(), GL_DYNAMIC_DRAW);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(iconVerts.size() / 4));
            }

            if (!fallbackIconVerts.empty()) {
                glBindTexture(GL_TEXTURE_2D, itemIconAtlas.textureID);
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(fallbackIconVerts.size() * sizeof(float)),
                             fallbackIconVerts.data(), GL_DYNAMIC_DRAW);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(fallbackIconVerts.size() / 4));
            }

            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);

            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    // ── Pass 3: Item count text (batched into single draw call) ──
    if (textRenderer)
    {
        constexpr float kBaseGlyphSize = 8.0f;
        constexpr std::array<float, 4> kTextColor = {1.0f, 1.0f, 1.0f, 1.0f};

        textRenderer->beginBatch(static_cast<float>(screenW), static_cast<float>(screenH));

        for (int i = 0; i < count; ++i)
        {
            if (slots[i].itemId == 0 || slots[i].count <= 1)
                continue;

            const float slotSize = static_cast<float>(slots[i].size);
            const float textScale = params.countTextScale * slotSize / kBaseGlyphSize;

            const std::string countStr = std::to_string(slots[i].count);
            const float textWidth = textRenderer->measureText(countStr, textScale).width;
            const float textX = static_cast<float>(slots[i].x + slots[i].size) - textWidth + params.countTextOffsetX * slotSize;
            const float pixelScale = (kBaseGlyphSize * textScale) /
                                     static_cast<float>(textRenderer->getAtlas().getPixelHeight());
            const float descent = static_cast<float>(textRenderer->getAtlas().getDescent()) * pixelScale;
            const float textY = static_cast<float>(screenH - (slots[i].y + slots[i].size)) +
                                params.countTextOffsetY * slotSize - descent;

            textRenderer->batchRender(countStr, textX, textY, textScale, kTextColor);
        }

        textRenderer->endBatch();
    }
}
