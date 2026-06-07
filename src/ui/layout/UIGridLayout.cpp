#include "UIGridLayout.h"

void UIGridLayout::layout(const UIRenderContext& ctx) {
    layout();
    UIWidget::layout(ctx);
}

void UIGridLayout::layout() {
    const auto& children = getChildren();
    if (children.empty() || m_columns <= 0) return;

    int col = 0;
    int row = 0;
    for (const auto& child : children) {
        if (!child) continue;

        child->x = static_cast<float>(col) * (m_cellWidth + m_spacing);
        child->y = static_cast<float>(row) * (m_cellHeight + m_spacing);
        child->width = m_cellWidth;
        child->height = m_cellHeight;

        ++col;
        if (col >= m_columns) {
            col = 0;
            ++row;
        }
    }

    // Update own dimensions to fit all children
    const int totalRows = (static_cast<int>(children.size()) + m_columns - 1) / m_columns;
    width = static_cast<float>(m_columns) * m_cellWidth
          + static_cast<float>(m_columns - 1) * m_spacing;
    height = static_cast<float>(totalRows) * m_cellHeight
           + static_cast<float>(totalRows - 1) * m_spacing;
}
