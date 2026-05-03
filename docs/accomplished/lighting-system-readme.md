# Mecraft 光照系统设计与实现指南（高性能版）

本文档给出一套面向区块体素世界（C++/OpenGL）的可落地光照架构，目标是在保证画面可读性的同时，把光照更新成本控制在可预期范围内，并与现有异步网格管线平滑协作。

---

## 1. 目标与约束

### 1.1 核心目标

1. 支持两类基础光：`Sky Light`（天空光）+ `Block Light`（方块发光）。
2. 支持增量更新：方块放置/破坏后局部传播，不全图重算。
3. 区块边界无缝：跨 chunk 传播正确，不出现接缝。
4. 渲染侧低开销：网格顶点一次携带光照数据，shader 低成本采样。
5. 可扩展：后续可接入日夜循环、彩色光源、低频 GI 近似。

### 1.2 性能约束（建议）

- 单帧光照预算：`< 2.0 ms`（中端 CPU，普通场景）。
- 方块更新峰值：连续放置/破坏时保持交互流畅，不阻塞主线程。
- 内存预算：光照数据尽量控制在每 chunk 数十 KB 量级。
- 与网格更新解耦：光照计算与 meshing 分离，避免互相卡死。

### 1.3 现有工程对齐点

- 现有 `Renderer` 已具备异步 `ChunkMeshingService` 与 in-flight 版本控制。
- 当前可将“光照更新完成 -> 标记 chunk dirty -> 网格重建”接入现有链路。
- 本文设计不依赖延迟渲染，兼容当前前向渲染管线。

---

## 2. 光照模型选型

### 2.1 基础模型（第一优先级）

1. **天空光（0-15）**
   - 从顶部向下传播。
   - 受方块 `opacity` 影响衰减（或直接阻断）。
2. **方块光（0-15）**
   - 从光源点向 6 邻域 BFS 传播。
   - 每步衰减至少 1，遇半透明方块按 `opacity` 额外衰减。
3. **顶点 AO（0-3 或 0-1 浮点）**
   - 仅用于增强体积感，不参与真实光传播。

### 2.2 GI 近似（第二阶段，可选）

- 推荐先做**低频近似**，不要上重型实时 GI：
  - 方案 A：仅保留 AO + 更好的天空光，性价比最高。
  - 方案 B：每 chunk 维护低分辨率“二次反弹缓存”（如 4x4x4），低频更新。
- 结论：首版优先把增量传播和边界正确性做扎实，再考虑 GI。

---

## 3. 数据结构与内存布局

> 目标：缓存友好、低带宽、可并发。

### 3.1 每体素光照打包

每个体素两个 4-bit 通道：

- `sky`：0-15
- `block`：0-15

打包建议：

```cpp
// low 4 bits: block, high 4 bits: sky
uint8_t packed = (sky << 4) | block;
```

这样每个体素 1 字节，若 chunk 为 `16x256x16`，光照原始数据约 `65,536 B`。

### 3.2 辅助缓存

为避免每次传播都做昂贵扫描，建议加入：

1. `heightMap[16][16]`：每列最高不透明方块 Y。
2. `columnSkyDirty[16][16]`：天空光列脏标记。
3. `borderCache`：chunk 六个面边界光值快照（用于跨 chunk 增量同步）。
4. `lightRevision`：光照版本号，和网格版本配合做过期结果丢弃。

### 3.3 数据访问顺序

- 建议线性索引为 `x + z * SX + y * SX * SZ`（或任何固定顺序），关键是全局统一。
- BFS 队列中尽量使用紧凑结构：

```cpp
struct LightNode {
    int32_t wx, wy, wz;
    uint8_t level;
    uint8_t kind; // 0=sky, 1=block
};
```

- 避免 `std::queue` 高频小分配，使用预分配环形队列或 `std::vector + head index`。

---

## 4. 传播算法设计（增量为核心）

### 4.1 基础规则

- 邻接方式：6 邻域（X+/X-/Y+/Y-/Z+/Z-）。
- 传播条件：`nextLevel > oldLevel` 时做“增亮写入”。
- 衰减规则：
  - 空气：`-1`
  - 半透明：`-(opacity)`（最小 1）
  - 不透明：阻断（可视作衰减到 0）

### 4.2 双队列更新（推荐）

方块变化后，不要只做单向 BFS，需拆成两步：

1. **Remove Pass（减光）**
   - 把受影响区域旧值清退（或降级）。
   - 记录需要重新评估的边界节点。
2. **Add Pass（加光）**
   - 从仍有效光源和边界回灌点重新 BFS。

这样可避免“残留亮点”和错误遮蔽。

### 4.3 天空光特殊处理

- 对垂直列优先处理：
  1. 用 `heightMap` 快速判断顶部直达区间。
  2. 只在受影响列和其邻近列触发传播。
- 对洞穴/悬挑结构，再用局部 BFS 修正侧向渗透。

### 4.4 跨区块边界传播

关键原则：边界变化必须“消息化”，不要依赖偶然顺序。

建议流程：

1. 本 chunk 边界光值变化时，写入 `outgoingBorderDelta`。
2. 将相邻 chunk 标记为 `lightNeighborDirty`。
3. 邻 chunk 在自身更新时合并这些 delta 并触发局部 BFS。

优点：

- 避免强同步锁。
- 可批处理，适配任务系统。

---

## 5. 多线程与任务系统

### 5.1 线程职责划分

- **主线程**：
  - 接收玩家操作、提交方块改动事件。
  - 收集完成的光照结果，应用到世界状态。
- **Light Worker 线程池**：
  - 执行 chunk 光照初始化/增量传播。
  - 产出 `LightResult{chunkKey, revision, updatedRanges...}`。
- **Meshing Worker（已有）**：
  - 消费光照完成后的 dirty chunk，重建网格。

### 5.2 防止竞争与过期

1. 每个 chunk 维护 `lightRevision`。
2. 提交任务时携带 revision 快照。
3. 回收结果时若 revision 不匹配，直接丢弃。

这与当前 `Renderer::drainMeshingResults` 的版本控制思路一致。

### 5.3 优先级调度（强烈建议）

- 近处 chunk > 远处 chunk
- 玩家当前视锥内 > 视锥外
- 小范围增量 > 大范围初始化

可采用三层队列：`High / Normal / Low`，每帧按预算取任务。

---

## 6. 网格重建与渲染协作

### 6.1 顶点格式建议

在 `ChunkMesher` 产出顶点时写入：

- `blockLight`（0-15）
- `skyLight`（0-15）
- `ao`（0-3 或归一化）

可压缩为一个 `uint16_t` 或 `uint32_t` 属性，减少顶点带宽。

### 6.2 Shader 计算建议

基础混合模型：

```glsl
float b = blockLight / 15.0;
float s = skyLight   / 15.0;
float aoTerm = ao;

// 日夜可通过 skyIntensity 调整
float light = max(b, s * skyIntensity);
vec3 litColor = baseColor * light * aoTerm;
```

### 6.3 透明方块注意事项

- 透明面排序已在 `Renderer.cpp` 有基础实现（按 chunk 距离）。
- 光照上建议先做“顶点静态值”，避免透明 pass 再做昂贵采样。
- 若后续要提升质量，可对透明材质引入独立衰减曲线。

---

## 7. 调试、可视化与指标

### 7.1 必备调试视图

1. 显示 `sky light` 热力图。
2. 显示 `block light` 热力图。
3. 显示 AO 结果。
4. 显示 `light dirty chunk` 与队列长度。

### 7.2 关键统计项（每帧）

- `lightTasksSubmitted`
- `lightTasksCompleted`
- `lightInFlight`
- `lightNodesVisited`
- `lightBoundarySyncCount`
- `lightTimeMs`（主线程合并 + worker 总耗时）

建议参照当前 meshing 统计接口风格，新增 `LightFrameStats`。

### 7.3 回归测试建议

1. 单光源封闭房间测试（遮光正确性）。
2. 挖洞贯通地表测试（天空光下渗正确性）。
3. chunk 边界放置/破坏测试（无接缝闪烁）。
4. 高频连点放置测试（无明显帧尖峰）。

---

## 8. 落地计划（分阶段）

### Phase 0: 数据打底

- 在 `Chunk` 中固化光照存储、heightMap、revision。
- 在 `Block` 数据中补齐 `opacity/lightEmission`。
- 跑通序列化与加载（若有存档）。

### Phase 1: 单线程正确性

- 实现 block light + sky light 的增/减双 pass。
- 完成跨 chunk 边界同步。
- 提供调试渲染和最小测试用例。

### Phase 2: 异步化与预算控制

- 引入 `LightService`（线程池 + 任务队列 + 优先级）。
- 主线程只做任务提交与结果合并。
- 增加每帧预算阈值，避免抖动。

### Phase 3: 渲染质量提升

- `ChunkMesher` 写入 AO + 双通道光。
- shader 接入日夜系数与材质参数。
- 优化透明材质表现。

### Phase 4: 可选扩展

- 彩色光（RGB light，成本较高，建议低优先级）。
- 低频 GI 近似缓存。
- 特效层（体积雾、god-ray）与主光照解耦。

---

## 9. 关键接口草案（建议）

```cpp
class LightEngine {
public:
    void onChunkLoaded(Chunk& chunk);
    void onChunkUnloaded(int chunkX, int chunkZ);

    void onBlockChanged(int wx, int wy, int wz, BlockId oldId, BlockId newId);

    // 每帧提交/合并
    void submitJobs(const Camera& camera, int budget);
    void drainCompleted(World& world);

    LightFrameStats getFrameStats() const;
};
```

```cpp
struct LightFrameStats {
    int submitted = 0;
    int completed = 0;
    int inFlight = 0;
    int nodesVisited = 0;
    int boundarySync = 0;
    float workerMs = 0.0f;
    float mergeMs = 0.0f;
};
```

---

## 10. 参数建议（首版默认值）

- `maxLightLevel = 15`
- `airOpacity = 1`
- `solidOpacity = 15`
- `lightTaskSubmitBudget = 8 ~ 32 / frame`
- `maxNodesPerJob`：根据 profiling 调整（例如 32K）
- `highPriorityDistance`：玩家周边 4~6 chunk

---

## 11. 风险与规避

1. **风险：边界循环更新导致抖动**
   - 规避：delta 去重 + revision 检查 + 每帧合并上限。
2. **风险：大面积改动触发长尾任务**
   - 规避：分片任务 + 可中断队列 + 距离优先级。
3. **风险：光照正确但网格延迟导致视觉跳变**
   - 规避：光照完成后优先提交对应 chunk meshing。

---

## 12. 结论

这套方案的重点不是“最先进渲染效果”，而是“正确、稳定、可扩展、可维护”的工程实现：

- 先把 `Sky + Block + AO + 增量传播 + 边界同步` 做到高性能可控。
- 与现有异步 meshing 管线深度配合，保证主线程轻量。
- 用统计与调试视图驱动迭代，逐步加入更高级效果。

按以上阶段推进，可以在不推翻现有渲染架构的前提下，持续提升光照质量与帧稳定性。
