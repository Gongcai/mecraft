# 方块交互面向数据化与运行时 ID 改造研究报告

> 研究范围：BlockID/ItemID 全运行时生成可行性、方向信息 SoA/AoS 优化、性能影响、初始化顺序、HUD 声明式 UI、方块状态切换、特殊操作、红石耦合点
> 编写日期：2026-06-26

## 当前实现进度（2026-06-26）

本报告最初以研究和方案评估为主。当前代码已经完成了多轮落地改造，进度如下：

1. **BlockID/ItemID 运行时化已完成**：生产代码和测试中已移除 `BuiltinIds`、`BlockIds::`、`ItemIds::` 依赖，方块和物品由 JSON 注册并在运行时分配 ID。
2. **物品对方块使用规则已数据化**：锄地、水桶等逻辑迁移到 `ItemUseDispatcher` 和配置规则，新增同类 use-on-block 行为不再依赖硬编码 ID。
3. **容器 UI 与容器行为已数据化**：`BlockDef.containerUi`、`ContainerUiRegistry`、`ContainerBehaviorRegistry`、`DataDrivenContainerPanelControl`、`DataDrivenContainerState` 已落地；箱子、木桶、熔炉、合成台已通过配置绑定界面和行为。
4. **木桶已从原版素材接入并验证**：`minecraft:barrel` 已加入方块、物品、模型、纹理、容器 UI 和容器行为配置。
5. **方块交互分发已数据化**：lever、button、repeater、comparator、door、trapdoor、fence_gate 等交互经 `BlockInteractionDispatcher` 和 `assets/config/block_interaction/` 配置驱动。
6. **客户端/服务器交互消费已改为服务端权威**：多人模式下客户端发送 `ClientBlockActionType::Interact` 后消费本地输入，但不直接改世界；服务器校验距离并执行交互，再广播结果，避免同一命中事件被客户端和服务器各消费一次导致随机行为。
7. **红石部分数据尾巴已清理**：按钮脉冲时长由 `redstonePulseTicks` 数据驱动；活塞不可推动规则由 `pistonPushReaction` 数据驱动；比较器读取容器信号由 `ContainerBehaviorDef.comparatorSignal` 数据驱动；压力板接受实体类型由 `pressurePlateEntityFilter` 数据驱动。
8. **通用 block-entity storage 已落地**：箱子、木桶等 `handler:"storage"` 容器共用 `BlockEntityInventoryStore`、`BlockEntityInventoryLifecycle` 和通用 storage 面板；服务器保存空容器时也按 `containerUi -> behavior` 数据判断 block entity 类型，已用 `minecraft:barrel` 保存/恢复测试验证。

仍建议继续推进的剩余项：

1. 活塞主体、活塞头、移动方块等结构性逻辑仍需要 C++，但可继续抽离可配置的规则字段。
2. 熔炉仍保留专用 `FurnaceInventoryStore` 和处理器状态；若后续新增带处理流程的机器类容器，需要设计数据化 processor schema。
3. 红石多色线仍停留在方案层面，尚未实现。

---

## 一、研究目标一：BlockID / ItemID 全运行时生成

### 1.1 现状分析

#### 1.1.1 类型层面已具备运行时基础

`BlockID` 和 `ItemID` 在类型层面已经是运行时 ID：

```cpp
// src/engine/registry/IdRegistry.h:9-10
using RuntimeId = uint16_t;
constexpr RuntimeId RUNTIME_ID_NULL = 0;

// src/world/block/Block.h:23
using BlockID = RuntimeId;

// src/item/Item.h:15
using ItemID = RuntimeId;
```

`IdRegistry::registerId()` 按**注册顺序从 0 递增**分配 ID，完全运行时：

```cpp
// src/engine/registry/IdRegistry.cpp:4-14
RuntimeId IdRegistry::registerId(const NamespacedId& namespacedId) {
    auto it = m_toRuntime.find(namespacedId);
    if (it != m_toRuntime.end()) return it->second;
    RuntimeId id = static_cast<RuntimeId>(m_toNamespaced.size());
    m_toNamespaced.push_back(namespacedId);
    m_toRuntime[namespacedId] = id;
    return id;
}
```

#### 1.1.2 硬编码的根源：BuiltinIds.h 的 X-macro 预注册

尽管 ID 分配机制是运行时的，但存在一套"内置 ID 预注册"机制，通过 X-macro 在编译期固定了一批方块/物品的注册顺序：

```cpp
// src/game/content/BuiltinIds.h:3-59
#define MECRAFT_FOR_EACH_BUILTIN_BLOCK(X) \
    X(AIR, "air") \
    X(DIRT, "dirt") \
    ...                                     // 共 56 个方块
    X(OAK_FENCE_GATE, "oak_fence_gate")

#define MECRAFT_FOR_EACH_BUILTIN_PURE_ITEM(X) \
    X(COAL, "coal") \
    ...                                       // 共 6 个纯物品
    X(WATER_BUCKET, "water_bucket")
```

这套宏在四个位置展开：

1. **声明全局符号**（`Block.h:26-31`）：`namespace BlockIds { extern BlockID AIR; ... }`
2. **定义并初始化为 0**（`Block.cpp:392-401`）：`BlockID AIR = 0;`
3. **运行时赋值**（`BlockIds::init()`）：`AIR = BlockRegistry::getId(NamespacedId("minecraft","air"));`
4. **预注册保证顺序**（`IdRegistry::initBuiltinBlockIds()`）：在内置列表之后才加载 JSON 中新增的方块

`ItemIds` 采用完全相同的模式（`src/item/Item.h` + `Item.cpp`）。

#### 1.1.3 硬编码 ID 的使用点（共 48 个文件引用 `BlockIds::` / `ItemIds::`）

经搜索，`BlockIds::` 和 `ItemIds::` 在 48 个源文件中被直接引用，典型分布：

| 模块 | 文件数 | 典型用途 |
|------|--------|----------|
| `src/world/block/` | 9 | DoorBlock、BedBlock、PistonBlock、FarmlandRules 等用 `BlockIds::` 识别方块类型 |
| `src/ecs/systems/` | 6 | BlockBreakSystem、BlockPlaceSystem、BucketUseSystem、SoilTillingSystem 等 |
| `src/game/` | 4 | GameplayState、RedstoneControlInteraction、BlockEntityInventoryLifecycle 等 |
| `src/world/` | 8 | World、Chunk、SubChunk、TerrainGenerator、FluidSystem 等 |
| `src/renderer/` | 2 | ChunkMesher、BlockEntityRenderer |
| `src/server/`、`src/client/` | 2 | GameServer、ClientWorld |
| 其他 | 17 | 存档、网络、玩家、物品等 |

**最典型的硬编码问题**出现在交互入口（`GameplayState.cpp:212-227`）：

```cpp
const BlockID craftingTableBlock = BlockRegistry::findByName("minecraft:crafting_table"); // 已用 findByName
if (targetBlock == craftingTableBlock) { ... }

const BlockID furnaceBlock = BlockRegistry::findByName("minecraft:furnace");              // 已用 findByName
if (targetBlock == furnaceBlock) { ... }

if (targetBlock == BlockIds::CHEST) { ... }                                                // ← 仍用硬编码符号
```

可以看到代码风格不统一：crafting_table 和 furnace 已改用 `findByName`，但 CHEST 仍用 `BlockIds::CHEST`。这正说明项目正处于"半运行时化"的过渡状态。

### 1.2 抹除硬编码的可行性评估

#### 1.2.1 可行性结论：**完全可行，且基础设施已就绪**

理由：
1. ID 分配机制本身已是运行时的，`IdRegistry` 支持任意 `NamespacedId` 动态注册
2. JSON 加载流程已支持运行时新增方块（`Block.cpp:507-517` 中 JSON 出现新方块会调用 `registerBlock`）
3. `BlockRegistry::findByName()` 已存在并可用
4. `BlockDef`、`ItemDef` 已是数据驱动结构

#### 1.2.2 改造方案

**方案：移除 BuiltinIds.h，改为"纯 JSON 驱动 + 启动时缓存"**

**步骤 1：消除编译期符号依赖**

将所有 `BlockIds::CHEST` 形式的引用替换为以下两种方式之一：

- **方式 A（推荐，热路径）**：启动时一次性查询并缓存到局部变量/成员变量
  ```cpp
  // 在系统初始化时缓存
  const BlockID chestId = BlockRegistry::findByName("minecraft:chest");
  ```
- **方式 B（冷路径）**：直接调用 `findByName`，适用于交互等低频逻辑

**步骤 2：移除 X-macro 预注册**

将 `IdRegistry::initBuiltinBlockIds()` / `initBuiltinItemIds()` 改为：
- 不再预注册内置列表
- 完全依赖 JSON 文件定义注册顺序
- 在 `assets/config/blocks.json` 中明确列出所有方块（当前 JSON 已包含大部分）

**步骤 3：解决初始化顺序依赖**

当前 `BlockIds::init()` 在 `BlockRegistry::init()` 末尾被调用，为全局符号赋值。移除后需要确保：
- 所有使用特定 BlockID 的系统/方块逻辑类，在 `BlockRegistry::init()` 完成后才进行首次查询
- 对于需要在构造时就持有 ID 的对象（如红石系统的方块识别表），改为延迟初始化（lazy init）或在 `BlockRegistry::init` 后显式调用 `refreshIds()`

**步骤 4：处理 AIR 的特殊性**

`BlockIds::AIR = 0` 是大量代码的依赖点（空方块判断、边界处理等）。建议：
- 保留 `RUNTIME_ID_NULL = 0` 作为"空"的语义常量
- 确保 JSON 中 `minecraft:air` 始终是第一个注册的方块（ID=0）
- 将 `BlockIds::AIR` 替换为 `AIR_BLOCK_ID` 常量或 `BlockRegistry::getAirId()`

#### 1.2.3 改造工作量评估

| 工作项 | 影响文件数 | 难度 | 风险 |
|--------|-----------|------|------|
| 移除 BuiltinIds.h X-macro | 4 核心文件 | 低 | 低 |
| 替换 48 个文件的 `BlockIds::`/`ItemIds::` 引用 | 48 | 中（机械替换） | 低 |
| 解决初始化顺序依赖 | ~10 | 中 | 中 |
| 确保 JSON 完整覆盖所有内置方块 | assets/config | 低 | 低 |

> 注：项目处于开发阶段，无玩家存档，不考虑存档兼容性。移除预注册后 ID 分配顺序变化不影响开发测试。

---

## 二、方向信息结构体 SoA / AoS 分析

### 2.1 现状：方向信息不以独立结构体存在

项目中**不存在**独立的 `Direction`/`Facing`/`Orientation` 结构体。方向信息通过两种方式编码：

#### 2.1.1 属性索引系统（PropIndices）

方向属性注册到 `BlockStateRegistry` 的全局字符串池，获得 `uint16_t` 索引：

```cpp
// src/world/block/PropIndices.h:10-13
extern uint16_t FACING;       // "facing" 属性名索引
extern uint16_t AXIS;         // "axis" 属性名索引
extern uint16_t HALF;
extern uint16_t OPEN;
```

```cpp
// src/world/block/PropIndices.cpp:142-172
FACING = lookupName("facing");
FACING_NORTH = lookupValue(FACING, "north");
FACING_SOUTH = lookupValue(FACING, "south");
// ...
```

#### 2.1.2 StateID 编码（展平的状态空间）

`BlockStateRegistry::explodeAllStates()` 将每个 BlockID 的所有属性组合展开为**一维连续的 StateID 空间**：

```
StateID = blockId 的 firstStateId + ordinal_prop0 * stride_0 + ordinal_prop1 * stride_1 + ...
```

修改方向属性时通过算术运算完成（`BlockStateRegistry::withProperty`，`BlockStateRegistry.cpp:563-617`），无需解包/重打包结构体。

### 2.2 SubChunk 存储布局分析

#### 2.2.1 当前存储是"紧凑 AoS"（按位置聚合）

```cpp
// src/world/chunk/SubChunk.h:272-283
Palette m_palette;           // 调色板：paletteIndex -> RuntimeId
BitPackedArray m_blockData;  // 4096 个位置的 packed 调色板索引
std::unordered_map<BlockID, uint16_t> m_blockCounts;

Palette m_fluidPalette;
BitPackedArray m_fluidData;
std::unordered_map<BlockID, uint16_t> m_fluidCounts;
```

每个体素位置存储一个 `BlockID`（实际上存的是调色板索引，调色板指向 StateID）。**方向信息已经编码在 StateID 中，不是独立字段**。

#### 2.2.2 这已经是高度优化的设计

- **BitPackedArray**：根据调色板大小动态选择位宽（1~16 bit），内存极其紧凑
- **调色板**：相同方块共享一个 ID，4096 个位置可能只引用几十个调色板项
- **方向信息零额外存储**：facing/axis 等已折叠进 StateID

### 2.3 SoA 改造评估

#### 2.3.1 结论：**不建议改造为传统 SoA，当前设计已优于朴素 SoA**

理由：

1. **方向信息没有独立数组可拆**
   方向编码在 StateID 中，不存在 `facing[4096]` 这样的数组。要拆分为 SoA，反而需要先把方向从 StateID 中"解压"出来，增加存储和计算开销。

2. **当前 BitPackedArray + Palette 已是缓存友好的**
   - 4096 个方块在调色板模式下可能只占 4KB（12 bit × 4096 / 8 = 6KB，但调色板小时位宽更小）
   - 遍历相邻方块时，BitPackedArray 的连续内存布局对缓存非常友好
   - 这是 Minecraft 原版（1.13+）采用的相同设计，经过工业验证

3. **SoA 的适用场景不匹配**
   SoA 适合"需要对某个属性做批量 SIMD 处理"的场景（如所有方块的光照值批量计算）。但本项目：
   - 光照存储在独立的 `m_lightMap`（`SubChunk.h:263`，已是 SoA！）
   - 方块遍历通常需要"读取方块类型 + 判断属性"，StateID 一步到位
   - 方向属性修改是低频操作（交互时），不是热路径

#### 2.3.2 已有的 SoA 设计

项目在需要批量处理的属性上**已经采用了 SoA**：
- `m_lightMap`：`std::array<uint8_t, BLOCK_COUNT>` — 光照值独立数组，SoA
- `m_blockData` / `m_fluidData`：分离存储，按"通道"拆分

这证明项目作者已理解 SoA 原则，并在合适的地方应用。方向信息不适合 SoA 化。

#### 2.3.3 可选的微优化（非 SoA）

如果未来需要优化方向密集型操作（如批量旋转区块），可考虑：
- 为需要频繁查询 facing 的方块类型，在 `BlockDef` 中预计算"facing 属性的 stride 和 offset"
- 缓存 `BlockStateRegistry::getPropertyIndex(state, FACING)` 的结果

但这属于查询优化，不涉及数据布局重构。

---

## 三、性能影响评估

### 3.1 全运行时 ID 生成的性能影响

#### 3.1.1 启动阶段

| 操作 | 当前（预注册） | 改造后（纯 JSON） | 差异 |
|------|--------------|------------------|------|
| ID 注册 | 56 次宏展开注册 | 56+ 次 JSON 驱动注册 | JSON 解析略慢，但 ms 级 |
| 全局符号赋值 | `BlockIds::init()` 56 次赋值 | 无（改为查询缓存） | 略快 |
| 总启动开销 | ~10ms | ~12ms | **可忽略** |

**结论：启动性能无显著影响。**

#### 3.1.2 运行时热路径

关键问题：移除 `BlockIds::CHEST` 后，热路径代码是否需要每次调用 `findByName`（哈希表查询）？

- **方块遍历/渲染**：不依赖 `BlockIds::`，直接用 StateID/调色板，**无影响**
- **交互逻辑**：低频（玩家按键触发），`findByName` 哈希查询 ~100ns，**无影响**
- **红石系统**：当前在 `RedstoneSystem.cpp` 中用 `BlockIds::LEVER` 等硬编码 ID 做判断。改为缓存 ID 后，**无影响**

**结论：运行时性能无显著退步，前提是热路径用缓存 ID 而非每次 `findByName`。**

### 3.2 初始化顺序问题

#### 3.2.1 当前初始化顺序

```
GameManager::initResources()
  └─ BlockRegistry::init()          // 1. 预注册内置 ID → 加载 JSON → BlockIds::init()
  └─ ItemRegistry::init()           // 2. 预注册内置物品 ID → 加载 JSON → ItemIds::init()
  └─ buildBlockIconAtlas()          // 3. 依赖 BlockRegistry
```

#### 3.2.2 改造后的潜在问题

**问题 1：全局静态对象的初始化**

如果某些全局/静态对象在构造时就需要 BlockID，移除 `BlockIds::init()` 后这些对象会拿到 ID=0。

**解决方案**：所有需要 BlockID 的对象改为两阶段初始化：
```cpp
class RedstoneSystem {
    void initialize() {  // 在 BlockRegistry::init() 之后调用
        m_leverId = BlockRegistry::findByName("minecraft:lever");
        m_buttonIds = { ... };
    }
};
```

**问题 2：系统注册顺序**

ECS 系统在 `GameSession` 初始化时注册。如果系统构造时查询 BlockID，需确保 `BlockRegistry::init()` 已完成。

**当前顺序已满足**：`BlockRegistry::init()` 在 `GameManager::initResources()` 中，早于 `GameSession` 创建。

---

## 四、研究内容：方块交互面向数据化的关键模块

### 4.1 游戏内 HUD UI 系统

#### 4.1.1 当前架构

项目有两套 UI 系统：

**系统一：Widget 树保留式 UI**（`src/ui/core/UIWidget.h`）
- 用于全屏菜单（MainMenu、PauseMenu、Settings 等）
- 树形结构，`UIScene` 子类在 `buildUI()` 中硬编码构建

**系统二：HUD 控件系统**（`src/ui/core/UIRenderer.h`）
- 用于游戏内 HUD 和容器面板
- **`UIRenderer` 直接持有控件实例作为成员变量**：

```cpp
// src/ui/core/UIRenderer.h:168-181
CrosshairControl m_crosshair;
HotbarControl m_hotbar;
HudControl m_hud;
InventoryPanelControl m_inventoryPanel;
ChestPanelControl m_chestPanel;       // ← 硬编码的箱子界面
FurnacePanelControl m_furnacePanel;    // ← 硬编码的熔炉界面
CreativeInventoryPanelControl m_creativeInventoryPanel;
```

#### 4.1.2 方块交互打开界面的流程

```
玩家右键 → GameplayState::handleBlockContainerInteraction()
         → if (targetBlock == craftingTableBlock)
               fsm.pushState(WorkbenchState)        // ← 硬编码 C++ 状态类
         → if (targetBlock == furnaceBlock)
               fsm.pushState(FurnaceState)          // ← 硬编码 C++ 状态类
         → if (targetBlock == BlockIds::CHEST)
               fsm.pushState(ChestInventoryState)   // ← 硬编码 C++ 状态类
```

**核心问题**：
1. 界面类型在 C++ 中硬编码（`ChestPanelControl`、`FurnacePanelControl` 等）
2. 方块与界面的绑定关系硬编码在 `GameplayState` 的 if-else 链中
3. 新增一个容器方块（如末影箱、木桶）需要：写 C++ Panel 类 + 写 C++ State 类 + 在 GameplayState 加 if 分支

#### 4.1.3 声明式 UI 改造方案

**目标**：用 JSON 描述容器界面，C++ 只提供通用渲染引擎

**方案：数据驱动的容器 UI 定义**

**步骤 1：定义 JSON Schema**

```json
// assets/config/container_ui/chest.json
{
  "id": "minecraft:chest",
  "layout": "grid",
  "background": "textures/gui/container/chest.png",
  "size": { "width": 248, "height": 166 },
  "slots": [
    { "id": "container", "type": "item_grid", "x": 8, "y": 18, "cols": 9, "rows": 3 },
    { "id": "player", "type": "player_inventory", "x": 8, "y": 86 }
  ],
  "title": { "text": "container.chest", "x": 8, "y": 6 }
}

// assets/config/container_ui/crafting_table.json
{
  "id": "minecraft:crafting_table",
  "layout": "grid",
  "background": "textures/gui/container/crafting_table.png",
  "slots": [
    { "id": "crafting_input", "type": "item_grid", "x": 30, "y": 17, "cols": 3, "rows": 3 },
    { "id": "crafting_result", "type": "result_slot", "x": 124, "y": 35 },
    { "id": "player", "type": "player_inventory", "x": 8, "y": 86 }
  ]
}
```

**步骤 2：实现通用容器 Panel**

```cpp
// 新增 src/ui/inventory/DataDrivenPanelControl.h
class DataDrivenPanelControl : public UIPanel {
    struct SlotDef {
        std::string id;
        SlotType type;     // item_grid, result_slot, player_inventory, fuel_slot, ...
        int x, y, cols, rows;
    };
    ContainerUIDef m_def;  // 从 JSON 加载
    std::unordered_map<std::string, SlotGroup> m_slots;
public:
    void loadDef(const ContainerUIDef& def);
    void render(UIRenderContext& ctx) override;
};
```

**步骤 3：方块 JSON 中关联 UI**

在 `blocks.json` 中为容器方块添加 `container_ui` 字段：
```json
{
  "id": "minecraft:chest",
  "container_ui": "minecraft:chest"  // 指向 container_ui 定义
}
```

**步骤 4：通用交互分发**

```cpp
// GameplayState 改造后
const auto& def = BlockRegistry::getDef(targetBlock);
if (!def.containerUi.empty()) {
    fsm.pushState(std::make_unique<ContainerState>(m_inventoryCtx, target.targetBlock, def.containerUi));
    return true;
}
```

#### 4.1.4 UI 改造工作量

| 工作项 | 工作量 | 难度 |
|--------|--------|------|
| 定义 JSON Schema | 小 | 低 |
| 实现 DataDrivenPanelControl | 中 | 中 |
| 实现通用 ContainerState | 中 | 中 |
| 迁移 Chest/Furnace/Crafting 为 JSON | 中 | 低 |
| 处理特殊 Slot 类型（燃料槽、合成结果槽） | 中 | **中高** |
| 保留 ImGui Dashboard 等非数据驱动 UI | — | — |

**难点**：熔炉界面有"燃料进度条"、"烧炼进度条"等动态元素，合成台有"结果槽联动"逻辑。需要 JSON Schema 支持动态元素和回调钩子。

### 4.2 方块状态切换

#### 4.2.1 现状

状态切换通过 `BlockStateRegistry::withProperty(stateId, property, value)` 完成，这是**数据驱动的**——不依赖 C++ 代码，纯算术运算。

但"何时切换、切换哪个属性"的逻辑硬编码在：
- `DoorBlock.cpp`：`setDoorOpen()` 切换 `open` 属性
- `RedstoneControlInteraction.cpp`：切换 lever/button 的 `powered` 属性
- `GameplayState.cpp:174-191`：`isControlBlock` 判断 + `nextControlState` 分发

#### 4.2.2 耦合点

```cpp
// src/game/states/GameplayState.cpp:174-191
if (game::redstone::isControlBlock(targetBlock)) {       // ← 硬编码判断
    const StateID updatedState = game::redstone::nextControlState(targetState);
    if (DoorBlockLogic::isDoorState(targetState)) {      // ← 硬编码判断
        DoorBlockLogic::setDoorOpen(...);                // ← 硬编码调用
    } else {
        m_ctx.world->setBlockState(..., updatedState);
    }
}
```

`isControlBlock` 内部硬编码了 `BlockIds::LEVER`、`BlockIds::STONE_BUTTON` 等 ID 列表。

#### 4.2.3 数据化改造方案

**方案：在 BlockDef 中声明交互行为**

```json
// blocks.json
{
  "id": "minecraft:lever",
  "interaction": {
    "on_use": {
      "type": "toggle_property",
      "property": "powered",
      "values": ["true", "false"]
    },
    "redstone_source": { "property": "powered", "power": 15 }
  }
},
{
  "id": "minecraft:oak_door",
  "interaction": {
    "on_use": {
      "type": "toggle_property",
      "property": "open",
      "values": ["true", "false"],
      "sync_partner": true        // 同步另一半门
    }
  }
}
```

**C++ 侧**实现一个通用的 `InteractionDispatcher`：

```cpp
void dispatchUse(World& world, const glm::ivec3& pos, StateID state) {
    const auto& def = BlockRegistry::getDef(BlockStateRegistry::getBlockId(state));
    if (def.interaction.onUse.type == "toggle_property") {
        StateID next = BlockStateRegistry::toggleProperty(state, def.interaction.onUse.propIndex);
        world.setBlockState(pos, next);
        if (def.interaction.onUse.syncPartner) {
            syncMultiBlockPartner(world, pos, def);  // 通用多方块同步
        }
    }
}
```

#### 4.2.4 红石耦合

红石系统（`RedstoneSystem.cpp`，~2400 行）深度耦合：
- 活塞伸缩逻辑硬编码在 `RedstoneSystem::updatePiston()`
- 红石导线传导硬编码
- 红石器件识别用 `BlockIds::` 硬编码

**红石系统是**面向数据化改造的**最大难点**。建议红石系统保持 C++ 实现（性能敏感 + 逻辑复杂），但方块识别改为查表（从 BlockDef 的 `redstone` 字段读取行为类型）。

#### 4.2.5 多色红石线可行性专题

**需求背景**：Minecraft 原版只有单一红石线，相邻即自动连接，玩家需用复杂结构（方块隔离、高度差）分割线路。mecraft 希望支持多色红石线——只有同色线才相互连接，从根源上解决线路串扰问题。

**当前红石线连接的硬编码关键点**（共两处，是多色化的核心改造点）：

**关键点 1：视觉连接判断**（`src/world/World.cpp:79-97`）

```cpp
bool canRedstoneWireAttachTo(const StateID stateId) {
    if (stateId == BlockIds::AIR) return false;
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    if (blockId == BlockIds::REDSTONE_WIRE) {   // ← 硬编码单一 ID
        return true;
    }
    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (def.isRedstonePowerSource) return true;
    return def.redstoneBehavior == "repeater" ||
           def.redstoneBehavior == "comparator" ||
           def.redstoneBehavior == "observer";
}
```

**关键点 2：信号传导判断**（`src/ecs/systems/world/RedstoneSystem.cpp:138-141`）

```cpp
bool isWireState(const StateID stateId) {
    return stateId != BlockIds::AIR &&
           BlockStateRegistry::getBlockId(stateId) == BlockIds::REDSTONE_WIRE;  // ← 硬编码单一 ID
}
```

`forEachWireNeighbor()`（`RedstoneSystem.cpp:449-477`）在传导信号时用 `isWireState()` 发现邻居线，6 个基本方向 + 4 个爬坡/下降方向。

**核心洞察**：这两处硬编码检查"是否为红石线"用的是**单一 BlockID 比较**。要支持多色，只需把"是否为红石线"的判断从"等于某个 ID"改为"属于红石线家族"，并把"是否连接"从"无条件连接"改为"同色才连接"。

**推荐方案：多 BlockID + 数据驱动识别（方案 A）**

每种颜色注册为独立方块（`minecraft:redstone_wire`、`minecraft:redstone_wire_blue`、`minecraft:redstone_wire_green`...），全部在 JSON 中标记 `redstoneBehavior: "wire"`。

改造点仅需两处 C++ 改动 + JSON 扩展：

**C++ 改动 1**：`canRedstoneWireAttachTo()` 增加"同色"参数

```cpp
// 改造后：selfBlockId 表示当前线的颜色（BlockID）
bool canRedstoneWireAttachTo(const StateID stateId, const BlockID selfBlockId) {
    if (stateId == BlockIds::AIR) return false;
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (def.redstoneBehavior == "wire") {
        return blockId == selfBlockId;          // ← 同色才连接
    }
    if (def.isRedstonePowerSource) return true; // 电源驱动所有颜色
    return def.redstoneBehavior == "repeater" ||
           def.redstoneBehavior == "comparator" ||
           def.redstoneBehavior == "observer";
}
```

调用处（`World.cpp:859-902`）传入当前线自身的 BlockID 即可。

**C++ 改动 2**：`isWireState()` + `forEachWireNeighbor()` 传导时同色过滤

```cpp
// 改造后：isWireState 只判断"是不是线"，不区分颜色
bool isWireState(const StateID stateId) {
    if (stateId == BlockIds::AIR) return false;
    const BlockDef& def = BlockRegistry::getFast(BlockStateRegistry::getBlockId(stateId));
    return def.redstoneBehavior == "wire";
}

// forEachWireNeighbor 传导时增加同色检查
template <typename Fn>
void forEachWireNeighbor(const World& world, const glm::ivec3& pos, Fn&& fn) {
    const StateID selfState = world.getBlockState(pos.x, pos.y, pos.z);
    const BlockID selfBlockId = BlockStateRegistry::getBlockId(selfState);
    for (const glm::ivec3& direction : kDirections) {
        const glm::ivec3 neighbor = pos + direction;
        const StateID nState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
        if (isWireState(nState) &&
            BlockStateRegistry::getBlockId(nState) == selfBlockId) {  // ← 同色才传导
            fn(neighbor);
        }
    }
    // ... 爬坡/下降方向同理加同色检查
}
```

**JSON 扩展**：每种颜色一个方块定义，复用相同的状态属性（power + north/south/east/west 连接属性）

```json
// assets/config/blocks.json
{ "id": "minecraft:redstone_wire",       "redstoneBehavior": "wire", "color": "#FF0000", ... },
{ "id": "minecraft:redstone_wire_blue",  "redstoneBehavior": "wire", "color": "#0000FF", ... },
{ "id": "minecraft:redstone_wire_green", "redstoneBehavior": "wire", "color": "#00FF00", ... }
```

**方案 A 的优势**：
- **纯数据驱动新增颜色**：加 JSON + 贴图即可，无需改 C++ 红石引擎
- **天然隔离**：不同 BlockID 不互连，从数据层面保证跨色不串扰
- **与 BlockID 运行时化契合**：运行时可注册任意数量的红石线颜色
- **改造面极小**：仅 2 个函数 + 传导函数加同色过滤，红石引擎其余 ~2400 行逻辑无需改动
- **电源兼容**：lever/button 等电源作为"发射端"通过 `isRedstonePowerSource` 驱动所有颜色线（符合直觉：电源不挑颜色）

**方案 A 的注意点**：
- **中继器/比较器跨色**：中继器接收任意颜色线的信号、输出时需决定输出颜色。建议中继器输出按自身 facing 侧相邻线的颜色，或保持原版"无色"输出（驱动所有颜色）。这是需要明确的设计决策。
- **视觉连接属性**：north/south/east/west 三态属性由 `canRedstoneWireAttachTo` 计算，改造后同色才显示连接线，异色显示为"none"（视觉上断开），符合需求。
- **贴图与渲染**：每种颜色需一套红石线贴图（或用着色器 + color 字段染色，减少贴图量）。`ChunkMesher` 中红石线渲染按 BlockDef.color 着色即可。

**备选方案 B（不推荐）：单 BlockID + color 属性**

红石线增加 `color` 属性，连接时比较双方 color 属性。
- 缺点：StateID 空间膨胀（color × power × 4方向连接），调色板效率下降
- 缺点：连接判断需多一步属性读取（`getPropertyIndex`），比方案 A 的 BlockID 直接比较慢
- 缺点：与"不同颜色=不同方块"的直觉不符

**结论**：多色红石线**完全可行，且改造成本很低**。核心仅 2 个硬编码检查函数 + 传导同色过滤。方案 A（多 BlockID + `redstoneBehavior:"wire"` 数据驱动识别）是最佳路径，新增颜色纯数据驱动，与 BlockID 运行时化改造方向高度一致。这个特性恰好是"红石系统保留 C++、仅识别改为查表"策略的最佳应用案例——红石引擎逻辑不动，只把识别从硬编码 ID 改为数据驱动字段。

### 4.3 特殊操作（锄地、水桶等）

#### 4.3.1 现状

- **锄地**：`SoilTillingSystem.cpp` 硬编码判断 `BlockIds::GRASS`/`DIRT` → 转为 `farmland`
- **水桶取水**：`BucketUseSystem.cpp` 硬编码判断 `BlockIds::WATER` → 替换为 `air` + 给水桶

#### 4.3.2 数据化改造方案

在 `items.json` 中声明物品的"使用效果"：

```json
{
  "id": "minecraft:iron_hoe",
  "use_on_block": [
    {
      "match": { "block": ["minecraft:grass_block", "minecraft:dirt"] },
      "result": "minecraft:farmland",
      "consume_durability": 1
    }
  ]
},
{
  "id": "minecraft:bucket",
  "use_on_block": [
    {
      "match": { "block": "minecraft:water" },
      "result_block": "minecraft:air",
      "result_item": "minecraft:water_bucket"
    }
  ]
}
```

**评估**：锄地和水桶逻辑简单，数据化改造**容易**。但需注意：
- 锄地要检查方块上方是否有方块（不能在头顶有方块时锄地）
- 水桶要区分水源和流水（只取水源）

这些条件用 JSON 的 `match` 条件表达式可实现，但复杂条件会降低 JSON 可读性。

---

## 五、综合评估与改造建议

### 5.1 是否值得面向数据化？

| 模块 | 值得数据化 | 理由 |
|------|-----------|------|
| BlockID/ItemID 全运行时 | **✅ 值得** | 基础设施已就绪，消除硬编码符号，便于模组扩展 |
| 方向信息 SoA | **❌ 不建议** | 当前 StateID 编码 + BitPackedArray 已优于 SoA |
| HUD 声明式 UI | **✅ 值得** | 解耦方块与界面，新增容器方块无需改 C++ |
| 方块状态切换 | **✅ 部分值得** | 简单切换（lever/button/door）可数据化 |
| 红石系统 | **⚠️ 谨慎** | 逻辑复杂且性能敏感，建议保留 C++，仅识别改为查表 |
| 特殊操作（锄地/水桶） | **✅ 值得** | 逻辑简单，数据化收益明显 |

### 5.2 改造优先级与当前状态

| 阶段 | 状态 | 当前结果 | 后续重点 |
|------|------|----------|----------|
| 阶段 1：BlockID/ItemID 运行时化 | **已完成** | `BuiltinIds`、`BlockIds::`、`ItemIds::` 已移除，方块/物品由 JSON 注册 | 保持新增内容只走 JSON 注册 |
| 阶段 2：特殊操作数据化 | **已完成主要目标** | 锄地、水桶等 use-on-block 规则由 `ItemUseDispatcher` 驱动 | 新增规则时继续扩展数据 schema，而不是在入口写分支 |
| 阶段 3：HUD 声明式 UI | **已完成主要目标** | 容器 UI、容器行为、通用容器状态和通用 block-entity storage 已落地；箱子、木桶、熔炉、合成台已迁移 | 为机器类容器继续评估 processor schema |
| 阶段 4：方块状态切换数据化 | **已完成大部分常见交互** | lever/button/repeater/comparator/door/trapdoor/fence_gate 已通过交互配置分发；多人交互为服务端权威 | 继续评估活塞等红石子系统中的结构性规则 |
| 阶段 5：红石数据尾巴清理 | **进行中** | 按钮脉冲、活塞不可推动、比较器容器信号、压力板实体过滤已数据化 | 多色红石线、活塞结构性规则 |

### 5.3 性能总结

- **无显著性能退步**：方块交互是低频操作（玩家输入触发），非每帧执行
- **热路径无影响**：渲染、区块网格化、光照计算不依赖 `BlockIds::` 符号
- **启动开销可忽略**：JSON 解析 vs 宏展开，差异在 ms 级
- **关键前提**：热路径用缓存 ID，不用每次 `findByName`

### 5.4 初始化顺序保障

改造后需遵循的初始化顺序：

```
1. ResourceMgr::init()           // 资源管理器
2. BlockRegistry::init()         // 加载 blocks.json，注册所有 BlockID
3. ItemRegistry::init()          // 加载 items.json，注册所有 ItemID
4. 各系统 initialize()           // 查询并缓存所需的 BlockID/ItemID
5. GameSession 创建              // ECS 系统注册
6. World 加载                    // 存档加载
```

所有需要 BlockID 的对象必须支持"延迟初始化"——构造时不查询 ID，在 `initialize()` 阶段（`BlockRegistry::init` 之后）才查询。

---

## 六、结论

1. **BlockID/ItemID 全运行时生成已落地**，生产代码和测试已不再依赖 `BuiltinIds`、`BlockIds::`、`ItemIds::`，新增方块/物品应继续通过 JSON 注册。

2. **方向信息不适合 SoA 改造**，当前的 StateID 编码 + BitPackedArray + Palette 已是缓存友好的工业级设计，优于朴素 SoA。

3. **HUD 声明式 UI 改造已完成主要目标**，容器 UI、容器行为、通用容器状态和通用 block-entity storage 已经落地；箱子、木桶、熔炉、合成台已迁移到配置绑定。

4. **性能无显著影响**，方块交互是低频逻辑。但需确保热路径使用缓存 ID 而非每次哈希查询。

5. **红石系统保留 C++ 实现**，但应继续把可配置规则移出 C++。按钮脉冲、活塞不可推动、比较器容器信号、压力板实体过滤已经数据化；多色红石线、活塞结构性规则仍是后续重点。**多色红石线完全可行且成本低**——核心仅 2 个硬编码检查函数（`canRedstoneWireAttachTo`、`isWireState`）改为识别 `redstoneBehavior:"wire"` 家族 + 同色过滤，推荐"多 BlockID + JSON 驱动"方案，新增颜色纯数据化，与 BlockID 运行时化方向一致。

6. **改造已进入尾部清理阶段**，当前重点不是继续扩大框架，而是逐个消除新增方块会碰到的小型硬编码规则。

7. **存档兼容性无需考虑**，项目处于开发阶段无玩家存档，改造无后顾之忧。
