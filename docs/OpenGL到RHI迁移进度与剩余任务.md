# OpenGL 到 RHI 迁移进度与剩余任务

## 1. 文档目的

本文记录 `dev/rhi-rendering-migration` 分支相对主分支 `lightoff` 的实际迁移进度、当前技术状态、剩余直接 OpenGL 调用和后续实施顺序。

迁移目标不是为 OpenGL 状态机增加一层包装，而是建立面向 Vulkan、Direct3D 11 和 Direct3D 12 等现代图形 API 的显式 RHI。OpenGL 仅作为第一个后端，用于验证资源、管线、描述符、命令列表和同步契约。

## 2. 当前状态

- 开发分支：`dev/rhi-rendering-migration`
- 主分支：`lightoff`
- 当前工作树：干净
- 最近游戏画面验证：主场景显示正常，延迟渲染画面正常
- 整体结论：核心渲染路径和大量 UI 已迁移，但业务层直接 OpenGL 尚未清零，不能视为迁移完成

## 3. 已完成工作

### 3.1 RHI 核心能力

- 建立显式 buffer、texture、texture view、sampler、shader、pipeline layout、pipeline、bind group layout 和 bind group 生命周期。
- 支持渲染附件、深度附件、viewport、scissor、push constants、直接绘制、索引绘制、间接绘制和 compute dispatch。
- 支持 buffer 更新与复制、buffer 到 texture 上传、texture 复制、blit、mipmap 生成和三维纹理区域复制。
- 支持局部深度附件清除。
- 增加设备能力字段 `maxSamplerAnisotropy`。
- 增加显式 timestamp query pool：
  - query pool 创建与销毁；
  - command list 写入 timestamp；
  - 非阻塞结果可用性检查；
  - 批量 `uint64_t` 结果读取；
  - generation 句柄校验；
  - OpenGL 首个后端实现。
- 已增加 timestamp query pool 的运行时生命周期和时间顺序测试。

### 3.2 核心场景渲染

- 地形核心绘制路径已使用 RHI 管线、描述符和命令列表。
- 角色 GBuffer、阴影和前向绘制已迁移。
- 第一人称手臂、方块、物品和阴影资源已迁移。
- 天空、方块云层、天空捕获和天空盒模糊链路已迁移。
- 方块选中框和破坏覆盖层已迁移。
- 延迟渲染目标和级联阴影纹理已迁移。
- 体素 GI clipmap 的三维纹理、上传、复制和 mipmap 已迁移。

### 3.3 资源系统

- 二维纹理、立方体纹理、环境纹理和方块纹理数组由 RHI 创建与管理。
- block、item、HUD 和 block-icon atlas 生命周期已迁移。
- 删除纹理数组和 atlas 的 OpenGL registry 桥接。
- 删除无人使用的旧 OpenGL 阴影纹理契约 `MecraftTextureContract`。

### 3.4 UI 基础能力与控件

- `UIRenderContext` 已传递 `RhiCommandList`。
- 增加显式嵌套 scissor 上下文，支持父子裁剪求交与恢复。
- 背景模糊目标和模糊 pass 已迁移至 RHI。
- 渲染上下文已提供背景模糊的 `RhiTextureViewHandle`。
- 已迁移以下控件或效果：
  - 屏幕过渡；
  - 进度条；
  - 单选按钮；
  - 开关；
  - 通知；
  - 滑块；
  - 复选框；
  - 数字步进器；
  - 准星；
  - 滚动区域内容裁剪与滚动条；
  - 文本输入框背景、边框、选区、光标和裁剪；
  - 控制台消息裁剪与背景矩形。
- 基础形状采用静态单位 quad、显式 pipeline 和 push constants，不再使用 CPU 圆形细分和动态 GL 顶点上传。
- 已添加并通过 Vulkan SPIR-V 编译验证的玻璃面板 RHI shader：
  - `assets/shaders/ui_glass_rhi.vert`
  - `assets/shaders/ui_glass_rhi.frag`

## 4. 当前关键进行项

### 4.1 RenderDebugService 时间戳迁移

RHI timestamp query pool 已完成，下一步是迁移 `RenderDebugService`：

- 将阴影级联的 `glQueryCounter` 改为 `RhiCommandList::writeTimestamp`。
- 每帧每级联记录 Start、OpaqueEnd、End 三个时间点。
- 4 帧环形缓冲、4 个级联共需要 48 个 query。
- 将 End 时间点移动到 transparent shadow command list 提交前。
- CPU 侧通过 RHI 批量检查可用性并读取结果。
- 普通 pass 的 `GL_TIME_ELAPSED` 需改为成对 timestamp。
- 普通 pass 可能跨多个 command list 或 submit，必须将计时点放在真实命令记录边界，不能由调试服务隐式创建或提交帧。

### 4.2 UIPanel 玻璃面板

已完成 texture view 上下文和 shader，仍需：

- 创建纯色 pipeline 和玻璃采样 pipeline。
- 创建 clamp-to-edge 线性 sampler。
- 创建 combined texture sampler bind group layout 和 bind group。
- bind group 直接消费 `UIRenderContext::backdropBlurView`。
- 使用静态单位 quad 和 push constants 绘制背景、边框、高光与底部阴影。
- 删除 `GlRhiTextureRegistry`、旧 `Shader*`、VAO/VBO 和动态 buffer 更新。
- 不保留任何无模糊纹理时切回 OpenGL 的路径。

## 5. 剩余直接 OpenGL 文件

以下清单来自业务层源码扫描。合法后端边界 `src/renderer/rhi/gl/*` 和平台窗口实现 `src/engine/platform/Window.cpp` 未计入。

### 5.1 游戏与渲染基础

- `src/game/Game.cpp`：截图读取仍使用 `glReadPixels`。
- `src/renderer/core/Shader.cpp`：旧 OpenGL shader 封装，需随消费者迁移后删除。
- `src/renderer/debug/RenderDebugService.cpp`：GPU elapsed query 和 shadow timestamp。
- `src/renderer/gl/GlStateGuard.cpp`：旧 GL 状态保护工具。

### 5.2 UI 公共层与字体

- `src/ui/core/UIRenderUtils.cpp`：GL 状态保护及旧几何工具。
- `src/ui/font/GlyphAtlas.cpp`：glyph texture 创建与上传。
- `src/ui/font/TextRenderer.cpp`：文本 shader、VAO/VBO、纹理绑定和绘制。

字体迁移必须保证 glyph 预热和 atlas 上传发生在 UI render pass 开始之前，禁止在 active rendering 内执行 texture 上传。

### 5.3 UI 控件

- `src/ui/widgets/UIPanel.cpp`
- `src/ui/widgets/UITabControl.cpp`
- `src/ui/widgets/UIContextMenu.cpp`
- `src/ui/widgets/UIDropdown.cpp`
- `src/ui/widgets/UIImage.cpp`

其中 `UIPanel`、`UITabControl` 和 `UIContextMenu` 涉及玻璃背景纹理采样，必须完整迁移 texture view、sampler、bind group 和 pipeline，不能只替换几何。

`UIImage` 需要 textured pipeline、solid-color pipeline、texture view、sampler、bind group layout 和 bind group。

### 5.4 HUD 与物品栏

- `src/ui/hud/CommandInputOverlay.cpp`
- `src/ui/hud/HotbarControl.cpp`
- `src/ui/hud/HudControl.cpp`
- `src/ui/hud/Pickable.cpp`
- `src/ui/inventory/CreativeInventoryPanelControl.cpp`
- `src/ui/inventory/DataDrivenContainerPanelControl.cpp`
- `src/ui/inventory/InventoryPanelControl.cpp`

这些模块仍依赖旧 shader、GL state guard、原生纹理绑定或动态 VAO/VBO。应按共享资源和绘制类型拆分 RHI pipeline，避免每个控件重复建立同类资源。

## 6. 建议实施顺序

1. 完成 `RenderDebugService` 阴影 timestamp 迁移并验证统计结果。
2. 将普通 pass elapsed timer 改为成对 RHI timestamp。
3. 完成 `UIPanel` 玻璃 pipeline 接入。
4. 迁移 `UITabControl`、`UIContextMenu` 和 `UIDropdown`。
5. 迁移 `UIImage` 的纯色与纹理路径。
6. 设计并迁移 `GlyphAtlas` 和 `TextRenderer`，建立帧前 glyph 上传阶段。
7. 迁移 HUD 和物品栏共享绘制资源。
8. 将截图读取改为 RHI readback buffer 和显式复制流程。
9. 删除无消费者的 `Shader`、`GlStateGuard` 和 `UIRenderUtils::GLStateGuard`。
10. 扫描业务层直接 GL，确保只剩 RHI OpenGL 后端与平台窗口边界。

## 7. 每个迁移切片的验证要求

```sh
cmake --build cmake-build-rhi --target mecraft -j 19
cmake --build cmake-build-linux-vcpkg --target rhi_core_test chunk_mesher_test -j 19
./cmake-build-linux-vcpkg/rhi_core_test
./cmake-build-linux-vcpkg/chunk_mesher_test
git diff --check
```

遇到无法解释的崩溃、地址偏移或增量构建异常时，应删除对应构建目录并执行干净构建。已知 `rhi_core_test` 会输出 GTK locale warning，但退出码为 0。

## 8. 最终完成标准

- 业务层不再包含直接 OpenGL 创建、销毁、状态修改、纹理绑定、buffer 更新、绘制、query 或 readback 调用。
- 直接 OpenGL 仅存在于 `src/renderer/rhi/gl/*` 和必要的平台窗口上下文边界。
- RHI API 保持显式资源、描述符、pipeline 和 command list 模型，不暴露 OpenGL 状态机概念。
- 所有纹理消费者使用 `RhiTextureViewHandle`、sampler 和 bind group。
- 所有 GPU 计时与 readback 使用 RHI 资源和命令。
- 字体 atlas 上传不发生在 active render pass 内。
- 干净构建、核心测试、区块网格测试和游戏运行画面验证全部通过。
- 完成最终业务层 GL 扫描，并逐项确认扫描结果均属于合法后端或平台边界。

## 9. 近期关键提交

```text
654016fe test(rhi): 覆盖时间戳查询池生命周期
6467d831 feat(rhi): 实现时间戳查询池能力
b95348d8 refactor(renderer): 删除废弃的OpenGL纹理契约
823662d6 feat(ui): 添加RHI玻璃面板着色器
494e3975 refactor(ui): 暴露模糊背景RHI纹理视图
3fc4b60f feat(ui): 将控制台消息背景迁移至RHI
6b61d61a refactor(ui): 将控制台消息裁剪迁移至RHI
b72d90af feat(ui): 将文本输入框图形迁移至RHI
d781fd20 feat(ui): 将滚动区域滚动条迁移至RHI
5c625473 feat(ui): 增加嵌套RHI裁剪上下文
```
