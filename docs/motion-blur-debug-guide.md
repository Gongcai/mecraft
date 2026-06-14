# 动态模糊与速度缓冲调试指南

## 关键约定

`assets/shaders/velocity_resolve.frag` 生成的是全局 velocity buffer，不是 Motion Blur
专用输入。它同时被以下 pass 使用：

- TAA temporal resolve
- SSAO temporal resolve
- reflection temporal resolve
- volumetric fog temporal resolve
- motion blur

因此 velocity resolve 必须保持标准世界空间重投影：

```glsl
vec4 previousClip = uPreviousViewProj * vec4(worldPos, 1.0);
vec2 previousUv = previousClip.xy / previousClip.w * 0.5 + 0.5;
vec2 cameraVelocity = closestUv - previousUv;
```

不要在这里按深度分层添加或移除相机平移。即使 `motionBlurEnabled == false`，
TAA、SSAO、反射和体积雾仍然会读取 velocity buffer；错误或偏置过的速度会表现为
走路时画面发糊、拖影或历史采样错位。

## 排查顺序

1. 关闭 `Motion Blur`
   - 如果仍然发糊，问题不在 MotionBlurPass 本身。

2. 关闭 `TAA`
   - 如果发糊消失，优先检查 velocity buffer 和 TAA history sampling。

3. 开启 `Force Zero Velocity`
   - 如果发糊变化明显，说明 temporal pass 正在被 velocity 影响。
   - 这个开关只用于定位，不是修复方案。

4. 开启 `Freeze TAA Jitter`
   - 如果症状变化明显，检查 current/previous jitter 和 velocity 的矩阵约定是否匹配。

## Motion Blur 的调参位置

如果需要减少跑动时近处物体的动态模糊，不要修改 `velocity_resolve.frag`。应在
`assets/shaders/motion_blur.frag` 或 `MotionBlurPass` 中处理，例如按深度、速度大小、
材质或屏幕区域降低 blur radius。这样 motion blur 关闭时不会影响其它 temporal pass。

## 常见误区

- `previousViewProj` 已经包含上一帧相机平移和旋转。用当前帧重建出的 `worldPos`
  直接乘 `previousViewProj` 就是正确的静态几何重投影。
- 手动给 `worldPos` 加 `currentCameraPosition - previousCameraPosition` 会改变共享
  velocity buffer 的语义，容易让 TAA 以错误坐标读取历史颜色。
- 深度阈值如果用的是非线性 depth buffer 值，不等价于稳定的世界距离阈值。
