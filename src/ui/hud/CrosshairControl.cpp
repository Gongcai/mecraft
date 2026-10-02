#include "CrosshairControl.h"

#include <algorithm>

#include <glm/vec4.hpp>

#include "../../renderer/rhi/RhiCommandList.h"
#include "../../resource/GameResources.h"
#include "../core/UITheme.h"
#include "../core/UIRenderer.h"

namespace {

struct CrosshairImagePushConstants {
    glm::vec4 screenRect;
    glm::vec4 extent;
    glm::vec4 uvRect;
    glm::vec4 tint;
};

static_assert(sizeof(CrosshairImagePushConstants) == 64u);

} // namespace

void CrosshairControl::init(GameResources& resources, RhiDevice& rhiDevice) {
    UIWidget::init(resources, rhiDevice);
}

void CrosshairControl::shutdown() {
    UIWidget::shutdown();
}

void CrosshairControl::setSize(const float size) {
    m_size = std::clamp(size, 0.5f, 4.0f);
}

float CrosshairControl::getSize() const {
    return m_size;
}

void CrosshairControl::setColor(const std::array<float, 4>& color) {
    m_color = color;
}

const std::array<float, 4>& CrosshairControl::getColor() const {
    return m_color;
}

void CrosshairControl::renderSelf(const UIRenderContext& context) const {
    if (context.phase != UIRenderPhase::Record || context.resources == nullptr || context.commandList == nullptr ||
        context.uiRenderer == nullptr || !context.panelQuadVertexBuffer.isValid() ||
        !context.imageTexturePipeline.isValid() || context.uiWidth <= 0 || context.uiHeight <= 0) {
        return;
    }

    const RhiTextureHandle texture = context.resources->texture2D.getGuiHandle("hud_crosshair");
    const RhiBindGroupHandle bindGroup = context.uiRenderer->resolveImageBindGroup(texture);
    if (!bindGroup.isValid()) {
        return;
    }

    constexpr float kNativeSize = 15.0f;
    constexpr float kGuiScale = 2.0f;
    const float size = kNativeSize * kGuiScale * m_size;
    const float x = (static_cast<float>(context.uiWidth) - size) * 0.5f;
    const float y = (static_cast<float>(context.uiHeight) - size) * 0.5f;
    const UITheme* theme = context.theme;
    const auto& tint = theme != nullptr ? theme->crosshair : m_color;
    const CrosshairImagePushConstants pushConstants{
        glm::vec4(static_cast<float>(context.uiWidth), static_cast<float>(context.uiHeight), x, y),
        glm::vec4(size, size, 0.0f, 0.0f), glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
        glm::vec4(tint[0], tint[1], tint[2], tint[3] * alpha)};

    context.commandList->setGraphicsPipeline(context.imageTexturePipeline);
    context.commandList->setVertexBuffer(0u, context.panelQuadVertexBuffer, 0u);
    context.commandList->setBindGroup(0u, bindGroup);
    context.commandList->setScissor(context.fullFramebufferScissor());
    context.commandList->pushConstants(&pushConstants, sizeof(pushConstants),
                                      rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
    context.commandList->draw(6u, 1u, 0u, 0u);
}
