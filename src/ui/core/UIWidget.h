#pragma once

#include <array>
#include <memory>
#include <vector>

#include "../layout/UILayout.h"
#include "UIInputEvent.h"
#include "UIEventResult.h"
#include "UIRenderContext.h"

class UIWidget {
public:
    virtual ~UIWidget() = default;

    // Lifecycle
    virtual void init(ResourceMgr& resourceMgr) {
        for (auto& child : m_children) {
            child->init(resourceMgr);
        }
    }
    virtual void shutdown() {
        for (auto& child : m_children) {
            child->shutdown();
        }
    }

    // Tree management
    void addChild(std::unique_ptr<UIWidget> child) {
        child->m_parent = this;
        m_children.push_back(std::move(child));
    }

    void clearChildren() {
        for (auto& child : m_children) {
            child->shutdown();
        }
        m_children.clear();
    }

    [[nodiscard]] UIWidget* getParent() const { return m_parent; }
    [[nodiscard]] const std::vector<std::unique_ptr<UIWidget>>& getChildren() const { return m_children; }

    // Position and layout
    mutable float x = 0.0f;
    mutable float y = 0.0f;
    mutable float width = 0.0f;
    mutable float height = 0.0f;
    mutable Anchor anchor = Anchor::TopLeft;
    mutable float anchorOffsetX = 0.0f;
    mutable float anchorOffsetY = 0.0f;

    // Transform
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    void setScale(float s) { scaleX = scaleY = s; }

    // Appearance
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    float alpha = 1.0f;
    mutable bool visible = true;
    bool interactive = false;
    bool focusable = false;

    [[nodiscard]] bool isFocused() const { return m_focused; }
    virtual void setFocused(bool focused) { m_focused = focused; }

    void requestFocus() { m_focusRequested = true; }
    [[nodiscard]] bool consumeFocusRequest() {
        const bool requested = m_focusRequested;
        m_focusRequested = false;
        return requested;
    }

    [[nodiscard]] UIWidget* consumeRequestedFocusDeep() {
        for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
            if (UIWidget* requested = (*it)->consumeRequestedFocusDeep()) {
                return requested;
            }
        }
        if (consumeFocusRequest() && focusable && visible) {
            return this;
        }
        return nullptr;
    }

    void collectFocusableWidgets(std::vector<UIWidget*>& out) {
        if (!visible) {
            return;
        }
        if (focusable) {
            out.push_back(this);
        }
        for (auto& child : m_children) {
            child->collectFocusableWidgets(out);
        }
    }

    // Absolute position resolution
    [[nodiscard]] float getAbsoluteX(const UIRenderContext& ctx) const {
        const float localOffsetX = anchorOffsetX + x;
        if (m_parent) {
            float px = m_parent->getAbsoluteX(ctx);
            float pw = m_parent->width * m_parent->scaleX;
            return resolveInParent(px, pw, width * scaleX, anchor, localOffsetX);
        }
        return resolveInParent(0.0f, static_cast<float>(ctx.screenWidth), width * scaleX, anchor, localOffsetX);
    }

    [[nodiscard]] float getAbsoluteY(const UIRenderContext& ctx) const {
        const float localOffsetY = anchorOffsetY + y;
        if (m_parent) {
            float py = m_parent->getAbsoluteY(ctx);
            float ph = m_parent->height * m_parent->scaleY;
            return resolveInParentY(py, ph, height * scaleY, anchor, localOffsetY);
        }
        return resolveInParentY(0.0f, static_cast<float>(ctx.screenHeight), height * scaleY, anchor, localOffsetY);
    }

    [[nodiscard]] bool hitTest(float px, float py, const UIRenderContext& ctx) const {
        // Input coordinates are already in reference space (converted by routeUIInput).
        // Flip Y (GLFW Y=0 at top → widget coords Y=0 at bottom).
        float flippedY = static_cast<float>(ctx.screenHeight) - py;
        float ax = getAbsoluteX(ctx);
        float ay = getAbsoluteY(ctx);
        float aw = width * scaleX;
        float ah = height * scaleY;
        return px >= ax && px < ax + aw && flippedY >= ay && flippedY < ay + ah;
    }

    // Animation
    virtual void updateAnimations(float dt) {
        for (auto& child : m_children) {
            child->updateAnimations(dt);
        }
    }

    virtual void layout(const UIRenderContext& ctx) {
        for (auto& child : m_children) {
            child->layout(ctx);
        }
    }

    // Per-frame logic update (override in subclasses)
    virtual void onUpdate(float dt) { (void)dt; }

    // Recursively update this widget and all children
    void update(float dt) {
        onUpdate(dt);
        for (auto& child : m_children) {
            child->update(dt);
        }
    }

    // Data context injection (type-erased pointer to domain-specific data)
    void setDataContext(const void* data) { m_dataContext = data; }
    [[nodiscard]] const void* getDataContext() const { return m_dataContext; }

    // Rendering
    virtual void render(const UIRenderContext& ctx) const {
        if (!visible) return;
        renderSelf(ctx);
        for (const auto& child : m_children) {
            child->render(ctx);
        }
    }

    virtual void renderOverlay(const UIRenderContext& ctx) const {
        if (!visible) return;
        for (const auto& child : m_children) {
            child->renderOverlay(ctx);
        }
    }

    virtual UIEventResult onOverlayInput(const UIInputEvent& event, const UIRenderContext& ctx) {
        if (!visible) return UIEventResult::Ignored;
        UIEventResult aggregate = UIEventResult::Ignored;
        // Overlay input follows overlay draw order and gives floating panels first chance.
        for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
            UIEventResult result = (*it)->onOverlayInput(event, ctx);
            if (result == UIEventResult::Consumed) return UIEventResult::Consumed;
            if (result == UIEventResult::Handled) {
                aggregate = UIEventResult::Handled;
            }
        }
        return aggregate;
    }

    // Input
    virtual UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
        if (!visible) return UIEventResult::Ignored;
        UIEventResult aggregate = UIEventResult::Ignored;
        // Reverse order child dispatch (top-most first)
        for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
            UIEventResult result = (*it)->onInput(event, ctx);
            if (result == UIEventResult::Consumed) return UIEventResult::Consumed;
            if (result == UIEventResult::Handled) {
                aggregate = UIEventResult::Handled;
            }
        }
        return aggregate;
    }

protected:
    virtual void renderSelf(const UIRenderContext& ctx) const {
        (void)ctx;
    }

    // Resolve position within a parent or screen using anchor
    [[nodiscard]] static float resolveInParent(float parentX, float parentW, float controlW,
                                                Anchor a, float offset) {
        float base = parentX;
        switch (a) {
            case Anchor::TopLeft:
            case Anchor::CenterLeft:
            case Anchor::BottomLeft:
                break;
            case Anchor::TopCenter:
            case Anchor::Center:
            case Anchor::BottomCenter:
                base += (parentW - controlW) * 0.5f;
                break;
            case Anchor::TopRight:
            case Anchor::CenterRight:
            case Anchor::BottomRight:
                base += parentW - controlW;
                break;
        }
        return base + offset;
    }

    [[nodiscard]] static float resolveInParentY(float parentY, float parentH, float controlH,
                                                 Anchor a, float offset) {
        float base = parentY;
        switch (a) {
            case Anchor::BottomLeft:
            case Anchor::BottomCenter:
            case Anchor::BottomRight:
                break;
            case Anchor::CenterLeft:
            case Anchor::Center:
            case Anchor::CenterRight:
                base += (parentH - controlH) * 0.5f;
                break;
            case Anchor::TopLeft:
            case Anchor::TopCenter:
            case Anchor::TopRight:
                base += parentH - controlH;
                break;
        }
        return base + offset;
    }

private:
    UIWidget* m_parent = nullptr;
    std::vector<std::unique_ptr<UIWidget>> m_children;
    bool m_focused = false;
    bool m_focusRequested = false;
    const void* m_dataContext = nullptr;
};
