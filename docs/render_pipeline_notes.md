# Render Pipeline Notes

## Color Space / Gamma Debt

当前 Hybrid Deferred V1 的默认后处理使用中性参数：

- `exposure = 1.0`
- `gamma = 1.0`
- `saturation = 1.0`
- `contrast = 1.0`

原因：目前贴图采样、天空/前向透明路径、G-buffer lighting 输出还没有完全统一到线性颜色工作流。如果在这个阶段默认执行 `pow(color, 1.0 / 2.2)`，部分已经接近 sRGB/SDR 的中间调会被再次抬亮，表现为画面泛白。

后续完整颜色空间工作流完成后，应恢复标准做法：

1. 明确贴图资产的颜色空间：albedo/lightmap/colormap 作为 sRGB 输入，normal/data texture 保持 linear。
2. 在 shader 或 texture internal format 层把 albedo 类输入转换到 linear。
3. 所有 lighting、fog、bloom、water composite、HDR tonemap 在 linear space 中完成。
4. 最后一步才做 linear -> sRGB 输出转换，可以使用 `pow(color, 1.0 / 2.2)` 或 `GL_FRAMEBUFFER_SRGB`，二者只选一个，避免双重 gamma。
5. UI/text 如果仍按 LDR/sRGB authoring，需要确认是在后处理之后直接画到 backbuffer，或单独做正确的 sRGB 输出处理。

验收标准：

- 默认参数下画面不泛白、不灰雾化，白天、洞穴、火把、日落都保持稳定对比。
- `gamma = 2.2` 只在完整 linear pipeline 中作为正确输出转换存在，而不是作为补偿性调色参数。
- Forward Legacy 与 Hybrid Deferred 在同一时间/seed 下保持可解释的亮度差异。

