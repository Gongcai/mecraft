#include "UISlider.h"

#include <algorithm>
#include <cmath>
#include <glm/vec4.hpp>

#include "../../resource/ResourceMgr.h"
#include "../../renderer/rhi/RhiCommandList.h"
#include "../../renderer/rhi/RhiDevice.h"
#include "../../renderer/rhi/RhiShaderSourceLoader.h"

#include <cstdlib>

UISlider::UISlider() {
    interactive = true;
    focusable = true;
    height = 24.0f;
    width = 200.0f;
}

UISlider::~UISlider() { shutdown(); }

void UISlider::init(ResourceMgr& resourceMgr) {
    m_rhiDevice = &resourceMgr.rhiDevice();
    const auto vertexSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.vert");
    const auto fragmentSource = renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.frag");
    if (!vertexSource || !fragmentSource) std::abort();
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "UiSlider.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_vertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "UiSlider.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = m_rhiDevice->createShader(shaderDesc);
    RhiPipelineLayoutDesc layoutDesc;
    layoutDesc.debugName = "UiSlider.PipelineLayout";
    layoutDesc.pushConstantBytes = 48u;
    layoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
    m_pipelineLayout = m_rhiDevice->createPipelineLayout(layoutDesc);
    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "UiSlider.Pipeline";
    pipelineDesc.vertexShader = m_vertexShader;
    pipelineDesc.fragmentShader = m_fragmentShader;
    pipelineDesc.layout = m_pipelineLayout;
    pipelineDesc.vertexInput.bindings.push_back({0u, sizeof(float) * 2u, RhiVertexInputRate::Vertex});
    pipelineDesc.vertexInput.attributes.push_back({0u, 0u, RhiVertexFormat::Float2, 0u});
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(m_rhiDevice->swapchainColorFormat());
    pipelineDesc.depthFormat = m_rhiDevice->swapchainDepthStencilFormat();
    RhiBlendAttachmentState blend;
    blend.blendEnabled = true;
    blend.srcColor = RhiBlendFactor::SrcAlpha;
    blend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    blend.srcAlpha = RhiBlendFactor::One;
    blend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    pipelineDesc.blend.attachments.push_back(blend);
    m_pipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);
    initMesh();
    if (!m_vertexShader.isValid() || !m_fragmentShader.isValid() ||
        !m_pipelineLayout.isValid() || !m_pipeline.isValid() || !m_vertexBuffer.isValid()) std::abort();
    m_handleScaleTween.setImmediate(1.0f);
}

void UISlider::shutdown() {
    cleanupMesh();
    if (m_rhiDevice) {
        if (m_pipeline.isValid()) m_rhiDevice->destroyPipeline(m_pipeline);
        if (m_pipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_pipelineLayout);
        if (m_fragmentShader.isValid()) m_rhiDevice->destroyShader(m_fragmentShader);
        if (m_vertexShader.isValid()) m_rhiDevice->destroyShader(m_vertexShader);
    }
    m_pipeline = {}; m_pipelineLayout = {}; m_fragmentShader = {}; m_vertexShader = {};
    m_rhiDevice = nullptr;
}

void UISlider::setRange(float min, float max) {
    m_min = min;
    m_max = max;
    m_value = std::clamp(m_value, m_min, m_max);
}

void UISlider::setValue(float value) {
    m_value = std::clamp(value, m_min, m_max);
}

float UISlider::getValue() const {
    return m_value;
}

void UISlider::setStep(float step) {
    m_step = step;
}

void UISlider::setOnValueChanged(std::function<void(float)> callback) {
    m_onValueChanged = std::move(callback);
}

void UISlider::setStyle(const UISliderStyle& style) {
    m_localStyle = style;
    m_hasLocalStyle = true;
}

void UISlider::clearLocalStyle() {
    m_hasLocalStyle = false;
}

void UISlider::updateAnimations(float dt) {
    m_handleScaleTween.tick(dt);
    UIWidget::updateAnimations(dt);
}

float UISlider::valueToNormalized(float val) const {
    if (m_max <= m_min) return 0.0f;
    return (val - m_min) / (m_max - m_min);
}

float UISlider::normalizedToValue(float norm) const {
    return m_min + norm * (m_max - m_min);
}

float UISlider::trackLeft(const UIRenderContext& ctx) const {
    return getAbsoluteX(ctx) + resolveStyle(ctx).handleSize * 0.5f;
}

float UISlider::trackRight(const UIRenderContext& ctx) const {
    return getAbsoluteX(ctx) + width * scaleX - resolveStyle(ctx).handleSize * 0.5f;
}

float UISlider::handleScreenX(const UIRenderContext& ctx) const {
    float tl = trackLeft(ctx);
    float tr = trackRight(ctx);
    return tl + valueToNormalized(m_value) * (tr - tl);
}

float UISlider::pointerToValue(float px, const UIRenderContext& ctx) const {
    float tl = trackLeft(ctx);
    float tr = trackRight(ctx);
    if (tr <= tl) return m_min;
    float norm = std::clamp((px - tl) / (tr - tl), 0.0f, 1.0f);
    return normalizedToValue(norm);
}

void UISlider::applyStep() {
    if (m_step > 0.0f) {
        m_value = std::round(m_value / m_step) * m_step;
        m_value = std::clamp(m_value, m_min, m_max);
    }
}

void UISlider::initMesh() {
    constexpr float vertices[] = {0,0, 1,0, 1,1, 0,0, 1,1, 0,1};
    RhiBufferDesc desc;
    desc.debugName = "UiSlider.VertexBuffer";
    desc.size = sizeof(vertices);
    desc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                 rhiFlag(RhiBufferUsage::TransferDst);
    desc.memoryUsage = RhiMemoryUsage::GpuOnly;
    desc.initialState = RhiResourceState::VertexBuffer;
    desc.memoryCategory = RhiMemoryCategory::Geometry;
    m_vertexBuffer = m_rhiDevice->createBuffer(desc, vertices, sizeof(vertices));
}

void UISlider::cleanupMesh() {
    if (m_rhiDevice && m_vertexBuffer.isValid()) m_rhiDevice->destroyBuffer(m_vertexBuffer);
    m_vertexBuffer = {};
}

void UISlider::renderSelf(const UIRenderContext& ctx) const {
    if (!ctx.commandList || !m_pipeline.isValid() || !m_vertexBuffer.isValid()) return;

    const UIResolvedSliderStyle resolved = resolveStyle(ctx);
    float trackH = resolved.trackHeight;
    float handleSize = resolved.handleSize;
    const auto& trackCol = resolved.track;
    const auto& fillCol = resolved.fill;
    const auto& handleCol = resolved.handle;

    float ay = getAbsoluteY(ctx);
    float ah = height * scaleY;

    float cy = ay + ah * 0.5f;
    float tl = trackLeft(ctx);
    float tr = trackRight(ctx);
    float hx = handleScreenX(ctx);

    float handleScale = m_handleScaleTween.value();
    float hs = handleSize * handleScale;
    const bool active = m_dragging || m_handleHovered || isFocused();

    ctx.commandList->setGraphicsPipeline(m_pipeline);
    ctx.commandList->setVertexBuffer(0u, m_vertexBuffer, 0u);
    auto drawShape = [&](float x, float y, float w, float h, float radius, Color shapeColor) {
        shapeColor[3] *= alpha;
        struct Push { glm::vec4 screenRect; glm::vec4 rectRadius; glm::vec4 color; };
        const Push push{glm::vec4(ctx.screenWidth, ctx.screenHeight, x, y),
                        glm::vec4(w, h, radius, 0),
                        glm::vec4(shapeColor[0], shapeColor[1], shapeColor[2], shapeColor[3])};
        ctx.commandList->pushConstants(&push, sizeof(push), rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment));
        ctx.commandList->draw(6u, 1u, 0u, 0u);
    };
    drawShape(tl, cy - trackH * 0.5f, tr - tl, trackH, trackH * 0.5f, trackCol);

    if (hx > tl + 0.5f) {
        drawShape(tl, cy - trackH * 0.5f, hx - tl, trackH, trackH * 0.5f, fillCol);
    }

    Color ringCol = fillCol;
    ringCol[3] = active ? 0.46f : 0.26f;
    const float ringR = hs * 0.5f + (active ? 2.5f : 1.5f);
    drawShape(hx - ringR, cy - ringR, ringR * 2, ringR * 2, ringR, ringCol);
    drawShape(hx - hs * 0.5f, cy - hs * 0.5f, hs, hs, hs * 0.5f, handleCol);
}

UIEventResult UISlider::onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
    if (!visible || !interactive) return UIEventResult::Ignored;

    UIEventResult childResult = UIWidget::onInput(event, ctx);
    if (childResult == UIEventResult::Consumed) return UIEventResult::Consumed;

    float handleSize = resolveStyle(ctx).handleSize;
    float hx = handleScreenX(ctx);
    float ay = getAbsoluteY(ctx);
    float ah = height * scaleY;
    float cy = ay + ah * 0.5f;
    float padding = 4.0f;

    // Flip GLFW Y to widget coords
    float flippedY = static_cast<float>(ctx.screenHeight) - event.y;

    switch (event.type) {
        case UIInputEventType::PointerMove: {
            if (m_dragging) {
                float newVal = pointerToValue(event.x, ctx);
                if (newVal != m_value) {
                    m_value = newVal;
                    applyStep();
                    if (m_onValueChanged) m_onValueChanged(m_value);
                }
                return UIEventResult::Consumed;
            }
            bool insideWidget = hitTest(event.x, event.y, ctx);
            bool insideHandle = std::abs(event.x - hx) <= (handleSize * 0.5f + padding)
                                && std::abs(flippedY - cy) <= (handleSize * 0.5f + padding);
            bool hovered = insideWidget || insideHandle;
            if (hovered && !m_handleHovered) {
                m_handleHovered = true;
                m_handleScaleTween.start(m_handleScaleTween.value(), 1.12f, 0.1f, EasingType::EaseOut);
            } else if (!hovered && m_handleHovered) {
                m_handleHovered = false;
                m_handleScaleTween.start(m_handleScaleTween.value(), 1.0f, 0.1f, EasingType::EaseOut);
            }
            return hovered ? UIEventResult::Handled : UIEventResult::Ignored;
        }
        case UIInputEventType::PointerDown: {
            if (event.button == UIPointerButton::Primary) {
                bool insideWidget = hitTest(event.x, event.y, ctx);
                if (insideWidget) {
                    m_dragging = true;
                    m_handleHovered = true;
                    m_handleScaleTween.start(m_handleScaleTween.value(), 1.16f, 0.08f, EasingType::EaseOut);
                    m_value = pointerToValue(event.x, ctx);
                    applyStep();
                    if (m_onValueChanged) m_onValueChanged(m_value);
                    return UIEventResult::Consumed;
                }
            }
            break;
        }
        case UIInputEventType::PointerUp: {
            if (event.button == UIPointerButton::Primary && m_dragging) {
                m_dragging = false;
                m_handleScaleTween.start(m_handleScaleTween.value(),
                                         m_handleHovered ? 1.12f : 1.0f,
                                         0.1f,
                                         EasingType::EaseOut);
                return UIEventResult::Consumed;
            }
            break;
        }
        case UIInputEventType::Command: {
            if (isFocused()) {
                float stepVal = m_step > 0.0f ? m_step : (m_max - m_min) * 0.05f;
                // Only consume Left/Right for slider adjustment, let Up/Down pass through for focus navigation
                if (event.command == UICommand::NavigateLeft) {
                    m_value = std::max(m_min, m_value - stepVal);
                    applyStep();
                    if (m_onValueChanged) m_onValueChanged(m_value);
                    return UIEventResult::Consumed;
                }
                if (event.command == UICommand::NavigateRight) {
                    m_value = std::min(m_max, m_value + stepVal);
                    applyStep();
                    if (m_onValueChanged) m_onValueChanged(m_value);
                    return UIEventResult::Consumed;
                }
                if (event.command == UICommand::Home) {
                    m_value = m_min;
                    if (m_onValueChanged) m_onValueChanged(m_value);
                    return UIEventResult::Consumed;
                }
                if (event.command == UICommand::End) {
                    m_value = m_max;
                    if (m_onValueChanged) m_onValueChanged(m_value);
                    return UIEventResult::Consumed;
                }
            }
            break;
        }
        case UIInputEventType::Scroll: {
            if (hitTest(event.x, event.y, ctx) || isFocused()) {
                float stepVal = m_step > 0.0f ? m_step : (m_max - m_min) * 0.05f;
                if (event.scrollY > 0) {
                    m_value = std::min(m_max, m_value + stepVal);
                } else if (event.scrollY < 0) {
                    m_value = std::max(m_min, m_value - stepVal);
                }
                applyStep();
                if (m_onValueChanged) m_onValueChanged(m_value);
                return UIEventResult::Handled;
            }
            break;
        }
        default:
            break;
    }

    return UIEventResult::Ignored;
}

UISliderStyle UISlider::resolveBaseStyle(const UIRenderContext& ctx) const {
    if (m_hasLocalStyle) {
        return m_localStyle;
    }

    UISliderStyle style = UIStyleResolver::sliderStyleFromTheme(ctx.theme);
    if (!ctx.theme) {
        style.trackNormal = m_trackColor;
        style.trackDisabled = m_trackColor;
        style.fillNormal = m_fillColor;
        style.fillDisabled = m_fillColor;
        style.handleNormal = m_handleColor;
        style.handleHover = m_handleHoverColor;
        style.handleDisabled = m_handleColor;
    }
    return style;
}

UIResolvedSliderStyle UISlider::resolveStyle(const UIRenderContext& ctx) const {
    return UIStyleResolver::resolveSlider(resolveBaseStyle(ctx), currentStyleState());
}

int UISlider::currentStyleState() const {
    if (!interactive) {
        return static_cast<int>(UIStyleState_Disabled);
    }

    int state = static_cast<int>(UIStyleState_Normal);
    if (m_handleHovered || m_dragging || isFocused()) {
        state |= static_cast<int>(UIStyleState_Hovered);
    }
    return state;
}
