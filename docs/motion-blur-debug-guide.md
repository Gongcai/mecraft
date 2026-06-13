# 动态模糊拉扯问题调试指南

## 问题描述
走路时画面出现拉扯抖动感，疑似动态模糊计算错误。

## 已实现的修改

### 1. DerivativeMain 深度分层逻辑
参考 `DerivativeMain/program/Post/Temporal.frag::Reproject()`，实现了：
- **近处物体（depth ≤ 0.56）**：不添加相机平移引起的速度，只受旋转影响
- **远处物体（depth > 0.56）**：添加相机平移速度，正常计算运动模糊

### 2. 调试选项
在 `Dashboard` 中添加了 `Disable Camera Translation Velocity` 选项：
- **关闭**（默认）：使用深度分层逻辑
- **开启**：完全禁用相机平移速度，所有物体只受旋转影响

## 调试步骤

### 第一步：验证问题来源
1. 启动游戏，打开 Dashboard（通常 F3）
2. 找到 `Disable Camera Translation Velocity` 选项
3. 勾选开启
4. 测试走路时是否还有拉扯感

**结果分析：**
- 如果拉扯感**消失**：问题在相机平移速度计算上
- 如果拉扯感**依然存在**：问题可能在 TAA 或其他地方

### 第二步：测试其他相关选项
1. `Force Zero Velocity`：强制所有速度为0
   - 如果勾选后拉扯消失 → 问题确实在 velocity 计算
   - 如果依然存在 → 问题在 TAA 的 temporal blending

2. `Freeze TAA Jitter`：冻结 TAA 抖动
   - 如果勾选后拉扯消失 → 问题在 jitter 与 velocity 不匹配

3. `TAA`：关闭 TAA
   - 如果关闭后拉扯消失 → 问题在 TAA，而非动态模糊

## 可能的问题点

### A. Shader 端问题

#### A1. 深度阈值不正确
当前使用 `depth > 0.56` 作为远近分界：
```glsl
if (uDisableCameraTranslation == 0 && closestFragment.z > 0.56) {
    cameraTranslation = uCameraPosition - uPreviousCameraPosition;
}
```

**检查点：**
- 0.56 对应多少米的距离？
- 是否需要转换为线性深度？
- DerivativeMain 的深度格式是什么？

#### A2. 速度方向错误
当前计算：`cameraVelocity = closestUv - previousUv`

**检查点：**
- 方向是否正确？（当前 → 上一帧，还是上一帧 → 当前？）
- DerivativeMain 的速度方向是什么？

#### A3. 坐标系不一致
当前在 world space 添加相机平移：
```glsl
vec3 worldPos = reconstructWorldPosition(...);
vec3 worldPosWithTranslation = worldPos + cameraTranslation;
```

**检查点：**
- `uCameraPosition` 和 `uPreviousCameraPosition` 是否在同一坐标系？
- 是否需要考虑 world origin rebasing？

### B. C++ 端问题

#### B1. 相机位置更新时机
```cpp
// RenderScene.cpp:632
ctx.camera.position = camera.getPosition();

// RenderScene.cpp:802
m_previousContext = ctx;
```

**检查点：**
- `camera.getPosition()` 是否在正确的时机被调用？
- 是否在物理更新之后、渲染之前？
- 帧间是否有延迟或双缓冲导致的不同步？

#### B2. 矩阵和位置不匹配
```cpp
ctx.camera.view = camera.getViewMatrix();
ctx.camera.position = camera.getPosition();
```

**检查点：**
- `getViewMatrix()` 使用的位置和 `getPosition()` 是否完全一致？
- 查看 `Camera::getViewMatrix()` 实现：
  ```cpp
  return glm::lookAt(m_position, m_position + m_front, m_up);
  ```
  确认使用的是同一个 `m_position`。

#### B3. previousViewProj 延迟问题
```cpp
ctx.previousViewProj = m_previousContext.camera.viewProj;
```

**检查点：**
- `m_previousContext` 是否真的是上一帧的？
- 第一帧（`m_hasPreviousContext = false`）的处理是否正确？

### C. DerivativeMain vs 我们的实现差异

#### C1. Iris/OptiFine 的特殊处理
DerivativeMain 依赖于 Iris/OptiFine 提供的：
- `cameraPosition` 和 `previousCameraPosition` uniform
- 这些值可能经过了特殊处理（如 chunk rebasing）

**我们的实现：**
```cpp
m_velocityShader->setVec3("uCameraPosition", ctx.camera.position);
m_velocityShader->setVec3("uPreviousCameraPosition", ctx.prevCamera.position);
```

**检查点：**
- 是否需要对相机位置做 modulo 处理？
- 大坐标（如 x=10000）是否会导致精度问题？

#### C2. Projection 矩阵差异
DerivativeMain 使用的矩阵：
- `gbufferProjectionInverse`
- `gbufferPreviousProjection`
- `gbufferModelViewInverse`
- `gbufferPreviousModelView`

**我们的实现：**
- `uInvViewProj = inverse(projection * view)`
- `uPreviousViewProj = previousProjection * previousView`

**检查点：**
- 矩阵计算顺序是否一致？
- 是否需要分开传递 view 和 projection？

## 推荐的下一步

### 1. 添加可视化调试
在 `velocity_resolve.frag` 输出速度的可视化：
```glsl
// 临时输出速度大小作为颜色
FragVelocity = abs(cameraVelocity) * 10.0; // 放大10倍便于观察
```

### 2. 添加日志输出
在 `VelocityPass.cpp` 输出关键值：
```cpp
if (ctx.frameIndex % 60 == 0) { // 每秒输出一次
    spdlog::info("Camera pos: ({}, {}, {})", 
        ctx.camera.position.x, ctx.camera.position.y, ctx.camera.position.z);
    spdlog::info("Previous camera pos: ({}, {}, {})", 
        ctx.prevCamera.position.x, ctx.prevCamera.position.y, ctx.prevCamera.position.z);
    glm::vec3 delta = ctx.camera.position - ctx.prevCamera.position;
    spdlog::info("Camera delta: ({}, {}, {}), length: {}", 
        delta.x, delta.y, delta.z, glm::length(delta));
}
```

### 3. 对比 DerivativeMain 的实际输出
如果有 Minecraft + Iris + DerivativeMain：
- 查看它的 velocity buffer（通过调试工具）
- 对比走路时近处和远处的速度值

### 4. 尝试不同的深度阈值
修改 `velocity_resolve.frag`：
```glsl
// 尝试不同的阈值
if (uDisableCameraTranslation == 0 && closestFragment.z > 0.8) { // 0.56 → 0.8
    cameraTranslation = uCameraPosition - uPreviousCameraPosition;
}
```

或者完全禁用远景的平移速度：
```glsl
// 所有物体都不受平移影响
vec3 cameraTranslation = vec3(0.0);
```

## 参考资料

- `DerivativeMain/program/Post/Temporal.frag` - Reproject() 函数
- `DerivativeMain/program/Post/MotionBlur.glsl` - 动态模糊实现
- `DerivativeMain/lib/Head/Functions.inc` - 工具函数
- 当前实现：`assets/shaders/velocity_resolve.frag`
