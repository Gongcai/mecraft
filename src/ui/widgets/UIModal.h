#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/UIStyle.h"
#include "../core/UIWidget.h"
#include "../core/Tween.h"
#include "UIButton.h"
#include "UIPanel.h"
#include "UIText.h"

class Shader;

// Modal dialog widget with dimmed overlay, centered content panel, title, and action buttons.
class UIModal : public UIWidget {
public:
    UIModal();
    ~UIModal() override;

    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    void setTitle(const std::string& title);

    // Get the content panel to add custom child widgets.
    [[nodiscard]] UIPanel& getContentPanel() { return m_contentPanel; }

    // Add a button to the bottom button row. Returns the button index.
    int addButton(const std::string& text, std::function<void()> onClick);

    // Show or close the modal with animations.
    void show();
    void close();

    [[nodiscard]] bool isOpen() const { return m_open; }

    // If true, clicking the overlay background closes the modal.
    void setCloseOnOverlayClick(bool close) { m_closeOnOverlayClick = close; }
    void setStyle(const UIModalStyle& style);
    void clearLocalStyle();

    std::function<void()> onClose;

protected:
    void render(const UIRenderContext& ctx) const override;
    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;
    void updateAnimations(float dt) override;

private:
    void layoutButtons();
    [[nodiscard]] UIModalStyle resolveBaseStyle(const UIRenderContext& ctx) const;
    [[nodiscard]] UIResolvedModalStyle resolveStyle(const UIRenderContext& ctx) const;
    [[nodiscard]] UIResolvedModalStyle fallbackStyle() const;

    Shader* m_shader = nullptr;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    bool m_open = false;
    bool m_closeOnOverlayClick = true;
    UIPanel m_overlayPanel;
    UIPanel m_contentPanel;
    UIText m_title;
    std::vector<std::unique_ptr<UIButton>> m_buttons;
    std::vector<std::function<void()>> m_buttonCallbacks;

    Tween<float> m_overlayAlpha;
    Tween<float> m_panelScale;
    bool m_hasLocalStyle = false;
    UIModalStyle m_localStyle;

    static constexpr float kPanelWidth = 360.0f;
    static constexpr float kPanelMinHeight = 180.0f;
    static constexpr float kTitleHeight = 40.0f;
    static constexpr float kButtonRowHeight = 50.0f;
    static constexpr float kButtonSpacing = 10.0f;
    static constexpr float kPadding = 16.0f;
};
