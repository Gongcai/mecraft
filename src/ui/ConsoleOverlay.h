#pragma once

#include <array>
#include <cstddef>
#include <string>

#include <glad/glad.h>

#include "ConsoleDisplayBox.h"

class ResourceMgr;
class Shader;
class TextRenderer;

class ConsoleOverlay
{
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown();

    void appendLine(const std::string& message,
                    double createdAtSec,
                    ConsoleDisplayBox::MessageType type = ConsoleDisplayBox::MessageType::Normal);
    void clear();
    [[nodiscard]] bool empty() const;

    void setMaxLines(std::size_t maxLines);
    void render(double nowSec, const TextRenderer& textRenderer) const;

private:
    void drawOverlayRect(int screenW,
                         int screenH,
                         int x,
                         int y,
                         int w,
                         int h,
                         const std::array<float, 4>& color) const;

    Shader* m_crosshairShader = nullptr;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    mutable ConsoleDisplayBox m_display;
    std::size_t m_maxLines = 64;
    std::size_t m_visibleBoxes = 6;
    float m_holdSeconds = 5.0f;
    float m_fadeEndSeconds = 8.0f;
};

