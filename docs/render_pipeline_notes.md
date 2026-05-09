# Render Pipeline Notes

## Color Space / Gamma

当前 Hybrid Deferred 的主世界渲染路径按显式 linear workflow 处理：

1. 方块 atlas、lightmap、草/树叶 colormap、实体/掉落物/粒子贴图在 shader 中显式 `sRGB -> linear`。
2. G-buffer 存储 linear albedo，deferred lighting、fog、SSAO、bloom、透明合成在 linear/HDR scene target 中进行。
3. 后处理最后一步执行 tone mapping 和 `linear -> sRGB` 输出，默认 `gamma = 2.2`。
4. UI/text/dashboard 在后处理之后直接绘制到 backbuffer，暂时保持原来的 LDR/sRGB authoring，不参与世界 HDR 后处理。

当前没有启用 `GL_FRAMEBUFFER_SRGB`，避免和 shader 末端 `pow(color, 1.0 / gamma)` 形成双重 gamma。若后续改用 `GL_FRAMEBUFFER_SRGB`，必须移除 shader 末端手动 gamma 输出，二者只能保留一个。

验收标准：

- 默认参数下画面不泛白、不灰雾化，白天、洞穴、火把、日落都保持稳定对比。
- `gamma = 2.2` 是最后的 linear -> sRGB 输出转换，不再作为补偿性调色参数。
- Forward Legacy 与 Hybrid Deferred 在同一时间/seed 下保持可解释的亮度差异。

## Historical Note

Hybrid Deferred V1 早期曾临时使用中性后处理参数：

- `exposure = 1.0`
- `gamma = 1.0`
- `saturation = 1.0`
- `contrast = 1.0`

原因是当时贴图采样、天空/前向透明路径、G-buffer lighting 输出还没有统一到线性颜色工作流。如果在那个阶段默认执行 `pow(color, 1.0 / 2.2)`，部分已经接近 sRGB/SDR 的中间调会被再次抬亮，表现为画面泛白。
