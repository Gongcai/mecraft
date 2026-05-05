#pragma once

#include "UIWidget.h"

class UIGridLayout : public UIWidget {
public:
    UIGridLayout() = default;

    void setColumns(int cols) { m_columns = cols > 0 ? cols : 1; }
    [[nodiscard]] int getColumns() const { return m_columns; }

    void setCellWidth(float w) { m_cellWidth = w; }
    [[nodiscard]] float getCellWidth() const { return m_cellWidth; }

    void setCellHeight(float h) { m_cellHeight = h; }
    [[nodiscard]] float getCellHeight() const { return m_cellHeight; }

    void setSpacing(float s) { m_spacing = s; }
    [[nodiscard]] float getSpacing() const { return m_spacing; }

    // Recalculate child positions based on grid parameters.
    // Call after adding children or changing grid properties.
    void layout();

protected:
    void renderSelf(const UIRenderContext& ctx) const override { (void)ctx; }

private:
    int m_columns = 1;
    float m_cellWidth = 0.0f;
    float m_cellHeight = 0.0f;
    float m_spacing = 0.0f;
};
