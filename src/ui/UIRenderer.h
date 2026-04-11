#pragma once

#include <glad/glad.h>
#include <array>
#include <cstddef>
#include <string>
#include "ConsoleDisplayBox.h"

class Window;
class ResourceMgr;
class Shader;
class Inventory;

class UIRenderer
{
public:
    UIRenderer();
    ~UIRenderer();

    void init(ResourceMgr& resourceMgr);
    void shutdown();

    void render(const Window& window, const Inventory& inventory);
    void renderCommandInputBox(const std::string& text);
    void appendCommandLine(const std::string& command);
    void appendOutputLine(const std::string& message,
                          ConsoleDisplayBox::MessageType type = ConsoleDisplayBox::MessageType::Normal);
    void appendWarningLine(const std::string& message);
    void appendSuccessLine(const std::string& message);
    void clearConsoleLines();
    void renderText(const std::string& text,
                    float x,
                    float y,
                    float scale,
                    const std::array<float, 4>& color,
                    float screenWidth,
                    float screenHeight);

    void setTextAdvanceFactor(float factor);
    [[nodiscard]] float getTextAdvanceFactor() const;

    void setCommandCaretBlinkPeriodMs(float periodMs);
    [[nodiscard]] float getCommandCaretBlinkPeriodMs() const;

    void setCrosshairSize(float size);
    [[nodiscard]] float getCrosshairSize() const;

    void setCrosshairColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getCrosshairColor() const;

    // Hotbar colors
    void setHotbarBgColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getHotbarBgColor() const;

    void setHotbarBorderColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getHotbarBorderColor() const;

    void setHotbarIconTintColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getHotbarIconTintColor() const;

private:
    void initCrosshairMesh();
    void rebuildCrosshairMesh();
    void renderCrosshair(const Window& window);
    void cleanupCrosshairMesh();

    void initHotbarMesh();
    void cleanupHotbarMesh();
    void renderHotbar(const Window& window, const Inventory& inventory);

    void initTextMesh();
    void cleanupTextMesh();
    void drawOverlayRect(int screenW,
                         int screenH,
                         int x,
                         int y,
                         int w,
                         int h,
                         const std::array<float, 4>& color);

    // Crosshair
    Shader* m_crosshairShader = nullptr;
    GLuint m_crosshairVao = 0;
    GLuint m_crosshairVbo = 0;
    int m_vertexCount = 0;
    float m_crosshairSize = 1.0f;
    std::array<float, 4> m_crosshairColor {1.0f, 1.0f, 1.0f, 1.0f};

    // Hotbar
    Shader* m_inventoryShader = nullptr;
    GLuint m_hotbarVao = 0;
    GLuint m_hotbarVbo = 0;
    ResourceMgr* m_resourceMgr = nullptr;
    std::array<float, 4> m_hotbarBgColor {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> m_hotbarBorderColor {1.0f, 1.0f, 1.0f, 0.9f};
    std::array<float, 4> m_hotbarIconTintColor {1.0f, 1.0f, 1.0f, 1.0f};

    // Bitmap text
    Shader* m_textShader = nullptr;
    GLuint m_textVao = 0;
    GLuint m_textVbo = 0;
    GLuint m_fontTexture = 0;
    float m_textAdvanceFactor = 0.70f;
    float m_commandCaretBlinkPeriodMs = 530.0f;

    ConsoleDisplayBox m_consoleDisplayBox;
    std::size_t m_consoleMaxLines = 64;
    std::size_t m_consoleVisibleBoxes = 6;
    float m_consoleHoldSeconds = 5.0f;
    float m_consoleFadeEndSeconds = 8.0f;
};
