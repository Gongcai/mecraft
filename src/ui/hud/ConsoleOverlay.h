#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "renderer/rhi/RhiHandles.h"
#include "../widgets/ConsoleDisplayBox.h"
#include "../core/UIWidget.h"

class ResourceMgr;
class RhiDevice;
class TextRenderer;
struct UITheme;

class ConsoleOverlay : public UIWidget {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    void appendLine(const std::string& message, double createdAtSec,
                    ConsoleDisplayBox::MessageType type = ConsoleDisplayBox::MessageType::Normal);
    void clear();
    [[nodiscard]] bool empty() const;

    void setMaxLines(std::size_t maxLines);
    void setTextRenderer(const TextRenderer* textRenderer);

protected:
    void renderSelf(const UIRenderContext& context) const override;

private:
    void renderMessages(double nowSec, const TextRenderer& textRenderer, const UIRenderContext& context) const;
    void drawOverlayRect(const UIRenderContext& context, int rectX, int rectY, int rectW, int rectH,
                         const std::array<float, 4>& rectColor) const;

    RhiDevice* m_rhiDevice = nullptr;
    RhiBufferHandle m_vertexBuffer;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_fragmentShader;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiPipelineHandle m_pipeline;

    mutable ConsoleDisplayBox m_display;
    const TextRenderer* m_textRenderer = nullptr;
    std::size_t m_maxLines = 64;
    std::size_t m_visibleBoxes = 6;
    float m_holdSeconds = 5.0f;
    float m_fadeEndSeconds = 8.0f;
};
