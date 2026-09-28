#pragma once

#include "../item/Item.h"

#include <string_view>

namespace ui {

inline bool shouldUseBakedBlockIcon(const ItemDef& itemDef) {
    const std::string_view iconTexture = itemDef.iconTextureName != nullptr ? itemDef.iconTextureName : "";
    const bool hasExplicitItemIcon = !iconTexture.empty() && iconTexture != "unknown";
    return itemDef.renderBlock != 0 && !hasExplicitItemIcon;
}

} // namespace ui
