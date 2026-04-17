# 光照系统多线程化改造方案（Actor / 边界队列）

## Summary

将当前“全局 `LightEngine` + 全局 BFS 队列 + 直接跨 Chunk 读写”的模型，重构为“每个 Chunk 独占光照写权限 + 边界消息投递 + 主线程合并结果”的 Actor 式任务系统。

目标是：
- 让光照传播从主线程剥离到 worker。
- 保证任一时刻只有所属 Chunk 的任务能修改该 Chunk 光照数据。
- 保留现有 meshing 的异步提交/过期丢弃思路，复用到 lighting。
- 第一阶段先做到“安全和可验证”，第二阶段再追求更高并发与吞吐。

成功标准：
- 不再出现 worker 直接写邻居 Chunk 光照内存。
- 区块边界放置/破坏方块时无长期接缝错误。
- Chunk 加载、卸载、重建 mesh 与 lighting 结果之间无悬垂引用或过期提交污染。
- 主线程 `World::update()` 不再执行大块 BFS，只负责提交任务、合并结果、标记 remesh。

## Key Changes

### 1. 重新定义光照所有权与执行边界

新增 `LightService` 作为调度层，替代当前单体 `LightEngine` 的全局工作队列职责。

职责划分：
- `World`
    - 仍然拥有 Chunk 生命周期。
    - 在 block change / chunk load / chunk unload 时向 `LightService` 发事件。
    - 在每帧调用 `LightService::submitJobs(...)` 和 `LightService::drainCompleted(...)`。
- `LightService`
    - 维护“哪些 Chunk 需要光照更新”的待处理集合。
    - 为每个 Chunk 维护待消费的边界输入队列。
    - 生成 `LightJob`，提交到现有 `ThreadPool`。
    - 合并 `LightResult`，更新 Chunk 的 `lightRevision`，再标记受影响 sub-chunk dirty。
- `LightJob`
    - 只处理一个 Chunk。
    - 只读取自身快照和邻居读快照。
    - 只产出“本 Chunk 的新光照写集”和“发往邻居的边界消息”，绝不直接写邻居。
- `Chunk`
    - 成为光照状态唯一所有者。
    - 新增 `lightRevision`、light pending 标记、边界 inbox/outbox 元数据。

### 2. 引入明确的数据接口

新增或调整以下接口/类型：

```cpp
enum class LightKind : uint8_t {
    Sky,
    Block
};

enum class LightDirtyReason : uint8_t {
    ChunkLoaded,
    BlockChanged,
    NeighborBoundary,
};

struct BorderLightNode {
    uint8_t localX;
    uint8_t y;
    uint8_t localZ;
    uint8_t level;
    LightKind kind;
};

struct BorderUpdateBatch {
    int64_t targetChunkKey = 0;
    uint64_t sourceRevision = 0;
    uint8_t fromDirection = 0; // 0=+X,1=-X,2=+Z,3=-Z
    std::vector<BorderLightNode> nodes;
};

struct LightJob {
    int64_t chunkKey = 0;
    uint64_t revision = 0;
    std::shared_ptr<Chunk> chunk;
    std::shared_ptr<const Chunk> neighborPosX;
    std::shared_ptr<const Chunk> neighborNegX;
    std::shared_ptr<const Chunk> neighborPosZ;
    std::shared_ptr<const Chunk> neighborNegZ;
    std::vector<BorderUpdateBatch> inbox;
    LightDirtyReason reason = LightDirtyReason::NeighborBoundary;
};

struct LightChunkDelta {
    int64_t chunkKey = 0;
    uint64_t revision = 0;
    std::vector<uint8_t> packedLight;   // 整列 16*256*16，首版直接全量返回
    uint32_t dirtySubChunkMask = 0;
};

struct LightResult {
    LightChunkDelta selfDelta;
    std::vector<BorderUpdateBatch> outgoing;
    uint32_t nodesVisited = 0;
    float workerMs = 0.0f;
};

struct LightFrameStats {
    int submitted = 0;
    int completed = 0;
    int inFlight = 0;
    int boundarySync = 0;
    int nodesVisited = 0;
    float workerMs = 0.0f;
    float mergeMs = 0.0f;
};
```

新增服务接口：

```cpp
class LightService {
public:
    void start(ThreadPool* pool);
    void shutdown();

    void onChunkLoaded(const std::shared_ptr<Chunk>& chunk);
    void onChunkUnloaded(int64_t chunkKey);
    void onBlockChanged(int wx, int wy, int wz, BlockID oldId, BlockID newId);

    void submitJobs(const glm::vec3& cameraPos, int submitBudget);
    void drainCompleted(World& world, int mergeBudget = 32);

    LightFrameStats getFrameStats() const;
};
```

### 3. 分阶段改造 LightEngine 逻辑

#### Phase 1：先做“单 Chunk 结果对象化”，不立刻全并发
- 保留现有 BFS 规则和 sky/block remove-add 双 pass 语义。
- 从当前 `LightEngine` 中抽出纯计算逻辑，形成 `LightSolver::solve(const LightJob&) -> LightResult`。
- 首版 `LightSolver` 内部允许继续使用世界坐标运算，但必须写入本地缓冲，不直接回写 `Chunk`。
- 首版 `LightResult.selfDelta.packedLight` 直接返回整列 packed light，全量覆盖，避免先做复杂稀疏 patch。
- 主线程合并 `LightResult` 时：
    - 校验 `chunkKey + revision`。
    - 若 revision 已过期则丢弃整个结果。
    - 应用本 Chunk packedLight。
    - 标记 `dirtySubChunkMask`。
    - 将 `outgoing` 投递到邻居 inbox，并把邻居加入 light pending 集合。

这一阶段即使只允许同一时刻一个 lighting job 在跑，也先把“结果合并”和“边界消息”机制建立起来。

#### Phase 2：加入每 Chunk inbox / pending / in-flight
- `LightService` 为每个 chunkKey 维护：
    - `pendingDirtyReasons`
    - `boundaryInbox`
    - `inFlightRevision`
    - `queued/inFlight` 状态
- 调度规则：
    - 若 Chunk 已 in-flight，则新的 block change / border update 只累积到 inbox，不重复提交。
    - 当前 job 完成后，如 inbox 非空或 chunk 仍 dirty，重新排队。
- 同一 Chunk 任意时刻最多一个光照任务在飞行中。
- 不禁止相邻 Chunk 同时执行，但它们只读邻居快照、只写自己结果，因此不会直接冲突。

#### Phase 3：把 chunk load / unload 生命周期补齐
- `World::loadChunk` 不再直接调用 `LightEngine::onChunkLoaded` 做重传播。
- 改成：
    - 新 Chunk 初始化基础光照状态。
    - 向 `LightService::onChunkLoaded` 注册。
    - 为自己和已存在邻居都投递一次边界同步 dirty。
- `World::unloadChunk` 改成：
    - 通知 `LightService::onChunkUnloaded(chunkKey)`。
    - 清空该 Chunk inbox/pending/inFlight 元数据。
    - 后续迟到结果按 revision/存在性丢弃。
- 邻居裸指针仍可保留给 meshing 使用，但 lighting 调度层不得依赖其生命周期安全，统一用 `chunkKey + shared_ptr snapshot`。

#### Phase 4：与 meshing 链路联动
- 复用 meshing 的过期丢弃模式，为 Chunk 增加独立 `lightRevision`。
- 仅在 `drainCompleted` 成功应用光照结果后，标记受影响 sub-chunk dirty。
- renderer/meshing 维持现有逻辑，无需理解 Actor 细节，只消费 dirty 标记与 revision 后的稳定状态。
- 统计项接入 debug UI，至少展示：
    - `lightInFlight`
    - `lightTasksSubmitted`
    - `lightTasksCompleted`
    - `lightBoundarySyncCount`
    - `lightNodesVisited`

### 4. 任务调度与优先级

直接复用现有 `ThreadPool::submit(task, priority)`。

优先级策略：
- 玩家附近 Chunk 优先于远处 Chunk。
- `BlockChanged` 优先于 `NeighborBoundary`。
- 已可见 Chunk 优先于仅加载未可见 Chunk。

推荐优先级计算：
- `priorityBase = distanceSqToCamera`
- `reasonBias`
    - `BlockChanged = 0`
    - `ChunkLoaded = 500`
    - `NeighborBoundary = 1000`

限制规则：
- 每帧 `submitBudget` 初版设为 8。
- 同一 Chunk 只允许一个 lighting job in-flight。
- 首版不做细粒度 node budget 抢占；一个 job 处理完本 chunk 当前 inbox + 本地 dirty 再返回。
- 若 profiling 显示单 job 过大，再引入 `maxNodesPerJob` 分片。

### 5. 本地光照解算规则

保留当前正确性优先的传播语义，不在本次改造中改数学规则。

约束：
- sky light、block light 继续使用 packed nibble。
- sky light 继续保留当前 downward 与 lateral 规则。
- block light 继续按 `opacity` 衰减。
- remove pass 与 add pass 继续分离。
- 边界消息只作为 BFS seeds，不携带“强制覆盖邻居值”语义。

边界消息生成规则：
- 仅当本 chunk 边界 voxel 的传播会让邻居对应边界内侧值提升时，生成 outgoing seed。
- outgoing batch 按目标邻居和光种类合并。
- 同帧对同一目标 Chunk 的多个 batch 可在主线程投递时 merge。

### 6. Chunk 数据改造

`Chunk` 增加：
- `uint64_t m_lightRevision = 1;`
- `bool m_lightQueued = false;`
- `bool m_lightInFlight = false;`
- 必要的 light metadata 访问器。

不改动：
- `SubChunk` 的实际光照存储格式。
- meshing 读取光照的方式。
- 裸邻居指针的渲染用途。

合并阶段的写入方式：
- 新增 `Chunk::replacePackedLight(const uint8_t* data, size_t size)` 或等价接口。
- 只允许主线程调用该接口。
- 写入后按结果里的 `dirtySubChunkMask` 标记 dirty，并递增 `m_lightRevision`。

### 7. 迁移顺序

1. 抽出 `LightResult` / `LightFrameStats` / `LightService` 壳子。
2. 把现有 `LightEngine` BFS 逻辑改成“计算结果返回对象”，不直接写世界。
3. 在主线程用 `drainCompleted` 合并 self delta，建立 revision 丢弃机制。
4. 加入 outgoing border batch 和邻居 inbox。
5. 用 `LightService` 接管 `onChunkLoaded` / `onBlockChanged`。
6. 再开启真正的 worker 并发与优先级调度。
7. 最后补 debug stats 和性能收敛。

## Test Plan

### 功能正确性
- 单个发光方块在同 Chunk 内传播正确。
- 单个发光方块跨 Chunk 边界传播正确。
- 边界处放置实心方块后，remove pass 能正确回收两侧光照。
- 边界处破坏实心方块后，邻居 inbox 能触发重新传播，无永久接缝。
- chunk load 后与已加载邻居之间的边界光照最终一致。
- chunk unload 期间迟到的 lighting result 被安全丢弃，不污染新加载 chunk。

### 并发与生命周期
- 同一 Chunk 高频连续改块时，不出现并发写同一 Chunk。
- 相邻两个 Chunk 同时 in-flight 时，不崩溃、不出现直接跨 Chunk 写。
- worker 持有旧 `shared_ptr` 时主线程卸载 chunk，不发生裸指针悬挂访问。
- 旧 revision 结果在 merge 阶段被丢弃，不覆盖新状态。

### 性能回归
- 对比当前主线程方案，玩家连续放置/破坏方块时主线程尖峰下降。
- 连续加载多个 chunk 时 lighting in-flight 不无限增长。
- 边界消息数量在正常地形下可控，无明显消息风暴。

### 观测性
- debug UI 能显示 `submitted/completed/inFlight/boundarySync/nodesVisited`。
- 可人工验证“边界消息积压”和“过期结果丢弃”是否发生。

## Assumptions

- 本次改造不引入 RGB light、GI、AO 规则变化，只改执行模型。
- 首版 `LightResult` 使用整列 packed light 全量返回，先换安全性与清晰度，不先做稀疏 patch。
- 光照状态最终仍只在主线程 merge 后生效；worker 不直接改世界对象。
- 首版允许短暂最终一致性延迟，不要求 chunk 边界在同一帧内绝对同步。
- 现有 meshing 服务、线程池和 dirty-subchunk 机制继续沿用，仅增加 lighting 对接层。
