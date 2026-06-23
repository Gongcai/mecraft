#pragma once

#include "../item/Item.h"
#include "../world/block/Block.h"

namespace ui {

inline bool shouldUseBakedBlockIcon(const ItemDef& itemDef) {
    if (itemDef.renderBlock == 0) {
        return false;
    }

    const BlockDef& blockDef = BlockRegistry::get(itemDef.renderBlock);
    return blockDef.renderShapeName == "model" || blockDef.biomeTint != BiomeTintKind::None;
}

}
