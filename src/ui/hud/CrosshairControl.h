#pragma once

#include <array>

#include "../core/UIWidget.h"

struct GameResources;
class RhiDevice;

class CrosshairControl : public UIWidget {
public:
    void init(GameResources& resources, RhiDevice& rhiDevice) override;
    void shutdown() override;

    void setSize(float size);
    [[nodiscard]] float getSize() const;

    void setColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getColor() const;

protected:
    void renderSelf(const UIRenderContext& ctx) const override;

private:
    float m_size = 1.0f;
    std::array<float, 4> m_color{1.0f, 1.0f, 1.0f, 1.0f};
};
