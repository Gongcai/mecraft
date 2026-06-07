#pragma once

#include <array>
#include <cstddef>
#include <string>

#include <glad/glad.h>

#include "../widgets/ConsoleDisplayBox.h"
#include "../core/UIWidget.h"

class ResourceMgr;
class Shader;
class TextRenderer;
struct UITheme;

class ConsoleOverlay : public UIWidget
{
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    void appendLine(const std::string& message,
                    double createdAtSec,
                    ConsoleDisplayBox::MessageType type = ConsoleDisplayBox::MessageType::Normal);
    void clear();
    [[nodiscard]] bool empty() const;

    void setMaxLines(std::size_t maxLines);
    void setTextRenderer(const TextRenderer* textRenderer);

protected:
    void renderSelf(const UIRenderContext& context) const override;

private:
    void renderMessages(double nowSec, const TextRenderer& textRenderer, const UITheme* theme = nullptr) const;
    void drawOverlayRect(int screenW,
                         int screenH,
                         int rectX,
                         int rectY,
                         int rectW,
                         int rectH,
                         const std::array<float, 4>& rectColor) const;

    Shader* m_crosshairShader = nullptr;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    mutable ConsoleDisplayBox m_display;
    const TextRenderer* m_textRenderer = nullptr;
    std::size_t m_maxLines = 64;
    std::size_t m_visibleBoxes = 6;
    float m_holdSeconds = 5.0f;
    float m_fadeEndSeconds = 8.0f;
};

