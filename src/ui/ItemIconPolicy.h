#pragma once

#include "../item/Item.h"

namespace ui {

inline bool shouldUseBakedBlockIcon(const ItemDef& itemDef) {
    return itemDef.renderBlock != 0;
}

} // namespace ui
