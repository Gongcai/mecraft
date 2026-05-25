The user wants to understand how the UI system is implemented in this project. Let me explore the codebase to find UI-related files and understand the architecture.
## Mecraft UI 系统架构分析

这是一个**完全自研的保留模式(Retained-mode) UI 框架**，不依赖任何通用UI库（ImGui 仅用于调试面板）。所有游戏内UI均基于自研框架实现。

---

### 整体架构

```
Application Layer
  MainMenuAppState ──── GameplayAppState ──── UIState
       │                       │                  │
  [MainMenuScreen]       [HUD Controls]    [PauseMenuScreen]
       └──────────────────┬──────────────────┘
                          │
                  ┌───────▼────────┐
                  │   UIRenderer   │  ← 中央调度器
                  │ UIInputAdapter │  ← 输入路由
                  └───────┬────────┘
                          │
          ┌───────────────┼───────────────┐
          │               │               │
    Game HUD        Inventories       UIScene
    Crosshair       Panel            (Menu Screen)
    Hotbar          Creative
    HUD Stats       Crafting
    HeldItem        ItemGrid
    Console
          │               │               │
          └───────────────┼───────────────┘
                          │
                  ┌───────▼────────┐
                  │  UIWidget (基类) │
                  │  组合模式控件树   │
                  └───────┬────────┘
                          │
           ┌──────────────┼──────────────┐
           │              │              │
      Primitives     Composites      Utilities
      UIPanel        UIButton        TextRenderer
      UIText         UICheckbox      GlyphAtlas
      UIImage        UIDropdown      UIRenderUtils
                     UISlider        UITheme
                     UIScrollArea    Tween/Easing
                     UITooltip
```

---

### 核心设计

**1. 控件体系 — 组合模式 (Composite Pattern)**

`UIWidget` 是所有控件基类，采用经典树形结构：
- `m_children` (`vector<unique_ptr<UIWidget>>`) + `m_parent` 指针
- 统一生命周期：`init()` / `shutdown()` 递归传播
- 统一渲染：`render()` → 先 `renderSelf()` 再递归渲染子节点
- 统一输入：`onInput()` 反序遍历子节点（顶层优先），返回 `UIEventResult`

**2. 布局系统 — 9宫格锚点**

- `Anchor` 枚举：TopLeft/TopCenter/.../BottomRight，共9个锚点
- 每个 Widget 通过 `anchor` + `anchorOffsetX/Y` 定位
- `UIStackLayout`：水平/垂直堆叠，自动计算尺寸
- `UIGridLayout`：行列网格布局
- `UIWidget::getAbsoluteX/Y()` 递归解析最终像素坐标

**3. 事件系统 — 冒泡 + 消费语义**

- `UIInputAdapter::routeInput()` 将原始输入转为 `UIInputEvent`
- `UIRenderer::routeUIInput()` 分发：场景优先 → 控件分发
- `UIEventResult`：`Ignored` / `Handled` / `Consumed`（Consumed 立即终止分发）
- 支持焦点管理和键盘导航

**4. 场景系统 — 生命周期管理**

`UIScene` 管理菜单屏幕生命周期：`Entering` → `Active` → `Exiting`
- 子类重写 `buildUI()` 构建控件树
- `onSceneEnter()/onSceneExit()` 触发转场动画
- 现有场景：`MainMenuScreen`、`PauseMenuScreen`

**5. 渲染架构 — 直接 OpenGL**

- 参考分辨率 `1280x720`，通过 `uiScale` 适配任意窗口
- `UIRenderUtils::pushColorQuad/pushTexturedQuad` 批量构建顶点
- `TextRenderer` 支持 `beginBatch/batchRender/endBatch` 合并 draw call
- RAII `GLStateGuard` 管理 GL 状态（禁深度测试、启用 Alpha 混合）
- 脏标记优化：`HotbarControl` 等控件跳过不变的顶点重建

**6. 动画系统 — Tween 补间**

- `Tween<T>` 模板（支持 `float` 和 `array<float,4>` 颜色）
- 多种缓动函数 + loop/pingPong 模式
- `TweenGroup` 批量管理，控件内置 hover/press 过渡动画

**7. 主题系统 — 数据驱动**

- `UITheme` 包含全配色/间距/字体参数，支持 JSON 加载
- `UIThemePresets` 提供 dark/light 预设
- 通过 `UIRenderContext::theme` 传递，控件优先用本地颜色，回退到主题

---

### 关键入口

| 入口 | 位置 |
|------|------|
| `UIRenderer::render()` | `src/ui/UIRenderer.cpp:448` — 游戏HUD渲染 |
| `UIRenderer::renderSceneOnly()` | `src/ui/UIRenderer.cpp:534` — 菜单场景渲染 |
| `UIInputAdapter::routeInput()` | `src/ui/UIInputAdapter.cpp:68` — 输入路由 |
| `UIRenderer::routeUIInput()` | `src/ui/UIRenderer.cpp:298` — 输入分发 |

---

### 文件结构总览

| 目录/模块 | 文件数 | 说明 |
|-----------|--------|------|
| `src/ui/UIWidget.h` | 1 | 控件基类 |
| `src/ui/UI*.{h,cpp}` | ~30 | 核心框架 + 基础控件 |
| `src/ui/*Control.{h,cpp}` | ~16 | 游戏特化控件 (HUD/物品栏/合成等) |
| `src/ui/screens/` | 4 | 菜单场景 |
| `src/ui/TextRenderer.{h,cpp}` | 2 | FreeType文本渲染 |
| `src/ui/Tween.h / Easing.h` | 2 | 补间动画 |
| `assets/shaders/ui_color.*` | 2 | UI着色器 |
| `assets/textures/gui/` | ~49 | UI纹理资源 |
| `assets/third_party/imgui/` | ~9 | 调试用ImGui |