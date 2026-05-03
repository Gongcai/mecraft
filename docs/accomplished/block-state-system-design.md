# 方块状态系统（Block States）设计文档

> 目标：引入方块属性与扁平化状态 ID，使方块支持方向、朝向等状态变体，同时保持数据驱动架构和存储层的高效性。

---

## 1. 背景与动机

### 1.1 现状

| 维度 | 当前实现 |
|------|---------|
| 方块标识 | `BlockID = RuntimeId (uint16_t)`，每种方块仅一个固定 ID |
| 方块定义 | `BlockDef` 仅描述静态属性（固体、透明、纹理等），无状态变体 |
| 方块常量 | `BlockIds::TORCH = 28`，火把无朝向区分 |
| 射线检测 | `World::raycast` 返回 `hitBlock + placeBlock`，无命中面法线 |
| 方块放置 | 直接写入 `blockToPlace`，所有方块行为相同 |
| 渲染形状 | `BlockRenderShape { Cube, Cross }`，Mesher 中硬编码 if/else 链 |
| 配置驱动 | `blocks.json` 定义 30 种方块，无 `properties` 字段 |

### 1.2 核心问题

1. **无方向性**：火把没有朝向、原木没有轴向、熔炉没有正面——所有方块外观和行为千篇一律
2. **扩展困难**：未来加入楼梯、半砖、门等方块时，缺少表达状态组合的机制
3. **放置逻辑缺失**：所有方块放置时使用相同逻辑，无法根据命中面或玩家朝向决定方向
4. **渲染形状不可扩展**：`BlockRenderShape` 枚举只有两种值，Mesher 中硬编码 if 链，新增形状需改核心代码

### 1.3 为什么现在做

项目已具备 Palette + BitPackedArray + SubChunk 存储基础设施，且尚无存档系统。引入 Block State 是在此基础上最自然的延伸，迁移成本最低。

---

## 2. 总体架构

```
blocks.json（声明式配置）
    │
    │  "properties": { "facing": ["north","south","east","west"] }
    │  "placementStrategy": "horizontal_facing"
    │  "renderShape": "stairs"
    │
    ▼
BlockRegistry::init()
    ├── BlockStateRegistry::explodeAllStates()        ← 状态爆炸（属性排列组合 → StateID）
    ├── PlacementStrategyRegistry::initBuiltin()      ← 放置策略注册
    └── MeshBuilderRegistry::initBuiltin()            ← 渲染策略注册

运行时数据流（热路径，零字符串）：
    ┌──────────────────────────────────────────────────────────────┐
    │ 放置: PlacementContext → PlacementStrategyFn → StateID(整数) │
    │ 存储: StateID → Palette → BitPackedArray（现有机制，零改动）  │
    │ 渲染: StateID → MeshBuilderFn → 顶点（零字符串）            │
    └──────────────────────────────────────────────────────────────┘
```

**核心原则**：

- **字符串仅在 JSON 解析和调试时出现**，运行时热路径全链路使用整数索引
- **存储层最小改动**：StateID 是 uint16_t，与现有 BlockID 类型兼容；Palette 需增加 Direct16 模式（应对 >256 种状态），BitPackedArray/SubChunk 逻辑不变
- **数据驱动优先**：90% 的新方块只需改 JSON，10% 特殊方块写策略函数并注册

---

## 3. Block State 扁平化系统

### 3.1 数据驱动定义

在 `blocks.json` 中扩展 `properties` 和 `defaultState`：

```json
{
  "id": "minecraft:torch",
  "properties": {
    "facing": ["floor", "north", "south", "east", "west"]
  },
  "defaultState": { "facing": "floor" },
  "placementStrategy": "attach_wall",
  "supportRule": "attached_face",
  "isSolid": false,
  "isTransparent": true,
  "isLightSource": true,
  "lightLevel": 14,
  "renderShape": "torch",
  "timeToBreak": 10,
  "textures": { "all": "blue_wool" }
}
```

```json
{
  "id": "minecraft:oak_stairs",
  "properties": {
    "facing": ["north", "south", "east", "west"],
    "half": ["top", "bottom"]
  },
  "defaultState": { "facing": "north", "half": "bottom" },
  "placementStrategy": "stairs",
  "renderShape": "stairs",
  "textures": { "all": "oak_planks" }
}
```

没有 `properties` 的方块行为不变，1 个 BlockID = 1 个默认 StateID。

### 3.2 状态爆炸（State Explosion）

引擎启动时，对每个方块的 `properties` 做排列组合，生成连续的全局 `StateID`：

```
minecraft:torch
  ├── StateID 100 → torch[facing=floor]
  ├── StateID 101 → torch[facing=north]
  ├── StateID 102 → torch[facing=south]
  ├── StateID 103 → torch[facing=east]
  └── StateID 104 → torch[facing=west]

minecraft:oak_stairs
  ├── StateID 105 → stairs[facing=north, half=bottom]
  ├── StateID 106 → stairs[facing=north, half=top]
  ├── StateID 107 → stairs[facing=south, half=bottom]
  ├── StateID 108 → stairs[facing=south, half=top]
  ├── StateID 109 → stairs[facing=east,  half=bottom]
  ├── StateID 110 → stairs[facing=east,  half=top]
  ├── StateID 111 → stairs[facing=west,  half=bottom]
  └── StateID 112 → stairs[facing=west,  half=top]
```

没有 `properties` 的方块：`StateID == BlockID`（向后兼容）。

**ID 空间分析**：Minecraft 1.21 约 ~12000 个状态，`uint16_t`（65535）足够。单方块属性总数需控制，如楼梯 `4×2=8` 可接受，但 `6×3×2=36` 的组合需谨慎。

### 3.3 BlockStateRegistry 实现

```cpp
// ── src/world/BlockStateRegistry.h ──

#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include "../core/NamespacedId.h"

using StateID = uint16_t;
using BlockID = uint16_t;

// 属性值的紧凑整数表示
struct PropertyKey {
    uint16_t nameIndex;    // 全局属性名池索引 (如 0=facing, 1=axis, 2=half)
    uint16_t valueIndex;   // 该属性的值索引 (如 facing: 0=floor, 1=north, ...)
};

// 每 StateID 的纹理索引缓存 — 状态爆炸时预计算，运行时零字符串
// 替代 BlockDef 中的 texTop/texBottom/... 字段
struct StateTextureIndices {
    int texTop;
    int texBottom;
    int texLeft;
    int texRight;
    int texFront;
    int texBack;
};

// 每个 StateID 对应的紧凑数据 — 仅整数，缓存友好
struct BlockStateEntry {
    StateID stateId;
    BlockID blockId;
    uint8_t propertyCount;
    uint16_t propertiesOffset;  // 指向 s_statePropertiesPool 的偏移量（uint16_t，支持 >256 条目）
    uint16_t textureOffset;     // 指向 s_stateTextures 的偏移量
};

class BlockStateRegistry {
public:
    // 引擎启动时调用：对所有方块的 properties 做排列组合
    static void explodeAllStates();

    // ──── 热路径 API（零字符串）──────────────────────

    static StateID getDefaultState(BlockID blockId);
    static StateID getState(BlockID blockId, uint16_t propKey, uint16_t propValue);
    static StateID getState(BlockID blockId,
                            std::initializer_list<std::pair<uint16_t, uint16_t>> props);
    static StateID getState(BlockID blockId,
                            const std::vector<std::pair<uint16_t, uint16_t>>& props);
    static BlockID getBlockId(StateID stateId);
    static uint16_t getPropertyIndex(StateID stateId, uint16_t nameIndex);
    static uint8_t getPropertyCount(StateID stateId);

    // 状态转换：从当前 StateID 修改单个属性，返回新 StateID
    // 用于开门/关门、含水/脱水等原地方块状态切换
    // 内部实现为：提取当前状态所有属性 → 替换指定属性 → 查找新 StateID
    static StateID withProperty(StateID currentState, uint16_t propKey, uint16_t newValue);
    static StateID withProperty(StateID currentState, uint16_t propKey, const std::string& newValue);

    // 获取指定 StateID 的预计算纹理索引（用于渲染）
    static const StateTextureIndices& getStateTextures(StateID stateId);

    // ──── 冷路径 API（字符串，仅启动/调试时用）────────────

    static void registerBlockProperties(BlockID blockId,
                                         std::vector<std::pair<std::string, std::vector<std::string>>> properties,
                                         std::map<std::string, std::string> defaultState);
    static uint16_t getPropertyNameIndex(const std::string& name);
    static uint16_t getPropertyValueIndex(uint16_t nameIndex, const std::string& value);
    static const std::string& getPropertyName(uint16_t nameIndex);
    static const std::string& getPropertyValue(uint16_t nameIndex, uint16_t valueIndex);
    static std::string stateToString(StateID stateId);
    static size_t getStateCount();

private:
    // 全局字符串池（冷数据，启动/调试时访问）
    static std::vector<std::string> s_propertyNamePool;
    static std::unordered_map<std::string, uint16_t> s_propertyNameLookup;
    static std::vector<std::vector<std::string>> s_propertyValuePool;
    static std::vector<std::unordered_map<std::string, uint16_t>> s_propertyValueLookup;

    // 热数据（纯整数，缓存友好）
    static std::vector<BlockStateEntry> s_states;
    static std::vector<PropertyKey> s_statePropertiesPool;
    static std::vector<StateTextureIndices> s_stateTextures;  // 每状态的纹理缓存
    static std::unordered_map<BlockID, StateID> s_defaultState;
    static std::unordered_map<uint64_t, StateID> s_stateLookup;

    // withProperty 优化：为每个 BlockID 预建属性名 → 属性在 properties 数组中的位置映射
    // 避免每次 withProperty 调用时线性搜索
    struct BlockPropertyLayout {
        uint16_t firstStateId;        // 该方块的第一个 StateID
        uint8_t propertyCount;
        uint8_t propertiesPerState;   // = propertyCount
        // propertyPositionInArray[nameIndex] = 该属性在 PropertyKey 数组中的偏移
        std::vector<uint8_t> propertyPosition;
        // propertyStride[nameIndex] = 该属性的值数量（用于快速计算 withProperty 的 StateID 偏移）
        std::vector<uint16_t> propertyStride;
    };
    static std::unordered_map<BlockID, BlockPropertyLayout> s_blockPropertyLayouts;

    static uint64_t computeStateKey(BlockID blockId,
                                     const std::vector<PropertyKey>& props);
};
```

#### 内存布局

```
s_states[101] = { stateId:101, blockId:28, propertyCount:1, propertiesOffset:50 }
                                            │
                                            ▼
s_statePropertiesPool[50] = { nameIndex:0, valueIndex:1 }
                                      │           │
s_propertyNamePool[0] = "facing"      │   s_propertyValuePool[0][1] = "north"
  (冷数据,仅调试用)                     │     (冷数据,仅调试用)
                                        │
                               热路径只访问绿色部分
```

### 3.4 属性索引缓存

启动时将常用属性名/值的整数索引缓存到全局常量，策略函数热路径零字符串、零哈希：

```cpp
namespace PropIndices {
    // 属性名索引
    uint16_t FACING = 0;
    uint16_t AXIS   = 0;
    uint16_t HALF   = 0;
    uint16_t OPEN   = 0;          // 门、栅栏门、活板门
    uint16_t POWERED = 0;         // 拉杆、按钮
    uint16_t LIT    = 0;          // 熔炉、红石灯
    uint16_t WATERLOGGED = 0;     // 含水方块

    // 属性值索引
    uint16_t FACING_FLOOR = 0;
    uint16_t FACING_NORTH = 0;
    uint16_t FACING_SOUTH = 0;
    uint16_t FACING_EAST  = 0;
    uint16_t FACING_WEST  = 0;
    uint16_t AXIS_X = 0;
    uint16_t AXIS_Y = 0;
    uint16_t AXIS_Z = 0;
    uint16_t HALF_TOP    = 0;
    uint16_t HALF_BOTTOM = 0;
    uint16_t OPEN_TRUE    = 0;
    uint16_t OPEN_FALSE   = 0;
    uint16_t POWERED_TRUE  = 0;
    uint16_t POWERED_FALSE = 0;
    uint16_t LIT_TRUE    = 0;
    uint16_t LIT_FALSE   = 0;
    uint16_t WATERLOGGED_TRUE  = 0;
    uint16_t WATERLOGGED_FALSE = 0;

    void init() {
        FACING = BlockStateRegistry::getPropertyNameIndex("facing");
        AXIS   = BlockStateRegistry::getPropertyNameIndex("axis");
        HALF   = BlockStateRegistry::getPropertyNameIndex("half");
        // 以下属性可能尚未注册（含水/交互属性延后实现），
        // getPropertyNameIndex 对不存在的属性返回 0，需校验
        OPEN       = BlockStateRegistry::getPropertyNameIndex("open");
        POWERED    = BlockStateRegistry::getPropertyNameIndex("powered");
        LIT        = BlockStateRegistry::getPropertyNameIndex("lit");
        WATERLOGGED = BlockStateRegistry::getPropertyNameIndex("waterlogged");

        FACING_FLOOR = BlockStateRegistry::getPropertyValueIndex(FACING, "floor");
        FACING_NORTH = BlockStateRegistry::getPropertyValueIndex(FACING, "north");
        FACING_SOUTH = BlockStateRegistry::getPropertyValueIndex(FACING, "south");
        FACING_EAST  = BlockStateRegistry::getPropertyValueIndex(FACING, "east");
        FACING_WEST  = BlockStateRegistry::getPropertyValueIndex(FACING, "west");

        AXIS_X = BlockStateRegistry::getPropertyValueIndex(AXIS, "x");
        AXIS_Y = BlockStateRegistry::getPropertyValueIndex(AXIS, "y");
        AXIS_Z = BlockStateRegistry::getPropertyValueIndex(AXIS, "z");

        HALF_TOP    = BlockStateRegistry::getPropertyValueIndex(HALF, "top");
        HALF_BOTTOM = BlockStateRegistry::getPropertyValueIndex(HALF, "bottom");

        // 布尔属性值索引（延后实现时初始化）
        if (OPEN != 0) {
            OPEN_TRUE  = BlockStateRegistry::getPropertyValueIndex(OPEN, "true");
            OPEN_FALSE = BlockStateRegistry::getPropertyValueIndex(OPEN, "false");
        }
        if (POWERED != 0) {
            POWERED_TRUE  = BlockStateRegistry::getPropertyValueIndex(POWERED, "true");
            POWERED_FALSE = BlockStateRegistry::getPropertyValueIndex(POWERED, "false");
        }
        if (LIT != 0) {
            LIT_TRUE  = BlockStateRegistry::getPropertyValueIndex(LIT, "true");
            LIT_FALSE = BlockStateRegistry::getPropertyValueIndex(LIT, "false");
        }
        if (WATERLOGGED != 0) {
            WATERLOGGED_TRUE  = BlockStateRegistry::getPropertyValueIndex(WATERLOGGED, "true");
            WATERLOGGED_FALSE = BlockStateRegistry::getPropertyValueIndex(WATERLOGGED, "false");
        }
    }
}
```

### 3.5 对存储层的影响与 Palette 弹性扩容

StateID 是 `uint16_t`，与现有 `BlockID` 类型相同，Palette/BitPackedArray 无需感知语义变化。但需解决 **Palette 256 上限问题**。

#### 问题分析

当前 Palette 使用 `uint8_t` 索引，单 SubChunk（16³=4096 格）最多支持 256 种不同状态。在引入 Block State 后：

- 原版生存中通常够用（同一区域不会出现太多不同状态）
- 但复杂建造场景（多种颜色楼梯 × 4方向 × 2半 = 数十种状态）或红石密集区域，256 上限极易被突破
- 一旦突破，当前实现会静默溢出导致数据损坏

#### 弹性扩容方案（三级存储模式）

采用与 Minecraft Java 版相同的弹性机制：

```
┌─────────────────────────────────────────────────────────┐
│  状态数 ≤ 16    →  IndirectPalette (4 bits/entry)       │
│                    uint8_t 索引，Palette 数组 ≤ 16 条    │
│                                                         │
│  状态数 ≤ 256   →  IndirectPalette (8 bits/entry)       │
│                    uint8_t 索引，Palette 数组 ≤ 256 条   │
│                                                         │
│  状态数 > 256   →  DirectPalette (16 bits/entry)        │
│                    抛弃 Palette，每格直接存 StateID      │
│                    BitPackedArray 位宽 = 16              │
└─────────────────────────────────────────────────────────┘
```

**实现要点**：

```cpp
// Palette 类改造
class Palette {
public:
    uint16_t getOrCreateIndex(RuntimeId runtimeId);  // 返回值改为 uint16_t
    RuntimeId getRuntimeId(uint16_t paletteIndex) const;  // 入参改为 uint16_t
    uint8_t bitsPerEntry() const;

    bool isDirect() const { return m_mode == PaletteMode::Direct; }

private:
    enum class PaletteMode : uint8_t {
        Indirect8  = 0,  // ≤256 种，uint8_t 索引
        Direct16   = 1,  // >256 种，直接存 StateID
    };

    PaletteMode m_mode = PaletteMode::Indirect8;

    // Indirect 模式
    std::vector<RuntimeId> m_indexToId;
    std::unordered_map<RuntimeId, uint16_t> m_idToIndex;  // value 改为 uint16_t

    // Direct 模式：m_indexToId 为空，BitPackedArray 直接存 StateID
};
```

**关键变化**：

| 组件 | 改动 | 影响 |
|------|------|------|
| `Palette` | 索引类型 `uint8_t` → `uint16_t`，增加 Direct 模式 | 小范围改造 |
| `BitPackedArray` | 无改动，位宽 16 仍在支持范围内 | 无 |
| `SubChunk` | `getBlock` / `setBlock` 适配 Palette 返回值变化 | 小 |
| 快照采集 | Direct 模式下快照仍为 `BlockID[]`，语义不变 | 无 |

**内存影响分析**：

| 模式 | 单 SubChunk 存储开销 | 适用场景 |
|------|---------------------|---------|
| Indirect (≤256 种, 8 bits) | 4096 × 1B = 4KB + Palette | 绝大多数场景 |
| Direct (>256 种, 16 bits) | 4096 × 2B = 8KB | 极端建造场景 |

Direct 模式下内存仅翻倍（4KB → 8KB），完全可接受。实际上 Direct 模式极少触发，无需过度优化。

#### SubChunk 级 Palette 压缩仍然有效

即使在全局有数千个 StateID 的世界中，单个 16³ 区域通常只包含少量不同状态。Palette 的核心价值在于局部压缩，Block State 不影响这一机制。

### 3.6 多纹理状态映射（Texture Variants）

#### 问题

当前 `BlockDef` 中 6 面纹理是静态的（`texTop/texBottom/...`）。引入状态后，同一方块的不同状态需要不同纹理。例如：

- 熔炉 `facing=north`：正面纹理在 Z- 面，侧面纹理在其余面
- 熔炉 `facing=south`：正面纹理在 Z+ 面，侧面纹理在其余面
- 点亮的熔炉 `lit=true`：正面纹理替换为发光版本

当前的 `MeshBuilderFn` 只持有 `BlockDef` 引用，无法根据 StateID 获取正确的纹理。

#### 解决方案：每状态纹理预计算 + JSON 声明式映射

**核心思想**：在状态爆炸时，根据 JSON 中声明的纹理规则，为每个 StateID 预计算 6 面纹理索引，缓存到 `s_stateTextures` 数组。运行时通过 `getStateTextures(stateId)` 直接获取，零字符串、零计算。

#### blocks.json 纹理变体语法

```json
{
  "id": "minecraft:furnace",
  "properties": {
    "facing": ["north", "south", "east", "west"],
    "lit": ["true", "false"]
  },
  "defaultState": { "facing": "north", "lit": "false" },
  "placementStrategy": "horizontal_facing",
  "renderShape": "cube",
  "textures": {
    "top": "furnace_top",
    "bottom": "furnace_top",
    "side": "furnace_side"
  },
  "textureVariants": [
    {
      "when": { "facing": "north" },
      "override": { "front": "furnace_front" }
    },
    {
      "when": { "facing": "south" },
      "override": { "back": "furnace_front" }
    },
    {
      "when": { "facing": "east" },
      "override": { "right": "furnace_front" }
    },
    {
      "when": { "facing": "west" },
      "override": { "left": "furnace_front" }
    },
    {
      "when": { "lit": "true" },
      "override": { "front": "furnace_front_lit", "back": "furnace_front_lit",
                    "left": "furnace_front_lit", "right": "furnace_front_lit" }
    }
  ]
}
```

**语义**：
1. `textures` 定义基础纹理（不变状态的默认面纹理）
2. `textureVariants` 是条件覆盖列表：当状态的属性匹配 `when` 时，用 `override` 覆盖对应面
3. 多条 `when` 规则按顺序叠加（先匹配的先应用，后面的可覆盖前面的）
4. 未被覆盖的面保持 `textures` 中的默认值

#### 预计算过程

```cpp
void BlockStateRegistry::explodeAllStates() {
    // ... 对每个方块的 properties 做排列组合，生成 BlockStateEntry ...

    // 对每个生成的 StateID，计算其纹理索引
    for (StateID sid = firstStateId; sid <= lastStateId; ++sid) {
        const BlockStateEntry& entry = s_states[sid];
        const BlockDef& baseDef = BlockRegistry::getFast(entry.blockId);

        // 从 BlockDef 获取基础纹理
        StateTextureIndices textures;
        textures.texTop    = baseDef.texTop;
        textures.texBottom = baseDef.texBottom;
        textures.texLeft   = baseDef.texLeft;
        textures.texRight  = baseDef.texRight;
        textures.texFront  = baseDef.texFront;
        textures.texBack   = baseDef.texBack;

        // 应用 textureVariants 覆盖
        for (const auto& variant : baseDef.textureVariants) {
            if (stateMatchesVariant(sid, variant.when)) {
                applyTextureOverride(textures, variant.override, resourceMgr);
            }
        }

        s_stateTextures.push_back(textures);
    }
}
```

#### MeshBuilderFn 中的使用

```cpp
static void buildFurnace(ChunkMeshData& meshData, const MeshBuildContext& ctx) {
    // 从 StateID 获取预计算的纹理，而非从 BlockDef
    const StateTextureIndices& textures = BlockStateRegistry::getStateTextures(ctx.stateId);

    // 逐面渲染，使用 textures.texTop / .texFront / ... 等
    for (int face = 0; face < 6; ++face) {
        // ... 面剔除检查 ...
        FaceRenderData renderData;
        renderData.tileIndex = getTextureFromStateIndices(textures, face);
        // ... 生成顶点 ...
    }
}
```

#### 与现有 Cube 贪心合并的兼容

对于 Cube 形状且使用纹理变体的方块（如熔炉），不能走贪心合并路径（贪心合并假设同 BlockID 的所有面纹理一致）。需在 `BlockDef` 中标记：

```cpp
struct BlockDef {
    // ... 现有字段 ...

    // 是否有纹理变体（有 textureVariants 条目时自动设为 true）
    bool hasTextureVariants = false;
};
```

在 `isOpaqueCubeCandidate()` / `isTransparentCubeCandidate()` 中增加判断：

```cpp
bool isOpaqueCubeCandidate(const BlockDef& def) {
    return def.isCubeShape() && !def.isTransparent && !def.hasTextureVariants;
}
```

有纹理变体的 Cube 方块退回到逐方块逐面渲染路径（与 Cross 等特殊形状同列），但使用 `getStateTextures()` 获取正确的面纹理。

### 3.7 状态转换 API（State Transitions）

#### 需求场景

游戏运行中有大量"原地方块状态切换"的场景，不涉及方块的销毁和重建：

| 场景 | 属性变化 | 触发方式 |
|------|---------|---------|
| 开/关门 | `open: false ↔ true` | 玩家右键 |
| 开/关栅栏门 | `open: false ↔ true` | 玩家右键 |
| 含水/脱水 | `waterlogged: false ↔ true` | 水流进入/退出 |
| 点亮/熄灭熔炉 | `lit: false ↔ true` | 物品放入/取出 |
| 开/关活板门 | `open: false ↔ true` | 玩家右键 |
| 拉杆开/关 | `powered: false ↔ true` | 玩家右键 |

这些操作只需要修改方块的一个属性值，不需要销毁和重新放置方块。

#### API 设计

```cpp
// BlockStateRegistry 中新增：

// 从当前 StateID 修改单个属性，返回新 StateID
// 如果属性名/值无效，返回原 StateID（不变）
static StateID withProperty(StateID currentState, uint16_t propKey, uint16_t newValue);
static StateID withProperty(StateID currentState, uint16_t propKey, const std::string& newValue);

// 便捷方法：切换布尔属性（true ↔ false）
static StateID toggleProperty(StateID currentState, uint16_t propKey);
```

#### 优化实现

朴素实现：提取当前状态所有属性 → 替换指定属性 → 查哈希表 → 返回新 StateID。但每次调用需要构建属性 vector 并查哈希表，开销较高。

**优化**：利用状态爆炸时的排列组合规律，`withProperty` 可以通过纯算术计算出目标 StateID 偏移量，**无需查哈希表**。

```
排列组合的内存布局（以楼梯为例）：
  属性顺序：[facing(4值), half(2值)]

  StateID 105 → stairs[facing=north(0), half=bottom(0)]
  StateID 106 → stairs[facing=north(0), half=top(1)]
  StateID 107 → stairs[facing=south(1), half=bottom(0)]
  StateID 108 → stairs[facing=south(1), half=top(1)]
  ...

  规律：每个属性有一个 stride = 它右侧所有属性值数量的乘积
    facing 的 stride = 2 (half 有 2 个值)
    half   的 stride = 1

  withProperty(105, FACING, FACING_SOUTH) 的计算：
    当前偏移 = (facing=0) * 2 + (half=0) * 1 = 0
    目标偏移 = (facing=1) * 2 + (half=0) * 1 = 2
    delta = 2 - 0 = 2
    结果 = 105 + 2 = 107 ✓
```

```cpp
// BlockPropertyLayout 中的预计算数据：
struct BlockPropertyLayout {
    uint16_t firstStateId;        // 该方块的第一个 StateID
    uint8_t propertyCount;
    std::vector<uint8_t> propertyPosition;  // 属性名索引 → 在 PropertyKey 数组中的位置
    std::vector<uint16_t> propertyStride;   // 属性名索引 → 该属性的 stride
    std::vector<uint16_t> propertyValueCount; // 属性名索引 → 该属性的值数量
};

StateID BlockStateRegistry::withProperty(StateID currentState, uint16_t propKey, uint16_t newValue) {
    const BlockStateEntry& entry = s_states[currentState];
    auto it = s_blockPropertyLayouts.find(entry.blockId);
    if (it == s_blockPropertyLayouts.end()) return currentState;

    const BlockPropertyLayout& layout = it->second;
    const uint8_t pos = layout.propertyPosition[propKey];

    // 从当前状态的属性中取出旧值
    const PropertyKey& oldProp = s_statePropertiesPool[entry.propertiesOffset + pos];
    const int16_t delta = static_cast<int16_t>(newValue) - static_cast<int16_t>(oldProp.valueIndex);

    if (delta == 0) return currentState;  // 值没变

    // 纯算术计算新 StateID
    return currentState + delta * layout.propertyStride[propKey];
}
```

**性能**：O(1)，一次数组访问 + 一次乘法 + 一次加法，无需查哈希表。

#### 使用示例

```cpp
// 右键开门
void onDoorInteract(World& world, const glm::ivec3& pos) {
    StateID current = world.getBlock(pos.x, pos.y, pos.z);
    StateID newState = BlockStateRegistry::toggleProperty(current, PropIndices::OPEN);
    if (newState != current) {
        world.setBlock(pos.x, pos.y, pos.z, static_cast<BlockID>(newState));
    }
}

// 水流进入楼梯
void onWaterFlowInto(World& world, const glm::ivec3& pos) {
    StateID current = world.getBlock(pos.x, pos.y, pos.z);
    StateID newState = BlockStateRegistry::withProperty(
        current, PropIndices::WATERLOGGED, PropIndices::WATERLOGGED_TRUE);
    world.setBlock(pos.x, pos.y, pos.z, static_cast<BlockID>(newState));
}
```

---

## 4. 射线检测增强

### 4.1 现状问题

当前 `World::raycast` 签名：

```cpp
bool World::raycast(const PhysicsInfo& ray, float maxDist,
                    glm::ivec3& hitBlock, glm::ivec3& placeBlock) const;
```

- 不返回命中面法线（`hitNormal`）
- 通过 `lastX/lastY/lastZ` 间接推导放置位置
- 项目中已有 `RayHit` 结构体（`PhysicsInfo.h:17-22`）含 `normal` 字段，但从未使用

### 4.2 改造方案

将 `World::raycast` 改为返回已有的 `RayHit`：

```cpp
// PhysicsInfo.h 中已有的 RayHit：
struct RayHit {
    bool hit = false;
    glm::ivec3 blockPos{};
    glm::ivec3 normal{};    // 命中面的法线，用于计算放置位置
    float distance = 0.0f;
};

// World.h 新签名：
RayHit World::raycast(const PhysicsInfo& ray, float maxDist) const;
```

DDA 算法中记录法线——在步进分支中，触发步进的轴即为法线方向：

```cpp
RayHit World::raycast(const PhysicsInfo& ray, float maxDist) const {
    RayHit result;
    // ... DDA 初始化（不变）...

    glm::ivec3 normal(0, 0, 0);

    while (dist <= maxDist) {
        BlockID block = getBlock(x, y, z);
        if (block != BlockIds::AIR && block != BlockIds::WATER) {
            result.hit = true;
            result.blockPos = glm::ivec3(x, y, z);
            result.normal = normal;
            result.distance = dist;
            return result;
        }

        // 步进时记录法线 = 反步进方向
        if (tMaxX < tMaxY) {
            if (tMaxX < tMaxZ) {
                normal = glm::ivec3(-static_cast<int>(stepX), 0, 0);
                x += static_cast<int>(stepX);
                dist = tMaxX; tMaxX += tDeltaX;
            } else {
                normal = glm::ivec3(0, 0, -static_cast<int>(stepZ));
                z += static_cast<int>(stepZ);
                dist = tMaxZ; tMaxZ += tDeltaZ;
            }
        } else {
            if (tMaxY < tMaxZ) {
                normal = glm::ivec3(0, -static_cast<int>(stepY), 0);
                y += static_cast<int>(stepY);
                dist = tMaxY; tMaxY += tDeltaY;
            } else {
                normal = glm::ivec3(0, 0, -static_cast<int>(stepZ));
                z += static_cast<int>(stepZ);
                dist = tMaxZ; tMaxZ += tDeltaZ;
            }
        }
    }
    return result;
}
```

### 4.3 涉及改动

| 文件 | 改动 |
|------|------|
| `World.h` | `raycast` 签名改为返回 `RayHit` |
| `World.cpp` | DDA 中记录法线，填充 `RayHit` |
| `BlockTargetComponent` | 新增 `glm::ivec3 hitNormal{}` 字段 |
| `BlockInteractionBridgeSystem.cpp` | 使用 `RayHit` 替代 `hitBlock/placeBlock` |
| `Player.cpp` | 高亮框绘制使用 `RayHit` |

---

## 5. 放置策略系统

### 5.1 设计原则

不引入虚函数继承体系（与数据驱动架构冲突），而是使用 **策略函数注册表**：

- JSON 中声明策略名（如 `"placementStrategy": "attach_wall"`）
- 引擎内置策略函数，通过字符串名注册
- 新增常见方块只需改 JSON；特殊方块写一个策略函数并注册

### 5.2 PlacementContext 与策略签名

```cpp
// ── src/world/Placement.h ──

#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

using StateID = uint16_t;
using BlockID = uint16_t;

// 放置时的完整上下文
struct PlacementContext {
    BlockID blockId;           // 要放置的方块基类
    glm::ivec3 placePos;      // 放置坐标
    glm::ivec3 hitNormal;     // 命中面法线
    glm::vec3 playerPos;      // 玩家位置（眼高）
    float playerYaw;          // 玩家水平角（度）
    float playerPitch;        // 玩家俯仰角（度）
    bool isSneaking;          // 是否潜行
};

// 策略函数签名：输入上下文，输出 StateID（0 = 放置失败）
using PlacementStrategyFn = StateID(*)(const PlacementContext& ctx);

class PlacementStrategyRegistry {
public:
    static void registerStrategy(const std::string& name, PlacementStrategyFn fn);
    static PlacementStrategyFn getStrategy(const std::string& name);
    static void initBuiltinStrategies();

private:
    static std::unordered_map<std::string, PlacementStrategyFn> s_strategies;
};
```

### 5.3 内置策略实现

```cpp
// ── src/world/Placement.cpp ──

// 默认：无方向方块
static StateID strategySimple(const PlacementContext& ctx) {
    return BlockStateRegistry::getDefaultState(ctx.blockId);
}

// 依附型：火把、梯子 — 仅依赖 hitNormal
static StateID strategyAttachWall(const PlacementContext& ctx) {
    const auto& n = ctx.hitNormal;
    if (n.y == 1)  return BlockStateRegistry::getState(ctx.blockId,
                        {PropIndices::FACING, PropIndices::FACING_FLOOR});
    if (n.y == -1) return 0; // 不能挂天花板
    if (n.z == -1) return BlockStateRegistry::getState(ctx.blockId,
                        {PropIndices::FACING, PropIndices::FACING_NORTH});
    if (n.z == 1)  return BlockStateRegistry::getState(ctx.blockId,
                        {PropIndices::FACING, PropIndices::FACING_SOUTH});
    if (n.x == -1) return BlockStateRegistry::getState(ctx.blockId,
                        {PropIndices::FACING, PropIndices::FACING_WEST});
    if (n.x == 1)  return BlockStateRegistry::getState(ctx.blockId,
                        {PropIndices::FACING, PropIndices::FACING_EAST});
    return 0;
}

// 水平朝向：熔炉、箱子、南瓜 — 仅依赖 playerYaw
static StateID strategyHorizontalFacing(const PlacementContext& ctx) {
    float angle = fmod(ctx.playerYaw + 360.0f, 360.0f);
    std::string facing;
    if (angle < 45.0f || angle >= 315.0f)      facing = "south";
    else if (angle >= 45.0f && angle < 135.0f)  facing = "west";
    else if (angle >= 135.0f && angle < 225.0f) facing = "north";
    else                                         facing = "east";
    return BlockStateRegistry::getState(ctx.blockId,
                {PropIndices::FACING, BlockStateRegistry::getPropertyValueIndex(PropIndices::FACING, facing)});
}

// 轴向：原木、玄武岩 — 依赖 hitNormal 的轴向
static StateID strategyAxisOriented(const PlacementContext& ctx) {
    const auto& n = ctx.hitNormal;
    if (n.y != 0) return BlockStateRegistry::getState(ctx.blockId,
                        {PropIndices::AXIS, PropIndices::AXIS_Y});
    if (n.x != 0) return BlockStateRegistry::getState(ctx.blockId,
                        {PropIndices::AXIS, PropIndices::AXIS_X});
    return BlockStateRegistry::getState(ctx.blockId,
                {PropIndices::AXIS, PropIndices::AXIS_Z});
}

// 楼梯：facing 由玩家朝向决定，half 由点击面决定
static StateID strategyStairs(const PlacementContext& ctx) {
    float angle = fmod(ctx.playerYaw + 360.0f, 360.0f);
    std::string facing;
    if (angle < 45.0f || angle >= 315.0f)      facing = "south";
    else if (angle >= 45.0f && angle < 135.0f)  facing = "west";
    else if (angle >= 135.0f && angle < 225.0f) facing = "north";
    else                                         facing = "east";

    std::string half = (ctx.hitNormal.y == -1) ? "top" : "bottom";
    if (ctx.isSneaking) half = (half == "top") ? "bottom" : "top";

    return BlockStateRegistry::getState(ctx.blockId, {
        {PropIndices::FACING, BlockStateRegistry::getPropertyValueIndex(PropIndices::FACING, facing)},
        {PropIndices::HALF,   BlockStateRegistry::getPropertyValueIndex(PropIndices::HALF, half)}
    });
}

void PlacementStrategyRegistry::initBuiltinStrategies() {
    registerStrategy("simple",            strategySimple);
    registerStrategy("attach_wall",       strategyAttachWall);
    registerStrategy("horizontal_facing", strategyHorizontalFacing);
    registerStrategy("axis_oriented",     strategyAxisOriented);
    registerStrategy("stairs",            strategyStairs);
}
```

### 5.4 BlockDef 扩展

```cpp
struct TextureVariantRule {
    std::map<std::string, std::string> when;     // 属性匹配条件
    std::map<std::string, std::string> override;  // 面纹理覆盖
};

struct BlockDef {
    // ... 现有字段不变 ...

    // 新增：放置策略名（空或 "simple" = 无方向方块）
    std::string placementStrategy = "simple";

    // 新增：支撑规则（空 = 不需要支撑）
    std::string supportRule = "";

    // 新增：渲染形状名（替代原 BlockRenderShape 枚举）
    std::string renderShapeName = "cube";
    uint8_t renderShapeTag = 0;  // 运行时热路径用，0=cube

    bool isCubeShape() const { return renderShapeTag == 0; }

    // 新增：纹理变体规则（JSON 中的 textureVariants）
    std::vector<TextureVariantRule> textureVariants;

    // 新增：是否有纹理变体（有 textureVariants 条目时自动设为 true）
    // 影响 Cube 方块是否可走贪心合并路径
    bool hasTextureVariants = false;
};
```

### 5.5 BlockInteractionBridgeSystem 串联

```cpp
// 当前（第 100-102 行）：
glm::ivec3 hitBlock{};
glm::ivec3 placeBlock{};
const bool hasHit = world.raycast(buildPickRay(transform, camera), kPickDistance, hitBlock, placeBlock);

// 改为：
RayHit rayHit = world.raycast(buildPickRay(transform, camera), kPickDistance);
target.hasTarget = rayHit.hit;
target.targetBlock = rayHit.hit ? rayHit.blockPos : glm::ivec3{};
target.hitNormal = rayHit.hit ? rayHit.normal : glm::ivec3{};
glm::ivec3 placePos = rayHit.hit ? rayHit.blockPos + rayHit.normal : glm::ivec3{};

// 当前放置（第 187 行）：
world.setBlock(placeBlock.x, placeBlock.y, placeBlock.z, blockToPlace);

// 改为：
const BlockDef& blockDef = BlockRegistry::getFast(blockToPlace);
PlacementStrategyFn strategy = PlacementStrategyRegistry::getStrategy(blockDef.placementStrategy);

StateID finalStateId;
if (strategy) {
    PlacementContext ctx{
        blockToPlace,
        placePos,
        rayHit.normal,
        transform.position + glm::vec3(0.0f, transform.eyeHeight, 0.0f),
        camera.yaw,
        camera.pitch,
        false  // TODO: 从输入状态获取 isSneaking
    };
    finalStateId = strategy(ctx);
} else {
    finalStateId = BlockStateRegistry::getDefaultState(blockToPlace);
}

if (finalStateId != 0) {
    world.setBlock(placePos.x, placePos.y, placePos.z, static_cast<BlockID>(finalStateId));
    // ... 后续音效、冷却等逻辑不变
}
```

---

## 6. 渲染策略系统

### 6.1 现状问题

当前 `ChunkMesher.cpp` 的 `buildSubChunkMeshData` 中：

```
buildOpaqueGreedyFaces()        ← Cube && !transparent
buildTransparentGreedyFaces()   ← Cube && transparent
for each block:
    if (Cross) addCrossedQuads()    ← 硬编码 if
    if (Cube) continue              ← 已处理
    fallback: 6面渲染               ← 对新形状（楼梯等）几何错误
```

新增形状需要修改 Mesher 核心循环，违反开闭原则。

### 6.2 MeshBuilderRegistry

```cpp
// ── src/renderer/MeshBuilderRegistry.h ──

#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

class ChunkMeshData;
struct BlockDef;
struct SubChunkMeshingSnapshot;

using StateID = uint16_t;
using BlockID = uint16_t;

// 网格构建上下文
struct MeshBuildContext {
    StateID stateId;          // 完整状态 ID（含属性信息，用于查询纹理变体）
    BlockID blockId;          // 方块基类 ID
    const BlockDef& blockDef; // 方块基础定义（静态属性）
    int localX, localY, localZ;
    const SubChunkMeshingSnapshot& snapshot;
};

// 网格构建策略函数
using MeshBuilderFn = void(*)(ChunkMeshData& meshData, const MeshBuildContext& ctx);

class MeshBuilderRegistry {
public:
    static void registerBuilder(const std::string& shapeName, MeshBuilderFn fn);
    static MeshBuilderFn getBuilder(const std::string& shapeName);

    // 同时分配运行时 tag（热路径整数判断用）
    static uint8_t allocateShapeTag(const std::string& shapeName);
    static uint8_t getShapeTag(const std::string& shapeName);

    static void initBuiltinBuilders();

private:
    static std::unordered_map<std::string, MeshBuilderFn> s_builders;
    static std::unordered_map<std::string, uint8_t> s_shapeTags;
    static uint8_t s_nextTag;
};
```

### 6.3 内置构建器

```cpp
// ── src/renderer/MeshBuilderRegistry.cpp ──

// Cube 由贪心合并路径处理，逐方块路径中为 nullptr
// registerBuilder("cube", nullptr);

static void buildCross(ChunkMeshData& meshData, const MeshBuildContext& ctx) {
    ChunkMesher::addCrossedQuadsImpl(
        meshData.cutoutVertices,
        glm::vec3(static_cast<float>(ctx.localX),
                  static_cast<float>(ctx.localY),
                  static_cast<float>(ctx.localZ)),
        ctx.blockDef, ctx.localX, ctx.localY, ctx.localZ, ctx.snapshot);
}

static void buildTorch(ChunkMeshData& meshData, const MeshBuildContext& ctx) {
    // 根据 StateID 的 facing 属性决定偏移和朝向
    // floor: 居中竖立
    // north/south/east/west: 贴墙偏移 + 微倾斜
    // ...
}

static void buildSlab(ChunkMeshData& meshData, const MeshBuildContext& ctx) {
    // 根据 half 属性决定顶半/底半
    // bottom: Y 0.0~0.5, 顶面在 Y=0.5
    // top:    Y 0.5~1.0, 底面在 Y=0.5
    // ...
}

static void buildStairs(ChunkMeshData& meshData, const MeshBuildContext& ctx) {
    // 根据 facing + half 组合生成楼梯几何
    // 可用 3 个 AABB 组合或逐三角面生成
    // ...
}

void MeshBuilderRegistry::initBuiltinBuilders() {
    allocateShapeTag("cube");      // tag=0
    registerBuilder("cross",  buildCross);
    allocateShapeTag("cross");    // tag=1
    registerBuilder("torch",  buildTorch);
    allocateShapeTag("torch");    // tag=2
    registerBuilder("slab",   buildSlab);
    allocateShapeTag("slab");     // tag=3
    registerBuilder("stairs", buildStairs);
    allocateShapeTag("stairs");   // tag=4
}
```

### 6.4 改造后的 ChunkMesher 主循环

```cpp
ChunkMeshData ChunkMesher::buildSubChunkMeshData(const SubChunkMeshingSnapshot& snapshot) {
    ChunkMeshData meshData;
    // ... reserve ...

    // 第一步：Cube 方块走贪心合并（不变）
    buildOpaqueGreedyFaces(snapshot, meshData);
    buildTransparentGreedyFaces(snapshot, meshData);

    // 第二步：非 Cube 方块走策略分派
    constexpr int S = SubChunk::SIZE;
    for (int y = 0; y < S; ++y) {
        for (int z = 0; z < S; ++z) {
            for (int x = 0; x < S; ++x) {
                const BlockID blockId = snapshot.blocks[scToIndex(x, y, z)];
                if (blockId == 0) continue;

                const BlockDef& def = BlockRegistry::getFast(blockId);

                // 热路径：整数比较判断 Cube
                if (def.isCubeShape()) continue;

                // 策略分派
                MeshBuilderFn builder = MeshBuilderRegistry::getBuilder(def.renderShapeName);
                if (builder) {
                    MeshBuildContext ctx{blockId, blockId, def, x, y, z, snapshot};
                    builder(meshData, ctx);
                }
            }
        }
    }
    // ...
}
```

### 6.5 BlockDef 渲染形状改造

JSON 加载时同步设置 `renderShapeName` 和 `renderShapeTag`：

```cpp
// Block.cpp JSON 解析中：
if (blockJson.contains("renderShape") && blockJson["renderShape"].is_string()) {
    def.renderShapeName = blockJson["renderShape"].get<std::string>();
    def.renderShapeTag = MeshBuilderRegistry::getShapeTag(def.renderShapeName);
}
```

---

## 7. 启动初始化流程

```cpp
void BlockRegistry::init(ResourceMgr* resourceMgr) {
    // Step 1: 注册所有方块基类 ID（不变）
    s_idRegistry.initBuiltinBlockIds();

    // Step 2: 创建默认 BlockDef（不变）
    s_blocks.resize(s_idRegistry.size());
    // ...

    // Step 3: 初始化策略注册表（在 JSON 解析之前）
    PlacementStrategyRegistry::initBuiltinStrategies();
    MeshBuilderRegistry::initBuiltinBuilders();

    // Step 4: 加载 blocks.json（新增解析 properties/placementStrategy/renderShape）
    for (const auto& blockJson : root["blocks"]) {
        // ... 现有解析逻辑 ...

        // 新增：解析 properties
        if (blockJson.contains("properties") && blockJson["properties"].is_object()) {
            std::vector<std::pair<std::string, std::vector<std::string>>> props;
            std::map<std::string, std::string> defaults;
            for (auto& [key, val] : blockJson["properties"].items()) {
                std::vector<std::string> values;
                for (auto& v : val) values.push_back(v.get<std::string>());
                props.emplace_back(key, std::move(values));
            }
            if (blockJson.contains("defaultState") && blockJson["defaultState"].is_object()) {
                for (auto& [k, v] : blockJson["defaultState"].items()) {
                    defaults[k] = v.get<std::string>();
                }
            }
            BlockStateRegistry::registerBlockProperties(id, std::move(props), std::move(defaults));
        }

        // 新增：解析 placementStrategy
        if (blockJson.contains("placementStrategy") && blockJson["placementStrategy"].is_string()) {
            def.placementStrategy = blockJson["placementStrategy"].get<std::string>();
        }

        // 新增：解析 supportRule
        if (blockJson.contains("supportRule") && blockJson["supportRule"].is_string()) {
            def.supportRule = blockJson["supportRule"].get<std::string>();
        }

        // 改造：renderShape 解析
        if (blockJson.contains("renderShape") && blockJson["renderShape"].is_string()) {
            def.renderShapeName = blockJson["renderShape"].get<std::string>();
            def.renderShapeTag = MeshBuilderRegistry::getShapeTag(def.renderShapeName);
        } else {
            def.renderShapeName = "cube";
            def.renderShapeTag = 0;
        }

        s_blocks[id] = def;
    }

    // Step 5: 状态爆炸（JSON 全部解析后）
    BlockStateRegistry::explodeAllStates();
    PropIndices::init();

    s_initialized = true;
    BlockIds::init();
}
```

---

## 8. 依附方块支撑检查（预留设计）

依附型方块（火把、梯子）需要检查支撑方块是否仍在。架构已通过 `supportRule` 预留：

| supportRule | 含义 | 检查逻辑 |
|-------------|------|---------|
| `""` | 不需要支撑 | 无 |
| `attached_face` | 依附在命中面上 | 检查 `hitNormal` 反方向的邻居是否为固体 |
| `ground` | 需要地面支撑 | 检查下方方块是否为固体 |
| `wall_both` | 双侧墙壁支撑（如栅栏门） | 检查两侧 |

当支撑方块被破坏时，触发周围依附方块的支撑检查，失去支撑的方块掉落为物品。此功能可延后实现。

---

## 9. 扩展性分析

### 9.1 新增方块的成本

| 方块类型 | 需要做什么 | 改 C++？ |
|----------|-----------|----------|
| 梯子 | JSON 加 `"placementStrategy": "attach_wall"` + `"supportRule": "attached_face"` | 否 |
| 南瓜 | JSON 加 `"placementStrategy": "horizontal_facing"` | 否 |
| 原木类 | JSON 加 `"placementStrategy": "axis_oriented"` | 否 |
| 铁砧 | JSON 加 `"placementStrategy": "horizontal_facing"` + `"renderShape": "anvil"` | 写 `buildAnvil` 构建器约 40 行 |
| 活塞 | JSON + 新 `attach_six_faces` 策略 | 写策略函数约 20 行 |
| 栅栏门 | JSON + 新 `fence_gate` 策略 | 写策略函数约 30 行 |
| 门（双格+开关） | JSON + 新 `door` 策略 | 写策略函数约 60 行 |
| 床（多方块） | JSON + 新 `bed` 策略 | 写策略函数约 80 行 |

### 9.2 与虚函数方案的对比

| 维度 | 虚函数继承 | 策略注册表（本方案） |
|------|-----------|-------------------|
| 新方块（常见） | 写新子类 | 改 JSON |
| 新方块（特殊） | 写新子类 | 写策略函数 + 改 JSON |
| 代码分布 | 每种方块一个 .h/.cpp | 策略函数集中在 Placement.cpp / MeshBuilderRegistry.cpp |
| 与数据驱动一致性 | 违背 JSON 驱动 | 保持 JSON 驱动优先 |
| 运行时开销 | 虚函数表间接调用 | 函数指针直接调用 |
| 序列化友好度 | 类名无法序列化 | 字符串策略名可序列化 |
| Mod 支持 | 需暴露基类 + 工厂 | 只需调用 registerStrategy / registerBuilder |
| 单元测试 | 需构建 World 上下文 | 策略函数是纯函数，可直接单测 |

---

## 10. 三个 Registry 对齐总览

```
blocks.json
    │
    │  "renderShape": "stairs"
    │  "placementStrategy": "stairs"
    │  "properties": { "facing":[...], "half":[...] }
    │
    ▼
BlockRegistry::init()
    ├── BlockStateRegistry::explodeAllStates()     ← 状态爆炸（整数索引）
    ├── PlacementStrategyRegistry::initBuiltin()   ← 放置策略注册
    └── MeshBuilderRegistry::initBuiltin()         ← 渲染策略注册

运行时热路径数据流：
    ┌──────────────────────────────────────────────────────────────┐
    │ 放置: PlacementContext → PlacementStrategyFn → StateID(整数) │
    │ 存储: StateID → Palette(Indirect/Direct) → BitPackedArray   │
    │ 渲染: StateID → StateTextures(预计算) → MeshBuilderFn → 顶点 │
    │ 切换: withProperty(stateId, key, value) → 新StateID (O(1))  │
    └──────────────────────────────────────────────────────────────┘

字符串出现位置：仅 JSON 解析 + 调试输出
运行时全链路：纯整数索引 + 函数指针
```

| Registry | 键（冷） | 键（热） | 值 | 热路径操作 |
|----------|---------|---------|---|-----------|
| `BlockStateRegistry` | `string` 属性名/值 | `uint16_t` 属性索引 | `StateID` | 整数哈希查找 |
| `PlacementStrategyRegistry` | `string` 策略名 | `Fn*` | 函数指针 | 直接调用 |
| `MeshBuilderRegistry` | `string` 形状名 | `Fn*` + `uint8_t tag` | 函数指针 | 整数比较 + 直接调用 |

---

## 11. 风险与注意事项

### 11.1 Palette 256 上限与弹性扩容（已解决）

| 维度 | 说明 |
|------|------|
| **风险** | Palette 使用 `uint8_t` 索引，单 SubChunk 最多 256 种状态。复杂建造场景（多色楼梯 × 方向 × 半砖）极易突破上限，导致数据损坏 |
| **严重度** | 高 — 静默溢出，无报错 |
| **应对** | 实现三级弹性扩容（Indirect8 → Direct16），详见 §3.5。Direct 模式下每格 16 bits = 8KB/SubChunk，可接受 |
| **验证** | 需编写单元测试：向同一 SubChunk 写入 >256 种状态，验证无数据丢失 |

### 11.2 多纹理状态映射（已解决）

| 维度 | 说明 |
|------|------|
| **风险** | 同一方块的不同状态需要不同面纹理（如熔炉正面朝向不同），但 `BlockDef` 中纹理是静态的 |
| **严重度** | 高 — 无此机制，所有状态渲染相同纹理，视觉错误 |
| **应对** | 状态爆炸时为每个 StateID 预计算纹理索引，缓存到 `s_stateTextures`，详见 §3.6 |
| **关键约束** | 有纹理变体的 Cube 方块不可走贪心合并，需退回逐面渲染路径 |

### 11.3 状态转换 API（已解决）

| 维度 | 说明 |
|------|------|
| **风险** | 开门/关门、含水/脱水等原地方块状态切换缺少高效 API，若每次都要查哈希表或重建属性 vector，开销过大 |
| **严重度** | 中 — 功能缺失，非崩溃级 |
| **应对** | 实现 `withProperty()` / `toggleProperty()` API，利用排列组合的 stride 规律做纯算术计算，O(1) 无哈希查找，详见 §3.7 |

### 11.4 含水方块（Waterlogged）预警

| 维度 | 说明 |
|------|------|
| **风险** | 如果未来加入 `waterlogged: [true, false]` 属性，几乎所有非完整方块（楼梯、半砖、栅栏、门等）的状态数直接翻倍。10 种含水方块 × 平均 8 状态 = +80 个 StateID，但每个方块的排列组合复杂度翻倍 |
| **严重度** | 低 — `uint16_t` (65535) 足够支撑，但需注意实现细节 |
| **应对** | 1. 状态爆炸算法需高效，预分配 `s_states` 和 `s_stateTextures` 容量，避免逐个 push_back 导致的多次 reallocation 和内存抖动；2. 监控 `BlockStateRegistry::getStateCount()`，设置告警阈值（如 >30000）；3. JSON 配置中含水属性统一命名为 `waterlogged`，确保属性名池中只有一个条目 |
| **内存估算** | Minecraft 1.21 约 12000 个状态，含水翻倍约 24000，`uint16_t` 上限 65535 仍有 2.7x 余量。`s_states` + `s_stateTextures` 约 24000 × (8+24)B ≈ 750KB，完全可接受 |

### 11.5 其他注意事项

| 风险 | 严重度 | 应对 |
|------|--------|------|
| StateID 空间爆炸 | 中 | uint16_t 上限 65535，监控单方块属性组合数，如 `facing(6) × half(2) × shape(3) = 36` 可接受，但需避免 `6×3×4×2=144` 级别的组合 |
| renderShapeTag 依赖初始化顺序 | 低 | `MeshBuilderRegistry::initBuiltinBuilders()` 必须在 JSON 解析前调用 |
| 存档兼容 | 低 | 当前无存档系统；未来引入时 StateID 不可直接序列化，应序列化为 `(blockId + properties)` |
| PropIndices 缺失属性 | 低 | 如果 JSON 中没有定义 `facing` 属性，`PropIndices::FACING` 将取到 0（默认值），可能导致错误的属性查找。需在 `init()` 中校验所有属性名是否存在 |

---

## 12. 实施计划

| 步骤 | 内容 | 涉及文件 | 预估工时 |
|------|------|---------|---------|
| **1** | Raycast 返回 RayHit | `World.h/cpp`, `BlockTargetComponent`, `BlockInteractionBridgeSystem.cpp`, `Player.cpp` | 0.5 天 |
| **2** | BlockStateRegistry（含 `withProperty` / `getStateTextures` / `s_stateTextures`） | 新增 `BlockStateRegistry.h/cpp`，修改 `Block.h/cpp` | 2-3 天 |
| **3** | Palette 弹性扩容（Indirect8 + Direct16） | `Palette.h/cpp`, `SubChunk.h/cpp` | 1 天 |
| **4** | PlacementStrategyRegistry + 内置策略 | 新增 `Placement.h/cpp` | 1 天 |
| **5** | MeshBuilderRegistry + 内置构建器 | 新增 `MeshBuilderRegistry.h/cpp`，修改 `ChunkMesher.cpp` | 1-2 天 |
| **6** | ECS 串联 | `BlockInteractionBridgeSystem.cpp` | 2 小时 |
| **7** | blocks.json 更新 + textureVariants 配置 | 火把/原木/熔炉加 properties + textureVariants | 1 小时 |
| **8** | 含水方块预留 | `PropIndices` 增加 `WATERLOGGED`；blocks.json 水相关方块加 `waterlogged` 属性 | 可延后 |
| **9** | 依附检查 | 可延后 | — |

**建议执行顺序**：1 → 2 → 3 → 4 → 6 → 5 → 7 → 8

- 步骤 2（BlockStateRegistry）是所有后续步骤的基础
- 步骤 3（Palette 扩容）应在状态爆炸实现后立即跟进，避免 256 上限问题
- 步骤 5（渲染策略）和步骤 6（ECS 串联）可并行，因为渲染策略不影响放置逻辑
- 步骤 8（含水方块）可延后至水物理系统实现时再统一处理
