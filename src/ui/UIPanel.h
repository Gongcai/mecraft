#pragma once

#include <array>
#include <glad/glad.h>

#include "UIWidget.h"

class Shader;

class UIPanel : public UIWidget {
public:
    UIPanel();
    ~UIPanel() override;

    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    void setBackgroundColor(const std::array<float, 4>& c) { m_bgColor = c; }
    [[nodiscard]] const std::array<float, 4>& getBackgroundColor() const { return m_bgColor; }
    void setUseLocalColors(bool v) { m_hasLocalBgColor = v; m_hasLocalBorderColor = v; }

    void setBorderColor(const std::array<float, 4>& c) { m_borderColor = c; }
    [[nodiscard]] const std::array<float, 4>& getBorderColor() const { return m_borderColor; }

    void setBorderWidth(float w) { m_borderWidth = w; }
    [[nodiscard]] float getBorderWidth() const { return m_borderWidth; }

protected:
    void renderSelf(const UIRenderContext& ctx) const override;

private:
    void initMesh();
    void cleanupMesh();
    void rebuildMesh(float x0, float y0, float x1, float y1) const;
    void rebuildBorderMesh(float x0, float y0, float x1, float y1, float bw) const;

    Shader* m_shader = nullptr;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    std::array<float, 4> m_bgColor{0.2f, 0.2f, 0.2f, 0.8f};
    std::array<float, 4> m_borderColor{1.0f, 1.0f, 1.0f, 0.5f};
    float m_borderWidth = 0.0f;
    bool m_hasLocalBgColor = false;
    bool m_hasLocalBorderColor = false;
};
