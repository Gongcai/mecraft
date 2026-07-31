#include "UIImage.h"

#include <algorithm>
#include <cmath>

#include <glm/vec4.hpp>

#include "../../renderer/rhi/RhiCommandList.h"
#include "../../resource/ResourceMgr.h"
#include "../core/UIRenderer.h"

namespace {

struct ImageTexturePushConstants {
    glm::vec4 screenRect;
    glm::vec4 extent;
    glm::vec4 uvRect;
    glm::vec4 tint;
};

struct ImageSolidPushConstants {
    glm::vec4 screenRect;
    glm::vec4 rectRadius;
    glm::vec4 color;
};

static_assert(sizeof(ImageTexturePushConstants) == 64u);
static_assert(sizeof(ImageSolidPushConstants) == 48u);

[[nodiscard]] RhiRect2D imageScissor(const UIRenderContext& context) {
    if (context.hasScissor) {
        return context.scissor;
    }
    return {0, 0,
            static_cast<uint32_t>(
                std::max(1.0f, std::round(static_cast<float>(context.screenWidth) * context.pixelScale()))),
            static_cast<uint32_t>(
                std::max(1.0f, std::round(static_cast<float>(context.screenHeight) * context.pixelScale())))};
}

} // namespace

void UIImage::init(ResourceMgr& resourceMgr) {
    UIWidget::init(resourceMgr);
}

void UIImage::shutdown() {
    UIWidget::shutdown();
}

void UIImage::loadTexture(ResourceMgr& resourceMgr, const std::string& name, const std::string& path) {
    int imgW = 0, imgH = 0;
    const RhiTextureHandle texture = resourceMgr.loadGuiTexture(name, path, imgW, imgH);
    setTexture(texture, 0.0f, 0.0f, 1.0f, 1.0f);
    if (imgW > 0 && imgH > 0) {
        width = static_cast<float>(imgW);
        height = static_cast<float>(imgH);
    }
}

void UIImage::setAtlasTile(const TextureAtlas& atlas, int tileIndex) {
    if (!atlas.texture.isValid() || tileIndex < 0) {
        m_texture = {};
        m_useTexture = false;
        return;
    }
    m_texture = atlas.texture;
    const auto uv = atlas.getUV(tileIndex);
    m_u0 = uv.first.x;
    m_v0 = uv.first.y;
    m_u1 = uv.second.x;
    m_v1 = uv.second.y;
    m_useTexture = true;
}

void UIImage::setTexture(RhiTextureHandle texture, float u0, float v0, float u1, float v1) {
    m_texture = texture;
    m_u0 = u0;
    m_v0 = v0;
    m_u1 = u1;
    m_v1 = v1;
    m_useTexture = texture.isValid();
}

void UIImage::setSolidColor(const std::array<float, 4>& c) {
    m_tintColor = c;
    m_useTexture = false;
}

void UIImage::renderSelf(const UIRenderContext& ctx) const {
    if (ctx.commandList == nullptr || !ctx.panelQuadVertexBuffer.isValid() || ctx.uiRenderer == nullptr) {
        return;
    }

    const float ax = getAbsoluteX(ctx);
    const float ay = getAbsoluteY(ctx);
    const float aw = width * scaleX;
    const float ah = height * scaleY;
    if (aw <= 0.0f || ah <= 0.0f) {
        return;
    }

    std::array<float, 4> tint = m_tintColor;
    tint[3] *= alpha;
    RhiCommandList& commandList = *ctx.commandList;
    commandList.setScissor(imageScissor(ctx));

    if (m_useTexture && m_texture.isValid()) {
        if (!ctx.imageTexturePipeline.isValid()) {
            return;
        }
        const RhiBindGroupHandle bindGroup = ctx.uiRenderer->resolveImageBindGroup(m_texture);
        if (!bindGroup.isValid()) {
            return;
        }
        const ImageTexturePushConstants pushConstants{
            glm::vec4(static_cast<float>(ctx.screenWidth), static_cast<float>(ctx.screenHeight), ax, ay),
            glm::vec4(aw, ah, 0.0f, 0.0f), glm::vec4(m_u0, m_v0, m_u1, m_v1),
            glm::vec4(tint[0], tint[1], tint[2], tint[3])};
        commandList.setGraphicsPipeline(ctx.imageTexturePipeline);
        commandList.setVertexBuffer(0u, ctx.panelQuadVertexBuffer, 0u);
        commandList.setBindGroup(0u, bindGroup);
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(6u, 1u, 0u, 0u);
        return;
    }

    if (!m_useTexture && ctx.panelSolidPipeline.isValid()) {
        const ImageSolidPushConstants pushConstants{
            glm::vec4(static_cast<float>(ctx.screenWidth), static_cast<float>(ctx.screenHeight), ax, ay),
            glm::vec4(aw, ah, 0.0f, 0.0f), glm::vec4(tint[0], tint[1], tint[2], tint[3])};
        commandList.setGraphicsPipeline(ctx.panelSolidPipeline);
        commandList.setVertexBuffer(0u, ctx.panelQuadVertexBuffer, 0u);
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
        commandList.draw(6u, 1u, 0u, 0u);
    }
}
