# Namespaced ID 与调色板系统开发文档

> 目标：将现有的数字 `BlockID`/`ItemID` 替换为 Namespaced ID（命名 ID），并在 Chunk 存储层引入调色板（Palette）+ 位打包（Bit Packing）机制。

---

## 1. 背景与动机

### 1.1 现状

| 项目 | 当前实现 |
|------|---------|
| `BlockID` | `uint8_t`，范围 0-254，已用 ~30 种 |
| `ItemID` | `uint16_t`，范围 0-4095，已用 3 种 |
| 方块常量 | `BlockType::DIRT = 1` 硬编码在 `Block.h` |
| 物品常量 | `ItemType::COAL = 256` 硬编码在 `Item.h` |
| 注册表 | `std::array<BlockDef, 255>` / `std::array<ItemDef, 4096>` 固定数组 |
| Chunk 存储 | `std::array<BlockID, 65536>`，每格 1 字节 |
| Block↔Item 映射 | 低段 ID 空间共享，`static_cast` 直接转换 |
| 存档 | **不存在**，纯程序化生成 |

### 1.2 核心问题

1. **硬上限**：`uint8_t` 最多 256 种方块，`uint16_t` 最多 65536 种物品，无法支持 Mod 扩展
2. **ID 冲突**：数字 ID 全局分配，Mod 之间必然碰撞
3. **可读性差**：调试时看到 `3` 而非 `minecraft:stone`
4. **存储浪费**：一个只有 3 种方块的区块，每格仍占 8 bit
5. **存档兼容**：一旦引入存档，新增方块会导致 ID 偏移，旧存档损坏

### 1.3 为什么现在做

**项目当前没有存档系统**，这是迁移的最佳窗口期。一旦有了存档，迁移还需处理数据兼容，成本成倍增加。

---

## 2. 总体架构

```
┌─────────────────────────────────────────────────────┐
│                   配置 / 序列化层                      │
│         minecraft:stone, mecraft:ruby_ore            │
│              （Namespaced ID 字符串）                  │
└──────────────────────┬──────────────────────────────┘
                       │ IdRegistry（全局映射）
                       ▼
┌─────────────────────────────────────────────────────┐
│                    运行时逻辑层                        │
│              RuntimeId（紧凑 uint16_t 整数）           │
│     BlockDef/ItemDef 查找、逻辑判断、API 接口          │
└──────────────────────┬──────────────────────────────┘
                       │ Palette（区块级局部映射）
                       ▼
┌─────────────────────────────────────────────────────┐
│                   Chunk 存储层                        │
│          PaletteIndex + Bit Packing（2~N bit/格）     │
│     位宽 = ceil(log2(palette.size()))                │
└─────────────────────────────────────────────────────┘
```

**三层 ID 体系**：

| 层级 | 类型 | 示例 | 用途 |
|------|------|------|------|
| Namespaced ID | `std::string_view` 或 `const char*` | `minecraft:stone` | 配置文件、序列化、Mod API、调试 |
| Runtime ID | `uint16_t` | `3` | 运行时 O(1) 查找、逻辑判断、跨系统通信 |
| Palette Index | `uint8_t` 变长位宽 | `0b01` (2 bit) | Chunk 内部存储，极度紧凑 |

---

## 3. 核心组件设计

### 3.1 NamespacedId

轻量级不可变标识符，格式为 `namespace:path`。

```cpp
// src/core/NamespacedId.h
#pragma once
#include <string>
#include <string_view>
#include <functional>
#include <cstdint>

class NamespacedId {
public:
    // 构造：从 "namespace:path" 字符串解析
    explicit NamespacedId(std::string_view full);
    NamespacedId(std::string_view ns, std::string_view path);

    // 默认命名空间为 "minecraft"
    explicit NamespacedId(std::string_view path_only);

    [[nodiscard]] std::string_view namespaceStr() const { return m_ns; }
    [[nodiscard]] std::string_view path() const { return m_path; }
    [[nodiscard]] std::string full() const;
    [[nodiscard]] uint64_t hash() const { return m_hash; }

    bool operator==(const NamespacedId& other) const;
    bool operator!=(const NamespacedId& other) const;
    bool operator<(const NamespacedId& other) const;  // 用于有序容器

    // 预计算常见 ID（编译期常量）
    static const NamespacedId AIR;
    static const NamespacedId STONE;
    static const NamespacedId DIRT;
    // ...

private:
    std::string m_ns;     // 命名空间，如 "minecraft"
    std::string m_path;   // 路径，如 "stone"
    uint64_t m_hash;      // 预计算哈希，加速比较和查找

    static uint64_t computeHash(std::string_view ns, std::string_view path);
};

// 哈希支持，用于 unordered_map/unordered_set
namespace std {
template<> struct hash<NamespacedId> {
    size_t operator()(const NamespacedId& id) const noexcept {
        return static_cast<size_t>(id.hash());
    }
};
}
```

**设计要点**：
- 预计算哈希值，比较时先比哈希再比字符串，热路径性能接近整数
- 驻留池（String Interning）可选：注册后 `NamespacedId` 的字符串数据由注册表持有，`NamespacedId` 实例只存 `string_view` 指向注册表的字符串，避免拷贝
- 格式验证：`namespace` 只允许小写字母/数字/下划线，`path` 允许小写字母/数字/下划线/斜杠

### 3.2 IdRegistry — 统一 ID 注册中心

```cpp
// src/core/IdRegistry.h
#pragma once
#include "NamespacedId.h"
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <mutex>

// 运行时紧凑整数 ID
using RuntimeId = uint16_t;
constexpr RuntimeId RUNTIME_ID_NULL = 0;

class IdRegistry {
public:
    // 注册一个 Namespaced ID，返回分配的 RuntimeId
    // 如果已存在，返回已有的 RuntimeId
    RuntimeId registerId(const NamespacedId& namespacedId);

    // 查询
    [[nodiscard]] RuntimeId getRuntimeId(const NamespacedId& namespacedId) const;
    [[nodiscard]] const NamespacedId& getNamespacedId(RuntimeId runtimeId) const;
    [[nodiscard]] bool contains(const NamespacedId& namespacedId) const;
    [[nodiscard]] size_t size() const;

    // 预注册内置 ID（在 init 阶段调用，保证 RuntimeId 分配顺序稳定）
    void initBuiltinIds();

private:
    std::unordered_map<NamespacedId, RuntimeId> m_toRuntime;
    std::vector<NamespacedId> m_toNamespaced;  // index = RuntimeId
};
```

**设计要点**：
- `RuntimeId` 从 1 开始分配（0 = NULL/AIR），保证与现有 `BlockType::AIR = 0` 兼容
- 内置 ID 在 `initBuiltinIds()` 中按固定顺序注册，保证 `minecraft:air = 0`, `minecraft:dirt = 1` 等 RuntimeId 稳定
- Mod ID 在内置 ID 之后动态注册
- 线程安全：注册阶段加锁，运行阶段只读无需锁

**内置 ID 预注册顺序**（与现有 BlockType 常量一一对应）：

```
0  → minecraft:air          (原 BlockType::AIR)
1  → minecraft:dirt         (原 BlockType::DIRT)
2  → minecraft:grass_block  (原 BlockType::GRASS)
3  → minecraft:stone        (原 BlockType::STONE)
4  → minecraft:sand         (原 BlockType::SAND)
5  → minecraft:oak_log      (原 BlockType::WOOD)
...
28 → minecraft:torch        (原 BlockType::TORCH)
29 → minecraft:brown_mushroom
--- 物品（从 256 开始） ---
256 → minecraft:coal
257 → minecraft:iron_pickaxe
```

这样 `RuntimeId` 与旧数字 ID 在内置范围内完全等价，**过渡期可无缝兼容**。

### 3.3 Palette — 区块级调色板

```cpp
// src/world/Palette.h
#pragma once
#include "Block.h"  // BlockID → RuntimeId
#include <vector>
#include <cstdint>
#include <unordered_map>

class Palette {
public:
    // 获取 palette index，不存在则插入
    uint8_t getOrCreateIndex(RuntimeId runtimeId);

    // 根据 palette index 获取 RuntimeId
    [[nodiscard]] RuntimeId getRuntimeId(uint8_t paletteIndex) const;

    // 当前 palette 中的条目数
    [[nodiscard]] size_t size() const;

    // 存储 bit 宽度 = ceil(log2(max(size(), 2)))
    [[nodiscard]] uint8_t bitsPerEntry() const;

    // 重建/优化（删除未使用的条目，重新压缩）
    void optimize();

    // 序列化/反序列化（未来存档用）
    // void writeTo(NbtWriter& writer) const;
    // static Palette readFrom(NbtReader& reader);

private:
    // palette index → RuntimeId（紧凑）
    std::vector<RuntimeId> m_indexToId;

    // RuntimeId → palette index（快速反查）
    // 注：不能用数组，因为 RuntimeId 空间可能很大
    std::unordered_map<RuntimeId, uint8_t> m_idToIndex;
};
```

**核心原理**：

一个 16×256×16 的区块通常只包含 5~20 种方块。如果只有 4 种：
- 旧方案：每格 8 bit，总计 65536 字节
- Palette：4 种方块 → 2 bit/格，总计 65536 × 2 / 8 = 16384 字节，**节省 75%**

### 3.4 BitPackedArray — 位打包存储

```cpp
// src/world/BitPackedArray.h
#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>

class BitPackedArray {
public:
    BitPackedArray() = default;
    BitPackedArray(size_t count, uint8_t bitsPerEntry);

    // 读取第 index 个 entry
    [[nodiscard]] uint32_t get(size_t index) const;

    // 写入第 index 个 entry
    void set(size_t index, uint32_t value);

    // 更新位宽（Palette 扩展时调用）
    void resize(uint8_t newBitsPerEntry);

    // 填充
    void fill(uint32_t value);

    [[nodiscard]] size_t size() const { return m_count; }
    [[nodiscard]] uint8_t bitsPerEntry() const { return m_bitsPerEntry; }
    [[nodiscard]] size_t dataByteSize() const { return m_data.size() * sizeof(uint64_t); }

private:
    size_t m_count = 0;
    uint8_t m_bitsPerEntry = 0;
    std::vector<uint64_t> m_data;  // 64-bit 字为单位存储

    static constexpr int ENTRIES_PER_WORD = 64; // 最多64个entry在一个word中
};
```

**位打包原理**：

```
bitsPerEntry = 2, 每个uint64_t可存 32 个 entry

| entry0 | entry1 | entry2 | ... | entry31 |
|  2bit  |  2bit  |  2bit  | ... |  2bit   |
```

**关键性能点**：
- `get()` / `set()` 使用位运算，无分支，2~4 个 CPU 指令
- 位宽变化（Palette 扩展）时 `resize()` 重建数据

### 3.5 改造后的 Chunk

```cpp
// src/world/Chunk.h（改造后关键部分）
class Chunk {
public:
    static constexpr int SIZE_X = 16;
    static constexpr int SIZE_Y = 256;
    static constexpr int SIZE_Z = 16;
    static constexpr std::size_t BLOCK_COUNT =
        static_cast<std::size_t>(SIZE_X) * SIZE_Y * SIZE_Z;

    // --- 公共接口（不变） ---
    [[nodiscard]] BlockID getBlock(int x, int y, int z) const;  // 返回 RuntimeId
    void setBlock(int x, int y, int z, BlockID id);             // 接收 RuntimeId

private:
    // --- 存储（改造后） ---
    Palette m_palette;                         // 局部映射表
    BitPackedArray m_blockData;                // 位打包数据

    // 光照保持不变
    std::array<uint8_t, BLOCK_COUNT> m_lightMap{};

    std::array<int, SIZE_X * SIZE_Z> m_heightMap{};
    // ...
};
```

**`getBlock` / `setBlock` 实现变化**：

```cpp
BlockID Chunk::getBlock(int x, int y, int z) const {
    size_t idx = toIndex(x, y, z);
    uint8_t paletteIdx = static_cast<uint8_t>(m_blockData.get(idx));
    return m_palette.getRuntimeId(paletteIdx);
}

void Chunk::setBlock(int x, int y, int z, BlockID id) {
    size_t idx = toIndex(x, y, z);
    uint8_t paletteIdx = m_palette.getOrCreateIndex(id);
    m_blockData.set(idx, paletteIdx);

    // Palette 扩展时需要更新位宽
    if (m_palette.size() > (1u << m_blockData.bitsPerEntry())) {
        m_blockData.resize(m_palette.bitsPerEntry());
    }
}
```

---

## 4. 改造后的注册表

### 4.1 BlockRegistry

```cpp
// src/world/Block.h（改造后关键部分）
class BlockRegistry {
public:
    static void init(ResourceMgr* resourceMgr = nullptr);

    // 按 RuntimeId 查找（热路径）
    [[nodiscard]] static const BlockDef& get(BlockID runtimeId);
    [[nodiscard]] static bool exists(BlockID runtimeId);

    // 按 NamespacedId 查找
    [[nodiscard]] static BlockID getId(const NamespacedId& namespacedId);
    [[nodiscard]] static bool tryGetId(const NamespacedId& namespacedId, BlockID& outId);
    [[nodiscard]] static const NamespacedId& getNamespacedId(BlockID runtimeId);

    // 按短名查找（兼容旧配置文件）
    [[nodiscard]] static BlockID findByName(const std::string& name);

    // 注册新方块（Mod API）
    static BlockID registerBlock(const NamespacedId& id, BlockDef def);

    static void printAllBlocks();

private:
    static std::vector<BlockDef> s_blocks;                        // index = RuntimeId
    static std::vector<NamespacedId> s_namespacedIds;             // index = RuntimeId
    static std::unordered_map<NamespacedId, BlockID> s_idLookup;  // 快速反查
};
```

### 4.2 ItemRegistry

```cpp
// src/item/Item.h（改造后关键部分）
class ItemRegistry {
public:
    static void init();

    [[nodiscard]] static const ItemDef& get(ItemID runtimeId);
    [[nodiscard]] static ItemID getId(const NamespacedId& namespacedId);
    [[nodiscard]] static bool tryGetId(const NamespacedId& namespacedId, ItemID& outId);
    [[nodiscard]] static const NamespacedId& getNamespacedId(ItemID runtimeId);
    [[nodiscard]] static ItemID findByName(const std::string& name);

    // Block ↔ Item 映射（显式注册，不再靠 static_cast）
    static void registerBlockItem(const NamespacedId& blockId, const NamespacedId& itemId);
    [[nodiscard]] static ItemID fromBlock(BlockID blockRuntimeId);
    [[nodiscard]] static BlockID toPlaceBlock(ItemID itemRuntimeId);
    [[nodiscard]] static BlockID toRenderBlock(ItemID itemRuntimeId);

    // 注册新物品（Mod API）
    static ItemID registerItem(const NamespacedId& id, ItemDef def);

private:
    static std::vector<ItemDef> s_items;
    static std::vector<NamespacedId> s_namespacedIds;
    static std::unordered_map<NamespacedId, ItemID> s_idLookup;

    // 显式 Block↔Item 映射表（替代旧的 ID 空间共享策略）
    static std::unordered_map<BlockID, ItemID> s_blockToItem;
    static std::unordered_map<ItemID, BlockID> s_itemToPlaceBlock;
    static std::unordered_map<ItemID, BlockID> s_itemToRenderBlock;
};
```

**Block↔Item 映射策略变更**：

| | 旧方案 | 新方案 |
|---|--------|--------|
| 方块物品 | `ItemID = BlockID`（数值相等） | 显式注册 `registerBlockItem("minecraft:dirt", "minecraft:dirt")` |
| 纯物品 | `ItemID >= 256` | `RuntimeId` 由注册顺序决定，无需预留段 |
| 查询 | `static_cast<ItemID>(blockId)` | `s_blockToItem[blockRuntimeId]` |

---

## 5. 配置文件格式变更

### 5.1 blocks.json

```json
{
  "blocks": [
    {
      "id": "minecraft:air",
      "isSolid": false,
      "isTransparent": true,
      "isSelectable": false,
      "opacity": 0
    },
    {
      "id": "minecraft:dirt",
      "textures": { "all": "dirt" },
      "drop": "minecraft:dirt"
    },
    {
      "id": "minecraft:coal_ore",
      "textures": { "top": "stone", "side": "coal_ore" },
      "drop": "minecraft:coal"
    },
    {
      "id": "mymod:ruby_ore",
      "textures": { "top": "stone", "side": "ruby_ore" },
      "drop": "mymod:ruby"
    }
  ]
}
```

**变更点**：
- `"id": 1` → `"id": "minecraft:dirt"`（数字 → Namespaced ID）
- `"drop": "coal"` → `"drop": "minecraft:coal"`（统一 Namespaced ID 格式）
- `"name"` 字段废弃，由 `id` 的 path 部分代替
- `"icon"` / `"placeBlock"` / `"renderBlock"` 等引用全部改为 Namespaced ID

### 5.2 items.json

```json
{
  "items": [
    {
      "id": "minecraft:coal",
      "iconTexture": "coal",
      "maxStack": 64
    },
    {
      "id": "minecraft:iron_pickaxe",
      "iconTexture": "iron_pickaxe",
      "isTool": true,
      "maxDurability": 250
    },
    {
      "id": "minecraft:dirt",
      "placeBlock": "minecraft:dirt",
      "renderBlock": "minecraft:dirt"
    }
  ]
}
```

### 5.3 recipes.json

```json
{
  "recipes": [
    {
      "pattern": ["W"],
      "key": { "W": "minecraft:oak_log" },
      "result": "minecraft:oak_planks",
      "resultCount": 4
    }
  ]
}
```

---

## 6. 分阶段开发计划

### Phase 1：Namespaced ID 基础设施 + 注册表改造（5-8 天）

**目标**：引入 Namespaced ID，替换硬编码数字常量，但 **不改变 Chunk 存储方式**。

| 任务 | 文件 | 详情 |
|------|------|------|
| 1.1 新建 `NamespacedId` 类 | `src/core/NamespacedId.h/.cpp` | 类型定义、哈希、比较、格式验证 |
| 1.2 新建 `IdRegistry` | `src/core/IdRegistry.h/.cpp` | 全局映射、内置 ID 预注册 |
| 1.3 重写 `BlockRegistry` | `src/world/Block.h/.cpp` | `std::array` → `std::vector` + `unordered_map`；`findByName` → `getId(NamespacedId)` |
| 1.4 重写 `ItemRegistry` | `src/item/Item.h/.cpp` | 同上；新增 `registerBlockItem` 显式映射 |
| 1.5 更新 `BlockType` / `ItemType` | `src/world/Block.h`, `src/item/Item.h` | 常量改为 `inline const NamespacedId` 或运行时查询 |
| 1.6 更新 `BlockDef` / `ItemDef` | 同上 | `name: const char*` → `NamespacedId` |
| 1.7 更新 `ItemStack` | `src/item/Item.h` | `ItemID` 语义不变（仍为 RuntimeId），接口不变 |
| 1.8 更新 `BlockDropTable` | `src/item/Item.h/.cpp` | 数组 → `unordered_map<BlockID, BlockDropEntry>` |
| 1.9 迁移 `blocks.json` | `assets/config/blocks.json` | 数字 ID → Namespaced ID |
| 1.10 迁移 `items.json` | `assets/config/items.json` | 同上 |
| 1.11 迁移 `recipes.json` | `assets/config/recipes.json` | 物品引用 → Namespaced ID |
| 1.12 适配全系统 | ~15 个 `.cpp` 文件 | 编译通过，功能回归 |

**Phase 1 完成后的状态**：
- `BlockID` = `RuntimeId`（`uint16_t`，由 `IdRegistry` 分配）
- Chunk 存储**不变**（`std::array<BlockID, 65536>`，只是 `BlockID` 从 `uint8_t` → `uint16_t`）
- 所有配置文件使用 Namespaced ID
- Mod 可以调用 `registerBlock()` / `registerItem()` 动态注册

### Phase 2：Palette + 位打包存储（5-8 天）

**目标**：Chunk 内部存储改为 Palette + BitPackedArray，实现内存优化和存档基础。

| 任务 | 文件 | 详情 |
|------|------|------|
| 2.1 新建 `Palette` 类 | `src/world/Palette.h/.cpp` | 局部映射表实现 |
| 2.2 新建 `BitPackedArray` 类 | `src/world/BitPackedArray.h/.cpp` | 位打包数组实现及单元测试 |
| 2.3 改造 `Chunk` 存储 | `src/world/Chunk.h/.cpp` | `std::array<BlockID>` → `Palette + BitPackedArray` |
| 2.4 适配 `Chunk::getBlock/setBlock` | 同上 | 加一层 Palette 查找 |
| 2.5 适配 `ChunkMesher` 快照 | `src/renderer/ChunkMesher.h/.cpp` | 快照结构适配新存储 |
| 2.6 适配 `LightEngine` | `src/world/LightEngine.h/.cpp` | 验证 `getBlock` 接口未变，无修改或极小改动 |
| 2.7 适配 `TerrainGenerator` | `src/world/TerrainGenerator.h/.cpp` | `setBlock` 调用不变，验证 |
| 2.8 Palette 优化策略 | `src/world/Palette.h/.cpp` | 区块生成完毕后 `optimize()` 压缩 |
| 2.9 单元测试 | `tests/` | Palette、BitPackedArray、Chunk 存储正确性 |

**Phase 2 完成后的状态**：
- Chunk 内存占用从固定 64KB 降为 16KB~32KB（取决于方块种类数）
- 为未来存档序列化打下基础（Palette 可直接写出为 NBT 格式）
- 性能：`getBlock/setBlock` 多一次 Palette 查找，但位打包带来的缓存友好性可能抵消开销

### Phase 3（可选）：存档序列化

在 Phase 2 基础上，可轻松实现：

| 任务 | 详情 |
|------|------|
| 3.1 Chunk 序列化 | 写出 Palette + 位打包数据 |
| 3.2 区块加载 | 读取 Palette，重建 BitPackedArray |
| 3.3 版本头 | 存档文件头包含 Namespaced ID 注册表快照，保证前向兼容 |

---

## 7. 性能分析与对策

### 7.1 热路径性能

| 操作 | 旧方案 | 新方案（Phase 1） | 新方案（Phase 2） |
|------|--------|-------------------|-------------------|
| `Chunk::getBlock` | `m_blocks[idx]` | `m_blocks[idx]`（`uint16_t`） | `palette.getRuntimeId(bitPacked.get(idx))` |
| `Chunk::setBlock` | `m_blocks[idx] = id` | 同左 | `palette.getOrCreateIndex(id); bitPacked.set(idx, pIdx)` |
| `BlockRegistry::get` | `s_blocks[id]` | `s_blocks[id]`（vector） | 同左 |
| `BlockRegistry::findByName` | 线性遍历 O(N) | `s_idLookup[id]` O(1) | 同左 |

### 7.2 关键优化

1. **Palette 查找用 `unordered_map`**：O(1) 均摊，但哈希开销存在。对于 `size() < 16` 的常见 Palette，可改用**线性扫描 `vector`**，实际更快（缓存友好）。

2. **BitPackedArray `get/set` 内联**：热路径函数标记 `inline` 或在头文件实现，编译器可优化为 2~4 条位运算指令。

3. **Chunk 快照（ChunkMesher）**：可考虑在快照中保留 Palette，快照内的 `getBlock` 仍走 Palette 查找；或展开为 `uint16_t` 数组（64KB × 2 = 128KB，可接受）。

4. **Palette 预分配**：`TerrainGenerator` 在填充区块时可 `palette.getOrCreateIndex()` 预热 Palette，避免渲染时反复扩展位宽。

### 7.3 内存对比

假设一个典型区块有 8 种方块：

| | 旧方案 | Phase 1 | Phase 2 |
|---|--------|---------|---------|
| 方块数据 | 65536 × 1 = 64 KB | 65536 × 2 = 128 KB | 65536 × 3 bit ≈ 24 KB |
| Palette | 无 | 无 | 8 × 2B = 16 B |
| **合计** | **64 KB** | **128 KB** | **~24 KB** |

Phase 1 内存翻倍（`uint8_t` → `uint16_t`），Phase 2 大幅改善。如果 Phase 1 内存不可接受，可将 Phase 1 和 Phase 2 合并开发。

---

## 8. 兼容性策略

### 8.1 旧数字 ID 兼容（过渡期）

`IdRegistry::initBuiltinIds()` 按固定顺序注册，保证内置方块的 `RuntimeId` 与旧数字 ID 完全一致。过渡期间代码中的 `BlockType::DIRT` 可改为：

```cpp
namespace BlockIds {
    // 方式1：返回 RuntimeId 的函数
    inline BlockID DIRT() { return BlockRegistry::getId(NamespacedId("minecraft:dirt")); }

    // 方式2：初始化后缓存的常量（推荐）
    extern BlockID DIRT;
    void initBlockIds();  // 在 BlockRegistry::init() 之后调用
}
```

### 8.2 旧配置文件兼容

`BlockRegistry::findByName(const std::string& name)` 保留，内部逻辑：
1. 尝试作为 Namespaced ID 解析（包含 `:`）
2. 失败则添加默认命名空间 `"minecraft:"` 前缀重试
3. 兼容旧数字 ID（尝试 `stoi` 转换，仅限内置 ID 范围）

### 8.3 存档兼容（未来）

存档中存储 Palette + 位打包数据，包含完整的 Namespaced ID 映射。加载时：
- 已知 ID → 直接映射为当前 RuntimeId
- 未知 ID（Mod 已移除）→ 映射为 `minecraft:info_update`（占位方块），不崩溃

---

## 9. 测试策略

### 9.1 单元测试

| 测试对象 | 测试要点 |
|---------|---------|
| `NamespacedId` | 解析、哈希一致性、比较、格式验证、默认命名空间 |
| `IdRegistry` | 注册、查询、重复注册、内置 ID 顺序 |
| `Palette` | 创建索引、反查、位宽计算、optimize |
| `BitPackedArray` | get/set 正确性、位宽变化、边界值、fill |
| `BlockRegistry` | NamespacedId 查找、从 JSON 加载、registerBlock |
| `ItemRegistry` | 同上 + BlockItem 映射 |

### 9.2 集成测试

| 测试场景 | 验证内容 |
|---------|---------|
| 地形生成 | Chunk setBlock/getBlock 往返一致 |
| 网格构建 | ChunkMesher 快照正确，渲染无误 |
| 光照计算 | LightEngine 行为不变 |
| 物品栏 | 拾取/放置/合成物品流程正确 |
| 方块掉落 | DropSystem 掉落物 ID 正确 |
| 配置加载 | JSON 中使用 Namespaced ID 的配置正确加载 |

### 9.3 性能测试

| 指标 | 测量方法 |
|------|---------|
| `Chunk::getBlock` 吞吐 | 循环 65536 次 getBlock 计时 |
| `Chunk::setBlock` 吞吐 | 填充整个 Chunk 计时 |
| 网格构建耗时 | 对比改造前后 `ChunkMesher::buildMeshData` 耗时 |
| 内存占用 | 对比 Chunk 实例 sizeof 及分配量 |

---

## 10. 文件清单

### 新增文件

| 文件 | 描述 |
|------|------|
| `src/core/NamespacedId.h` | Namespaced ID 类型定义 |
| `src/core/NamespacedId.cpp` | 实现 |
| `src/core/IdRegistry.h` | 全局 ID 注册中心 |
| `src/core/IdRegistry.cpp` | 实现 |
| `src/world/Palette.h` | 区块调色板 |
| `src/world/Palette.cpp` | 实现 |
| `src/world/BitPackedArray.h` | 位打包数组 |
| `src/world/BitPackedArray.cpp` | 实现 |
| `tests/test_namespaced_id.cpp` | NamespacedId 单元测试 |
| `tests/test_palette.cpp` | Palette 单元测试 |
| `tests/test_bit_packed_array.cpp` | BitPackedArray 单元测试 |

### 修改文件

| 文件 | 改动级别 |
|------|---------|
| `src/world/Block.h` | ★★★★ 重写 BlockID/BlockType/BlockRegistry |
| `src/world/Block.cpp` | ★★★★ 重写注册逻辑 |
| `src/item/Item.h` | ★★★★ 重写 ItemID/ItemType/ItemRegistry/BlockDropTable |
| `src/item/Item.cpp` | ★★★★ 重写注册逻辑、BlockItem 映射 |
| `src/world/Chunk.h` | ★★★ Palette + BitPackedArray 替换 m_blocks |
| `src/world/Chunk.cpp` | ★★★ getBlock/setBlock 重写 |
| `src/renderer/ChunkMesher.h` | ★★ 快照结构适配 |
| `src/renderer/ChunkMesher.cpp` | ★★ 快照捕获逻辑适配 |
| `src/world/TerrainGenerator.h/.cpp` | ★ 常量引用改为 NamespacedId |
| `src/world/LightEngine.h/.cpp` | ★ 极小改动，接口兼容 |
| `src/world/DropSystem.h/.cpp` | ★ ItemID 引用适配 |
| `src/world/World.h/.cpp` | ★ getBlock/setBlock 兼容 |
| `src/player/Inventory.h/.cpp` | ★ 兼容层清理 |
| `src/crafting/CraftingSystem.h/.cpp` | ★ JSON 解析适配 NamespacedId |
| `src/core/Game.cpp` | ★ 初始化顺序调整 |
| `src/core/states/GameplayState.h` | ★ 交互代码适配 |
| `src/core/states/InventoryState.h` | ★ UI 代码适配 |
| `assets/config/blocks.json` | ★★ 格式迁移 |
| `assets/config/items.json` | ★★ 格式迁移 |
| `assets/config/recipes.json` | ★ 格式迁移 |
| `CMakeLists.txt` | ★ 新增源文件 |

---

## 11. 风险与缓解

| 风险 | 缓解措施 |
|------|---------|
| Phase 1 内存翻倍（uint8_t→uint16_t） | 若不可接受，合并 Phase 1+2 一起做 |
| Palette 查找拖慢 getBlock | 小 Palette（<16项）用线性扫描代替哈希表 |
| BitPackedArray 位宽变化时数据拷贝 | setBlock 时检查位宽，需要时才 resize |
| 全系统改动面大，易出 bug | Phase 1 保证接口不变，只改内部实现；每步编译验证 |
| 旧代码中 `BlockType::DIRT` 散落各处 | 全局搜索替换，统一改为 `BlockIds::DIRT` |

---

## 12. 里程碑

```
Phase 1:  [Week 1-2]  NamespacedId + IdRegistry + Registry 重写 + 配置迁移
                       ↓ 全系统编译通过 + 功能回归 ✓
Phase 2:  [Week 3-4]  Palette + BitPackedArray + Chunk 存储改造
                       ↓ 性能测试 + 内存对比 ✓
Phase 3:  [Future]    存档序列化（NBT 格式）
```
