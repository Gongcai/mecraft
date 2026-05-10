# Kappa 光影包 HDR 渲染、色调映射与颜色分级技术分析

> 基于 RRe36 的 Kappa Shader 解包文件分析，参考 ACES v1.2 参考实现。

---

## 目录

1. [整体渲染管线概览](#1-整体渲染管线概览)
2. [HDR 渲染实现](#2-hdr-渲染实现)
3. [自动曝光系统](#3-自动曝光系统)
4. [Bloom 泛光系统](#4-bloom-泛光系统)
5. [色调映射 (Tonemapping)](#5-色调映射-tonemapping)
6. [颜色分级 (Color Grading)](#6-颜色分级-color-grading)
7. [色彩空间与线性工作流](#7-色彩空间与线性工作流)
8. [最终输出与抖动](#8-最终输出与抖动)
9. [关于 3D LUT](#9-关于-3d-lut)
10. [关键文件索引](#10-关键文件索引)

---

## 1. 整体渲染管线概览

Kappa 光影的后处理管线遵循标准的 HDR 渲染流程：

```
场景渲染 (HDR, 线性色彩空间, AP1/ACEScg)
    │
    ├─ Bloom 下采样 + 亮度降采样
    │
    ▼
┌─────────────────────────────────────┐
│  grading.fsh (核心后处理 Pass)       │
│                                     │
│  1. 读取 HDR 场景颜色               │
│  2. 混合 Bloom                      │
│  3. 应用 Purkinje 效应              │
│  4. 计算并应用曝光                   │
│  5. 颜色分级 (振动/饱和度/通道亮度) │
│  6. 暗角                            │
│  7. 胶片模拟 (RFilmEmulation)       │
│  8. 色调映射 (HDR → LDR)           │
│  9. 亮度/对比度 + Gamma 曲线        │
│  10. 输出 LDR                       │
└─────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────┐
│  final.fsh (最终输出 Pass)           │
│  - CAS 锐化                         │
│  - Bayer 抖动                       │
│  - 位深量化                         │
└─────────────────────────────────────┘
```

---

## 2. HDR 渲染实现

### 2.1 Render Target 布局

Kappa 使用多个浮点 Render Target 来存储 HDR 数据。根据 `lib/pipeline.glsl` 的定义：

- **colortex0**: 主场景颜色，RGBA16F 或 R11G11B10F 格式，存储 HDR 线性场景颜色
- **colortex3**: 用于 Bloom 和亮度降采样，Alpha 通道存储逐 tile 亮度值
- **colortex6**: 时间数据，Alpha 通道存储上一帧的曝光值

### 2.2 线性颜色工作流

整个渲染管线工作在线性色彩空间中。从 GBuffer 阶段开始，所有颜色数据通过 `convertToPipelineColor()` 从 sRGB 转换到 ACEScg (AP1) 色彩空间：

```glsl
// lib/util/colorspace.glsl
void convertToPipelineColor(inout vec3 x) {
    x = toLinear(x) * CT_sRGB_AP1;  // sRGB → linear → AP1
}
```

其中 `toLinear()` 实现标准 sRGB EOTF 逆变换：

```glsl
vec3 toLinear(vec3 x) {
    vec3 temp = mix(x / 12.92, pow(.947867 * x + .0521327, vec3(2.4)), step(0.04045, x));
    return max(temp, 0.0);
}
```

### 2.3 渲染精度

HDR 数据使用 16 位浮点 (half) 精度存储，最大值 65504.0。代码中频繁使用 `clamp16F()` 确保不溢出：

```glsl
const float half_max = 65504.0;
```

---

## 3. 自动曝光系统

曝光计算在 **顶点着色器** `exposure.vsh` 中完成（利用顶点着色器的轻量级特性），结果通过 `flat out float exposure` 传递给片元着色器。

### 3.1 亮度采集

场景亮度通过降采样 tile 系统获取。在 `bloom_0.fsh` 中，每 4x4 像素块计算一次平均亮度，存储在 `colortex3` 的 Alpha 通道中：

```glsl
// bloom_0.fsh - 4x4 亮度降采样
float getLuminance4x4(sampler2D tex) {
    uvec2 UV = uvec2(floor(gl_FragCoord.xy) * downsampleScale);
    float lumaSum = 0.0;
    uint samples = 0;
    for (uint x = 0; x < downsampleScale.x; ++x) {
        for (uint y = 0; y < downsampleScale.y; ++y) {
            uvec2 pos = (UV + ivec2(x, y));
            lumaSum += min(getLuma(texelFetch(tex, ivec2(pos), 0).rgb), 128.0);
            ++samples;
        }
    }
    return lumaSum / max(samples, 1);
}
```

### 3.2 两种曝光计算模式

通过 `exposureComplexEnabled` 宏切换：

#### 简单模式
对所有 tile 做带中心偏权的加权平均：

```glsl
float weight = 1.0 - linStep(length(uv * 2.0 - 1.0), 0.25, 0.75);
weight = cubeSmooth(weight) * 0.9 + 0.1;  // 中心权重更高
averageLuminance += currentLuminance * weight;
```

#### 复杂模式（三区域分析）
将 tile 分为三个区域，分别计算加权亮度：

1. **高于平均** (aboveAverage): 高光区域
2. **低于平均** (belowAverage): 暗部区域
3. **平均范围内** (withinAverage): 中间调区域

```glsl
float weightedLuma  = withinAverageData.x * areaPercentages.x * exposureAverageWeight;
weightedLuma       += aboveAverageData.x * areaPercentages.y * exposureBrightWeight;
weightedLuma       += belowAverageData.x * areaPercentages.z * exposureDarkWeight;
weightedLuma       /= areaPercentages.x * exposureAverageWeight
                    + areaPercentages.y * exposureBrightWeight
                    + areaPercentages.z * exposureDarkWeight;
```

### 3.3 曝光值计算

使用基于 EV (Exposure Value) 的映射曲线，将亮度映射到曝光乘数：

```glsl
const float K   = 14.0;                              // 校准常数
const float cal = exp2(autoExposureBias) * K / 100.0;
const float minExposure = exp2(autoExposureBias) / exposureHighClamp;
const float maxExposure = exp2(autoExposureBias) / exposureLowClamp;
const float a = cal / minExposure;
const float b = a - cal / maxExposure;

float targetExp = cal / (a - b * exp(-lum / b));     // S 曲线映射
```

### 3.4 时间平滑

曝光值使用指数衰减进行时间平滑，且**暗化速度快于亮化**（模拟人眼适应）：

```glsl
float decaySpeed = targetExp < lastExp ? 0.075 : 0.05;  // 暗化更快
return mix(lastExp, targetExp, saturate(decaySpeed * exposureDecay * (frameTime / 0.033)));
```

### 3.5 维度特化

不同维度有不同的曝光范围限制：

| 维度 | 暗部限制 | 亮部限制 | 曝光后缩放 |
|------|----------|----------|------------|
| 主世界 (0) | 0.08 | 40.0 | 1.0 |
| 下界 (-1) | 0.1 | 8.0 | 0.66 |
| 末地 (1) | 0.1 | 20.0 | 1.0 |

### 3.6 曝光应用

在 `grading.fsh` 中，曝光值乘以场景 HDR 颜色：

```glsl
#ifdef LOCAL_EXPOSURE
    float exposure = CalculateExposure(uv);  // 局部曝光
#endif
#ifdef manualExposureEnabled
    sceneHDR *= rcp(manualExposureValue);     // 手动 EV 值
#else
    sceneHDR *= exposure * exposureBias;      // 自动曝光 + 偏移
#endif
```

---

## 4. Bloom 泛光系统

### 4.1 架构

三 Pass 可分离高斯 Bloom，基于 chocapic13 的下采样方法：

```
场景 (colortex0)
    │
    ▼ Pass 0: bloom_0.fsh
    ├─ 下采样到 1:4 + 亮度 4x4 降采样
    │
    ▼ Pass 1: bloom_1.fsh
    ├─ 水平高斯模糊 (多尺度)
    ├─ 垂直高斯模糊 (多尺度)
    │
    ▼ Pass 2: bloom_2.fsh
    ├─ 7 级 mip 上采样合并 (1:4 ~ 1:256)
    └─ 曝光高斯平滑
```

### 4.2 下采样

使用 13-tap 加权下采样滤波器（基于 chocapic13 方法），权重分布：

```
权重 0.5  : 4 个对角相邻像素
权重 0.25 : 4 个正交相邻像素
权重 0.125: 4 个远对角 + 1 个中心像素
```

```glsl
// bloom_0.fsh
vec4 blur  = textureLod(coltex, qrescoord-1.0*offset, 0) * 0.5;   // 对角
blur      += textureLod(coltex, qrescoord+2.0*offset, 0) * 0.25;   // 正交
blur      += textureLod(coltex, qrescoord+2.0*diag,   0) * 0.125;  // 远角
blur      /= 4.0;
```

### 4.3 多尺度上采样合并

在 `bloom_2.fsh` 中，使用双三次插值从 7 个 mip 级别采样并平均：

```glsl
bloomData.rgb += textureBicubic(colortex3, uv / 4.0).rgb  / 4.0;    // 1:4
bloomData.rgb += textureBicubic(colortex3, uv / 8.0).rgb  / 4.0;    // 1:8
bloomData.rgb += textureBicubic(colortex3, uv / 16.0).rgb / 4.0;    // 1:16
bloomData.rgb += textureBicubic(colortex3, uv / 32.0).rgb / 4.0;    // 1:32
bloomData.rgb += textureBicubic(colortex3, uv / 64.0).rgb / 4.0;    // 1:64
bloomData.rgb += textureBicubic(colortex3, uv / 128.0).rgb / 4.0;   // 1:128
bloomData.rgb += textureBicubic(colortex3, uv / 256.0).rgb / 4.0;   // 1:256
bloomData.rgb /= 7.0;
```

### 4.4 Bloom 混合

在 `grading.fsh` 中，Bloom 在色调映射**之前**混合到 HDR 场景：

```glsl
float bloomInt = 0.13 / mix(max(exposure, 1.0), 1.0, 0.31);  // 受曝光调制
bloomInt *= bloomIntensity;

// 水下增强
if (isEyeInWater == 1) bloomInt = mix(bloomInt, 1.0, 0.4);

// 雾感知 Bloom
#ifdef bloomyFog
    bloomInt = mix(0.9, bloomInt, avgOf(fogData));
#endif

sceneHDR = mix(sceneHDR, bloom, saturate(bloomInt));
```

### 4.5 分辨率限制

Bloom 分辨率上限为 `min(1080, viewHeight)`，保持宽高比，定义在 `shaders.properties` 中。

---

## 5. 色调映射 (Tonemapping)

Kappa 提供 **4 种色调映射算子**，通过 `tonemapOperator` 宏选择：

```glsl
#define tonemapOperator ACES_AP1_SRGB
//[ACES_AP1_SRGB ACES_AP1_SRGB_RRT hejlBurgessAP1 tonemapReinhardACES]
```

### 5.1 ACES AP1 Custom（默认）

**文件**: `lib/academy/aces.glsl`

这是 Kappa 默认的色调映射器，基于 ACES 但使用自定义的分段样条曲线替代标准 RRT：

```glsl
vec3 ACES_AP1_SRGB(vec3 AP1) {
    AP1   *= acesRRTExposureBias;
    AP1    = ACES_CompressionLMT(AP1);     // 亮色压缩
    vec3 ACES = AP1 * AP1_AP0;              // AP1 → AP0
    ACES   = academyCustom(ACES);           // 自定义 RRT
    return OutputGamutTransform(ACES);      // 输出色彩空间转换
}
```

#### 5.1.1 RRT Sweeteners（参考渲染变换甜化器）

在应用色调曲线之前，先执行一系列"甜化"操作：

```glsl
vec3 rrtSweeteners(vec3 ACES2065) {
    // 1. Glow 模块 - 基于饱和度和亮度的辉光
    float s = sigmoidShaper((saturation - 0.4) / 0.2);
    float addedGlow = 1.0 + glowFwd(ycIn, rrtGlowGain * s, rrtGlowMid);
    ACES2065 *= addedGlow;

    // 2. Red Modifier - 红色色相修正
    float hueWeight = cubicBasisShaper(centeredHue, rrtRedWidth);
    ACES2065.r += hueWeight * saturation * (rrtRedPrivot - ACES2065.r) * (1.0 - rrtRedScale);

    // 3. AP0 → AP1 转换
    vec3 ACEScg = ACES2065 * AP0_AP1;

    // 4. 全局去饱和
    ACEScg = mix(vec3(dot(ACEScg, AP1_RGB_Y)), ACEScg, rrtSatFactor);

    // 5. Gamma 提升（色调映射前的响应调优）
    ACEScg = pow(ACEScg, vec3(rrtGammaLift));

    return ACEScg;
}
```

#### 5.1.2 自定义分段样条曲线

替代标准 RRT 的 `segmentedSplineC5Fwd`，使用有理函数：

```glsl
vec4 splineOperator(vec4 aces, const splineParam param) {
    aces *= 1.313;
    vec4 a = aces * (aces + param.a) - param.b;
    vec4 b = aces * (param.c * aces + param.d) + param.e;
    return clamp(a / b, 0.0, 65535.0);
}

// 参数：a=0.0313, b=0.00006, c=0.983729, d=0.5129510, e=0.168081
// 白点 = pi^4 ≈ 97.409
vec4 mapped = splineOperator(vec4(rgbPre, white), curve);
vec3 mappedColor = mapped.rgb / mapped.a;  // 白点归一化
```

#### 5.1.3 亮色压缩 LMT (Look Modification Transform)

基于 ACES v1.3 的 `ACES_CompressionLMT`，防止高饱和亮色被裁切：

```glsl
vec3 ACES_CompressionLMT(vec3 AP1_CV) {
    float Achromatic = max(AP1_CV.r, max(AP1_CV.g, AP1_CV.b));
    vec3 DistToAch = (Achromatic - AP1_CV) / abs(Achromatic);

    // 各色相独立的压缩阈值和限制
    const float LIM_CYAN    = 1.147, THR_CYAN    = 0.815;
    const float LIM_MAGENTA = 1.264, THR_MAGENTA = 0.803;
    const float LIM_YELLOW  = 1.312, THR_YELLOW  = 0.880;
    const float PWR = 1.2 * sqrt(3);

    // 对每个通道应用软压缩
    vec3 CompressionDist = vec3(
        CompressLMT(DistToAch.r, LIM_CYAN,    THR_CYAN,    PWR),
        CompressLMT(DistToAch.g, LIM_MAGENTA,  THR_MAGENTA, PWR),
        CompressLMT(DistToAch.b, LIM_YELLOW,   THR_YELLOW,  PWR)
    );

    return Achromatic - CompressionDist * abs(Achromatic);
}
```

压缩函数本身：

```glsl
float CompressLMT(float DistToAch, float lim, float thr, float pwr) {
    if (DistToAch >= thr) {
        float scl = (lim - thr) / pow(pow((1.0 - thr) / (lim - thr), -pwr) - 1.0, 1.0 / pwr);
        float nd = (DistToAch - thr) / scl;
        return thr + scl * nd / (pow(1.0 + pow(nd, pwr), 1.0 / pwr));
    }
    return DistToAch;
}
```

### 5.2 ACES AP1 RRT + ODT（完整参考实现）

使用标准 ACES RRT (Reference Rendering Transform) + ODT (Output Device Transform)：

```glsl
vec3 ACES_AP1_SRGB_RRT(vec3 AP1) {
    AP1   *= 1.313 * acesRRTExposureBias;
    vec3 ACES = AP1 * AP1_AP0;
    ACES   = academyRRT(ACES);      // 标准 RRT (C5 分段样条)
    ACES   = odtSRGB_D65(ACES);     // sRGB ODT (C9 分段样条)
    return LinearToSRGB(ACES);
}
```

**RRT** 使用 `segmentedSplineC5Fwd`：6 系数分段样条，工作在 log-log 空间，输入范围 2^-15 ~ 2^18。

**ODT** 使用 `segmentedSplineC9Fwd`：10 系数分段样条，目标 48 nit 影院白点，经过 `darkToDimSurround` 环境适应变换。

### 5.3 Hejl-Burgess

Jim Hejl / Burgess 的电影色调曲线，经典的游戏色调映射选择：

```glsl
vec3 hejlBurgessAP1(vec3 AP1) {
    vec3 ACES = AP1 * AP1_AP0;
    ACES = max(ACES - 0.004, 0.0);
    ACES = (ACES * (6.2 * ACES + 0.5)) / (ACES * (6.2 * ACES + 1.7) + 0.06);
    ACES = pow(ACES, vec3(2.2));     // 反转内置 gamma
    ACES = ACES * AP0_sRGB;
    return LinearToSRGB(ACES);       // 用 sRGB EOTF 重新编码
}
```

### 5.4 Reinhard (ACES 色彩空间)

在 ACES AP0 色彩空间中执行 Reinhard 色调映射，带 gamma 提升：

```glsl
vec3 tonemapReinhardACES(vec3 AP1) {
    float coeff = 0.8;
    AP1 *= 4.24;
    vec3 ACES = AP1 * AP1_AP0;
    ACES = pow(ACES, vec3(0.96));       // 轻微 gamma 提升
    ACES = ACES / (ACES + coeff);       // Reinhard
    ACES = saturate(ACES);
    return ACES * AP0_sRGB;
}
```

### 5.5 TAA/FXAA 中的 Reinhard

在时间抗锯齿和 FXAA Pass 中，使用轻量 Reinhard 在色调映射后的色彩空间中工作，以减少萤火虫噪点：

```glsl
// temporal.fsh / FXAA.fsh
color = color / (color + 1.0);   // 简单 Reinhard
// ... 执行 TAA/FXAA ...
color = color / (1.0 - color);   // 反转
```

---

## 6. 颜色分级 (Color Grading)

所有颜色分级操作在 `grading.fsh` 中执行。处理顺序经过精心设计：

### 6.1 处理流水线

```
HDR 场景颜色
    │
    ├─ [Bloom 混合]
    ├─ [Purkinje 效应]       ← HDR 空间
    ├─ [曝光应用]            ← HDR 空间
    ├─ [振动 + 饱和度]       ← HDR 空间
    ├─ [通道亮度]            ← HDR 空间
    ├─ [暗角]                ← HDR 空间
    ├─ [胶片模拟]            ← HDR 空间
    │
    ├─ ★ 色调映射 ★          ← HDR → LDR 转换点
    │
    ├─ [亮度 + 对比度]       ← LDR 空间
    └─ [Gamma 曲线]          ← LDR 空间
```

### 6.2 振动度与饱和度 (Vibrance + Saturation)

```glsl
vec3 vibranceSaturation(vec3 color) {
    float lum = dot(color, lumacoeffAP1);
    float mn  = min(min(color.r, color.g), color.b);
    float mx  = max(max(color.r, color.g), color.b);

    // 智能振动度：低饱和 + 高亮度区域增强更多
    float sat = (1.0 - saturate(mx - mn)) * saturate(1.0 - mx) * lum * 5.0;
    vec3 light = vec3((mn + mx) / 2.0);

    color = mix(color, mix(light, color, vibranceInt), saturate(sat));
    color = mix(color, light, saturate(1.0 - light) * (1.0 - vibranceInt) / 2.0 * abs(vibranceInt));
    color = mix(vec3(lum), color, saturationInt);  // 全局饱和度

    return color;
}
```

设计要点：
- **振动度** (Vibrance)：保护已高饱和区域和暗部，主要增强低饱和中高亮区域
- **饱和度** (Saturation)：全局线性混合，以 AP1 亮度系数为参考

### 6.3 通道亮度

独立调节 R/G/B 通道的亮度乘数：

```glsl
vec3 rgbLuma(vec3 x) {
    return x * vec3(colorlumR, colorlumG, colorlumB);
}
```

### 6.4 暗角 (Vignette)

基于屏幕空间径向距离的衰减：

```glsl
vec3 vignette(vec3 color) {
    float fade = length(uv * 2.0 - 1.0);
    fade = linStep(abs(fade) * 0.5, vignetteStart, vignetteEnd);
    fade = 1.0 - pow(fade, vignetteExponent) * vignetteIntensity;
    return color * fade;
}
```

### 6.5 胶片模拟 (RFilmEmulation)

自定义的三段式 (Toe/Mid/Shoulder) 胶片响应曲线：

```glsl
vec3 RFilmEmulation(vec3 LinearCV) {
    // Toe 段：暗部提升
    vec3 ToeColor = LinearCV * RFilmToeSlope;

    // Mid 段：中间调
    vec3 MidColor = (LinearCV - MidPoint) * RFilmMidSlope + MidPoint;

    // Toe/Mid 混合
    vec3 ToeAlpha = 1.0 - saturate(LinearCV / ToeLength);
    ToeAlpha = pow(ToeAlpha, RFilmToeRolloff);
    vec3 FinalColor = mix(MidColor * RFilmMidGain, ToeColor, ToeAlpha);

    // Shoulder 段：高光压缩
    FinalColor *= 1.0 / (1.0 + max(FinalColor - MidPoint, 0.0) * RFilmWhiteRolloff * 0.04);

    return FinalColor;
}
```

**参数分主世界和下界两套**：

| 参数 | 主世界 | 下界 |
|------|--------|------|
| ToeSlope | (1.28, 1.21, 1.19) * 1.04 | (1.08, 1.21, 1.29) * 1.56 |
| ToeRolloff | (2.0, 1.8, 1.5) | (1.6, 1.5, 1.35) |
| ToeLength | 0.29 | 0.35 |
| WhiteRolloff | (1.3, 1.7, 2.4) | 同左 |

### 6.6 Purkinje 效应

模拟人眼在暗环境下的视觉特性（暗视觉下蓝色感知增强）：

```glsl
vec3 purkinje(vec3 hdr) {
    const vec3 desatTint = vec3(0.60, 0.70, 1.00);  // 蓝色偏移

    // 转换到 XYZ 色彩空间计算暗视觉亮度
    vec3 xyz = hdr * AP1_XYZ + nightVision * 0.15;
    vec3 scotopicLuminance = xyz * (1.33 * (1.0 + (xyz.y + xyz.z) / xyz.x) - 1.68);

    float purkinje = dot((scotopicLuminance * XYZ_AP1), vec3(0.25, 0.50, 0.25));

    // 暗部混合蓝色调
    hdr = mix(hdr, vec3(purkinje) * desatTint, exp(-75.0 * purkinjeExponent * purkinje));

    // 添加噪声抖动（模拟暗视觉噪点）
    hdr = mix(hdr, (noise * 0.5 + 0.5) * hdr, exp(-50.0 * purkinje));

    return hdr;
}
```

### 6.7 亮度 + 对比度 + Gamma（色调映射后）

```glsl
vec3 brightnessContrast(vec3 color) {
    return (color - 0.5) * constrastInt + 0.5 + brightnessInt;
}

vec3 applyGammaCurve(vec3 x) {
    return pow(x, vec3(gammaCurve));
}
```

---

## 7. 色彩空间与线性工作流

### 7.1 ACES 色彩空间体系

Kappa 实现了完整的 ACES 色彩空间变换矩阵（`lib/academy/transforms.glsl`）：

| 色彩空间 | 用途 |
|----------|------|
| **AP0** (ACES2065-1) | ACES 参考空间，最宽色域 |
| **AP1** (ACEScg) | 工作空间，渲染在此进行 |
| **sRGB** (Rec.709) | 最终显示输出 |
| **P3D65** / **P3DCI** | 广色域显示支持 |
| **Rec.2020** | 超广色域 |
| **Adobe RGB** | 印刷色域 |

关键变换矩阵链：

```
sRGB → XYZ → D65→D60 (Bradford) → AP0   (sRGB_AP0)
AP0  → XYZ → D60→D65 (Bradford) → sRGB   (AP0_sRGB)
AP1  → XYZ → D60→D65 (Bradford) → sRGB   (AP1_sRGB)
```

### 7.2 色域压缩适配

支持多种输出色域，每种使用对应的 EOTF：

```glsl
vec3 OutputGamutTransform(vec3 ACES) {
    switch(currentColorSpace) {
        case COLOR_SPACE_SRGB:      return EOTF_IEC61966(ACES * AP0_sRGB);
        case COLOR_SPACE_DCI_P3:    return EOTF_P3DCI(ACES * AP0_P3DCI);
        case COLOR_SPACE_DISPLAY_P3:return EOTF_IEC61966(ACES * AP0_P3D65);
        case COLOR_SPACE_REC2020:   return EOTF_BT709(ACES * AP0_REC2020);
        case COLOR_SPACE_ADOBE_RGB: return EOTF_Adobe(ACES * AP0_AdobeRGB);
    }
}
```

### 7.3 sRGB EOTF 实现

```glsl
// sRGB 精确传输函数 (IEC 61966-2-1)
vec3 EOTF_IEC61966(vec3 LinearCV) {
    return mix(LinearCV * 12.92,
               clamp(pow(LinearCV, vec3(1.0/2.4)) * 1.055 - 0.055, 0.0, 1.0),
               step(0.0031308, LinearCV));
}
```

---

## 8. 最终输出与抖动

### 8.1 CAS 锐化

最终 Pass 使用 Contrast-Adaptive Sharpening (CAS)：

```glsl
vec3 textureCAS(sampler2D tex, vec2 uv, const float w) {
    // 3x3 采样
    vec3 avg = (tl + tc + tr + ml + mc + mr + bl + bc + br) / 9.0;
    // 对比度计算
    vec3 delta = abs(tl - avg) + abs(tc - avg) + ... ;
    float contrast = 1.0 - getLuma(delta) / 9.0;
    // 锐化
    vec3 color = mc * (1.0 + w * contrast);
    color -= (tc + bc + ml + mr + (tl + tr + bl + br) / 2.0) / 6.0 * w * contrast;
    return max(color, 0.0);
}
```

### 8.2 Bayer 抖动

使用 16x16 Bayer 矩阵进行有序抖动，模拟低位深输出：

```glsl
float bayer16(vec2 c) { return 0.25 * bayer8(0.5 * c) + bayer2(c); }

vec3 ditherImage(vec3 color) {
    const uint bits = uint(pow(2, screenBitdepth));  // 可选 1/2/4/6/8 bit
    vec3 cDither = color;
    cDither *= bits;
    cDither += bayer16(gl_FragCoord.xy) - 0.5;
    return round(cDither) / bits;
}
```

---

## 9. 关于 3D LUT

**Kappa 光影包中没有实现 3D LUT (Look-Up Table)。**

所有颜色分级都是**程序化** (procedural) 的——即通过数学函数实时计算，而非查表。这意味着：

- **优点**：无纹理采样开销，参数连续可调，无量化误差
- **缺点**：无法直接复用影视行业标准 LUT 预设，无法做极其复杂的非线性色彩变换

Kappa 的颜色分级能力通过以下程序化手段实现类似 LUT 的效果：

1. **RFilmEmulation**: 三段式胶片曲线（Toe/Mid/Shoulder）模拟胶片响应
2. **RRT Sweeteners**: ACES 标准的 Glow + Red Modifier + 去饱和
3. **分段样条曲线**: C5 (RRT) 和 C9 (ODT) 分段样条提供精确的色调映射
4. **通道独立控制**: R/G/B 独立亮度、振动度、饱和度

---

## 10. 关键文件索引

| 主题 | 文件路径 |
|------|----------|
| **色调映射 + 颜色分级** | `program/grade/grading.fsh` |
| **曝光计算** | `program/grade/exposure.vsh` |
| **最终输出** | `program/grade/final.fsh` |
| **ACES 主实现** | `lib/academy/aces.glsl` |
| **ACES 色彩空间矩阵** | `lib/academy/transforms.glsl` |
| **ACES 辅助函数** | `lib/academy/functions.glsl` |
| **分段样条曲线** | `lib/academy/spline.glsl` |
| **sRGB/线性转换** | `lib/util/colorspace.glsl` |
| **Bloom 下采样 Pass 0** | `program/post/bloom_0.fsh` |
| **Bloom 模糊 Pass 1** | `program/post/bloom_1.fsh` |
| **Bloom 合并 Pass 2** | `program/post/bloom_2.fsh` |
| **TAA (Reinhard)** | `program/post/temporal.fsh` |
| **FXAA (Reinhard)** | `program/post/FXAA.fsh` |
| **用户设置** | `settings.glsl` |
| **渲染管线文档** | `lib/pipeline.glsl` |
