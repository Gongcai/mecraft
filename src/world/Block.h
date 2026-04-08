//
// Created by Caiwe on 2026/3/24.
//

#ifndef MECRAFT_BLOCK_H
#define MECRAFT_BLOCK_H
#include <array>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

class ResourceMgr;

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
    constexpr BlockID COAL_ORE = 7;
    constexpr BlockID DIAMOND_ORE = 8;
    constexpr BlockID GOLD_ORE = 9;
    constexpr BlockID IRON_ORE = 10;
    constexpr BlockID WATER = 11;

    constexpr BlockID COUNT = 255; // 预留255种方块类型（0-254有效）
}

struct BlockDef {
    const char* name    = "unknown";  // 方块名称
    bool isSolid        = true;       // 是否为实体方块（如空气不是）
    bool isTransparent  = false;      // 是否透明
    bool isLightSource  = false;      // 是否为光源
    bool isSelectable   = true;    // 是否可选中
    uint8_t lightLevel  = 0;          // 发光强度（0-15）

    //六面纹理Atlas tile索引（上下左右前后）
    //值相同表示六面相同
    int texTop = 0;
    int texBottom = 0;
    int texLeft = 0;
    int texRight = 0;
    int texFront = 0;
    int texBack = 0;
};

class BlockRegistry {
public:
    static void init(ResourceMgr* resourceMgr = nullptr);
    static const BlockDef& get(BlockID id);
    static void printAllBlocks();
private:
    static std::array<BlockDef, BlockType::COUNT> s_blocks;

};


#endif //MECRAFT_BLOCK_H