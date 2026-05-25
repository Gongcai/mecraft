#pragma once

#include <array>
#include <functional>
#include <string>

#include <glad/glad.h>

#include "../core/UIWidget.h"
#include "UIText.h"
#include "../core/Tween.h"

class Shader;

class UICheckbox : public UIWidget {
public:
    UICheckbox();
    ~UICheckbox() override;

    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    void setChecked(bool checked);
    [[nodiscard]] bool isChecked() const;

    void setLabel(const std::string& text);
    void setOnChanged(std::function<void(bool)> callback);

    void updateAnimations(float dt) override;

protected:
    void renderSelf(const UIRenderContext& ctx) const override;
    UIEventResult onInput(const UIInputEvent& event, const UIRenderContext& ctx) override;

private:
    void initMesh();
    void cleanupMesh();

    Shader* m_shader = nullptr;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    UIText m_label;

    bool m_checked = false;
    bool m_hovered = false;

    std::array<float, 4> m_boxColor{0.25f, 0.25f, 0.25f, 0.9f};
    std::array<float, 4> m_boxHoverColor{0.35f, 0.35f, 0.35f, 1.0f};
    std::array<float, 4> m_boxBorderColor{0.5f, 0.5f, 0.5f, 0.5f};
    std::array<float, 4> m_checkColor{0.3f, 0.8f, 0.4f, 1.0f};

    std::function<void(bool)> m_onChanged;
    Tween<float> m_checkScaleTween;
};
