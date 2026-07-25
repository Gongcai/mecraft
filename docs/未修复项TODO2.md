# 未修复项 TODO 2

## Vulkan DLSS-G 性能与稳定性

### 当前现象

- 开启 DLSS-G 后，应用真实帧率可能从约 190 FPS 降至约 95 FPS，实际呈现帧率约为 192 FPS，最终帧率没有明显提升。
- 不同刷新率显示器上的结果接近。当前 Vulkan 接入使用固定 2x 帧生成且关闭 VSync，不按显示器刷新率动态调节。
- 偶尔出现 `presentation backend failed to present the acquired real frame`，暂时无法稳定复现。

### 初步判断

当前实现使用 `eBlockNoClientQueues`，但会在下一次图形提交前等待上一帧 DLSS-G 输入处理完成，并在 `ALL_COMMANDS` 阶段阻塞。这可能导致真实帧渲染与帧生成串行执行。

### 后续处理

先完成整个 Render Graph，再处理此问题。Render Graph 需要管理多帧资源槽、跨帧资源生命周期、外部 timeline fence 和按资源槽延迟复用。之后将 DLSS-G 作为图末端的外部消费者，只在复用对应输入资源槽时等待 Streamline completion fence，并使用 NVIDIA FrameView 验证实际显示节奏。
