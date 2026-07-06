#pragma once

#include <array>

#include <cstdint>

#include "../core/UIStyle.h"
#include "../core/UIWidget.h"
#include "../core/Tween.h"

class Shader;

class UIScrollArea : public UIWidget {
public:
    UIScrollArea();
    ~UIScrollArea() override;

    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    void setContentHeight(float contentHeight);
    [[nodiscard]] float getContentHeight() const;
    [[nodiscard]] float getScrollOffset() const;
    void setScrollOffset(float offset);
    void scrollToBottom();

    void setScrollbarVisible(bool scrollbarVisible);
    void setStyle(const UIScrollAreaStyle& style);
    void clearLocalStyle();

    void updateAnimations(float dt) override;

    // Override render to apply scissor and child offset
    void render(const UIRenderContext& ctx) const override;
    void renderOverlay(const UIRenderContext& ctx) const override;
    UIEventResult onOverlayInput(const UIInputEvent& event, const UIRenderContext& ctx) override;

protected:
    void renderSelf(const UIRenderContext& ctx) const override;
    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;
    [[nodiscard]] bool clipsDescendantInput() const override;
    [[nodiscard]] bool hitTestDescendantInputClip(float px, float py, const UIRenderContext& ctx) const override;

private:
    void renderScrollbar(const UIRenderContext& ctx) const;
    [[nodiscard]] float maxScroll() const;
    [[nodiscard]] bool hitTestScrollbarThumb(float px, float py, const UIRenderContext& ctx) const;
    [[nodiscard]] UIScrollAreaStyle resolveBaseStyle(const UIRenderContext& ctx) const;
    [[nodiscard]] UIResolvedScrollAreaStyle resolveStyle(const UIRenderContext& ctx, bool thumbHovered) const;

    void initMesh();
    void cleanupMesh();

    Shader* m_shader = nullptr;
    uint32_t m_vao = 0;
    uint32_t m_vbo = 0;

    float m_contentHeight = 0.0f;
    float m_scrollOffset = 0.0f;

    bool m_scrollbarVisible = true;
    bool m_draggingScrollbar = false;
    float m_dragStartY = 0.0f;
    float m_dragStartOffset = 0.0f;

    std::array<float, 4> m_scrollbarTrackColor{0.15f, 0.15f, 0.15f, 0.6f};
    std::array<float, 4> m_scrollbarThumbColor{0.45f, 0.45f, 0.45f, 0.8f};
    std::array<float, 4> m_scrollbarThumbHoverColor{0.60f, 0.60f, 0.60f, 0.9f};
    float m_scrollbarWidth = 8.0f;
    bool m_hasLocalStyle = false;
    UIScrollAreaStyle m_localStyle;

    bool m_thumbHovered = false;
    Tween<float> m_scrollTween;
};
