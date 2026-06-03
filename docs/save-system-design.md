# Mecraft 存档机制设计方案

## 目标

为 Mecraft 增加一套服务端权威的世界存档机制，使单机本地服务端和未来 dedicated server 都能持久化世界、玩家和运行时状态。

本方案优先满足：

- 已编辑方块和流体在区块卸载、退出游戏、重新进入后仍然存在
- 存档不依赖当前运行时 `BlockID/ItemID` 的整数分配顺序
- 区块读写不阻塞主循环，退出时可强制 flush
- 崩溃或断电时尽量不损坏已有存档
- 第一阶段实现范围足够小，可以基于现有 `World`、`Chunk`、`SubChunk`、`GameServer` 架构逐步接入

## 当前代码现状

### 相关模块

- `World` 是服务端权威世界，负责区块加载、卸载、生成、方块编辑、光照服务、流体 tick、邻居更新队列。
- `GameServer` 拥有 `World`，单机模式也通过本地 `GameServer + GameClient` 跑 C/S 流程。
- `ClientWorld` 是客户端镜像，只接收 `ChunkData`、`ChunkUnload`、`BlockUpdateBatch`，不应直接落盘。
- `Chunk` 是 16x256x16 的区块列，内部按 16 个 `SubChunk` 管理。
- `SubChunk` 已使用 `Palette + BitPackedArray` 存储方块层和流体层。
- `NamespacedId`、`IdRegistry`、`BlockRegistry`、`ItemRegistry` 已存在，可用于存档兼容映射。

### 当前缺口

- `World::loadChunk()` 和 `World::submitChunkLoad()` 只走地形生成，没有查询磁盘存档。
- `World::unloadChunk()` 直接从 `m_chunks` 删除区块，没有保存脏区块。
- `World::setBlockState()`、`World::setFluidState()` 会集中处理方块/流体变更，但没有持久化脏标记。
- `GameServer::init()` 只接受 seed，没有 world name/save path/save metadata。
- 玩家位置、背包、生命值、饥饿值、游戏模式、时间、天气、掉落物等运行时状态暂未持久化。

## 设计原则

### 服务端权威

存档系统挂在 `GameServer`/`World` 一侧。单机只是本地服务端，远程多人由 dedicated server 持久化。客户端只负责缓存和展示，不写世界存档。

### 磁盘使用 NamespacedId

内存里可以继续使用 `RuntimeId`、`BlockID`、`ItemID`，但磁盘格式必须保存 `minecraft:stone`、`minecraft:iron_pickaxe` 这类 namespaced id。

原因：

- `BuiltinIds.h` 顺序或 JSON 内容变化可能改变运行时整数 ID。
- 存档跨版本加载时，字符串 ID 更容易迁移、重映射和诊断。
- 现有 `BlockRegistry::getNamespacedId()`、`BlockRegistry::tryGetId()`、`ItemRegistry` 已能支撑转换。

### 分层存储

推荐目录结构：

```text
saves/
  <world_name>/
    level.json
    players/
      local.json
      <client_id_or_uuid>.json
    region/
      r.<rx>.<rz>.mcrg
    entities/
      dimension_overworld.json
```

第一阶段可先用单区块文件降低复杂度：

```text
saves/
  <world_name>/
    level.json
    players/
      local.json
    chunks/
      c.<cx>.<cz>.mchk
```

后续再迁移到 region 文件。对当前开发阶段，更建议先实现 `chunks/c.x.z.mchk`，验证逻辑闭环后再做 region 聚合优化。

### 只保存必要状态

不要保存 GPU mesh、VAO/VBO、渲染缓存、客户端镜像、光照任务队列等可重建数据。

优先保存：

- 世界元数据：seed、版本、生成器参数、spawn、时间、天气
- 已生成/已修改区块的方块层、流体层、高度图可选
- 玩家状态：位置、速度、视角、生命/饥饿/护甲、飞行、模式、背包、快捷栏选择
- 掉落物等需要跨退出保留的实体

可重建：

- 区块 mesh
- 光照图，第一阶段建议加载后重算
- `ChunkTicketManager` 状态
- `LightService` 队列、流体临时队列、邻居更新临时队列

## 存档格式

### level.json

职责：描述世界级元数据，启动时先读它。

示例：

```json
{
  "format": "mecraft.level",
  "version": 1,
  "worldName": "New World",
  "seed": 12345,
  "generator": {
    "type": "mecraft:default",
    "seaLevel": 63,
    "worldHeight": 256,
    "chunkSize": 16
  },
  "spawn": [0.0, 68.0, 0.0],
  "time": {
    "timeOfDay": 300.0,
    "totalGameTime": 300.0,
    "elapsedDays": 0
  },
  "weather": {
    "type": "clear",
    "wetness": 0.0,
    "storm": 0.0,
    "aerialReduction": 0.55
  },
  "lastSavedUtc": "2026-06-03T00:00:00Z"
}
```

说明：

- `seed` 仍是地形生成根。
- `generator` 固化关键生成参数，避免未来调整默认值导致旧世界变样。
- 第一阶段 `time.totalGameTime`、`elapsedDays` 可能需要给 `DayNightSystem` 增加恢复接口。
- `WeatherSystem` 当前只有 debug preset 和目标状态访问，若要精确恢复，需要增加 state/target 的序列化接口。

### chunk 文件

第一阶段建议每个区块一个二进制文件：`chunks/c.<cx>.<cz>.mchk`。

文件结构：

```text
MCHK header
  magic: "MCHK"
  version: uint16
  flags: uint16
  chunkX: int32
  chunkZ: int32
  payloadSize: uint32
  payloadCrc32: uint32

payload
  encoding: uint8
  subChunkMask: uint16
  heightMap: int16[16*16] optional
  subChunks: repeated SectionPayload
```

`SectionPayload`：

```text
subChunkY: uint8
blockLayer:
  paletteCount: varuint
  paletteNames: repeated utf8 string
  bitsPerEntry: uint8
  packedIndices: bytes
fluidLayer:
  paletteCount: varuint
  paletteNames: repeated utf8 string
  bitsPerEntry: uint8
  packedIndices: bytes
```

实现取舍：

- 先用自定义二进制，数据体积小，也贴合 `BitPackedArray`。
- 若想更快落地，可以第一版 payload 用 JSON 记录 palette + 4096 个 uint16 index，但区块文件会非常大，不建议长期使用。
- `paletteNames` 使用 `BlockRegistry::getNamespacedId(runtimeId).full()`。
- 加载时用 `BlockRegistry::tryGetId(NamespacedId(name), out)` 映射回 RuntimeId。
- 未识别方块第一阶段回退为 `BlockIds::AIR`，并写 warning；后续可引入 missing block 占位。

### 是否保存光照

第一阶段不保存光照，加载区块后执行：

1. `chunk->seedInitialLightMap()`
2. `LightService::onChunkLoaded(chunk)`
3. 让异步光照系统重算并同步客户端

优点是格式简单、兼容性强。缺点是进入旧存档时初始光照可能短暂更新。等存档流程稳定后可增加可选 `packedLight` 段作为启动优化。

### 是否只保存修改差量

不建议第一阶段做差量存档。

当前地形生成包含树、矿、洞穴、地表植被等逻辑，理论上可由 seed 再生。但保存差量会带来这些问题：

- 方块从生成态变为空气和原本就是空气需要区分。
- 未来生成器变化会影响旧区块基底。
- 流体层和 block layer 的组合差量更复杂。

建议第一阶段：区块一旦被保存，就保存完整 subchunk 数据。未生成/未访问过的区块不落盘，需要时继续由 seed 生成。

## 运行时架构

### 新增模块

建议增加：

```text
src/save/
  SaveManager.h/.cpp
  SaveFormat.h
  ChunkSerializer.h/.cpp
  PlayerSerializer.h/.cpp
  SavePaths.h/.cpp
```

核心职责：

- `SaveManager`
  - 持有存档根目录
  - 读写 `level.json`
  - 查询区块文件是否存在
  - 调度异步区块写入
  - 退出时 flush
- `ChunkSerializer`
  - `Chunk -> bytes`
  - `bytes -> Chunk`
  - RuntimeId 与 NamespacedId 转换
- `PlayerSerializer`
  - ECS 本地玩家/服务端连接玩家状态读写
- `SavePaths`
  - 统一处理 world name 到安全路径、临时文件、备份文件

### World 接入点

`World` 增加非 owning 指针或引用：

```cpp
class SaveManager;

class World {
public:
    void setSaveManager(SaveManager* saveManager);
    void flushSaves();

private:
    SaveManager* m_saveManager = nullptr;
    std::unordered_set<int64_t> m_dirtySaveChunks;
};
```

接入流程：

- `World::init(seed)`：只初始化生成器，不直接处理路径。
- `World::loadChunk(cx, cz)`：
  - 先问 `SaveManager::loadChunk(cx, cz)`。
  - 命中则返回反序列化后的 `Chunk`。
  - 未命中再调用 `TerrainGenerator::generateChunk()`。
- `World::submitChunkLoad(cx, cz)`：
  - 异步任务里也先读磁盘，未命中才生成。
  - 注意反序列化不能触碰 OpenGL 或主线程对象。
- `World::finalizeChunkLoad(chunk)`：
  - 统一做邻居链接、`LightService::onChunkLoaded()`、mesh dirty。
- `World::setBlockState()`、`World::setFluidState()`：
  - 成功改变 block/fluid 后调用 `markChunkSaveDirty(chunkX, chunkZ)`。
- `World::unloadChunk(cx, cz)`：
  - 如果区块 save-dirty，先提交保存任务，再从内存卸载。
  - 退出游戏时必须等待保存完成。

### 脏标记策略

新增两类标记：

- `generatedPersistDirty`：新生成区块是否需要落盘。
- `modifiedDirty`：玩家/系统修改后必须落盘。

建议第一阶段简单处理：

- 只有被修改过的区块保存。
- 纯生成但未修改的区块不保存，靠 seed 重建。
- 如果后续生成器会频繁变化或需要固定探索过的地形，再开启“生成后也保存”选项。

触发脏标记：

- `World::setBlockState()`
- `World::setFluidState()`
- 后续方块实体、容器、告示牌等 block entity 修改
- 后续世界系统直接改 chunk 时必须通过 World API 或显式标记

### 异步写入

保存任务不要直接持有正在被主线程修改的 `Chunk*`。推荐：

1. 主线程在安全点把 `Chunk` 抽成 `ChunkSaveSnapshot`。
2. 后台线程只写 snapshot bytes。

`ChunkSaveSnapshot` 包含：

- `chunkX/chunkZ`
- 每个非空 subchunk 的 block/fluid palette 和 packed indices
- heightMap 可选

第一阶段如果没有给 `Palette/BitPackedArray` 暴露快照接口，可以先通过 `SubChunk::getBlock()`、`getFluidLayer()` 扫 4096 个格子构建 palette。这不是最极致，但实现简单可靠。

### 原子写入

所有文件写入采用：

1. 写到 `*.tmp`
2. flush/close
3. 可选计算 CRC
4. rename 替换正式文件

Windows 上如果目标存在，使用标准库时需要处理替换语义。可以：

- rename 前删除旧 `.bak`
- 将旧正式文件 rename 到 `.bak`
- 将 `.tmp` rename 为正式文件
- 成功后保留或删除 `.bak`

不要在写了一半时覆盖正式文件。

## 玩家状态

### players/local.json

单机第一阶段保存本地玩家：

```json
{
  "version": 1,
  "clientId": 1,
  "position": [0.0, 68.0, 0.0],
  "velocity": [0.0, 0.0, 0.0],
  "yaw": 0.0,
  "pitch": 0.0,
  "mode": "survival",
  "selectedSlot": 0,
  "health": { "current": 20, "max": 20 },
  "armor": { "current": 0, "max": 20 },
  "food": { "current": 20, "max": 20, "saturation": 5 },
  "flight": { "isFlying": false },
  "inventory": [
    { "slot": 0, "item": "minecraft:stone", "count": 64, "durability": 0 }
  ]
}
```

映射来源：

- `TransformComponent::position`
- `VelocityComponent::velocity`
- `HealthComponent`
- `ArmorComponent`
- `FoodComponent`
- `FlightStateComponent`
- `InventoryDataComponent` 中的 `Inventory`
- `Inventory::getSelectedSlot()`、`Inventory::getSlotStack(slot)`

加载顺序：

1. 创建 ECS 本地玩家。
2. 如果玩家存档存在，覆盖 position、velocity、状态组件、背包。
3. 如果不存在，使用 spawn 和默认背包。

多人 dedicated server 后续应改为每个账号/UUID 一个玩家文件，而不是 client id。

## 世界实体和掉落物

第一阶段可以只保存玩家和区块。掉落物是否保存取决于玩法预期。

如果保存掉落物，可增加 `entities/dimension_overworld.json`：

```json
{
  "version": 1,
  "drops": [
    {
      "item": "minecraft:coal",
      "count": 3,
      "position": [1.0, 65.0, 2.0],
      "velocity": [0.0, 0.0, 0.0],
      "ageSeconds": 2.5,
      "lifeTimeSeconds": 30.0
    }
  ]
}
```

对应 `DropEntity` 已有足够字段。需要给 `DropSystem` 增加导出/导入接口，例如：

```cpp
std::vector<DropEntity> DropSystem::snapshotDrops() const;
void DropSystem::restoreDrops(std::span<const DropEntity> drops);
```

## GameServer/GameSession 接入

### 配置

`GameSessionConfig` 建议增加：

- `std::string worldName`
- `std::filesystem::path saveRoot`
- `bool enableSaving = true`
- `bool saveGeneratedChunks = false`

`GameServer::init()` 改为接受配置结构：

```cpp
struct GameServerConfig {
    uint32_t seed = 0;
    int renderDistance = 8;
    std::filesystem::path savePath;
    bool enableSaving = true;
};
```

或者短期保留旧接口，新增重载。

### 启动流程

单机：

1. 主菜单选择/创建 world name。
2. `GameSession::init(config)` 创建 `GameServer`。
3. `GameServer` 创建 `SaveManager`，读取或创建 `level.json`。
4. `World` 初始化 seed，绑定 `SaveManager`。
5. `GameServer` 从 `level.json` 恢复 spawn/time/weather。
6. ECS 本地玩家创建后，`PlayerSerializer` 覆盖玩家状态。

Dedicated server：

1. 命令行或配置文件指定 save path。
2. `GameServer` 独立持有 `SaveManager`。
3. 玩家连接/断开时保存对应玩家文件。

### 退出流程

`GameSession::shutdown()` 或 `GameServer` 析构前：

1. 停止接收新的世界修改。
2. 保存 `level.json`。
3. 保存玩家文件。
4. 保存实体文件。
5. 对所有 save-dirty active chunks 生成 snapshot 并提交写入。
6. 等待所有 pending save tasks 完成。
7. 再销毁 `World`、`ThreadPool`、ECS。

## 版本迁移

每种文件都带 `version`。

第一阶段只支持 version 1。加载更高版本直接失败并提示。

未来增加 `SaveMigration`：

- 方块/物品重命名映射：`minecraft:rose_bush -> minecraft:rose`
- 缺失内容 fallback
- level generator 参数补默认值
- 玩家状态字段补默认值

建议增加 `assets/config/id_migrations.json`：

```json
{
  "blocks": {
    "minecraft:old_name": "minecraft:new_name"
  },
  "items": {
    "minecraft:old_pickaxe": "minecraft:iron_pickaxe"
  }
}
```

## 实施阶段

### Phase 1：区块存档闭环

目标：挖/放方块后退出重进仍存在。

任务：

- 新增 `SaveManager`、`ChunkSerializer`、`SavePaths`。
- `World` 支持绑定 `SaveManager`。
- `World::loadChunk()`/`submitChunkLoad()` 优先读 chunk 文件。
- `World::setBlockState()`/`setFluidState()` 标记 chunk save dirty。
- `World::unloadChunk()` 对 dirty chunk 提交保存。
- `GameServer` 退出时 flush active dirty chunks。
- 添加区块序列化单元测试。

验收：

- 修改同一区块多个方块，退出重进后数据一致。
- 修改负坐标区块，退出重进后数据一致。
- 水/流体层修改能保存。
- 未修改区块不落盘也能由 seed 正常生成。

### Phase 2：世界元数据和玩家

目标：时间、天气、spawn、玩家位置、背包、生存状态恢复。

任务：

- `level.json` 读写。
- `DayNightSystem` 增加恢复接口。
- `WeatherSystem` 增加恢复接口。
- `PlayerSerializer` 读写 ECS 本地玩家。
- `Inventory` 序列化使用 `ItemRegistry::getNamespacedId()`。
- 主菜单/配置接入 world name/save path。

验收：

- 退出重进后玩家回到上次位置。
- 背包、选中槽、血量、饥饿值恢复。
- `/time set` 和 `/weather` 修改后可恢复。

### Phase 3：实体和服务端多人

目标：dedicated server 可持久化在线玩家、掉落物和未来同步实体。

任务：

- 玩家文件从 `local.json` 扩展到 per-player id。
- `DropSystem` snapshot/restore。
- `NetworkSyncTag` 实体定义稳定 kind/type id。
- 服务端定时 autosave。
- 玩家断线时保存个人状态。

验收：

- dedicated server 重启后世界、玩家、掉落物恢复。
- 多玩家各自位置/背包不串档。

### Phase 4：Region 文件和性能优化

目标：减少小文件数量，提高大量区块读写性能。

任务：

- 引入 `region/r.<rx>.<rz>.mcrg`，每个 region 管理 32x32 chunks。
- region header 存 offset/size/crc/timestamp。
- 后台保存合并写入，空洞复用或 append-only compact。
- 可选压缩：zstd 或 miniz。
- 可选保存 packed light 作为启动优化。

验收：

- 大范围探索不会产生海量小文件。
- autosave 峰值不卡顿。

## 测试计划

### 单元测试

- `ChunkSerializer`：
  - 空区块 round-trip
  - 包含多个 subchunk 的区块 round-trip
  - block layer + fluid layer round-trip
  - 负坐标 chunkX/chunkZ
  - palette 中包含 JSON-only 方块
  - 未知 namespaced id fallback
- `SavePaths`：
  - world name 清理
  - 负坐标 chunk 文件名
  - tmp/bak 路径生成
- `PlayerSerializer`：
  - 背包空槽不写或读回为空
  - `ItemID` 使用 namespaced id round-trip

### 集成测试

- 创建世界，等待 spawn chunks 生成，修改方块，flush，重新创建 `GameServer`，验证方块。
- 修改跨区块边界方块，验证两个区块 dirty 和保存。
- 设置流体状态，flush，重新加载后 `getFluidState()` 一致。
- 模拟区块卸载后重新进入，确认从磁盘加载而不是重新生成覆盖。

### 手动验证

- 单机新建世界，挖洞、放方块、放水，退出重进。
- 走到远处修改区块，再回到原地，确认原地修改仍在。
- 修改时间/天气/背包，退出重进。
- 删除某个 chunk 文件，确认该区块可由 seed 再生成，不影响其他区块。

## 关键风险与应对

### 运行时 ID 变化

风险：直接保存 `BlockID`/`ItemID` 会导致旧存档错块。

应对：磁盘 palette 和物品使用 namespaced id。加载时查询 registry。

### 后台线程读写正在变化的 Chunk

风险：主线程修改 chunk 时，后台线程同时遍历导致数据竞争。

应对：主线程先创建 snapshot，后台只写 snapshot。

### 保存失败丢档

风险：写到一半崩溃破坏正式文件。

应对：tmp 写入 + CRC + rename 替换 + bak。

### 异步生成与磁盘加载竞态

风险：同一 chunk 同时生成和读取/保存。

应对：复用 `m_generationInFlight`，让 chunk load job 内部决定“读盘或生成”，同一 key 只允许一个任务。

### 光照恢复不一致

风险：不保存光照会有短暂视觉更新。

应对：Phase 1 接受重算；加载后立即 `seedInitialLightMap()` 并 `LightService::onChunkLoaded()`。

## 推荐第一步改动清单

1. 新建 `src/save/SaveManager.*` 和 `src/save/ChunkSerializer.*`。
2. 给 `World` 增加 `setSaveManager()`、`flushSaves()`、`markChunkSaveDirty()`。
3. 在 `World::setBlockState()`、`setFluidState()` 真实变更后标记 dirty。
4. 在 chunk load job 中先 `SaveManager::tryLoadChunk(cx, cz)`，失败才地形生成。
5. 在 `World::unloadChunk()` 和 `GameServer` shutdown 时保存 dirty chunks。
6. 写 `tests/chunk_save_serializer_test.cpp` 覆盖 round-trip。

这样可以先得到最有价值的闭环：世界编辑能保存。玩家、时间、天气和实体随后按 Phase 2/3 接上。
