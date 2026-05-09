下面是我建议的详细重构方案。核心目标是：**移除 per-mesh VAO/VBO + per-draw uniform model，改成全局批量顶点池 + MDI command buffer**。风动画全部删除，世界 shader 统一使用世界坐标。

**目标架构**
当前路径：

```text
Chunk/SubChunk mesh -> 每个 mesh 一个 VAO/VBO
Renderer 遍历可见项 -> set model -> bind VAO -> glDrawArrays
```

目标路径：

```text
Chunk/SubChunk mesh -> 上传到全局 WorldRenderBuffer
Renderer 遍历可见项 -> 写 DrawArraysIndirectCommand
每个 pass -> glMultiDrawArraysIndirect
```

世界主渲染最终变成：

```text
opaque:      1 次 MDI
cutout:      1 次 MDI
transparent: 1 次 MDI，可选第二阶段接入
overlay/UI/entity: 暂时保持旧路径
```

**第一阶段：清理风动画和 model uniform**
先把世界 shader 简化掉，这是 MDI 的前置条件。

改 [`chunk_lit.vs`](/D:/project/mecraft/assets/shaders/chunk_lit.vs:1)：

- 删除 `uniform mat4 model`
- 删除 `uniform float uWindTime/uWindStrength/uWindSpeed/uWindSpatialFreq`
- 删除 vegetation wind 分支
- 改为：

```glsl
vec4 worldPos = vec4(aPos, 1.0);
vec4 viewPos = view * worldPos;
gl_Position = viewProj * worldPos;
```

改 [`Renderer.cpp`](/D:/project/mecraft/src/renderer/Renderer.cpp:356)：

- 删除世界 pass 对风相关 uniform 的设置
- 世界 chunk pass 不再设置 `model`
- 上传顶点时把 chunk offset 烘焙进 `BlockVertex::x/y/z`

这一步可以先独立提交，风险低，也能确认视觉无变化。

**第二阶段：引入全局 GPU 顶点池**
新增文件建议：

- `src/renderer/WorldRenderBuffer.h`
- `src/renderer/WorldRenderBuffer.cpp`
- `src/renderer/WorldDrawBatch.h`

核心结构：

```cpp
struct GpuMeshRange {
    uint32_t firstVertex = 0;
    uint32_t vertexCount = 0;
    uint32_t generation = 0;
};

struct WorldGpuMesh {
    GpuMeshRange opaque;
    GpuMeshRange cutout;
    GpuMeshRange transparent;
    bool hasBounds = false;
    glm::vec3 boundsMin{};
    glm::vec3 boundsMax{};
};

struct DrawArraysIndirectCommand {
    uint32_t count;
    uint32_t primCount;
    uint32_t first;
    uint32_t baseInstance;
};
```

`WorldRenderBuffer` 负责：

- 持有一个世界 VAO
- 持有 opaque/cutout/transparent 三个 VBO，或者第一版用一个 VBO 三个池都可以
- 持有三个 `GL_DRAW_INDIRECT_BUFFER`
- 提供 `uploadOpaque/uploadCutout/uploadTransparent`
- 提供 `free(range)`
- 提供 `beginFrameCommands/pass.add(range)/flush(pass)`

第一版建议三套 VBO：opaque、cutout、transparent。这样 pass 状态简单，调试也清楚。

**第三阶段：改 mesh 上传所有权**
现在 [`SubChunkMesh`](/D:/project/mecraft/src/world/SubChunk.h:25) 自己拥有 VAO/VBO，这不适合 MDI。建议分两步迁移，降低爆炸半径。

第一步保留 `SubChunkMesh` 类型名，但把它改成逻辑 mesh handle：

```cpp
struct SubChunkMesh {
    WorldGpuMesh gpu;
    uint32_t vertexCount = 0;
    uint32_t cutoutVertexCount = 0;
    uint32_t transparentVertexCount = 0;
    bool hasBounds = false;
    glm::vec3 boundsMin{};
    glm::vec3 boundsMax{};
};
```

删除或废弃：

- `vao/vbo`
- `cutoutVao/cutoutVbo`
- `transparentVao/transparentVbo`
- `upload/uploadCutout/uploadTransparent`

真正上传移动到 `Renderer::drainMeshingResults()`，也就是现在创建 `SubChunkMesh mesh; mesh.upload(...)` 的位置：[Renderer.cpp](/D:/project/mecraft/src/renderer/Renderer.cpp:1130)。

这里改成：

```cpp
WorldGpuMesh gpuMesh = m_worldRenderBuffer.uploadSubChunk(
    result.meshData,
    chunk.getWorldOffset(),
    result.scy
);
chunk.setSubChunkMesh(result.scy, gpuMesh);
```

上传时直接把局部顶点转成世界坐标。

**第四阶段：重新评估 column aggregate**
我建议 **opaque/cutout 不再重建 column 级大 VBO**，因为 MDI 已经解决 draw submit 问题。

现在 [`Chunk::updateColumnAggregateData`](/D:/project/mecraft/src/world/Chunk.cpp:539) 会复制 sub-chunk 顶点并重建 column mesh，[`Chunk::rebuildColumnMesh`](/D:/project/mecraft/src/world/Chunk.cpp:577) 还会重新上传一份 column VBO。MDI 后这份聚合的价值降低，反而浪费内存和更新成本。

新的策略：

- 剔除仍按 region -> column -> sub-chunk bounds 做
- opaque/cutout 直接为可见 sub-chunk range 写 MDI command
- 如果担心 command 数过多，没关系：几千到几万 indirect commands 仍比几千 GL draw calls 便宜很多
- column aggregate 可以保留为旧路径 fallback，第一版不要立刻删

也就是说：**第一版保留旧 column aggregate，但 MDI 路径不使用它。** 等 MDI 稳定后再删除 column aggregate GL 上传。

**第五阶段：Renderer 新增 MDI 路径**
在 [`Renderer.h`](/D:/project/mecraft/src/renderer/Renderer.h:205) 增加：

```cpp
WorldRenderBuffer m_worldRenderBuffer;
bool m_useMultiDrawIndirect = true;
```

替换 [`renderOpaqueChunksAndCollectPasses`](/D:/project/mecraft/src/renderer/Renderer.cpp:494) 的 draw 行为：

- 仍然做 region culling
- 仍然做 column culling
- sub-chunk 或 mesh bounds 通过后，不调用 `glDrawArrays`
- 改成 `m_worldRenderBuffer.addOpaque(mesh.gpu.opaque)`
- cutout 同理写入 cutout batch
- transparent 先收集条目，第二阶段排序后写 transparent batch

flush：

```cpp
m_worldRenderBuffer.flushOpaque();
m_worldRenderBuffer.flushCutout();
```

`drawCallCount` 统计建议拆成两个：

```cpp
int logicalDrawCount;   // 原本的 mesh draw 数，比如 4000
int glSubmitCount;      // MDI 后实际 GL draw submit，比如 2-3
```

Dashboard 里显示二者，替换现在单一的 Draw Calls：[Dashboard.cpp](/D:/project/mecraft/src/ui/Dashboard.cpp:224)。

**第六阶段：透明 pass**
透明可以第二阶段做，不要卡住 opaque/cutout。

透明 MDI 方案：

- 保留 [`renderTransparentChunks`](/D:/project/mecraft/src/renderer/Renderer.cpp:811) 的 back-to-front 排序
- 排序后的 item 不再 `set model/bind vao/draw`
- 改为按排序顺序 append transparent indirect command
- 最后一次 `glMultiDrawArraysIndirect`

这能保持当前“sub-chunk 级排序”的视觉语义。

**缓冲分配策略**
第一版不要做复杂 compaction，采用简单 free list：

- `allocate(vertexCount)` 找 first-fit 空洞
- 找不到就扩容 VBO，例如扩大到 `max(required, capacity * 1.5)`
- chunk unload 或 mesh rebuild 时 `free(oldRange)`
- 空闲块相邻就合并
- 碎片率超过阈值，比如 35%，触发整池 rebuild

整池 rebuild 可以低频做，先不要异步化。

**实施顺序**
1. 删除风动画和 model uniform，世界顶点改世界坐标。
2. 新增 `WorldRenderBuffer`，跑通一个简单全局 VAO/VBO。
3. opaque MDI 接入，旧路径用开关保留。
4. cutout MDI 接入。
5. Dashboard 显示 logical draws / GL submissions / indirect command count。
6. transparent MDI 接入。
7. 删除或停用 column aggregate 的 GPU 上传。
8. 做 buffer free list、扩容、碎片整理。
9. 压测 8/32/64 区块，记录 `Render Submit`、FPS、顶点数、logical draw 数。

**预期收益**
在 32/64 区块 3000-4000+ draw calls 的场景，opaque/cutout/transparent 全接入后，世界 GL submit 可以降到 3 次左右。CPU 渲染提交时间如果现在是瓶颈，通常会有非常明显改善；如果瓶颈转到顶点量或 fill-rate，MDI 后 FPS 仍会提升有限，但卡顿和 driver submit 压力会小很多。