#include "UINumericSpinner.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <glm/vec4.hpp>

#include "../../renderer/rhi/RhiCommandList.h"
#include "../../renderer/rhi/RhiDevice.h"
#include "../../renderer/rhi/RhiShaderSourceLoader.h"
#include "../../resource/ResourceMgr.h"
#include "../font/TextRenderer.h"

UINumericSpinner::UINumericSpinner() {
  interactive = true;
  focusable = true;
  width = 120.0f;
  height = 28.0f;
}

UINumericSpinner::~UINumericSpinner() { shutdown(); }

void UINumericSpinner::init(ResourceMgr &resourceMgr) {
  m_rhiDevice = &resourceMgr.rhiDevice();
  const auto vs =
      renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.vert");
  const auto fs =
      renderer::rhi::loadShaderSource("assets/shaders/ui_capsule_rhi.frag");
  if (!vs || !fs)
    std::abort();
  RhiShaderDesc sd;
  sd.debugName = "UiNumericSpinner.Vertex";
  sd.stage = RhiShaderStage::Vertex;
  sd.source = vs->c_str();
  sd.sourceSize = vs->size();
  m_vertexShader = m_rhiDevice->createShader(sd);
  sd.debugName = "UiNumericSpinner.Fragment";
  sd.stage = RhiShaderStage::Fragment;
  sd.source = fs->c_str();
  sd.sourceSize = fs->size();
  m_fragmentShader = m_rhiDevice->createShader(sd);
  RhiPipelineLayoutDesc ld;
  ld.debugName = "UiNumericSpinner.PipelineLayout";
  ld.pushConstantBytes = 48;
  ld.pushConstantStages =
      rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment);
  m_pipelineLayout = m_rhiDevice->createPipelineLayout(ld);
  RhiGraphicsPipelineDesc pd;
  pd.debugName = "UiNumericSpinner.Pipeline";
  pd.vertexShader = m_vertexShader;
  pd.fragmentShader = m_fragmentShader;
  pd.layout = m_pipelineLayout;
  pd.vertexInput.bindings.push_back(
      {0, sizeof(float) * 2, RhiVertexInputRate::Vertex});
  pd.vertexInput.attributes.push_back({0, 0, RhiVertexFormat::Float2, 0});
  pd.raster.cullMode = RhiCullMode::None;
  pd.depthStencil.depthTestEnabled = false;
  pd.depthStencil.depthWriteEnabled = false;
  pd.colorFormats.push_back(m_rhiDevice->swapchainColorFormat());
  RhiBlendAttachmentState blend;
  blend.blendEnabled = true;
  blend.srcColor = RhiBlendFactor::SrcAlpha;
  blend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
  blend.srcAlpha = RhiBlendFactor::One;
  blend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
  pd.blend.attachments.push_back(blend);
  m_pipeline = m_rhiDevice->createGraphicsPipeline(pd);
  initMesh();
  if (!m_vertexShader.isValid() || !m_fragmentShader.isValid() ||
      !m_pipelineLayout.isValid() || !m_pipeline.isValid() ||
      !m_vertexBuffer.isValid())
    std::abort();
  UIWidget::init(resourceMgr);
}

void UINumericSpinner::shutdown() {
  cleanupMesh();
  if (m_rhiDevice) {
    if (m_pipeline.isValid())
      m_rhiDevice->destroyPipeline(m_pipeline);
    if (m_pipelineLayout.isValid())
      m_rhiDevice->destroyPipelineLayout(m_pipelineLayout);
    if (m_fragmentShader.isValid())
      m_rhiDevice->destroyShader(m_fragmentShader);
    if (m_vertexShader.isValid())
      m_rhiDevice->destroyShader(m_vertexShader);
  }
  m_pipeline = {};
  m_pipelineLayout = {};
  m_fragmentShader = {};
  m_vertexShader = {};
  m_rhiDevice = nullptr;
  UIWidget::shutdown();
}

void UINumericSpinner::setValue(float value) {
  m_value = std::clamp(value, m_min, m_max);
  if (m_decimals == 0) {
    m_value = std::round(m_value);
  }
}

void UINumericSpinner::setRange(float min, float max) {
  m_min = min;
  m_max = max;
  setValue(m_value);
}

void UINumericSpinner::setStyle(const UINumericSpinnerStyle &style) {
  m_localStyle = style;
  m_hasLocalStyle = true;
}

void UINumericSpinner::clearLocalStyle() { m_hasLocalStyle = false; }

void UINumericSpinner::applyStep(float delta) {
  setValue(m_value + delta);
  if (onValueChanged)
    onValueChanged(m_value);
}

void UINumericSpinner::commitEditText() {
  if (m_editText.empty()) {
    m_editing = false;
    return;
  }
  // Parse the edit text as a float.
  char *end = nullptr;
  const float parsed = std::strtof(m_editText.c_str(), &end);
  if (end != m_editText.c_str()) {
    setValue(parsed);
    if (onValueChanged)
      onValueChanged(m_value);
  }
  m_editing = false;
  m_editText.clear();
}

std::string UINumericSpinner::formatValue() const {
  char buf[64];
  if (m_decimals <= 0) {
    std::snprintf(buf, sizeof(buf), "%d",
                  static_cast<int>(std::round(m_value)));
  } else {
    char fmt[32];
    std::snprintf(fmt, sizeof(fmt), "%%.%df", m_decimals);
    std::snprintf(buf, sizeof(buf), fmt, m_value);
  }
  return buf;
}

// Returns: -1 = minus, 0 = value, 1 = plus, -2 = outside.
int UINumericSpinner::hitTestZone(float px, float py,
                                  const UIRenderContext &ctx) const {
  const float flippedY = static_cast<float>(ctx.screenHeight) - py;
  const float ax = getAbsoluteX(ctx);
  const float ay = getAbsoluteY(ctx);
  const float aw = width * scaleX;
  const float ah = height * scaleY;
  const UIResolvedNumericSpinnerStyle resolved = resolveStyle(ctx);

  if (px < ax || px >= ax + aw || flippedY < ay || flippedY >= ay + ah)
    return -2;

  const float localX = px - ax;
  if (localX < resolved.buttonWidth)
    return -1;
  if (localX >= aw - resolved.buttonWidth)
    return 1;
  return 0;
}

void UINumericSpinner::onUpdate(float dt) {
  if (m_editing && isFocused()) {
    m_cursorBlinkTimer += dt;
    if (m_cursorBlinkTimer >= kCursorBlinkRate) {
      m_cursorBlinkTimer -= kCursorBlinkRate;
    }
    m_cursorVisible = m_cursorBlinkTimer < (kCursorBlinkRate * 0.5f);

    // Backspace auto-repeat.
    const bool backspaceActive =
        glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_BACKSPACE) == GLFW_PRESS;
    const bool backspacePressed =
        backspaceActive && !m_backspaceActiveLastFrame;
    if (backspacePressed && !m_editText.empty()) {
      m_editText.pop_back();
      m_backspaceHoldElapsed = 0.0f;
      m_backspaceRepeatAccum = 0.0f;
    } else if (backspaceActive && !m_editText.empty()) {
      m_backspaceHoldElapsed += dt;
      if (m_backspaceHoldElapsed > kBackspaceInitialDelay) {
        m_backspaceRepeatAccum += dt;
        while (m_backspaceRepeatAccum >= kBackspaceRepeatInterval &&
               !m_editText.empty()) {
          m_editText.pop_back();
          m_backspaceRepeatAccum -= kBackspaceRepeatInterval;
        }
      }
    } else {
      m_backspaceHoldElapsed = 0.0f;
      m_backspaceRepeatAccum = 0.0f;
    }
    m_backspaceActiveLastFrame = backspaceActive;
  } else {
    m_cursorBlinkTimer = 0.0f;
    m_cursorVisible = false;
  }
}

void UINumericSpinner::initMesh() {
  constexpr float vertices[] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
  RhiBufferDesc d;
  d.debugName = "UiNumericSpinner.VertexBuffer";
  d.size = sizeof(vertices);
  d.usage = rhiFlag(RhiBufferUsage::Vertex);
  d.memoryUsage = RhiMemoryUsage::GpuOnly;
  m_vertexBuffer = m_rhiDevice->createBuffer(d, vertices, sizeof(vertices));
}

void UINumericSpinner::cleanupMesh() {
  if (m_rhiDevice != nullptr && m_vertexBuffer.isValid()) {
    m_rhiDevice->destroyBuffer(m_vertexBuffer);
  }
  m_vertexBuffer = {};
}

void UINumericSpinner::renderSelf(const UIRenderContext &ctx) const {
  if (!ctx.commandList || !m_pipeline.isValid() || !m_vertexBuffer.isValid())
    return;
  const UIResolvedNumericSpinnerStyle resolved = resolveStyle(ctx);

  const float ax = getAbsoluteX(ctx);
  const float ay = getAbsoluteY(ctx);
  const float aw = width * scaleX;
  const float ah = height * scaleY;

  const float minusX = ax;
  const float valueX = ax + resolved.buttonWidth + resolved.gap;
  const float plusX = ax + aw - resolved.buttonWidth;
  const float valueW = aw - 2.0f * resolved.buttonWidth - 2.0f * resolved.gap;
  const float brdW = resolved.borderWidth;

  ctx.commandList->setGraphicsPipeline(m_pipeline);
  ctx.commandList->setVertexBuffer(0, m_vertexBuffer, 0);
  auto rect = [&](float x, float y, float w, float h, Color c) {
    c[3] *= alpha;
    struct P {
      glm::vec4 a, b, c;
    };
    P p{glm::vec4(ctx.screenWidth, ctx.screenHeight, x, y),
        glm::vec4(w, h, 0, 0), glm::vec4(c[0], c[1], c[2], c[3])};
    ctx.commandList->pushConstants(&p, sizeof(p),
                                   rhiFlag(RhiShaderStage::Vertex) |
                                       rhiFlag(RhiShaderStage::Fragment));
    ctx.commandList->draw(6, 1, 0, 0);
  };
  auto zone = [&](float x, float w, Color bg, Color border) {
    rect(x, ay, w, ah, border);
    rect(x + brdW, ay + brdW, w - 2 * brdW, ah - 2 * brdW, bg);
  };
  zone(minusX, resolved.buttonWidth, resolved.minusBackground,
       resolved.minusBorder);
  zone(valueX, valueW, resolved.valueBackground, resolved.valueBorder);
  zone(plusX, resolved.buttonWidth, resolved.plusBackground,
       resolved.plusBorder);
  if (m_editing && m_cursorVisible && isFocused()) {
    float cursorX = valueX + resolved.textPadding;
    if (ctx.textRenderer && !m_editText.empty())
      cursorX += ctx.textRenderer->measureText(m_editText, 1).width;
    rect(cursorX, ay + resolved.cursorInset, resolved.cursorWidth,
         ah - 2 * resolved.cursorInset, resolved.cursor);
  }

  // Render text.
  if (ctx.textRenderer) {
    const float textScale = 1.0f;

    // Minus sign.
    {
      const auto m = ctx.textRenderer->measureText("-", textScale);
      ctx.textRenderer->render("-",
                               minusX + (resolved.buttonWidth - m.width) * 0.5f,
                               ay + (ah - m.height) * 0.5f, textScale,
                               {resolved.text[0], resolved.text[1],
                                resolved.text[2], resolved.text[3] * alpha},
                               static_cast<float>(ctx.screenWidth),
                               static_cast<float>(ctx.screenHeight));
    }
    // Plus sign.
    {
      const auto m = ctx.textRenderer->measureText("+", textScale);
      ctx.textRenderer->render("+",
                               plusX + (resolved.buttonWidth - m.width) * 0.5f,
                               ay + (ah - m.height) * 0.5f, textScale,
                               {resolved.text[0], resolved.text[1],
                                resolved.text[2], resolved.text[3] * alpha},
                               static_cast<float>(ctx.screenWidth),
                               static_cast<float>(ctx.screenHeight));
    }
    // Value text.
    {
      const std::string valStr = m_editing ? m_editText : formatValue();
      const auto m = ctx.textRenderer->measureText(
          valStr.empty() ? " " : valStr, textScale);
      // Clip to value area.
      const float uiScale = ctx.pixelScale();
      ctx.commandList->setScissor(
          {static_cast<int>((valueX + 2) * uiScale),
           static_cast<int>(((ctx.screenHeight - ay - ah) + 2) * uiScale),
           static_cast<uint32_t>((valueW - 4) * uiScale),
           static_cast<uint32_t>((ah - 4) * uiScale)});

      ctx.textRenderer->render(valStr.empty() ? " " : valStr,
                               valueX + resolved.textPadding,
                               ay + (ah - m.height) * 0.5f, textScale,
                               {resolved.text[0], resolved.text[1],
                                resolved.text[2], resolved.text[3] * alpha},
                               static_cast<float>(ctx.screenWidth),
                               static_cast<float>(ctx.screenHeight));

      ctx.commandList->setScissor(
          {0, 0, static_cast<uint32_t>(ctx.screenWidth * uiScale),
           static_cast<uint32_t>(ctx.screenHeight * uiScale)});
    }
  }
}

UIEventResult UINumericSpinner::onInput(const UIInputEvent &event,
                                        const UIRenderContext &ctx) {
  if (!visible || !interactive)
    return UIEventResult::Ignored;

  const bool inside = hitTest(event.x, event.y, ctx);

  switch (event.type) {
  case UIInputEventType::PointerMove: {
    const int zone = hitTestZone(event.x, event.y, ctx);
    m_minusHovered = (zone == -1);
    m_plusHovered = (zone == 1);
    return inside ? UIEventResult::Handled : UIEventResult::Ignored;
  }

  case UIInputEventType::PointerDown:
    if (event.button == UIPointerButton::Primary && inside) {
      const int zone = hitTestZone(event.x, event.y, ctx);
      if (zone == -1) {
        applyStep(-m_step);
      } else if (zone == 1) {
        applyStep(m_step);
      } else if (zone == 0) {
        // Enter editing mode.
        m_editing = true;
        m_editText = formatValue();
        m_cursorBlinkTimer = 0.0f;
        m_cursorVisible = true;
      }
      requestFocus();
      return UIEventResult::Consumed;
    }
    break;

  case UIInputEventType::PointerUp:
    if (event.button == UIPointerButton::Primary) {
      return UIEventResult::Handled;
    }
    break;

  case UIInputEventType::KeyDown: {
    if (!isFocused())
      break;

    if (m_editing) {
      const int key = event.key;
      if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        commitEditText();
        return UIEventResult::Consumed;
      }
      if (key == GLFW_KEY_ESCAPE) {
        m_editing = false;
        m_editText.clear();
        return UIEventResult::Consumed;
      }
      // Handled by onUpdate for backspace auto-repeat.
      return UIEventResult::Consumed;
    }

    // Non-editing mode: arrow keys adjust value.
    if (event.command == UICommand::NavigateLeft ||
        event.command == UICommand::NavigateDown) {
      applyStep(-m_step);
      return UIEventResult::Consumed;
    }
    if (event.command == UICommand::NavigateRight ||
        event.command == UICommand::NavigateUp) {
      applyStep(m_step);
      return UIEventResult::Consumed;
    }
    if (event.command == UICommand::Home) {
      setValue(m_min);
      if (onValueChanged)
        onValueChanged(m_value);
      return UIEventResult::Consumed;
    }
    if (event.command == UICommand::End) {
      setValue(m_max);
      if (onValueChanged)
        onValueChanged(m_value);
      return UIEventResult::Consumed;
    }
    break;
  }

  case UIInputEventType::Command: {
    if (!isFocused())
      break;
    if (m_editing) {
      if (event.command == UICommand::Activate) {
        commitEditText();
        return UIEventResult::Consumed;
      }
      if (event.command == UICommand::Cancel) {
        m_editing = false;
        m_editText.clear();
        return UIEventResult::Consumed;
      }
    }
    break;
  }

  case UIInputEventType::TextInput: {
    if (!isFocused() || !m_editing)
      break;
    const std::uint32_t cp = event.codepoint;
    // Accept digits, minus sign (at start), and decimal point.
    if (cp >= 32 && cp < 127) {
      const char c = static_cast<char>(cp);
      if (std::isdigit(c) || c == '-' || c == '.') {
        // Only allow '-' at the start.
        if (c == '-' && !m_editText.empty())
          break;
        // Only one decimal point.
        if (c == '.' && m_editText.find('.') != std::string::npos)
          break;
        m_editText += c;
        m_cursorBlinkTimer = 0.0f;
        m_cursorVisible = true;
      }
    }
    return UIEventResult::Consumed;
  }

  case UIInputEventType::Scroll: {
    if (inside || isFocused()) {
      const float delta = (event.scrollY > 0.0f) ? m_step : -m_step;
      applyStep(delta);
      return UIEventResult::Consumed;
    }
    break;
  }

  default:
    break;
  }

  return UIEventResult::Ignored;
}

UINumericSpinnerStyle
UINumericSpinner::resolveBaseStyle(const UIRenderContext &ctx) const {
  if (m_hasLocalStyle) {
    return m_localStyle;
  }
  return UIStyleResolver::numericSpinnerStyleFromTheme(ctx.theme);
}

UIResolvedNumericSpinnerStyle
UINumericSpinner::resolveStyle(const UIRenderContext &ctx) const {
  int minusState = interactive ? static_cast<int>(UIStyleState_Normal)
                               : static_cast<int>(UIStyleState_Disabled);
  int plusState = interactive ? static_cast<int>(UIStyleState_Normal)
                              : static_cast<int>(UIStyleState_Disabled);
  int valueState = interactive ? static_cast<int>(UIStyleState_Normal)
                               : static_cast<int>(UIStyleState_Disabled);

  if (m_minusHovered) {
    minusState |= static_cast<int>(UIStyleState_Hovered);
  }
  if (m_plusHovered) {
    plusState |= static_cast<int>(UIStyleState_Hovered);
  }
  if (isFocused()) {
    valueState |= static_cast<int>(UIStyleState_Focused);
  }

  return UIStyleResolver::resolveNumericSpinner(
      resolveBaseStyle(ctx), minusState, plusState, valueState);
}
