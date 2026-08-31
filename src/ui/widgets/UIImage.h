#pragma once

#include <array>

#include <cstdint>

#include "../../renderer/rhi/RhiHandles.h"
#include "../core/UIWidget.h"

struct TextureAtlas;

class UIImage : public UIWidget {
public:
    UIImage() = default;
    ~UIImage() override { UIImage::shutdown(); }

    void init(GameResources& resources, RhiDevice& rhiDevice) override;
    void shutdown() override;

    // Load a named GUI texture by path, automatically sets widget size to image dimensions
    void loadTexture(GameResources& resources, const std::string& name, const std::string& path);

    // Set texture from an atlas tile index
    void setAtlasTile(const TextureAtlas& atlas, int tileIndex);

    // Set texture directly with explicit UV coordinates
    void setTexture(RhiTextureHandle texture, float u0, float v0, float u1, float v1);

    // Set a solid color texture (no texture, just tint)
    void setSolidColor(const std::array<float, 4>& c);

    void setTintColor(const std::array<float, 4>& c) { m_tintColor = c; }
    [[nodiscard]] const std::array<float, 4>& getTintColor() const { return m_tintColor; }

protected:
    void renderSelf(const UIRenderContext& ctx) const override;

private:
    RhiTextureHandle m_texture;
    float m_u0 = 0.0f, m_v0 = 0.0f, m_u1 = 1.0f, m_v1 = 1.0f;
    std::array<float, 4> m_tintColor{1.0f, 1.0f, 1.0f, 1.0f};
    bool m_useTexture = true;
};
