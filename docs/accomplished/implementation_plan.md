# Mecraft 区块加载优化方案

根据性能分析报告，我们将着手解决两个核心瓶颈：① 地形生成阻塞主线程，② GPU网格上传缺乏容量限制引发的扩容/合并卡顿。

本方案将这部分修改划分为两个阶段，以确保系统稳定性。

## ⚠️ User Review Required

**关于线程池配置的变更：**
目前 `ThreadPool` 用于网格生成(`ChunkMeshingService`)和光照(`LightService`)。引入地形生成后，线程池的负载会增加。地形生成任务是 CPU 密集型的。我们将优先保证地形生成和网格生成的公平调度。请确认是否接受这种对现有线程池的复用方式。
同意
## Open Questions

1. **初始 VBO 容量**：目前 opaque VBO 初始大小为 `1 << 18` (约 262k 顶点，8MB)。当加载大量区块时极易触发 `expand` (伴随 `glCopyBufferSubData`) 从而阻塞。我们建议将其初始容量提升至 `1 << 21` (约 2M 顶点，64MB)，以降低早期运行时的卡顿。这会略微增加内存占用，是否同意？
同意
## Proposed Changes

### 1. 异步地形生成 (Asynchronous Terrain Generation)

地形生成（包括 `generateChunk` 和 `seedInitialLightMap`）完全不依赖周围区块状态，是理想的并行任务。我们将改造 `World` 类以支持异步加载。

#### [MODIFY] `src/world/World.h`
- **新增状态追踪**：
  - `std::unordered_set<int64_t> m_generationInFlight;` 用于记录正在生成的区块，防止重复提交。
  - `std::mutex m_completedGenMutex;` 和 `std::vector<std::shared_ptr<Chunk>> m_completedGenQueue;` 用于收集工作线程完成的区块。
- **修改加载逻辑**：
  - 将原有的 `void loadChunk(int cx, int cz)` 拆分为：
    - `void submitChunkLoad(int cx, int cz)`：构建 Chunk 实例并推入线程池。
    - `void finalizeChunkLoad(std::shared_ptr<Chunk> chunk)`：在主线程执行邻居链接和事件分发（`onChunkLoaded`）。

#### [MODIFY] `src/world/World.cpp`
- 在 `World::update` 中：
  - 遍历 `m_loadQueue` 时，改为调用 `submitChunkLoad`。通过 `m_generationInFlight` 限制并发上限（例如 `kMaxGenerationInFlight = 4`），避免瞬间提交过多生成任务导致线程池饥饿。
  - 在处理完 `m_loadQueue` 后，获取 `m_completedGenMutex`，将 `m_completedGenQueue` 中的区块取出。
  - 针对每个完成的区块，执行 `finalizeChunkLoad`，将其加入 `m_chunks` 字典并绑定邻居（这部分非常轻量，且必须在主线程完成以保证多线程安全）。

### 2. 网格上传限流与缓冲池扩容 (Mesh Upload Budgeting & Pool Expansion)

当前的 `Renderer::drainMeshingResults` 仅使用时间预算 (`m_meshingDrainTimeBudgetMs`)。由于 `glBufferSubData` 具有隐式异步和缓冲机制，CPU 端的耗时测量往往不准确，导致一帧内堆积过多的网格上传任务。

#### [MODIFY] `src/renderer/WorldRenderBuffer.h`
- 将 `kInitialPoolVertices` 从 `1 << 18` 增加到 `1 << 21`。
- 将 `kInitialCutoutPoolVertices` 和 `kInitialTransparentPoolVertices` 等比例增加。
- 较大的初始容量能显著推迟甚至避免发生 `expand()` 和 `defragment()` 引起的全局显存重排卡顿。

#### [MODIFY] `src/renderer/Renderer.h`
- 增加基于数据量的预算配置：
  - `int m_meshingDrainVertexBudget = 65536;` // 约 2MB 顶点数据，作为每帧上传的安全上限。

#### [MODIFY] `src/renderer/Renderer.cpp`
- 修改 `drainMeshingResults` 逻辑：
  - 增加一个 `int uploadedVerticesThisFrame = 0;` 累加器。
  - 在每次 `tryPopCompleted` 后，计算该 SubChunk 需要上传的顶点数总和。
  - 如果 `uploadedVerticesThisFrame + currentVertices > m_meshingDrainVertexBudget`，则立即 `break` 退出 drain 循环，将剩余的上传推迟到下一帧。
  - 这一机制确保了即使有很多网格同时构建完毕，也会被平滑地分摊到多帧上传。

## Verification Plan

### Automated Tests
- 无需修改现有测试。

### Manual Verification
1. 启动游戏并大幅度移动角色，加载新区块。
2. 观察左上角的帧率波动，确保移动期间不会出现深度的掉帧（肉眼可见的卡顿应该被大幅平滑）。
3. 检查控制台/调试界面中的 InFlight 任务指标，确认地形生成和网格生成均在有条不紊地通过线程池调度。
4. 确认地形连接处没有出现撕裂或网格丢失的问题（确保 `finalizeChunkLoad` 中的邻居链接时序正确）。
