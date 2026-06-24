#pragma once

#include "../item/Item.h"
#include "../world/block/Block.h"

#include <string>

namespace ui {

inline bool shouldUseBakedBlockIcon(const ItemDef& itemDef) {
    if (itemDef.renderBlock == 0) {
        return false;
    }

    const BlockDef& blockDef = BlockRegistry::get(itemDef.renderBlock);
    const bool hasExplicitItemTexture = std::string(itemDef.iconTextureName) != "unknown";
    return !hasExplicitItemTexture &&
           (blockDef.renderShapeName == "model" ||
            blockDef.biomeTint != BiomeTintKind::None);
}

}
