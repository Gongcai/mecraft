//
// Created by Caiwe on 2026/3/24.
//

#ifndef MECRAFT_BLOCK_H
#define MECRAFT_BLOCK_H
#include <array>
#include <cstdint>
using BlockID = uint8_t;

// 方块类型
namespace BlockType {
    constexpr BlockID AIR    = 0;
    constexpr BlockID DIRT   = 1;
    constexpr BlockID GRASS  = 2;
    constexpr BlockID STONE  = 3;
    constexpr BlockID SAND   = 4;
    constexpr BlockID WOOD   = 5;
    constexpr BlockID GLASS  = 6;
    // ....
    constexpr BlockID COUNT = 255; // 预留255种方块类型（0-254有效）
}

struct BlockDef {
    const char* name;
    bool isSolid        = true;  // 是否为实体方块（如空气不是）
    bool isTransparent  = false; // 是否透明
    bool isLightSource  = false; // 是否为光源
    uint8_t lightLevel  = 0;     // 发光强度（0-15）

    //六面纹理Atlas tile索引（上下左右前后）
    //值相同表示六面相同
    int texTop,texBottom,texLeft,texRight,texFront,texBack;
};

class BlockRegistry {
public:
    static void init();
    static const BlockDef& get(BlockID id);
private:
    static std::array<BlockDef, BlockType::COUNT> s_blocks;

};



#endif //MECRAFT_BLOCK_H