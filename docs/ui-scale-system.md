# UI Scale System - 实现文档

## 概述

已成功实现**锚点系统（方案2）+ 分层缩放（方案3）混合方案**，这是现代游戏引擎的标准做法（Unity UGUI、Unreal UMG、Godot Control），也是 Minecraft 使用的 GUI Scale 系统。

## 核心组件

### 1. `UIScaleConfig.h` - 缩放配置（新增）

位置：`src/ui/core/UIScaleConfig.h`

#### GUIScale 枚举
```cpp
enum class GUIScale {
    Auto,           // 根据分辨率自动选择
    Small,          // 0.5x
    Normal,         // 1.0x
    Large,          // 2.0x
    ExtraLarge      // 3.0x (4K 显示器)
};
```

#### UIScaleStrategy 枚举
```cpp
enum class UIScaleStrategy {
    None,           // 不缩放（如准星 - 始终像素完美）
    Uniform,        // 统一缩放（如快捷栏、背包）
    TextOnly,       // 仅文本自适应缩放（如控制台）
    PixelPerfect    // 整数缩放（保持锐利）
};
```

#### UIScaleConfig 结构
```cpp
struct UIScaleConfig {
    float autoScale;         // 根据分辨率自动计算的基础缩放
    float guiScale;          // 用户选择的 GUI 缩放倍数
    float effectiveScale;    // 最终缩放 = guiScale
    int virtualWidth;        // 虚拟屏幕宽度（用于布局计算）
    int virtualHeight;       // 虚拟屏幕高度
};
```

**自动缩放分级**：
- 4K+ (≥2160p) → 3.0x
- 2K/1440p → 2.0x
- 1080p/720p → 1.0x
- <720p → 0.5x

### 2. UIRenderContext 增强

新增字段：
```cpp
UIScaleConfig scaleConfig;  // 统一缩放配置
```

新增辅助方法：
```cpp
// 获取锚点在虚拟坐标系中的位置
glm::vec2 getAnchorPosition(Anchor anchor) const;

// 获取特定策略的缩放值
float getScaleForStrategy(UIScaleStrategy strategy) const;
```

### 3. UIWidget 基类扩展

新增字段：
```cpp
UIScaleStrategy scaleStrategy = UIScaleStrategy::Uniform;
```

新增方法：
```cpp
void setScaleStrategy(UIScaleStrategy strategy);
UIScaleStrategy getScaleStrategy() const;
float getEffectiveScale(const UIRenderContext& ctx) const;
```

### 4. UIRenderer 更新

新增方法：
```cpp
void setGUIScale(GUIScale scale);
GUIScale getGUIScale() const;
```

新增成员变量：
```cpp
GUIScale m_guiScale = GUIScale::Auto;
```

## UI 元素缩放策略配置

在 `UIRenderer::init()` 中，各 UI 元素已配置如下策略：

| UI 元素 | 缩放策略 | 说明 |
|---------|---------|------|
| **准星 (Crosshair)** | `None` | 不缩放，始终像素完美，保持肌肉记忆 |
| **快捷栏 (Hotbar)** | `Uniform` | 跟随 GUI Scale 统一缩放 |
| **HUD (血条/饥饿值)** | `Uniform` | 跟随 GUI Scale 统一缩放 |
| **背包界面 (Inventory)** | `Uniform` | 跟随 GUI Scale 统一缩放 |
| **箱子界面 (Chest)** | `Uniform` | 跟随 GUI Scale 统一缩放 |
| **创造模式背包** | `Uniform` | 跟随 GUI Scale 统一缩放 |
| **命令输入 (Console)** | `TextOnly` | 仅文本自适应，忽略用户 GUI Scale |
| **控制台消息** | `TextOnly` | 仅文本自适应，忽略用户 GUI Scale |

## Debug Dashboard 集成

已在 `Dashboard` 中添加 `GUI Scale` 设置面板：

```cpp
void Dashboard::showGUIScaleSettings(UIRenderer& uiRenderer);
```

**功能**：
- 下拉菜单选择 5 档 GUI Scale（Auto/Small/Normal/Large/Extra Large）
- 显示各 UI 元素的缩放策略说明
- 实时生效，无需重启

**位置**：Debug Dashboard → GUI Scale 折叠面板

## 使用示例

### 设置 GUI Scale

```cpp
// 在代码中设置
uiRenderer.setGUIScale(GUIScale::Large);

// 或在 Debug Dashboard 中通过下拉菜单选择
```

### 为新 UI 组件配置缩放策略

```cpp
// 示例：创建一个不缩放的自定义准星
MyCustomCrosshair crosshair;
crosshair.init(resourceMgr);
crosshair.setScaleStrategy(UIScaleStrategy::None);  // 像素完美

// 示例：创建一个跟随 GUI Scale 的血条
MyHealthBar healthBar;
healthBar.init(resourceMgr);
healthBar.setScaleStrategy(UIScaleStrategy::Uniform);  // 统一缩放
```

### 在渲染时获取有效缩放

```cpp
void MyWidget::renderSelf(const UIRenderContext& ctx) const {
    // 方式1：使用组件自己的策略
    float scale = getEffectiveScale(ctx);
    
    // 方式2：直接从策略获取
    float uniformScale = ctx.getScaleForStrategy(UIScaleStrategy::Uniform);
    
    // 方式3：访问完整配置
    int virtualW = ctx.scaleConfig.virtualWidth;
    int virtualH = ctx.scaleConfig.virtualHeight;
}
```

## 向后兼容

保留了旧的 `uiScale` 字段和 `computeResponsiveUiScale()` 方法以确保向后兼容：

```cpp
// 旧代码仍然可以工作
context.uiScale = UIRenderer::computeResponsiveUiScale(actualW, actualH);
context.screenWidth = static_cast<int>(actualW / context.uiScale);
```

但推荐使用新系统：
```cpp
// 新代码使用统一配置
context.scaleConfig = UIScaleConfig::create(actualW, actualH, m_guiScale);
context.screenWidth = context.scaleConfig.virtualWidth;
```

## 技术细节

### 缩放计算流程

1. **输入**：实际窗口分辨率 (actualW × actualH)、用户 GUI Scale 选择
2. **自动缩放计算**：根据分辨率选择基础缩放（0.5x ~ 3.0x）
3. **用户 GUI Scale 应用**：
   - 若选择 `Auto`：使用自动缩放值
   - 否则使用用户选择的固定倍数
4. **虚拟分辨率计算**：
   ```cpp
   virtualWidth = actualW / effectiveScale
   virtualHeight = actualH / effectiveScale
   ```
5. **策略应用**：每个 UI 元素根据自己的 `scaleStrategy` 获取最终缩放

### 锚点系统

`Anchor` 枚举已存在于 `UILayout.h`：
```cpp
enum class Anchor {
    TopLeft, TopCenter, TopRight,
    CenterLeft, Center, CenterRight,
    BottomLeft, BottomCenter, BottomRight
};
```

辅助方法 `UIRenderContext::getAnchorPosition()` 可获取锚点在虚拟坐标系中的位置。

### 坐标系统

- **物理坐标**：窗口实际像素坐标
- **虚拟坐标**：缩放后的逻辑坐标（用于布局计算）
- **转换**：`physicalPos = virtualPos * effectiveScale`

## 优势

### ✅ 符合 Minecraft 传统
- GUI Scale 选项与 Java 版一致
- 玩家熟悉的 5 档缩放选择

### ✅ 灵活性
- 不同 UI 元素可独立配置缩放策略
- 准星不缩放，保持肌肉记忆
- 文本自适应缩放，保证可读性

### ✅ 跨分辨率适配
- 从 720p 到 4K 自动适配
- 虚拟坐标系统简化布局计算
- 支持超宽屏（21:9、32:9）

### ✅ 行业标准
- 与 Unity UGUI、Unreal UMG 设计理念一致
- 锚点 + 缩放策略模式是成熟方案

### ✅ 性能友好
- 简单的数学计算
- 无额外 GPU 开销

### ✅ 可扩展
- 易于添加新的缩放策略
- 支持未来的 DPI 感知功能

## 未来改进方向

### 1. DPI 感知（Windows）
```cpp
// 从窗口获取 DPI
float dpiScale = window.getDpiScale();  // 1.0 = 96dpi, 1.5 = 144dpi, 2.0 = 192dpi

// 融合到自动缩放计算
config.autoScale = UIScaleConfig::computeAutoScale(actualW, actualH) * dpiScale;
```

### 2. Safe Area 支持（超宽屏/刘海屏）
```cpp
struct SafeAreaInsets {
    float top, bottom, left, right;
};

SafeAreaInsets computeSafeArea(float actualW, float actualH) {
    SafeAreaInsets insets{};
    const float aspect = actualW / actualH;
    
    // 超宽屏 (21:9) 添加侧边距
    if (aspect > 2.0f) {
        const float excessWidth = actualW - (actualH * 2.0f);
        insets.left = insets.right = excessWidth * 0.5f;
    }
    
    return insets;
}
```

### 3. 用户自定义 UI 位置
- 允许玩家拖动 UI 元素位置
- 保存到配置文件

### 4. UI 预设模板
```cpp
enum class UILayoutPreset {
    Default,    // 默认布局
    Compact,    // 紧凑布局（竞技玩家）
    Minimal,    // 极简布局（截图/录像）
    Streamer    // 主播布局（摄像头友好）
};
```

## 测试建议

1. **不同分辨率测试**：
   - 720p (1280×720)
   - 1080p (1920×1080)
   - 1440p (2560×1440)
   - 4K (3840×2160)
   - 超宽屏 (3440×1440, 21:9)

2. **GUI Scale 档位测试**：
   - 在每个分辨率下测试 5 档 GUI Scale
   - 验证准星不缩放
   - 验证文本清晰可读

3. **动态分辨率切换**：
   - 运行时改变窗口大小
   - 全屏 ↔ 窗口模式切换

4. **UI 交互测试**：
   - 鼠标点击精度
   - 背包格子对齐
   - 滚动区域边界

## 相关文件

### 新增文件
- `src/ui/core/UIScaleConfig.h` - 缩放配置系统

### 修改文件
- `src/ui/core/UIRenderContext.h` - 添加 `scaleConfig` 和辅助方法
- `src/ui/core/UIWidget.h` - 添加 `scaleStrategy` 字段
- `src/ui/core/UIRenderer.h` - 添加 GUI Scale getter/setter
- `src/ui/core/UIRenderer.cpp` - 实现缩放系统，配置各 UI 元素策略
- `src/ui/Dashboard.h` - 添加 `showGUIScaleSettings()` 声明
- `src/ui/Dashboard.cpp` - 实现 GUI Scale 设置面板

## 总结

该实现成功将固定 1280×720 参考分辨率的旧系统升级为灵活的、策略驱动的缩放系统，同时：

- ✅ **保持向后兼容**
- ✅ **符合 Minecraft 传统**
- ✅ **采用行业标准设计**
- ✅ **支持多样化的 UI 需求**
- ✅ **为未来扩展打好基础**

玩家现在可以通过 Debug Dashboard 或未来的设置界面自由调整 GUI Scale，在任何分辨率下获得舒适的游戏体验。
