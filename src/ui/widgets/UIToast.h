#pragma once

#include <array>
#include <string>
#include <vector>

#include "renderer/rhi/RhiHandles.h"
#include "../core/UIStyle.h"
#include "../core/UIWidget.h"
#include "../core/Tween.h"

class RhiDevice;

// Toast notification widget that displays temporary messages at the bottom of the screen.
// Multiple toasts stack vertically and auto-dismiss after their duration expires.
class UIToast : public UIWidget {
public:
    enum class Type { Info, Success, Warning, Error };

    UIToast();
    ~UIToast() override;

    void init(GameResources& resources, RhiDevice& rhiDevice) override;
    void shutdown() override;

    // Show a toast notification. Duration is in seconds.
    void showToast(const std::string& text, Type type = Type::Info, float duration = 3.0f);

    // Maximum number of simultaneously visible toasts.
    void setMaxVisible(int maxVisible) { m_maxVisible = maxVisible; }
    void setStyle(const UIToastStyle& style);
    void clearLocalStyle();

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

    void cleanupMesh();
    [[nodiscard]] UIToastStyle resolveBaseStyle(const UIRenderContext& ctx) const;
    [[nodiscard]] UIResolvedToastStyle resolveStyle(const UIRenderContext& ctx, Type type) const;

    RhiDevice* m_rhiDevice = nullptr;
    RhiBufferHandle m_vertexBuffer;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_fragmentShader;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiPipelineHandle m_pipeline;

    std::vector<ToastEntry> m_toasts;
    int m_maxVisible = 5;
    bool m_hasLocalStyle = false;
    UIToastStyle m_localStyle;
};
