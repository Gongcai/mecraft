#pragma once

#include <array>
#include <string>
#include <vector>

#include <glad/glad.h>

#include "../core/UIWidget.h"
#include "../core/Tween.h"

class Shader;

// Toast notification widget that displays temporary messages at the bottom of the screen.
// Multiple toasts stack vertically and auto-dismiss after their duration expires.
class UIToast : public UIWidget {
public:
    enum class Type { Info, Success, Warning, Error };

    UIToast();
    ~UIToast() override;

    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    // Show a toast notification. Duration is in seconds.
    void showToast(const std::string& text, Type type = Type::Info, float duration = 3.0f);

    // Maximum number of simultaneously visible toasts.
    void setMaxVisible(int maxVisible) { m_maxVisible = maxVisible; }

protected:
    void renderSelf(const UIRenderContext& ctx) const override;
    void onUpdate(float dt) override;

private:
    struct ToastEntry {
        std::string text;
        Type type = Type::Info;
        float elapsed = 0.0f;
        float duration = 3.0f;
        Tween<float> alphaTween;
    };

    void initMesh();
    void cleanupMesh();
    [[nodiscard]] Color getToastColor(const UITheme* theme, Type type) const;

    Shader* m_shader = nullptr;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    std::vector<ToastEntry> m_toasts;
    int m_maxVisible = 5;
};
