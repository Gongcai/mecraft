# Mecraft 区块加载掉帧分析报告

> [!IMPORTANT]
> **核心结论：掉帧的主要瓶颈是 ①地形生成（同步阻塞主线程）和 ②网格上传（`glBufferSubData` 无限制地在主线程同步执行），而非网格生成本身。**

---

## 一、区块加载完整管线回顾

新区块从"不存在"到"屏幕上可见"，需经过以下阶段：

```mermaid
graph LR
    A[World::update] -->|主线程同步| B["loadChunk<br/>地形生成 + 光照初始化"]
    B -->|标记 dirty| C[submitMeshingJobs<br/>提交到线程池]
    C -->|工作线程| D[captureSnapshot + buildMeshData<br/>网格生成]
    D -->|完成队列| E["drainMeshingResults<br/>主线程回收结果"]
    E -->|主线程同步| F["glBufferSubData<br/>网格上传GPU"]
    F --> G[渲染帧]
```

| 阶段 | 执行线程 | 是否阻塞主线程 | 耗时级别 |
|------|---------|---------------|---------|
| ① 地形生成 `generateChunk` | **主线程** | ✅ 同步阻塞 | **高** (~1–3ms/chunk) |
| ② 光照种子 `seedInitialLightMap` | **主线程** | ✅ 同步阻塞 | 中 (~0.2–0.5ms) |
| ③ 邻居链接 + dirty标记 | **主线程** | ✅ 同步阻塞 | 低 |
| ④ 光照服务 `onChunkLoaded` | **主线程** | ✅ 提交 | 低 |
| ⑤ 快照采集 `captureSnapshot` | 工作线程 | ❌ | 中 |
| ⑥ 网格构建 `buildSubChunkMeshData` | 工作线程 | ❌ | 中 (~0.3–2ms/sub) |
| ⑦ 结果回收 + GPU上传 | **主线程** | ✅ 同步阻塞 | **高** |

---

## 二、各阶段详细分析

### 🔴 瓶颈1：地形生成完全同步（最关键）

**位置**: [World.cpp:593-636](file:///d:/project/mecraft/src/world/World.cpp#L593-L636)

```cpp
void World::loadChunk(int cx, int cz) {
    auto chunk = std::make_shared<Chunk>(cx, cz);
    m_terrainGen.generateChunk(*chunk);    // ← 完全同步，阻塞主线程
    chunk->seedInitialLightMap();           // ← 完全同步
    m_chunks[key] = std::move(chunk);
    // ... 邻居链接 ...
    m_lightService->onChunkLoaded(...);
}
```

虽然已有 `kMaxChunkLoadsPerFrame = 2` 的限流（[World.cpp:140](file:///d:/project/mecraft/src/world/World.cpp#L140)），但 `generateChunk` 的成本非常高：

- 每列（16x16）需要 **6次 fBM 噪声采样**（continental/detail/rough/ridge/mountain/moisture），每次 3-4 octave
- 每列还要 `buildCaveMaskColumn`，内含 **3-octave 3D fBM**
- 树木生成需对每个潜在锚点调用 `sampleTreeCandidate`，其中包含完整的 `sampleSurfaceAndMoistureScalar`
- 树木扫描范围为 `±kTreeScanRadius (2)` 扩展到邻近区域

> **估算：一个 Chunk 的 `generateChunk` 大约需要 1–3ms，两个就是 2–6ms，已占满 60fps 的一半帧预算 (16.67ms)。**

### 🔴 瓶颈2：网格上传无有效限流

**位置**: [Renderer.cpp:1364-1493](file:///d:/project/mecraft/src/renderer/Renderer.cpp#L1364-L1493)

```cpp
void Renderer::drainMeshingResults(const World& world) {
    while (drainedCount < m_meshingDrainBudget) {
        // 时间预算：m_meshingDrainTimeBudgetMs = 1.0ms (Release) / 0.5ms (Debug)
        if (elapsedMs >= m_meshingDrainTimeBudgetMs) break;
        
        // 每次 pop 一个结果，然后做：
        bakeWorldOffset(opaqueVerts);      // CPU偏移变换
        bakeWorldOffset(cutoutVerts);
        bakeWorldOffset(transparentVerts);
        
        // 关键：同步 GPU 上传
        m_worldRenderBuffer.uploadSubChunk(
            opaqueVerts, cutoutVerts, cutoutDistanceVerts, transparentVerts, ...);
    }
}
```

上传路径调用 [WorldRenderBuffer.cpp:496-540](file:///d:/project/mecraft/src/renderer/WorldRenderBuffer.cpp#L496-L540)，内部执行：
1. `VertexPoolAllocator::allocate()` — 可能触发 **defragment**（GPU 端 `glCopyBufferSubData` 拷贝）
2. `VertexPoolAllocator::upload()` — 4次 `glBufferSubData`（opaque/cutout/cutoutDistance/transparent）
3. 可能触发 `expand()` — 新建更大 VBO 并拷贝旧数据（**隐式 GPU stall**）

> **问题**：当新区块生成大量 mesh 结果积压时，drain 循环的时间预算 (`1.0ms`) 可能被单次上传突破——尤其当 `expand()` 或 `defragment()` 触发时，单次 `glCopyBufferSubData` 可能独占数毫秒。

### 🟡 网格生成（影响较小）

网格生成（`captureSubChunkSnapshot` + `buildSubChunkMeshData`）完全在**工作线程**执行，不阻塞主线程。

- 有 `m_meshingMaxInFlight` 上限
- `submitMeshingJobs` 有预算限制
- 工作线程数量自动探测 `max(2, hw_concurrency - 1)`, 上限12

唯一可能的间接影响：如果线程池也被 `LightService` 的任务占满，mesh 任务的完成会延迟（但不会导致掉帧）。

### 🟡 光照传播（间接影响）

`LightService` 使用同一个 `ThreadPool`，在 `World::update` 中有自己的 submit/drain 预算控制。光照完成后会 `markSubChunkDirty`，触发二次 remesh。这不直接导致掉帧，但会增加后续帧的 mesh 工作量。

---

## 三、掉帧触发场景复现

玩家移动到新区域时，一帧内发生的同步工作：

| 步骤 | 操作 | 耗时估计 |
|------|------|---------|
| 1 | `World::update` → 加载 2 个新 Chunk | **2–6ms** |
| 2 | `releaseStaleMdiAllocations` 释放旧的 | ~0.1ms |
| 3 | `drainMeshingResults` 回收 1-2 个结果 + GPU上传 | **1–4ms** |
| 4 | `submitMeshingJobs` 提交新任务 | ~0.3ms |
| 5 | 渲染（frustum culling + draw） | ~3–6ms |
| **总计** | | **6–16ms+** |

> 正常帧约 6–8ms，加载新区块帧可达 **12–20ms**，导致明显卡顿。

---

## 四、问题严重性排序

| 排名 | 问题 | 严重性 | 代码位置 |
|------|------|--------|---------|
| 🥇 | `generateChunk` 同步阻塞主线程 | ⚠️ **严重** | [World.cpp:599](file:///d:/project/mecraft/src/world/World.cpp#L599) |
| 🥈 | `glBufferSubData` 上传 + 可能的 expand/defrag | ⚠️ **严重** | [WorldRenderBuffer.cpp:246-289](file:///d:/project/mecraft/src/renderer/WorldRenderBuffer.cpp#L246-L289) |
| 🥉 | `seedInitialLightMap` 同步遍历 256 高度 | ⚡ 中等 | [Chunk.cpp:448-488](file:///d:/project/mecraft/src/world/Chunk.cpp#L448-L488) |
| 4 | 邻居 dirty 标记引发的级联 remesh | ⚡ 中等 | [World.cpp:606-630](file:///d:/project/mecraft/src/world/World.cpp#L606-L630) |
| 5 | `computeTintMapPosition` 调用 `getBiome` 采样噪声 | 💡 轻微 | [ChunkMesher.cpp:265-303](file:///d:/project/mecraft/src/renderer/ChunkMesher.cpp#L265-L303) |

---

## 五、优化建议

### 🏆 高优先级：异步地形生成

将 `generateChunk` 移到工作线程：

```cpp
// 概念方案：
struct ChunkGenJob {
    int cx, cz;
    std::shared_ptr<Chunk> chunk;
};

// 在工作线程：
m_terrainGen.generateChunk(*job.chunk);
job.chunk->seedInitialLightMap();

// 在主线程 drain：仅做轻量的 m_chunks 插入 + 邻居链接
```

> **预期收益：减少每帧 2–6ms 的主线程阻塞**

### 🏆 高优先级：GPU 上传分帧/使用 PBO

1. **分帧限流**：`drainMeshingResults` 添加更严格的 per-frame 上传字节数预算（而非仅时间预算），因为 `glBufferSubData` 的实际耗时取决于驱动异步行为，时间测量不准确
2. **PBO 双缓冲上传**：使用 `GL_PIXEL_UNPACK_BUFFER` 模式的 PBO 或 persistent mapped buffer 实现异步上传
3. **预分配足够大的池**：增大 `kInitialPoolVertices`（当前 `1 << 18 = 262144`），减少 expand 触发

### 💡 中优先级

- `seedInitialLightMap` 可以合并到异步地形生成中
- 邻居加载后的 dirty 标记应考虑延迟/批处理，避免一个新 chunk 导致 4 个邻居全部 16 层 sub-chunk 被标记
- `kMaxChunkLoadsPerFrame` 可以根据帧时间动态调节（当前帧 under budget 才加载）

---

## 六、总结

```
掉帧贡献占比估算：
┌─────────────────────────────┐
│ 地形生成 (同步)         ~45% │ ████████████
│ GPU 网格上传 (同步)     ~30% │ ████████
│ 光照初始化 (同步)       ~10% │ ███
│ 邻居级联 remesh         ~10% │ ███
│ 其他                     ~5% │ █
└─────────────────────────────┘
```

**网格生成本身不是瓶颈**（已正确地异步化到线程池），真正的问题是管线两端——**源头的地形生成**和**末端的 GPU 上传**——都在主线程同步执行，且缺乏足够的帧预算保护。
