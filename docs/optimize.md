# 区块加载到 GPU 上传优化方案

## Summary
目标是降低新区块加载时的主线程帧尖峰，而不是重写整条渲染架构。当前项目已经完成异步地形生成、异步 meshing、MDI 批量绘制；优先优化剩余瓶颈：mesh 结果回收、GPU upload、VBO 池扩容/碎片、dirty remesh 放大。

默认采用低风险分阶段方案：先稳定帧时间，再考虑更激进的 persistent mapped buffer。

## Key Changes
- **GPU 上传队列硬预算化**
  - 将 `drainMeshingResults` 改为两阶段：先 peek/缓存 completed result，再按 `vertexCount` 和时间预算上传。
  - 如果单个 result 超过预算，允许上传一个，但本帧不再继续 drain。
  - 不再“先出队再发现超预算”，避免一帧内连续触发多个 `glBufferSubData`。
  - 增加 debug 统计：本帧上传 vertices/bytes、deferred results、upload ms、pool expand 次数。

- **WorldRenderBuffer allocator 稳定化**
  - v1 禁用在线 `defragment()` 的 GPU copy 路径，避免不可控 `glCopyBufferSubData` 尖峰。
  - 保留 free-list 合并；分配失败时只扩容。
  - 扩容改为受控策略：每帧最多一次；扩容后记录统计；初始池不足时优先调大默认容量。
  - 删除或暂时不用 `registerRange/unregisterRange/kDefragmentThreshold` 这组半成品接口，避免维护者误以为 defrag 已完整支持。

- **减少邻居加载导致的 remesh 放大**
  - `finalizeChunkLoad` / `unloadChunk` 中邻居 dirty 标记改为边界感知。
  - 只标记与新邻居接壤、且该 subchunk 可能暴露边界面的 section。
  - 保留 block edit 的现有邻居 dirty 语义，避免破坏交互正确性。
  - 对同一 subchunk 的重复 dirty 标记合并，不重复提交 in-flight mesh job。

- **meshing 提交从全量扫描过渡到 dirty 队列**
  - 新增 dirty subchunk priority queue，dirty 时入队，提交时按距离和 revision 弹出。
  - 保留现有全量扫描作为 debug fallback。
  - 队列项包含 `chunkKey/scy/revision/distancePriority`；提交前检查 chunk 仍存在、revision 仍匹配、未 in-flight。
  - 这一步降低高 render distance 下每帧 `activeChunks * 16` 扫描和 `partial_sort` 成本。

- **MDI 命令合并做 A/B 开关**
  - 给 opaque/cutout command merge 加 debug 开关或阈值。
  - 默认：commands 小于 4096 时 merge；超过阈值直接 flush，避免排序成本过高。
  - transparent 不合并，继续保持 back-to-front section 顺序。

## Interfaces / Stats
- `Renderer::RenderWorkStats` 增加：
  - `meshUploadBytesThisFrame`
  - `meshUploadVerticesThisFrame`
  - `meshUploadDeferredCount`
  - `worldBufferExpandCount`
  - `worldBufferUploadMs`
- `WorldRenderBuffer` 增加只读统计接口：
  - pool capacity/used 已有，补充 per-frame `expandCount` 和 `uploadedBytes`。
- 不改 shader 输入语义，不改 `BlockVertex` 格式，不改变 MDI 默认开启行为。

## Test Plan
- **正确性**
  - 跑现有 `chunk_meshing_service_test`、`chunk_section_test`、`light_service_*`、`meshing_perf_test`。
  - 手动验证：快速飞行加载新区块、破坏/放置边界方块、水/玻璃/树叶透明和 cutout 渲染、区块卸载后无闪烁或残留 mesh。

- **性能验收**
  - RelWithDebInfo 下记录 8/16/24 render distance：
    - frame time p95/p99
    - mesh upload bytes/frame
    - deferred mesh result 队列长度
    - WorldRenderBuffer expand 次数
    - MDI logical commands vs GL submissions
  - 验收目标：快速移动时 `drainMeshingResults` 单帧主线程耗时稳定在预算附近，避免多毫秒 upload/expand 尖峰。

- **回归风险**
  - 特别检查 allocator free 后再 remesh 的场景：反复放置/破坏同一区块边界，确认没有 range 泄漏、错绘、旧 mesh 残留。
  - 检查长距离移动后 pool used/capacity 是否稳定，不持续无界增长。

## Assumptions
- v1 优先平滑帧时间，允许 mesh 可见延迟增加 1-3 帧。
- 暂不实现 persistent mapped buffer；只有当硬预算 + allocator 稳定后仍有明显上传尖峰，再进入 v2。
- 保持当前 MDI 架构，不回退 per-mesh VAO/VBO 路径。
- 优化目标是当前桌面 OpenGL 路径，不引入 Vulkan/compute/mesh shader 级别重构。
