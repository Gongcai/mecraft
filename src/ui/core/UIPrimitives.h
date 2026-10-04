#pragma once

#include <array>
#include <string>

struct UIRenderContext;

namespace ui {

// Record a bottom-left UI rectangle using the shared solid pipeline; collection is a no-op.
void drawSolidRect(const UIRenderContext& context, float x, float y, float width, float height,
                   const std::array<float, 4>& color);
// Record a filled rectangle with an inward border, clamped to half its shortest side.
void drawFramedRect(const UIRenderContext& context, float x, float y, float width, float height,
                    const std::array<float, 4>& background, const std::array<float, 4>& border, float borderWidth);
// Draw the existing overlay-surface panel style, including its prepared backdrop blur.
void drawPanelSurface(const UIRenderContext& context, float x, float y, float width, float height);
// Collect or record text according to the context phase; scale uses eight-unit glyph height.
void drawText(const UIRenderContext& context, const std::string& text, float x, float y, float scale,
              const std::array<float, 4>& color);
// Record a right-facing arrow from UI rectangles without a texture dependency.
void drawArrow(const UIRenderContext& context, float x, float y, float size, const std::array<float, 4>& color);

} // namespace ui
