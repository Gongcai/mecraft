#pragma once

#include <array>
#include <memory>
#include <vector>

#include "UILayout.h"
#include "UIInputEvent.h"
#include "UIEventResult.h"
#include "UIRenderContext.h"

class UIWidget {
public:
    virtual ~UIWidget() = default;

    // Lifecycle
    virtual void init(ResourceMgr& resourceMgr) { (void)resourceMgr; }
    virtual void shutdown() {}

    // Tree management
    void addChild(std::unique_ptr<UIWidget> child) {
        child->m_parent = this;
        m_children.push_back(std::move(child));
    }

    [[nodiscard]] UIWidget* getParent() const { return m_parent; }
    [[nodiscard]] const std::vector<std::unique_ptr<UIWidget>>& getChildren() const { return m_children; }

    // Position and layout
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    Anchor anchor = Anchor::TopLeft;
    float anchorOffsetX = 0.0f;
    float anchorOffsetY = 0.0f;

    // Transform
    float scaleX = 1.0f;
    float scaleY = 1.0f;

    // Appearance
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    float alpha = 1.0f;
    bool visible = true;
    bool interactive = false;

    // Absolute position resolution
    [[nodiscard]] float getAbsoluteX(const UIRenderContext& ctx) const {
        if (m_parent) {
            float px = m_parent->getAbsoluteX(ctx);
            float pw = m_parent->width * m_parent->scaleX;
            return resolveInParent(px, pw, width * scaleX, anchor, anchorOffsetX);
        }
        return resolveInParent(0.0f, static_cast<float>(ctx.screenWidth), width * scaleX, anchor, anchorOffsetX);
    }

    [[nodiscard]] float getAbsoluteY(const UIRenderContext& ctx) const {
        if (m_parent) {
            float py = m_parent->getAbsoluteY(ctx);
            float ph = m_parent->height * m_parent->scaleY;
            return resolveInParentY(py, ph, height * scaleY, anchor, anchorOffsetY);
        }
        return resolveInParentY(0.0f, static_cast<float>(ctx.screenHeight), height * scaleY, anchor, anchorOffsetY);
    }

    [[nodiscard]] bool hitTest(float px, float py, const UIRenderContext& ctx) const {
        // Input coordinates (from GLFW) have Y=0 at top; widget coords have Y=0 at bottom.
        // Flip the input Y to match widget coordinate space.
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

    // Rendering
    void render(const UIRenderContext& ctx) const {
        if (!visible) return;
        renderSelf(ctx);
        for (const auto& child : m_children) {
            child->render(ctx);
        }
    }

    // Input
    virtual UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) {
        if (!visible) return UIEventResult::Ignored;
        // Reverse order child dispatch (top-most first)
        for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
            UIEventResult result = (*it)->onInput(event, ctx);
            if (result == UIEventResult::Consumed) return UIEventResult::Consumed;
        }
        return UIEventResult::Ignored;
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
};
