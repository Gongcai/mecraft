#include "Pickable.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <glm/vec4.hpp>

#include "../../item/Item.h"
#include "../../renderer/rhi/RhiCommandList.h"
#include "../../resource/ResourceMgr.h"
#include "../ItemIconPolicy.h"
#include "../core/UIRenderContext.h"
#include "../core/UIRenderer.h"
#include "../font/TextRenderer.h"

namespace {

struct PickableSolidPushConstants {
    glm::vec4 screenRect;
    glm::vec4 rectRadius;
    glm::vec4 color;
};

struct PickableImagePushConstants {
    glm::vec4 screenRect;
    glm::vec4 extent;
    glm::vec4 uvRect;
    glm::vec4 tint;
};

static_assert(sizeof(PickableSolidPushConstants) == 48u);
static_assert(sizeof(PickableImagePushConstants) == 64u);

[[nodiscard]] RhiRect2D pickableScissor(const UIRenderContext& context)
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

int Pickable::hitTest(const SlotInfo* slots, const int count,
                      const float mouseX, const float mouseY)
{
    if (slots == nullptr || count <= 0) {
        return -1;
    }
    for (int i = 0; i < count; ++i) {
        if (mouseX >= slots[i].x &&
            mouseX < slots[i].x + slots[i].size &&
            mouseY >= slots[i].y &&
            mouseY < slots[i].y + slots[i].size) {
            return i;
        }
    }
    return -1;
}

void Pickable::render(const SlotInfo* slots, const int count,
                      const int hoveredIndex,
                      const RenderParams& params,
                      const UIRenderContext& context,
                      const ResourceMgr& resourceMgr,
                      const TextureAtlas& itemIconAtlas,
                      const TextureAtlas& itemTextureAtlas)
{
    if (slots == nullptr || count <= 0 ||
        context.screenWidth <= 0 || context.screenHeight <= 0) {
        return;
    }

    if (context.phase == UIRenderPhase::Record &&
        context.commandList != nullptr &&
        context.uiRenderer != nullptr &&
        context.panelQuadVertexBuffer.isValid()) {
        RhiCommandList& commandList = *context.commandList;
        const RhiRect2D scissor = pickableScissor(context);

        if (hoveredIndex >= 0 && hoveredIndex < count &&
            context.panelSolidPipeline.isValid()) {
            const SlotInfo& slot = slots[hoveredIndex];
            const PickableSolidPushConstants pushConstants{
                glm::vec4(static_cast<float>(context.screenWidth),
                          static_cast<float>(context.screenHeight),
                          static_cast<float>(slot.x),
                          static_cast<float>(context.screenHeight - (slot.y + slot.size))),
                glm::vec4(static_cast<float>(slot.size),
                          static_cast<float>(slot.size), 0.0f, 0.0f),
                glm::vec4(params.hoverBgColor[0], params.hoverBgColor[1],
                          params.hoverBgColor[2], params.hoverBgColor[3])
            };
            commandList.setGraphicsPipeline(context.panelSolidPipeline);
            commandList.setVertexBuffer(0u, context.panelQuadVertexBuffer, 0u);
            commandList.setScissor(scissor);
            commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                      rhiFlag(RhiShaderStage::Vertex) |
                                      rhiFlag(RhiShaderStage::Fragment));
            commandList.draw(6u, 1u, 0u, 0u);
        }

        if (context.imageTexturePipeline.isValid()) {
            const RhiBindGroupHandle itemIconBindGroup =
                itemIconAtlas.texture.isValid() && itemIconAtlas.tilesPerRow > 0
                    ? context.uiRenderer->resolveImageBindGroup(itemIconAtlas.texture)
                    : RhiBindGroupHandle{};
            const RhiBindGroupHandle itemTextureBindGroup =
                itemTextureAtlas.texture.isValid() && itemTextureAtlas.tilesPerRow > 0
                    ? context.uiRenderer->resolveImageBindGroup(itemTextureAtlas.texture)
                    : RhiBindGroupHandle{};

            commandList.setGraphicsPipeline(context.imageTexturePipeline);
            commandList.setVertexBuffer(0u, context.panelQuadVertexBuffer, 0u);
            commandList.setScissor(scissor);

            const auto drawAtlasItems = [&](const bool drawBakedBlockIcons,
                                            const TextureAtlas& atlas,
                                            const RhiBindGroupHandle bindGroup) {
                if (!bindGroup.isValid()) {
                    return;
                }
                commandList.setBindGroup(0u, bindGroup);
                for (int i = 0; i < count; ++i) {
                    const SlotInfo& slot = slots[i];
                    if (slot.itemId == 0) {
                        continue;
                    }

                    const ItemDef& itemDef =
                        ItemRegistry::get(static_cast<ItemID>(slot.itemId));
                    if (ui::shouldUseBakedBlockIcon(itemDef) != drawBakedBlockIcons) {
                        continue;
                    }
                    const int tileIndex = drawBakedBlockIcons
                        ? static_cast<int>(itemDef.renderBlock)
                        : resourceMgr.getItemTextureIndex(itemDef.iconTextureName);
                    if (tileIndex < 0) {
                        continue;
                    }

                    const auto uv = atlas.getUV(tileIndex);
                    const PickableImagePushConstants pushConstants{
                        glm::vec4(static_cast<float>(context.screenWidth),
                                  static_cast<float>(context.screenHeight),
                                  static_cast<float>(slot.x),
                                  static_cast<float>(context.screenHeight -
                                                     (slot.y + slot.size))),
                        glm::vec4(static_cast<float>(slot.size),
                                  static_cast<float>(slot.size), 0.0f, 0.0f),
                        glm::vec4(uv.first.x, uv.first.y, uv.second.x, uv.second.y),
                        glm::vec4(params.iconTintColor[0], params.iconTintColor[1],
                                  params.iconTintColor[2], params.iconTintColor[3])
                    };
                    commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                              rhiFlag(RhiShaderStage::Vertex) |
                                              rhiFlag(RhiShaderStage::Fragment));
                    commandList.draw(6u, 1u, 0u, 0u);
                }
            };
            drawAtlasItems(false, itemTextureAtlas, itemTextureBindGroup);
            drawAtlasItems(true, itemIconAtlas, itemIconBindGroup);
        }
    }

    const TextRenderer* textRenderer = context.textRenderer;
    if (textRenderer == nullptr) {
        return;
    }

    constexpr float kBaseGlyphSize = 8.0f;
    constexpr std::array<float, 4> kTextColor = {1.0f, 1.0f, 1.0f, 1.0f};
    for (int i = 0; i < count; ++i) {
        if (slots[i].itemId == 0 || slots[i].count <= 1) {
            continue;
        }

        const float slotSize = static_cast<float>(slots[i].size);
        const float textScale = params.countTextScale * slotSize / kBaseGlyphSize;
        const std::string countString = std::to_string(slots[i].count);
        const float textWidth = textRenderer->measureText(countString, textScale).width;
        const float textX = static_cast<float>(slots[i].x + slots[i].size) - textWidth +
                            params.countTextOffsetX * slotSize;
        const float textY = static_cast<float>(context.screenHeight -
                                               (slots[i].y + slots[i].size)) +
                            params.countTextOffsetY * slotSize;
        textRenderer->draw(context, countString, textX, textY, textScale, kTextColor);
    }
}
