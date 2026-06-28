# BlockState 语义层对象化与容量解耦改造规划

> 起因：红石线实现爬墙与天花板铺设时，`BlockStateRegistry::explodeAllStates()` 展开的状态数量突破 `uint16_t` 容量，已将 `StateID` 拓展到 `uint32_t`。但把"裸全局 ID 的容量"抬高并不是最终模型，本规划描述如何把 `StateID = uint32_t` 从语义层移除，让 ID 容量成为注册表与编码层的实现细节。

---

## 一、现状分析

### 1.1 核心类型链

| 类型 | 定义 | 位置 |
|------|------|------|
| `RuntimeId` | `using RuntimeId = uint32_t;` | `src/engine/registry/IdRegistry.h:9` |
| `RUNTIME_ID_NULL` | `= 0` | `src/engine/registry/IdRegistry.h:10` |
| `BlockID` | `using BlockID = RuntimeId;` | `src/world/block/Block.h:22` |
| `StateID` | `using StateID = uint32_t;` | `src/world/block/BlockStateRegistry.h:14` |

三者当前都是 `uint32_t` 的同义别名，可在数值上互换。`World::getBlockState()` 直接返回 `getBlock()` 的值，没有额外的状态查找层。

### 1.2 BlockStateRegistry 当前结构

`s_states` 是以 `StateID` 为下标的 `std::vector<BlockStateEntry>`，StateID 即该向量索引：

```cpp
// src/world/block/BlockStateRegistry.h:43-49
struct BlockStateEntry {
    StateID stateId = 0;
    BlockID blockId = 0;
    uint8_t propertyCount = 0;
    uint32_t propertiesOffset = 0;  // 在 s_statePropertiesPool 中的偏移
    uint32_t textureOffset = 0;     // 在 s_stateTextures 中的偏移
};
```

`explodeAllStates()`（`BlockStateRegistry.cpp:415-519`）启动时穷举所有 block property 笛卡尔积，分配递增的 `nextStateId`，构建 `BlockPropertyLayout` 记录 `propertyStride` 用于 `withProperty()` O(1) 跳转。**核心展开机制已经存在且正确**。

### 1.3 Palette 与 BitPackedArray 现状

Palette 内部用 `uint16_t` 作为 palette index：

```cpp
// src/world/block/Palette.h:12-26
uint16_t getOrCreateIndex(RuntimeId runtimeId);
RuntimeId getRuntimeId(uint16_t paletteIndex) const;
std::unordered_map<RuntimeId, uint16_t> m_idToIndex;
```

`bitsPerEntry()` 将位宽硬 clamp 到 `[1, 16]`：

```cpp
// src/world/block/Palette.cpp:38-45
uint8_t Palette::bitsPerEntry() const {
    if (m_indexToId.size() <= 1) return 1;
    const double bits = std::ceil(std::log2(static_cast<double>(m_indexToId.size())));
    return static_cast<uint8_t>(std::clamp(bits, 1.0, 16.0));  // 上限 16
}
```

SubChunk 读取时强转 `uint16_t`：

```cpp
// src/world/chunk/SubChunk.h:223-224
const uint16_t paletteIndex = static_cast<uint16_t>(m_blockData.getUnchecked(index));
return m_palette.getRuntimeIdUnchecked(paletteIndex);
```

`BitPackedArray`（`src/world/chunk/BitPackedArray.h`）本身用 `uint64_t` 字存储，`get()` 返回 `uint32_t`，`m_bitsPerEntry` 是 `uint8_t`，理论上最大支持 32 bit/entry。16-bit 上限完全来自 Palette 层的人为 clamp，不是 BitPackedArray 的限制。

### 1.4 网络层 StateID 编码

`BlockUpdateEntry` 固定写 32 位 state id：

```cpp
// src/net/Protocol.h:215-227
struct BlockUpdateEntry {
    int32_t x, y, z;
    uint32_t stateId = 0;          // 固定 32 位
    std::vector<uint8_t> packedLightPatch;
};
```

`PacketCodec.h` 编解码均为 `pushU32` / `readU32`：
- `encodeBlockUpdateBatch`（第 252-264 行）：`pushU32(buf, u.stateId);`
- `decodeBlockUpdateBatch`（第 694-711 行）：`out.updates[i].stateId = readU32(data, offset);`
- `ClientBlockAction.blockState`（`Protocol.h:134-142`）同样 `uint32_t`
- `encodeRleRuntimeId`（`PacketCodec.h:917-930`）：run length 用 `uint16_t`，值用 `pushU32`

### 1.5 Sentinel 0xFFFFFFFF (light-only update)

```cpp
// src/net/Protocol.h:28
constexpr uint32_t LIGHT_ONLY_BLOCK_UPDATE_STATE_ID = 0xFFFFFFFFu;
```

使用点：

| 文件 | 行号 | 用途 |
|------|------|------|
| `src/server/GameServer.cpp` | 54 | 别名 `kLightOnlyBlockUpdate` |
| `src/server/GameServer.cpp` | 2470 | 发送纯光照更新时写入哨兵 |
| `src/client/ClientWorld.cpp` | 10 | 客户端别名 |
| `src/client/ClientWorld.cpp` | 223 | `if (stateId != kLightOnlyBlockUpdate)` 判断 |

哨兵值占用了 state id 数值空间中的一个点，语义上不属于任何方块状态。

### 1.6 影响面

`StateID` 被 90+ 个源文件直接使用，覆盖：World、IWorldView、网络协议、服务器、客户端、ChunkSerializer、ChunkMesher、BlockMeshBuilder、RedstoneSystem、BlockInteractionDispatcher、Placement、PhysicsSystem、FluidState、ECS 系统（Hopper/MovingBlock/PressurePlate/RandomTick）、ItemUseDispatcher、BlockEntityRenderer、DropPhysics 等。

---

## 二、问题诊断

| # | 问题 | 危害 |
|---|------|------|
| P1 | `StateID` 是裸 `uint32_t` 别名，与 `BlockID`/`RuntimeId` 类型混淆，编译器无法区分 | 传错 ID 编译期无法发现，红石 facing 错配等 bug 易混入 |
| P2 | `Palette` palette index 是 `uint16_t`，`bitsPerEntry` clamp 到 16 | 状态数超过 65536 时 palette 溢出截断，subchunk 数据损坏 |
| P3 | `BlockUpdateEntry.stateId` 固定 32 位，网络协议结构绑死状态空间宽度 | 状态数增长直接放大每条更新字节数，无法用 VarUInt 收益 |
| P4 | `0xFFFFFFFF` sentinel 占用 state id 数值空间 | 与"state id 是 s_states 索引"的语义冲突，未来若 s_states 超过 0xFFFFFFFF 会碰撞 |
| P5 | `BlockStateEntry` 内部 `propertiesOffset`/`textureOffset` 用 `uint32_t` | 人为定义 32 位边界，与注册表 `std::vector` 自动增长语义不一致 |
| P6 | SubChunk 读取路径 `static_cast<uint16_t>(getUnchecked())` 强转 | 当 palette index 超过 65535 时静默截断 |

---

## 三、改造目标

1. **语义层对象化**：引入 `BlockStateId` opaque handle，外部代码不感知其位宽。
2. **注册表内部自动增长**：`s_states` 用 `std::vector`，内部偏移用 `size_t`，不再人为定义容量边界。
3. **存储层动态编码**：Palette index 改为 `uint32_t`，`bitsPerEntry` 由 palette size 自然决定（上限 32），由 `BitPackedArray` 容量决定。
4. **网络层 VarUInt / palette**：方块更新、ChunkData RLE 不固定写 32 位 state id，改用 VarUInt 或 chunk-local palette。
5. **去掉 magic sentinel**：`BlockUpdateKind { BlockState, LightOnly }` 取代 `0xFFFFFFFF` 哨兵。

---

## 四、分阶段实施计划

### 阶段 0：前置准备（无行为变更）

**目标**：建立 opaque 类型骨架，不改变任何运行时行为，可独立编译通过。

#### 0.1 引入 BlockStateId opaque 类型

在 `src/world/block/BlockStateRegistry.h` 顶部，将 `using StateID = uint32_t;` 替换为强类型 handle：

```cpp
// Opaque handle into the global BlockState table.
// External code must not assume its bit width; the registry owns the
// index space and may grow it without notice.
struct BlockStateId {
    uint32_t value = 0;
    constexpr BlockStateId() = default;
    constexpr explicit BlockStateId(uint32_t v) : value(v) {}
    constexpr bool operator==(const BlockStateId& o) const { return value == o.value; }
    constexpr bool operator!=(const BlockStateId& o) const { return value != o.value; }
    constexpr bool operator<(const BlockStateId& o) const { return value < o.value; }
};

// Legacy alias retained during migration; new code must use BlockStateId.
using StateID = BlockStateId;
```

**要点**：
- `StateID` 作为别名保留，避免一次性改动 90+ 文件。
- `BlockStateId` 是值类型，零运行时开销，可放入 `std::unordered_map`（需补 `hash` 特化）。
- 提供 `explicit` 构造，防止 `uint32_t` 隐式转换。

#### 0.2 提供 hash 与常用工具

```cpp
namespace std {
template <>
struct hash<BlockStateId> {
    size_t operator()(const BlockStateId& s) const noexcept {
        return hash<uint32_t>{}(s.value);
    }
};
}

constexpr BlockStateId NULL_BLOCK_STATE{0};
```

#### 0.3 引入 BlockStateKind 取代 sentinel

在 `src/net/Protocol.h` 增加：

```cpp
enum class BlockUpdateKind : uint8_t {
    BlockState = 0,  // entry carries a real block state
    LightOnly  = 1,  // entry only carries light data; stateId field is ignored
};
```

`BlockUpdateEntry` 增加 `kind` 字段（暂保留 `stateId` 兼容）：

```cpp
struct BlockUpdateEntry {
    int32_t x = 0, y = 0, z = 0;
    BlockUpdateKind kind = BlockUpdateKind::BlockState;
    uint32_t stateId = 0;  // 仅在 kind == BlockState 时有效
    std::vector<uint8_t> packedLightPatch;
};
```

**验收**：编译通过，所有现有逻辑行为不变；`0xFFFFFFFF` 仍由服务器写入，客户端仍按旧逻辑识别。

---

### 阶段 1：注册表内部 size_t 化

**目标**：`BlockStateRegistry` 内部所有偏移、计数改用 `size_t`，对外 API 接受/返回 `BlockStateId`。

#### 1.1 BlockStateEntry 字段类型调整

`src/world/block/BlockStateRegistry.h:43-49`：

```cpp
struct BlockStateEntry {
    BlockStateId stateId{};
    BlockID blockId = 0;
    uint8_t propertyCount = 0;
    size_t propertiesOffset = 0;  // 在 s_statePropertiesPool 中的偏移
    size_t textureOffset = 0;     // 在 s_stateTextures 中的偏移
};
```

#### 1.2 BlockPropertyLayout 字段类型调整

`src/world/block/BlockStateRegistry.h:92-99`：

```cpp
struct BlockPropertyLayout {
    BlockStateId firstStateId{};
    uint8_t propertyCount = 0;
    std::vector<uint8_t> propertyPosition;
    std::vector<size_t> propertyStride;   // 由 uint32_t 改为 size_t
    std::vector<uint16_t> valueCounts;
    std::vector<std::vector<uint16_t>> valueOrdinals;
};
```

#### 1.3 explodeAllStates 内部用 size_t 累加

`src/world/block/BlockStateRegistry.cpp:415-519`：将 `StateID nextStateId = static_cast<StateID>(blockCount);` 改为 `size_t nextStateId = blockCount;`，写入 `BlockStateEntry` 时构造 `BlockStateId{static_cast<uint32_t>(nextStateId)}`。

> 说明：`BlockStateId.value` 暂仍为 `uint32_t`，因为当前状态总数远未触及 32 位边界。容量解耦的目标是"未来可无痛扩到 64 位"，而非"立即扩到 64 位"。`size_t` 用于注册表内部累加，对外 handle 仍是 32 位，留出未来一次性把 `value` 改成 `uint64_t` 的余地。

#### 1.4 对外 API 签名迁移

将 `BlockStateRegistry` 公开 API 中所有 `StateID` 参数/返回值改为 `BlockStateId`：

```cpp
static BlockStateId getDefaultState(BlockID blockId);
static BlockStateId getState(BlockID blockId, uint16_t propKey, uint16_t propValue);
static BlockStateId getState(BlockID blockId, std::initializer_list<std::pair<uint16_t, uint16_t>> props);
static BlockStateId getState(BlockID blockId, const std::vector<std::pair<uint16_t, uint16_t>>& props);
static BlockID getBlockId(BlockStateId stateId);
static uint16_t getPropertyIndex(BlockStateId stateId, uint16_t nameIndex);
static uint8_t getPropertyCount(BlockStateId stateId);
static BlockStateId withProperty(BlockStateId currentState, uint16_t propKey, uint16_t newValue);
static BlockStateId withProperty(BlockStateId currentState, uint16_t propKey, const std::string& newValue);
static const StateTextureIndices& getStateTextures(BlockStateId stateId);
static const ModelVariant* getModelVariant(BlockStateId stateId);
static std::string stateToString(BlockStateId stateId);
static std::vector<BlockStateId> getStatesForBlock(BlockID blockId);
```

由于 `StateID` 是 `BlockStateId` 的别名，旧代码无需改动即可编译；新代码应直接使用 `BlockStateId`。

**验收**：注册表单测通过，`explodeAllStates` 后状态总数与改造前一致；`withProperty` 跳转结果一致。

---

### 阶段 2：Palette 容量解耦

**目标**：palette index 从 `uint16_t` 升级到 `uint32_t`，`bitsPerEntry` 上限从 16 提升到 32。

#### 2.1 Palette API 类型升级

`src/world/block/Palette.h`：

```cpp
class Palette {
public:
    uint32_t getOrCreateIndex(RuntimeId runtimeId);
    [[nodiscard]] RuntimeId getRuntimeId(uint32_t paletteIndex) const;
    [[nodiscard]] RuntimeId getRuntimeIdUnchecked(uint32_t paletteIndex) const {
        return m_indexToId[paletteIndex];
    }
    [[nodiscard]] uint8_t bitsPerEntry() const;
    [[nodiscard]] size_t dynamicMemoryBytes() const;
    std::vector<uint32_t> compact(const std::vector<RuntimeId>& usedIds);
    void clear();
private:
    std::vector<RuntimeId> m_indexToId;
    std::unordered_map<RuntimeId, uint32_t> m_idToIndex;
};
```

#### 2.2 bitsPerEntry 上限提升

`src/world/block/Palette.cpp:38-45`：

```cpp
uint8_t Palette::bitsPerEntry() const {
    if (m_indexToId.size() <= 1) return 1;
    const double bits = std::ceil(std::log2(static_cast<double>(m_indexToId.size())));
    // BitPackedArray supports up to 32 bits per entry (uint32_t storage).
    return static_cast<uint8_t>(std::clamp(bits, 1.0, 32.0));
}
```

#### 2.3 compact() 无效标记升级

`src/world/block/Palette.cpp:52-81`：`oldToNew` 类型改为 `std::vector<uint32_t>`，无效标记从 `UINT16_MAX` 改为 `UINT32_MAX`；`newIndex` 类型改为 `uint32_t`。

#### 2.4 SubChunk 读取路径去除强转

`src/world/chunk/SubChunk.h:223-224`、`239-240`：

```cpp
const uint32_t paletteIndex = m_blockData.getUnchecked(index);
return m_palette.getRuntimeIdUnchecked(paletteIndex);
```

移除 `static_cast<uint16_t>`。`SubChunk.cpp` 中所有 `static_cast<uint16_t>(m_blockData.getUnchecked(...))` 同步处理。

#### 2.5 BitPackedArray 边界确认

`BitPackedArray` 当前 `get()` 返回 `uint32_t`，`set()` 接受 `uint32_t`，`m_bitsPerEntry` 为 `uint8_t`。32 bit/entry 已在其支持范围内，无需改动。需补充一处断言：

```cpp
// src/world/chunk/BitPackedArray.cpp 构造函数与 resize()
if (m_bitsPerEntry > 32) {
    // ... 报错：超过 uint32_t 存储上限
}
```

#### 2.6 m_blockCounts / m_fluidCounts 类型

`src/world/chunk/SubChunk.h:292,296`：

```cpp
std::unordered_map<BlockID, uint32_t> m_blockCounts;
std::unordered_map<BlockID, uint32_t> m_fluidCounts;
```

由 `uint16_t` 改为 `uint32_t`，避免单 subchunk 内同方块计数溢出（虽与状态爆炸无直接关系，但顺势清理）。

**验收**：`chunk_save_serializer_test` 通过；构造一个 palette size > 65536 的 subchunk，写入读取一致。

---

### 阶段 3：网络层 sentinel 替换

**目标**：用 `BlockUpdateKind` 取代 `0xFFFFFFFF` 哨兵，清理 Protocol 常量。

#### 3.1 服务器侧

`src/server/GameServer.cpp`：
- 移除第 54 行 `constexpr uint32_t kLightOnlyBlockUpdate = ...;`
- 第 2470 行 `entry.stateId = kLightOnlyBlockUpdate;` 改为 `entry.kind = net::BlockUpdateKind::LightOnly;`
- 发送真正方块更新时显式设置 `entry.kind = net::BlockUpdateKind::BlockState;`

#### 3.2 客户端侧

`src/client/ClientWorld.cpp`：
- 移除第 10 行 `kLightOnlyBlockUpdate` 别名
- 第 223 行 `if (stateId != kLightOnlyBlockUpdate)` 改为 `if (entry.kind == net::BlockUpdateKind::BlockState)`

#### 3.3 协议常量清理

`src/net/Protocol.h:28` 删除 `LIGHT_ONLY_BLOCK_UPDATE_STATE_ID`。注释 `// LIGHT_ONLY_BLOCK_UPDATE_STATE_ID means...`（第 220 行）改为 `// kind == LightOnly means this entry only carries light data`。

#### 3.4 PacketCodec 编解码调整

`src/net/PacketCodec.h`：
- `encodeBlockUpdateBatch`：在 `pushU32(buf, u.stateId)` 前增加 `pushU8(buf, static_cast<uint8_t>(u.kind));`
- `decodeBlockUpdateBatch`：对应位置 `readU8` 还原 `kind`
- 长度校验从 `offset + 20` 调整为 `offset + 21`（多 1 字节 kind）

> 说明：增加 1 字节 kind 是可接受的开销；未来阶段 4 引入 VarUInt 后，kind 与 stateId 可合并编码进一步压缩。

**验收**：服务器发送纯光照更新，客户端正确识别不修改方块；服务器发送方块更新，客户端正确应用。

---

### 阶段 4：网络层 VarUInt 编码

**目标**：方块更新与 ChunkData RLE 改用 VarUInt 编码 state id，状态数量增长只影响编码长度，不影响协议结构。

#### 4.1 引入 VarUInt 工具

在 `src/net/PacketCodec.h` 增加内部工具（或独立 `VarInt.h`）：

```cpp
// Encode an unsigned 32-bit integer as 1..5 bytes (LEB128).
inline void pushVarU32(std::vector<uint8_t>& buf, uint32_t v) {
    while (v >= 0x80u) {
        buf.push_back(static_cast<uint8_t>(v | 0x80u));
        v >>= 7u;
    }
    buf.push_back(static_cast<uint8_t>(v));
}

inline uint32_t readVarU32(const uint8_t* data, size_t size, size_t& offset) {
    uint32_t result = 0;
    int shift = 0;
    while (offset < size) {
        const uint8_t byte = data[offset++];
        result |= static_cast<uint32_t>(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0) break;
        shift += 7;
        if (shift >= 32) {
            // malformed: shift overflow
            return 0;
        }
    }
    return result;
}
```

#### 4.2 BlockUpdateEntry 改用 VarUInt

`encodeBlockUpdateBatch` 中：
- `pushU32(buf, u.stateId)` → `pushVarU32(buf, u.stateId)`
- `pushU32(buf, count)` → `pushVarU32(buf, count)`（updates 数量也 VarUInt）
- `pushU32(buf, lightCount)` → `pushVarU32(buf, lightCount)`

解码侧对称调整。长度校验改为逐字段流式校验（VarUInt 无法预知字节数）。

#### 4.3 ClientBlockAction 改用 VarUInt

`Protocol.h:134-142` 的 `ClientBlockAction.blockState` 同样改用 VarUInt 编解码。

#### 4.4 ChunkData RLE 改用 VarUInt

`PacketCodec.h:917-930` `encodeRleRuntimeId`：
- `pushU16(buf, run)` → `pushVarU32(buf, run)`（run length 也用 VarUInt，移除 65535 上限）
- `pushU32(buf, value)` → `pushVarU32(buf, value)`

解码侧对称调整。

**验收**：协议互通测试；对比改造前后典型场景字节数（小 state id 场景应明显变小）。

---

### 阶段 5：ChunkSerializer 存档格式刷新

**目标**：存档格式同步支持大状态空间。当前处于开发阶段，所有存档可 dump，不保留旧格式兼容路径。

#### 5.1 Palette 序列化

Palette 持久化部分：
- palette size 用 VarUInt
- 每个 `RuntimeId` 用 VarUInt
- `BitPackedArray` 的 `bitsPerEntry` 用 `uint8_t`，raw words 直接拷贝

#### 5.2 旧存档处理

开发阶段不保留旧存档读取路径，旧存档直接废弃。`ChunkSerializer` 中原有 `uint16_t` palette index、16-bit clamp 相关读写代码直接删除，替换为新格式，不维护版本号分支。

**验收**：新存档写入再读取一致；旧存档文件不再被加载。

---

### 阶段 6：StateID 别名清理

**目标**：移除 `using StateID = BlockStateId;` 别名，全代码库统一使用 `BlockStateId`。

#### 6.1 全局替换

脚本化替换所有 `StateID` → `BlockStateId`（90+ 文件）。需注意：
- `BlockStateRegistry.h:14` 别名定义删除
- 头文件包含关系：使用 `BlockStateId` 的文件需 include `BlockStateRegistry.h` 或独立的 `BlockStateId.h`

#### 6.2 抽取独立头文件（可选）

若 `BlockStateId` 被广泛使用，可抽取到 `src/world/block/BlockStateId.h`，`BlockStateRegistry.h` include 它，减少头文件耦合。

#### 6.3 显式构造清理

由于 `BlockStateId` 构造函数是 `explicit`，`uint32_t` 字面量无法隐式构造。需排查并显式包裹：
- `BlockStateId{0}` 替代 `0`
- `BlockStateId{someU32}` 替代 `someU32`

#### 6.4 BlockID 同步考虑（可选）

`BlockID = RuntimeId = uint32_t` 同样是裸类型。若需彻底解耦，可同步引入 `BlockId` opaque handle。本规划暂不强制，因 BlockID 空间远小于 StateID 空间，未触及容量边界。

**验收**：全量编译通过；无 `StateID` 残留；类型混淆导致的历史 bug 通过编译期检查暴露。

---

## 五、文件改动清单

| 阶段 | 文件 | 改动类型 |
|------|------|----------|
| 0 | `src/world/block/BlockStateRegistry.h` | 新增 `BlockStateId` 类型、hash 特化、`NULL_BLOCK_STATE` |
| 0 | `src/net/Protocol.h` | 新增 `BlockUpdateKind`，`BlockUpdateEntry` 增加 `kind` 字段 |
| 1 | `src/world/block/BlockStateRegistry.h` | `BlockStateEntry`/`BlockPropertyLayout` 字段 `size_t` 化，API 签名 `BlockStateId` 化 |
| 1 | `src/world/block/BlockStateRegistry.cpp` | `explodeAllStates` 内部 `size_t` 累加，`withProperty` 适配 |
| 2 | `src/world/block/Palette.h` | palette index `uint16_t` → `uint32_t` |
| 2 | `src/world/block/Palette.cpp` | `bitsPerEntry` clamp 32，`compact` 类型升级 |
| 2 | `src/world/chunk/BitPackedArray.cpp` | 增加 >32 bit 断言 |
| 2 | `src/world/chunk/SubChunk.h` | 去除 `static_cast<uint16_t>`，`m_blockCounts`/`m_fluidCounts` 升级 |
| 2 | `src/world/chunk/SubChunk.cpp` | 同上 |
| 3 | `src/server/GameServer.cpp` | 移除 `kLightOnlyBlockUpdate`，改用 `BlockUpdateKind` |
| 3 | `src/client/ClientWorld.cpp` | 移除 `kLightOnlyBlockUpdate`，改用 `BlockUpdateKind` |
| 3 | `src/net/Protocol.h` | 删除 `LIGHT_ONLY_BLOCK_UPDATE_STATE_ID` |
| 3 | `src/net/PacketCodec.h` | `encodeBlockUpdateBatch`/`decodeBlockUpdateBatch` 增加 kind 字段 |
| 4 | `src/net/PacketCodec.h` | 新增 `pushVarU32`/`readVarU32`，BlockUpdate/RLE 改用 VarUInt |
| 5 | `src/save/ChunkSerializer.cpp` | Palette/BitPackedArray 序列化改用 VarUInt，删除旧 `uint16_t` 读写路径 |
| 6 | 90+ 文件 | `StateID` → `BlockStateId` 全局替换 |

---

## 六、依赖与顺序

```
阶段 0 (类型骨架) ── 阶段 1 (注册表 size_t) ── 阶段 2 (Palette 解耦)
                                                        │
                                                        ├─ 阶段 3 (sentinel 替换)
                                                        │
                                                        ├─ 阶段 4 (VarUInt 编码)
                                                        │
                                                        └─ 阶段 5 (存档刷新)
                                                                  │
                                                                  └─ 阶段 6 (别名清理)
```

- 阶段 0、1 可合并为一个 PR（纯类型重构，无行为变更）。
- 阶段 2 可独立 PR（Palette 容量解耦）。
- 阶段 3、4 可合并（网络层一次性改造，避免协议版本号多次 bump）。
- 阶段 5 必须在 4 之后（存档复用 VarUInt 工具），直接替换旧格式不做兼容。
- 阶段 6 是收尾，所有功能落地后再做别名清理，避免迁移期代码混乱。

---

## 七、风险与缓解

| 风险 | 缓解措施 |
|------|----------|
| `BlockStateId` 显式构造导致大量字面量包裹 | 阶段 0 提供字面量运算符 `""_bsid` 或常量 `NULL_BLOCK_STATE`；阶段 6 前用 `StateID` 别名过渡 |
| Palette index 升级 `uint32_t` 后内存增长 | `m_idToIndex` 单条目从 6 字节（4+2）变 8 字节（4+4），subchunk 内 palette 条目数通常 < 1000，影响可忽略 |
| VarUInt 解码流式校验复杂 | 在 `readVarU32` 内置 shift 溢出检查；保留旧 `pushU32` 函数供非状态字段使用 |
| 存档格式刷新 | 开发阶段不兼容旧存档，直接替换为新格式，避免维护双路径 |
| 网络协议版本不匹配 | 阶段 3、4 合并后，客户端/服务器版本号同步 bump；旧客户端连接新服务器时握手阶段拒绝 |
| 红石系统状态爆炸在改造前已发生 | 阶段 0-2 优先合入，解除 65536 硬上限；阶段 3-5 可在红石功能稳定后再做 |

---

## 八、测试策略

### 8.1 单元测试

- `BlockStateRegistry` 测试：`explodeAllStates` 后状态总数、`withProperty` 跳转正确性、`getState` 笛卡尔积覆盖。
- `Palette` 测试：构造 palette size > 65536，`getOrCreateIndex`/`getRuntimeId` 往返一致；`bitsPerEntry` 返回正确位宽（17、24、32）。
- `BitPackedArray` 测试：32 bit/entry 写入读取；跨 word 边界正确性。
- `PacketCodec` 测试：VarUInt 编解码往返；`BlockUpdateKind` 字段往返；RLE 大 run length（> 65535）往返。

### 8.2 集成测试

- 服务器发送纯光照更新 → 客户端不修改方块（阶段 3 验收）。
- 服务器发送方块更新 → 客户端正确应用（阶段 3 验收）。
- ChunkData 大状态空间加载 → 客户端渲染正确（阶段 2、5 验收）。
- 新存档写入再读取 → 方块状态正确还原（阶段 5 验收）。

### 8.3 红石专项测试

红石线爬墙与天花板铺设是本次改造的触发场景。阶段 2 完成后，需验证：
- 红石线所有 facing × color × 微方块状态组合展开后 `StateID` 不溢出。
- 放置/破坏红石线后，`withProperty` 跳转正确，不产生状态错乱。
- 多色红石线交叉、爬墙、天花板混合场景下，chunk 序列化/反序列化一致。

---

## 九、与红石系统升级的关系

当前 `docs/红石系统升级-多色爬墙与微方块线缆开发指导.md` 中描述的多色爬墙与微方块线缆，是状态爆炸的直接来源。本改造是红石升级的前置依赖：

1. **阶段 0-2 必须先于红石多色爬墙合入**，否则 palette 16-bit 上限会在运行时截断 palette index，导致 subchunk 数据静默损坏。
2. **阶段 3-5 可与红石功能并行**，红石功能在 32 位 state id 空间内已可工作，网络/存档优化是锦上添花。
3. **阶段 6 在红石功能稳定后做**，避免迁移期与功能开发冲突。

---

## 十、验收里程碑

| 里程碑 | 内容 | 验收标准 |
|--------|------|----------|
| M1 | 阶段 0+1 合入 | `BlockStateId` 类型存在，注册表内部 `size_t`，编译通过，单测通过 |
| M2 | 阶段 2 合入 | Palette index `uint32_t`，`bitsPerEntry` 上限 32，palette > 65536 测试通过 |
| M3 | 阶段 3+4 合入 | `BlockUpdateKind` 取代 sentinel，VarUInt 编码生效，协议互通测试通过 |
| M4 | 阶段 5 合入 | 存档格式刷新为 VarUInt，新存档写入读取一致，旧存档不再加载 |
| M5 | 阶段 6 合入 | 全代码库无 `StateID` 残留，`BlockStateId` 显式构造，编译期类型检查生效 |
| M6 | 红石多色爬墙上线 | 状态总数 > 65536，运行稳定，无 palette 截断 |

---

## 附录 A：关键代码位置速查

| 内容 | 文件 | 行号 |
|------|------|------|
| `StateID` 定义 | `src/world/block/BlockStateRegistry.h` | 14 |
| `BlockStateEntry` | `src/world/block/BlockStateRegistry.h` | 43-49 |
| `BlockPropertyLayout` | `src/world/block/BlockStateRegistry.h` | 92-99 |
| `s_states` 声明 | `src/world/block/BlockStateRegistry.h` | 108 |
| `explodeAllStates` | `src/world/block/BlockStateRegistry.cpp` | 415-519 |
| `LIGHT_ONLY_BLOCK_UPDATE_STATE_ID` | `src/net/Protocol.h` | 28 |
| `BlockUpdateEntry` | `src/net/Protocol.h` | 215-227 |
| `ClientBlockAction` | `src/net/Protocol.h` | 134-142 |
| `encodeBlockUpdateBatch` | `src/net/PacketCodec.h` | 252-264 |
| `decodeBlockUpdateBatch` | `src/net/PacketCodec.h` | 694-711 |
| `encodeRleRuntimeId` | `src/net/PacketCodec.h` | 917-930 |
| Palette index `uint16_t` | `src/world/block/Palette.h` | 12-26 |
| `bitsPerEntry` clamp 16 | `src/world/block/Palette.cpp` | 38-45 |
| SubChunk 强转 `uint16_t` | `src/world/chunk/SubChunk.h` | 223, 239 |
| `m_blockCounts`/`m_fluidCounts` | `src/world/chunk/SubChunk.h` | 292, 296 |
| 服务器 sentinel 写入 | `src/server/GameServer.cpp` | 54, 2470 |
| 客户端 sentinel 判断 | `src/client/ClientWorld.cpp` | 10, 223 |

---

## 附录 B：BlockStateId 设计要点

1. **值类型，非指针**：`BlockStateId` 是 4 字节值类型，零运行时开销，可放入 vector、unordered_map。
2. **explicit 构造**：禁止 `uint32_t` 隐式转换，防止 BlockID 误传为 StateID。
3. **不暴露位宽**：`value` 字段虽为 `uint32_t`，但外部代码不应直接读写；未来可改为 `uint64_t` 而不破坏 API。
4. **NULL 语义**：`NULL_BLOCK_STATE{0}` 对应 `RUNTIME_ID_NULL = 0`，即 air。
5. **hash 兼容**：提供 `std::hash` 特化，支持 `unordered_map<BlockStateId, T>`。
6. **比较运算符**：提供 `==`/`!=`/`<`，支持排序与容器 key。

---

本规划完成落地后，BlockState 容量将从"人为定义的 16/32 位边界"变为"注册表与编码层的实现细节"，红石系统及未来任何需要状态爆炸的方块系统都不再受容量约束。
