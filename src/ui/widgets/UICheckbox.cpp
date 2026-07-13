#include "UICheckbox.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/vec4.hpp>

#include "../../resource/ResourceMgr.h"
#include "../../renderer/rhi/RhiCommandList.h"
#include "../../renderer/rhi/RhiDevice.h"
#include "../../renderer/rhi/RhiShaderSourceLoader.h"

#include <cstdlib>

namespace {
void pushThickSegment(std::vector<float>& buf,
                      float x0, float y0,
                      float x1, float y1,
                      float thickness) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.001f) return;

    const float nx = -dy / len * thickness * 0.5f;
    const float ny = dx / len * thickness * 0.5f;
    buf.push_back(x0 + nx); buf.push_back(y0 + ny);
    buf.push_back(x1 + nx); buf.push_back(y1 + ny);
    buf.push_back(x1 - nx); buf.push_back(y1 - ny);
    buf.push_back(x0 + nx); buf.push_back(y0 + ny);
    buf.push_back(x1 - nx); buf.push_back(y1 - ny);
    buf.push_back(x0 - nx); buf.push_back(y0 - ny);
}
} // namespace

UICheckbox::UICheckbox() {
    interactive = true;
    focusable = true;
    width = 200.0f;
    height = 24.0f;
}

UICheckbox::~UICheckbox() { shutdown(); }

void UICheckbox::init(ResourceMgr& resourceMgr) {
    m_rhiDevice = &resourceMgr.rhiDevice();
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.vert");
    const auto shapeSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.frag");
    const auto colorSource = renderer::rhi::loadShaderSource("assets/shaders/ui_color_rhi.frag");
    if (!vertexSource || !shapeSource || !colorSource) std::abort();
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "UiCheckbox.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str(); shaderDesc.sourceSize = vertexSource->size();
    m_vertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.debugName = "UiCheckbox.ShapeFragment";
    shaderDesc.source = shapeSource->c_str(); shaderDesc.sourceSize = shapeSource->size();
    m_shapeFragmentShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "UiCheckbox.ColorFragment";
    shaderDesc.source = colorSource->c_str(); shaderDesc.sourceSize = colorSource->size();
    m_colorFragmentShader = m_rhiDevice->createShader(shaderDesc);
    RhiPipelineLayoutDesc layoutDesc;
    layoutDesc.debugName = "UiCheckbox.PipelineLayout";
    layoutDesc.pushConstantBytes = 48u;
    layoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_pipelineLayout = m_rhiDevice->createPipelineLayout(layoutDesc);
    auto createPipeline = [&](RhiShaderHandle fragment, const char* name) {
        RhiGraphicsPipelineDesc desc;
        desc.debugName = name; desc.vertexShader = m_vertexShader; desc.fragmentShader = fragment; desc.layout = m_pipelineLayout;
        desc.vertexInput.bindings.push_back({0u, sizeof(float) * 2u, RhiVertexInputRate::Vertex});
        desc.vertexInput.attributes.push_back({0u, 0u, RhiVertexFormat::Float2, 0u});
        desc.raster.cullMode = RhiCullMode::None;
        desc.depthStencil.depthTestEnabled = false; desc.depthStencil.depthWriteEnabled = false;
        desc.colorFormats.push_back(m_rhiDevice->swapchainColorFormat());
        desc.depthFormat = m_rhiDevice->swapchainDepthStencilFormat();
        RhiBlendAttachmentState blend; blend.blendEnabled = true;
        blend.srcColor = RhiBlendFactor::SrcAlpha; blend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
        blend.srcAlpha = RhiBlendFactor::One; blend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
        desc.blend.attachments.push_back(blend);
        return m_rhiDevice->createGraphicsPipeline(desc);
    };
    m_shapePipeline = createPipeline(m_shapeFragmentShader, "UiCheckbox.ShapePipeline");
    m_colorPipeline = createPipeline(m_colorFragmentShader, "UiCheckbox.ColorPipeline");
    initMesh();
    if (!m_vertexShader.isValid() || !m_shapeFragmentShader.isValid() || !m_colorFragmentShader.isValid() ||
        !m_pipelineLayout.isValid() || !m_shapePipeline.isValid() || !m_colorPipeline.isValid() ||
        !m_vertexBuffer.isValid()) std::abort();
    m_label.init(resourceMgr);
    m_label.anchor = Anchor::BottomLeft;
    m_checkScaleTween.setImmediate(m_checked ? 1.0f : 0.0f);
}

void UICheckbox::shutdown() {
    m_label.shutdown();
    cleanupMesh();
    if (m_rhiDevice) {
        if (m_colorPipeline.isValid()) m_rhiDevice->destroyPipeline(m_colorPipeline);
        if (m_shapePipeline.isValid()) m_rhiDevice->destroyPipeline(m_shapePipeline);
        if (m_pipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_pipelineLayout);
        if (m_colorFragmentShader.isValid()) m_rhiDevice->destroyShader(m_colorFragmentShader);
        if (m_shapeFragmentShader.isValid()) m_rhiDevice->destroyShader(m_shapeFragmentShader);
        if (m_vertexShader.isValid()) m_rhiDevice->destroyShader(m_vertexShader);
    }
    m_colorPipeline={}; m_shapePipeline={}; m_pipelineLayout={}; m_colorFragmentShader={}; m_shapeFragmentShader={}; m_vertexShader={}; m_rhiDevice=nullptr;
}

void UICheckbox::setChecked(bool checked) {
    m_checked = checked;
}

bool UICheckbox::isChecked() const {
    return m_checked;
}

void UICheckbox::setLabel(const std::string& text) {
    m_label.setText(text);
}

void UICheckbox::setOnChanged(std::function<void(bool)> callback) {
    m_onChanged = std::move(callback);
}

void UICheckbox::setStyle(const UICheckboxStyle& style) {
    m_localStyle = style;
    m_hasLocalStyle = true;
}

void UICheckbox::clearLocalStyle() {
    m_hasLocalStyle = false;
}

void UICheckbox::updateAnimations(float dt) {
    m_checkScaleTween.tick(dt);
    UIWidget::updateAnimations(dt);
}

void UICheckbox::initMesh() {
    std::vector<float> vertices{0,0, 1,0, 1,1, 0,0, 1,1, 0,1};
    pushThickSegment(vertices, 0.25f, 0.52f, 0.42f, 0.34f, 0.14f);
    pushThickSegment(vertices, 0.42f, 0.34f, 0.76f, 0.70f, 0.14f);
    RhiBufferDesc desc; desc.debugName="UiCheckbox.VertexBuffer"; desc.size=vertices.size()*sizeof(float);
    desc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                 rhiFlag(RhiBufferUsage::TransferDst);
    desc.memoryUsage = RhiMemoryUsage::GpuOnly;
    desc.initialState = RhiResourceState::VertexBuffer;
    m_vertexBuffer=m_rhiDevice->createBuffer(desc, vertices.data(), vertices.size()*sizeof(float));
}

void UICheckbox::cleanupMesh() {
    if (m_rhiDevice && m_vertexBuffer.isValid()) m_rhiDevice->destroyBuffer(m_vertexBuffer);
    m_vertexBuffer={};
}

void UICheckbox::renderSelf(const UIRenderContext& ctx) const {
    const bool record = ctx.phase == UIRenderPhase::Record;
    if (record && (!ctx.commandList || !m_shapePipeline.isValid() ||
                   !m_colorPipeline.isValid() || !m_vertexBuffer.isValid())) return;

    const UIResolvedCheckboxStyle resolved =
        UIStyleResolver::resolveCheckbox(resolveBaseStyle(ctx), currentStyleState());
    float boxSize = resolved.boxSize;
    const auto& boxCol = resolved.box;
    const auto& borderCol = resolved.border;
    const auto& checkCol = resolved.check;
    const auto& textCol = resolved.text;

    float ax = getAbsoluteX(ctx);
    float ay = getAbsoluteY(ctx);
    float ah = height * scaleY;
    float cy = ay + ah * 0.5f;

    float bx0 = ax;
    float by0 = cy - boxSize * 0.5f;
    float bx1 = ax + boxSize;
    float bw = resolved.borderWidth;

    if (record) {
        ctx.commandList->setVertexBuffer(0u, m_vertexBuffer, 0u);
        auto drawRect = [&](float x, float y, float w, float h, Color color) {
            color[3] *= alpha;
            struct Push { glm::vec4 screenRect; glm::vec4 rectRadius; glm::vec4 color; };
            const Push push{glm::vec4(ctx.screenWidth, ctx.screenHeight, x, y), glm::vec4(w,h,0,0),
                            glm::vec4(color[0],color[1],color[2],color[3])};
            ctx.commandList->pushConstants(&push,sizeof(push),rhiFlag(RhiShaderStage::Vertex)|rhiFlag(RhiShaderStage::Fragment));
            ctx.commandList->draw(6u,1u,0u,0u);
        };
        ctx.commandList->setGraphicsPipeline(m_shapePipeline);
        drawRect(bx0,by0,boxSize,boxSize,borderCol);
        drawRect(bx0+bw,by0+bw,boxSize-2*bw,boxSize-2*bw,boxCol);

    // Check mark, scaled from the center.
        if (m_checked) {
            float scale = m_checkScaleTween.value();
            if (scale > 0.01f) {
                Color c=checkCol; c[3]*=alpha;
                const float scaledSize=boxSize*scale;
                const float offset=boxSize*(1.0f-scale)*0.5f;
                struct Push { glm::vec4 screenRect; glm::vec4 rectRadius; glm::vec4 color; };
                const Push push{glm::vec4(ctx.screenWidth,ctx.screenHeight,bx0+offset,by0+offset),
                                glm::vec4(scaledSize,scaledSize,0,0),glm::vec4(c[0],c[1],c[2],c[3])};
                ctx.commandList->setGraphicsPipeline(m_colorPipeline);
                ctx.commandList->pushConstants(&push,sizeof(push),rhiFlag(RhiShaderStage::Vertex)|rhiFlag(RhiShaderStage::Fragment));
                ctx.commandList->draw(12u,1u,6u,0u);
            }
        }
    }

    // Render label text to the right of the box
    float labelX = bx1 + 8.0f;
    float th = ctx.textRenderer ? m_label.measureTextHeight(*ctx.textRenderer) : 0.0f;
    const_cast<UIText&>(m_label).anchorOffsetX = labelX;
    const_cast<UIText&>(m_label).anchorOffsetY = cy - th * 0.5f;
    const_cast<UIText&>(m_label).setTextColor(textCol);
    const_cast<UIText&>(m_label).alpha = alpha;
    m_label.render(ctx);
}

UIEventResult UICheckbox::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !interactive) return UIEventResult::Ignored;

    UIEventResult childResult = UIWidget::onInput(event, ctx);
    if (childResult == UIEventResult::Consumed) return UIEventResult::Consumed;

    bool inside = hitTest(event.x, event.y, ctx);

    switch (event.type) {
        case UIInputEventType::PointerMove: {
            if (inside != m_hovered) {
                m_hovered = inside;
            }
            return inside ? UIEventResult::Handled : UIEventResult::Ignored;
        }
        case UIInputEventType::PointerDown: {
            if (event.button == UIPointerButton::Primary && inside) {
                return UIEventResult::Handled;
            }
            break;
        }
        case UIInputEventType::PointerUp: {
            if (event.button == UIPointerButton::Primary && inside) {
                m_checked = !m_checked;
                m_checkScaleTween.start(m_checked ? 0.0f : 1.0f,
                                        m_checked ? 1.0f : 0.0f,
                                        0.15f, EasingType::EaseOut);
                if (m_onChanged) m_onChanged(m_checked);
                return UIEventResult::Consumed;
            }
            break;
        }
        case UIInputEventType::Command: {
            if (isFocused() && event.command == UICommand::Activate) {
                m_checked = !m_checked;
                m_checkScaleTween.start(m_checked ? 0.0f : 1.0f,
                                        m_checked ? 1.0f : 0.0f,
                                        0.15f, EasingType::EaseOut);
                if (m_onChanged) m_onChanged(m_checked);
                return UIEventResult::Consumed;
            }
            break;
        }
        default:
            break;
    }

    return UIEventResult::Ignored;
}

UICheckboxStyle UICheckbox::resolveBaseStyle(const UIRenderContext& ctx) const {
    if (m_hasLocalStyle) {
        return m_localStyle;
    }

    UICheckboxStyle style = UIStyleResolver::checkboxStyleFromTheme(ctx.theme);
    if (!ctx.theme) {
        style.boxNormal = m_boxColor;
        style.boxHover = m_boxHoverColor;
        style.boxDisabled = m_boxColor;
        style.borderNormal = m_boxBorderColor;
        style.borderDisabled = m_boxBorderColor;
        style.check = m_checkColor;
    }
    return style;
}

int UICheckbox::currentStyleState() const {
    if (!interactive) {
        return static_cast<int>(UIStyleState_Disabled);
    }

    int state = static_cast<int>(UIStyleState_Normal);
    if (m_hovered || isFocused()) {
        state |= static_cast<int>(UIStyleState_Hovered);
    }
    return state;
}
