# Kappa 光影包阴影系统技术分析

> 基于 RRe36 的 Kappa Shader 解包文件分析。

---

## 目录

1. [阴影系统总览](#1-阴影系统总览)
2. [Shadow Map 生成](#2-shadow-map-生成)
3. [阴影贴图扭曲 (Distortion Warp)](#3-阴影贴图扭曲-distortion-warp)
4. [偏移与抗锯齿策略 (Bias)](#4-偏移与抗锯齿策略-bias)
5. [可变半影阴影 (VPS)](#5-可变半影阴影-vps)
6. [PCF 阴影滤波](#6-pcf-阴影滤波)
7. [彩色阴影与半透明遮挡](#7-彩色阴影与半透明遮挡)
8. [接触阴影 (Contact Shadows)](#8-接触阴影-contact-shadows)
9. [次表面散射阴影 (SSS)](#9-次表面散射阴影-sss)
10. [水下焦散与吸收](#10-水下焦散与吸收)
11. [云影 (Cloud Shadows)](#11-云影-cloud-shadows)
12. [体积雾中的阴影采样](#12-体积雾中的阴影采样)
13. [半透明几何体的阴影路径](#13-半透明几何体的阴影路径)
14. [性能配置与用户设置](#14-性能配置与用户设置)
15. [关键文件索引](#15-关键文件索引)

---

## 1. 阴影系统总览

Kappa 的阴影系统是单级联 (Single-Cascade) 阴影贴图，**不使用**级联阴影贴图 (CSM)、VSM 或 ESM。核心特性包括：

- **可变半影阴影 (VPS)**: 基于 blocker 搜索估计阴影柔和度
- **R2 准随机序列 PCF**: 高质量的阴影边缘滤波
- **彩色阴影**: 支持半透明方块（彩色玻璃、水等）的颜色透射
- **接触阴影**: 屏幕空间光线步进补充小尺度细节阴影
- **次表面散射**: 薄材质光线透射
- **水下焦散**: 基于折射投影的焦散图案
- **云影**: 体积云投射的动态阴影

### 渲染管线流程

```
1. Shadow Map 生成 (shadow/vertex.vsh + fragment.fsh)
   ├─ 从光源视角渲染场景
   ├─ 应用扭曲 (Warp) 集中分辨率
   ├─ 写入双深度 (shadowtex0=透明, shadowtex1=不透明)
   ├─ 写入彩色遮挡数据 (shadowcolor0)
   └─ 写入辅助数据 (shadowcolor1: 法线/天空遮蔽/材质ID)

2. Shadow Map 后处理 (shadow/comp0.fsh)
   ├─ 计算水吸收 (Beer-Lambert)
   └─ 混合焦散

3. VPS 预计算 (world0/deferred2.fsh)
   ├─ 搜索 blocker 估计半影 sigma
   └─ 存入 colortex3.x

4. 主光照 (world0/deferred3.fsh)
   ├─ PCF 滤波采样阴影贴图
   ├─ 彩色阴影处理
   ├─ 接触阴影补充
   ├─ SSS 光线透射
   └─ 云影叠加

5. 半透明光照 (world0/gbuffers_translucent.fsh)
   └─ 独立的阴影采样路径

6. 体积雾 (world0/composite.fsh)
   └─ 每步雾光线步进采样阴影
```

---

## 2. Shadow Map 生成

### 2.1 顶点着色器 ([program/shadow/vertex.vsh](program/shadow/vertex.vsh))

从光源视角变换顶点：

```glsl
// 世界空间 → 光源视图空间 → 光源裁剪空间
vec4 pos = gl_Vertex;
pos = gl_ModelViewMatrix * pos;
pos.xyz = transMAD(shadowModelViewInverse, pos.xyz);  // → 场景空间
scenePos = pos.xyz;
worldPos = pos.xyz + cameraPosition;

// 风效动画 (树叶等)
#ifdef windEffectsEnabled
    if (windLod && (mc_Entity.x >= 10021 && mc_Entity.x <= 10024)) {
        vec2 windOffset = vertexWindEffect(pos.xyz + cameraPosition, 0.18, 1.0);
        pos.xz += windOffset * occlude;
    }
#endif

pos.xyz = transMAD(shadowModelView, pos.xyz);   // → 光源视图空间
pos = gl_ProjectionMatrix * pos;                 // → 光源裁剪空间
pos.xy = shadowmapWarp(pos.xy, warp);            // 应用扭曲
pos.z *= 0.2;                                    // 深度压缩
```

**深度压缩**: `pos.z *= 0.2` 将正交投影的深度范围压缩到 1/5，提高深度精度分布。

### 2.2 片元着色器 ([program/shadow/fragment.fsh](program/shadow/fragment.fsh))

写入多个 Render Target：

```glsl
layout(location = 0) out vec4 data0;   // → shadowcolor0: 颜色/焦散
layout(location = 1) out vec4 data1;   // → shadowcolor1: 辅助数据
```

**不透明几何体**:
```glsl
data0 = texture(gtexture, uv, -shadowmapTextureMipBias);
data0.rgb *= tint.rgb;
data0.rgb = linearToAP1Albedo(toLinear(data0.rgb));  // sRGB → 线性 AP1
```

**水/冰材质** (matID 102/103):
```glsl
vec3 waves = waterNormal(worldPos, rcp(shadowMapResolution) * warp, matID);
float caustic = projectedCaustic(scenePos, waves, lightDir);
data0 = vec4(caustic * 0.25, 1.0, 1.0, 1.0);  // 焦散存入 R 通道
```

**辅助数据打包**:
```glsl
data1.xy = encodeNormal(normal);              // 编码法线
data1.z  = pack2x8(skyOcclusion, matID/255.0); // 天空遮蔽 + 材质ID
```

### 2.3 Shadow Map 格式

| 纹理 | 格式 | 内容 |
|------|------|------|
| `shadowtex0` | D16 (深度) | 包含透明方块的深度 |
| `shadowtex1` | D16 (深度) | 仅不透明方块的深度 |
| `shadowcolor0` | RGBA16 | 不透明方块反照率 (RGB*4, A=有效标志) |
| `shadowcolor1` | RGBA16 | 法线编码 + 天空遮蔽 + 材质ID |

### 2.4 核心常量 ([lib/shadowconst.glsl](lib/shadowconst.glsl))

```glsl
const int shadowMapResolution   = 2048;     // 可选 512~16384
const float shadowDistance      = 128.0;    // 阴影渲染距离
const float shadowIntervalSize  = 2.0;      // 级联间隔 (单级联)
const float shadowmapDepthScale = 2560.0;   // (2*256) / 0.2
const float shadowmapBias       = 0.08 * (2048.0 / shadowMapResolution);  // 法线偏移基础值
```

---

## 3. 阴影贴图扭曲 (Distortion Warp)

### 3.1 实现 ([lib/light/warp.glsl](lib/light/warp.glsl))

使用径向畸变将阴影贴图分辨率集中在玩家附近：

```glsl
#define shadowmap_bias 0.85

float calculateWarp(in vec2 x) {
    return length(x * 1.169) * shadowmap_bias + (1.0 - shadowmap_bias);
}

vec2 shadowmapWarp(vec2 coord, out float distortion) {
    distortion = calculateWarp(coord);
    return coord / distortion;
}
```

**原理**: `calculateWarp` 返回一个距离中心越远越大的值，除以它后，边缘坐标被"拉回"中心，使得阴影贴图中心（玩家附近）的纹素密度远高于边缘。

**畸变因子范围**:
- 中心 (0,0): `distortion = 0.15` → 分辨率最高
- 边缘 (1,1): `distortion ≈ 1.65` → 分辨率最低

### 3.2 畸变补偿

PCF 滤波半径和深度偏移都需要乘以 `warp` 因子来补偿畸变导致的非均匀纹素大小：

```glsl
// 滤波半径补偿
vec2 offset = R2(i + dither) * softSigma;  // softSigma 已包含 warp 补偿

// 深度偏移补偿
pos.z -= (sigma * warp);  // sigma * warp 确保在不同畸变区域的偏移一致
```

---

## 4. 偏移与抗锯齿策略 (Bias)

Kappa 使用**三层偏移**策略来消除阴影痤疮 (Shadow Acne)：

### 4.1 法线偏移 (Normal-Offset Bias)

在场景空间中沿几何法线偏移阴影采样位置：

```glsl
// deferred2.fsh / deferred3.fsh
vec3 shadowPosition = scenePos;
shadowPosition += GeometryNormal
    * min(0.1 + length(scenePos) / 200.0, 0.5)           // 距离自适应
    * (2.0 - max0(dot(GeometryNormal, lightDir)))          // 法线-光方向夹角
    * log2(max(128.0 - shadowMapResolution * shadowmapDepthScale, euler)) / euler;  // 分辨率自适应
```

设计要点：
- **距离自适应**: `min(0.1 + dist/200, 0.5)` — 远处物体需要更大偏移
- **角度自适应**: `(2 - N·L)` — 掠射角度需要更大偏移
- **分辨率自适应**: `log2(...)` — 高分辨率需要更小偏移

基础法线偏移值 ([lib/shadowconst.glsl](lib/shadowconst.glsl)):
```glsl
const float shadowmapBias = 0.08 * (2048.0 / shadowMapResolution);
```

### 4.2 深度偏移 (Depth Bias)

在裁剪空间中沿 Z 轴应用固定偏移：

```glsl
pos.z -= 0.0012 * (saturate(a / 256.0));  // a = 场景空间距离原点的距离
```

偏移量随距离线性增长 (0~256 范围内)，最大 0.0012。

### 4.3 VPS Sigma 偏移

在 VPS 预计算阶段额外应用法线偏移：

```glsl
vec3 pos = scenePos + vec3(shadowmapBias) * lightDir;
```

### 4.4 几何法线偏移 ([deferred3.fsh](world0/deferred3.fsh):160-163)

```glsl
vec3 RShadow_GetBias(vec3 GeometryNormal, vec3 ShadowPosition, float Distortion) {
    float BiasScale = log2(max(128.0 - shadowMapResolution / 8.0, 4.0)) * 0.35;
    return mat3(shadowProjection) * (mat3(shadowModelView) * GeometryNormal)
         * Distortion * BiasScale;
}
```

---

## 5. 可变半影阴影 (VPS)

### 5.1 原理

VPS (Variable Penumbra Shadows) 通过搜索 blocker（遮挡物）来估计每个像素的阴影柔和度 (sigma)。原理与 PCSS (Percentage Closer Soft Shadows) 类似，但 Kappa 使用自定义实现。

### 5.2 Blocker 搜索 ([deferred2.fsh](world0/deferred2.fsh):154-210)

```glsl
float shadowVPSSigma(sampler2D tex, vec3 scenePos) {
    // 变换到阴影空间
    vec3 pos = scenePos + vec3(shadowmapBias) * lightDir;
    pos = transMAD(shadowModelView, pos);
    pos = projMAD(shadowProjection, pos);
    pos.z *= 0.2;
    pos.z -= 0.0012 * (saturate(a / 256.0));

    // 搜索参数
    const float penumbraScale = tan(radians(0.3 * shadowPenumbraScale));
    float penumbraMax = shadowmapDepthScale * penumbraScale * shadowProjection[0].x;
    float searchRad = min(0.5 * shadowProjection[0].x, penumbraMax / tau);

    // R2 准随机序列搜索 blocker
    for (uint i = 0; i < 6; ++i) {
        vec2 offset = R2((i + dither) * 64.0);
        offset = vec2(cos(offset.x * tau), sin(offset.x * tau)) * sqrt(offset.y);
        vec2 searchPos = posUnwarped + offset * searchRad;
        searchPos = shadowmapWarp(searchPos) * 0.5 + 0.5;

        float depth = texelFetch(tex, ivec2(searchPos * shadowmapSize), 0).x;
        float weight = step(depth, pos.z);  // 仅累加比当前点更近的深度

        depthSum += weight * clamp(depth, minDepth, maxDepth);
        weightSum += weight;
    }

    // sigma = 平均 blocker 深度差 × 半影系数
    float sigma = weightSum > 0.0 ? (pos.z - depthSum) * penumbraMax : 0.0;
    return max0(sigma) * rcp(warp);  // 补偿畸变
}
```

### 5.3 Sigma 的物理含义

- **sigma = 0**: 无 blocker → 锐利阴影（完全在光照中或完全在阴影中）
- **sigma > 0**: 有 blocker → 半影区域，sigma 越大阴影越柔和
- sigma 与 blocker 到接收面的距离成正比，符合物理半影原理

### 5.4 时间稳定性

VPS sigma 使用时间渐变噪声 (ditherGradNoiseTemporal) 作为随机种子，并存储在 `colortex3.x` 中供下一帧使用，提供一定的时间稳定性。

---

## 6. PCF 阴影滤波

### 6.1 核心滤波器 ([deferred3.fsh](world0/deferred3.fsh):121-158)

使用 **R2 准随机序列** 进行 PCF 采样，而非传统的泊松圆盘或网格采样：

```glsl
vec4 shadowFiltered(vec3 pos, float sigma) {
    float softSigma = max(sigma, shadowmapPixel.x * 2.0);  // 最小柔化半径
    float sharpenLerp = saturate(sigma / minSoftSigma);      // 边缘锐化插值

    for (uint i = 0; i < shadowFilterIterations; ++i) {
        vec2 offset = R2((i + dither) * 64.0);
        offset = vec2(cos(offset.x * tau), sin(offset.x * tau)) * sqrt(offset.y);

        vec4 colorSample = GetShadowBilinear(
            pos + vec3(offset * softSigma, 0.0), (1.0 - sharpenLerp));
        TotalShadow += colorSample;
        colorMin = min(colorMin, colorSample.rgb);
    }
    TotalShadow /= float(shadowFilterIterations);
    ...
}
```

### 6.2 R2 准随机序列

R2 序列是基于黄金比例的低差异序列，比纯随机采样有更好的覆盖性：

```glsl
// lib/offset/random.glsl
vec2 R2(float n) {
    return fract(vec2(n * 0.7548776662, n * 0.5698402909));
}
```

每个采样点使用极坐标变换 (`cos/sin + sqrt`) 生成均匀圆盘分布。

### 6.3 双线性 PCF 手动实现

对于硬件不支持 `sampler2DShadow` 的情况，Kappa 手动实现双线性 PCF：

```glsl
// gbuffers_translucent.fsh
float textureShadowPCF(sampler2D tex, vec3 pos) {
    vec2 uv = fract(pos.xy - 0.5 * shadowmapPixel);
    ivec2 location = ivec2(uv * shadowmapSize);

    vec4 samples = vec4(
        texelFetch(tex, location, 0).x,
        texelFetch(tex, location + ivec2(1, 0), 0).x,
        texelFetch(tex, location + ivec2(0, 1), 0).x,
        texelFetch(tex, location + ivec2(1, 1), 0).x
    );
    samples = step(vec4(pos.z), samples);  // 深度比较

    vec2 weights = fract(uv * shadowmapSize);
    return mix(
        mix(samples.x, samples.y, weights.x),
        mix(samples.z, samples.w, weights.x), weights.y
    );
}
```

### 6.4 硬件 PCF (deferred3.fsh)

不透明几何体使用 `sampler2DShadow` 启用硬件 PCF：

```glsl
const bool shadowHardwareFiltering = true;
uniform sampler2DShadow shadowtex0, shadowtex1;
```

配合 `textureGather` 获取 4 个深度比较结果进行手动双线性插值：

```glsl
vec4 GetShadowBilinear(vec3 uv, float SharpenAlpha) {
    vec4 OcclusionSamples = textureGather(shadowtex1, uv.xy, uv.z).wzxy;
    vec4 OcclusionSamples_T = textureGather(shadowtex0, uv.xy, uv.z).wzxy;
    // ... 手动双线性插值 + 颜色混合
}
```

### 6.5 边缘锐化

半影边缘通过 `linStep` 进行锐化，sigma 越小锐化越强：

```glsl
vec2 sharpenBorders = mix(vec2(0.5, 0.6), vec2(0.0, 1.0), sharpenLerp);
float sharpenedShadow = linStep(TotalShadow.a, sharpenBorders.x, sharpenBorders.y);

// 颜色边缘混合 — 减少彩色阴影的颜色溢出
float colorEdgeWeight = saturate(distance(TotalShadow.a, sharpenedShadow));
TotalShadow.rgb = mix(TotalShadow.rgb, colorMin, sqrt(colorEdgeWeight));
```

---

## 7. 彩色阴影与半透明遮挡

### 7.1 双深度比较

Kappa 使用两个深度纹理实现彩色阴影：

- `shadowtex0`: 包含透明方块的深度（水、彩色玻璃等）
- `shadowtex1`: 仅包含不透明方块的深度

```glsl
// comp0.fsh
vec2 depth = vec2(texture(shadowtex0, uv).x, texture(shadowtex1, uv).x);
if (depth.x < depth.y) data0 = albedo * vec4(vec3(0.25), 1.0);
```

当 `shadowtex0 < shadowtex1` 时，说明该位置有透明遮挡物，使用其颜色。

### 7.2 颜色读取

```glsl
vec3 ReadShadowColor(ivec2 Pixel) {
    vec4 Sample = texelFetch(shadowcolor0, Pixel, 0);
    return mix(vec3(1.0), Sample.rgb * 4.0, Sample.a);  // A=0 时为白色（无遮挡）
}
```

`shadowcolor0` 中存储的颜色乘以 4.0 以恢复亮度，Alpha 通道作为有效标志。

### 7.3 颜色边缘处理

在 PCF 滤波中，颜色边缘通过 `colorMin` 和 `colorEdgeWeight` 混合来减少颜色溢出：

```glsl
float colorEdgeWeight = saturate(distance(TotalShadow.a, sharpenedShadow));
TotalShadow.rgb = mix(TotalShadow.rgb, colorMin, sqrt(colorEdgeWeight));
```

---

## 8. 接触阴影 (Contact Shadows)

### 8.1 原理 ([lib/light/contactShadow.glsl](lib/light/contactShadow.glsl))

屏幕空间光线步进，用于补充阴影贴图无法捕捉的小尺度细节（如砖块缝隙、微小几何体）。

### 8.2 实现

```glsl
float getContactShadow(sampler2D depth, vec3 viewPos, float dither,
                       float sceneDepth, float nDotV, vec3 lightDir) {
    const uint steps = 16;
    const uint stride = 4;

    vec3 screenPos = viewToScreenSpace(viewPos, true);

    // 计算屏幕空间步进方向
    vec3 rStep = viewPos + abs(viewPos.z) * lightDir;
    rStep = viewToScreenSpace(rStep, true) - screenPos;
    rStep *= minOf((step(0.0, rStep) - screenPos) / rStep);
    rStep *= rcp(abs(abs(rStep.x) < abs(rStep.y) ? rStep.y : rStep.x));

    // 光线步进
    for (uint i = 0; i < maxIterations && !hit; ++i) {
        float pixelSteps = float(i * stride) + noise;
        screenPos = startPos + pixelSteps * rStep;

        float d = texelFetch(depth, ivec2(screenPos.xy * ResolutionScale), 0).x;
        float ascribedD = AscribeDepth(d, 1e-2 * ...);

        // 双重深度比较 (原始 + 线性化)
        hit = maxZ >= d && minZ <= ascribedD
           && maxZ >= dInterp && minZ <= ascribedDInterp
           && d > 0.65 && d < 1.0;
    }
    return float(!hit);
}
```

### 8.3 关键技术点

- **AscribeDepth**: 来自 Spectrum (Zombye)，扩展深度比较阈值以减少自阴影
- **双深度比较**: 同时使用原始深度和线性化深度，提高鲁棒性
- **步长**: 16 步 × 4 像素步幅 = 最远 64 像素
- **蓝噪声抖动**: 使用蓝噪声减少步进图案化

---

## 9. 次表面散射阴影 (SSS)

### 9.1 实现 ([deferred3.fsh](world0/deferred3.fsh):196-267)

SSS 分为两部分：标准漫反射阴影 + 光线透射。

```glsl
shadowData getShadowSubsurface(bool diffLit, vec3 scenePos, float sigma,
                                vec3 viewDir, vec3 albedo, float opacity) {
    // 1. 标准漫反射阴影 (正面受光面)
    if (diffLit) {
        pos.z -= (sigma * warp);
        data.color = shadowFiltered(pos, sigma).rgb;
    }

    // 2. 光线透射 (薄材质背面)
    float sssRad = 0.001 * sqrt(opacity);  // SSS 半径与透明度相关

    for (int i = 0; i < 5; i++) {
        vec3 offset = vec3(noise, -bluenoise) * offsetMult;
        float falloff = sqr(rcp(1.0 + length(offset)));
        offset.xy *= rcp(warp);  // 补偿畸变
        offset *= sssRad;

        // 在偏移位置采样阴影贴图 (模拟光线穿透)
        float sssShadowTemp = texture(shadowtex1, pos + vec3(offset.xy, offset.z));
        sssShadowTemp += texture(shadowtex1, pos + vec3(-offset.xy, offset.z));
        sssShadowTemp *= falloff;

        // 读取遮挡物颜色
        vec4 colorSample = texture(shadowcolor0, pos.xy + offset.xy);
        colorSum += colorSample.a > 0.1 ? colorSample.rgb : vec3(1.0);

        offsetMult += rStep;
    }

    // 3. Mie 相位函数调制
    vec3 scattering = ... * mix(mieHG(dot(viewDir, lightDirView), 0.65), 1.2, 0.4);

    data.subsurfaceScatter = scattering * colorSum * (sqrt2 * rpi * opacity);
}
```

### 9.2 SSS 材质模式

通过 `subsurfaceScatterMode` 控制：

| 模式 | 行为 |
|------|------|
| 0 | 关闭 SSS |
| 1 | 仅硬编码材质 (matID 2=树叶, 4=花草) |
| 2 | 硬编码 + labPBR opacity 通道 (默认) |
| 3 | 仅 labPBR opacity 通道 |

---

## 10. 水下焦散与吸收

### 10.1 焦散生成 ([shadow/fragment.fsh](program/shadow/fragment.fsh):91-125)

在阴影贴图生成阶段计算水表面法线，然后使用折射投影计算焦散：

```glsl
float projectedCaustic(vec3 pos, vec3 normal, vec3 lightDir) {
    vec3 dPdx = dFdx(pos);
    vec3 dPdy = dFdy(pos);
    float num = dotSelf(dPdx) * dotSelf(dPdy);

    vec3 refractLight = refract(-lightDir, normal, rcp(1.33));  // 水折射率 1.33
    dPdx += 2.0 * dFdx(refractLight);
    dPdy += 2.0 * dFdy(refractLight);

    float denom = dotSelf(dPdx) * dotSelf(dPdy);
    return sqrt(num * rcp(denom));  // 面积比 = 焦散强度
}
```

**原理**: 折射改变了光线的面积，面积缩小的地方光强增大，形成焦散。通过屏幕空间导数 (`dFdx/dFdy`) 计算折射前后的面积比。

### 10.2 水吸收 ([shadow/comp0.fsh](program/shadow/comp0.fsh):120-136)

使用 Beer-Lambert 定律计算水体吸收：

```glsl
float surfaceDist = distance(scenePos0, scenePos1);  // 透明面到不透明面的距离
vec3 absorb = exp(-max0(surfaceDist * waterDensity) * waterAbsorbCoeff);
absorb *= mix(caustic, 1.0, exp(-max0(surfaceDist) * sqrt2));

data0 = vec4(absorb * 0.25, 1.0);
```

`waterAbsorbCoeff` 在 [lib/atmos/waterConst.glsl](lib/atmos/waterConst.glsl) 中定义，包含 RGB 三通道的不同吸收系数。

---

## 11. 云影 (Cloud Shadows)

### 11.1 生成 ([world0/prepare2.fsh](world0/prepare2.fsh))

云影贴图渲染在天空盒捕获纹理 (`colortex5`) 的底部区域：

```glsl
#define cloudShadowmapRenderDistance 8e3
#define cloudShadowmapResolution 512

generateCloudShadowmap(vec2 uv, vec3 lightDir, float dither) {
    // 将阴影贴图 UV 转换为世界位置
    vec3 worldPos = transMAD(shadowModelViewInverse, shadowViewPos);

    // 体积云阴影步进 (40 步)
    for (int i = 0; i < 40; ++i) {
        // 采样体积云密度
        // 累积透射率
    }

    // 卷积云和卷层云阴影
    transmittance = linStep(transmittance, 0.05, 1.0);
}
```

### 11.2 采样

所有阴影消费 Pass 通过统一的 `readCloudShadowmap` 读取：

```glsl
float readCloudShadowmap(sampler2D shadowmap, vec3 position) {
    position = mat3(shadowModelView) * position;
    position /= cloudShadowmapRenderDistance;
    position.xy = position.xy * 0.5 + 0.5;
    position.xy /= vec2(1.0, 1.0 + (1.0 / 3.0));
    return texture(shadowmap, position.xy).a;
}
```

---

## 12. 体积雾中的阴影采样

### 12.1 实现 ([world0/composite.fsh](world0/composite.fsh):210-351)

在体积雾光线步进的每一步中采样阴影贴图：

```glsl
// 预计算阴影空间的起止位置和步进向量
vec3 shadowStartPos = projMAD(shadowProjection, transMAD(shadowModelView, startPos));
shadowStartPos.z *= 0.2;
vec3 shadowStep = (shadowEndPos - shadowStartPos) / float(steps);

do {
    shadowPos += shadowStep;

    // 应用扭曲
    vec3 shadowCoord = vec3(shadowmapWarp(shadowPos.xy), shadowPos.z) * 0.5 + 0.5;

    // 双深度采样
    float shadow0 = texture(shadowtex0, shadowCoord);  // 透明深度
    float shadow = 1.0;
    vec3 shadowCol = vec3(1.0);

    if (shadow0 < 1.0) {
        shadow = texture(shadowtex1, shadowCoord);  // 不透明深度
        if (abs(shadow - shadow0) > 0.1) {
            shadowCol = shadowColorSample(shadowcolor0, shadowCoord.xy);  // 彩色阴影
        }
    }

    // 云影叠加
    shadow *= readCloudShadowmap(colortex5, rPos);

    // 累积散射
    scattering[0] += (sunScatter * shadowCol * transmittance) * shadow;
} while (++i < steps);
```

### 12.2 水下体积雾

水下体积雾 ([volumetricWater](world0/composite.fsh):358+) 使用相同的阴影采样模式，但步数和裁剪距离更小。

---

## 13. 半透明几何体的阴影路径

### 13.1 顶点阶段 ([gbuffers_translucent.vsh](world0/gbuffers_translucent.vsh):70-85)

半透明几何体在**顶点着色器**中计算阴影位置（不透明几何体在 deferred 阶段计算）：

```glsl
void getShadowmapPos(vec3 scenePos) {
    scenePos += vec3(shadowmapBias) * lightDir;  // 法线偏移
    vec3 pos = transMAD(shadowModelView, scenePos);
    pos = projMAD(shadowProjection, pos);
    pos.z *= 0.2;
    pos.z -= 0.0012 * (saturate(length(scenePos) / 256.0));

    float warp = 1.0;
    shadowPosition.xy = shadowmapWarp(pos.xy, warp);
    shadowPosition = pos * 0.5 + 0.5;
    shadowWarp = warp;
}
```

### 13.2 片元阶段 ([gbuffers_translucent.fsh](world0/gbuffers_translucent.fsh):263-315)

使用**手动 PCF**（无硬件 `sampler2DShadow`）：

```glsl
vec4 shadowFiltered(vec3 pos, float sigma) {
    for (uint i = 0; i < shadowFilterIterations; ++i) {
        vec2 offset = R2((i + dither) * 64.0);
        offset = vec2(cos(offset.x * tau), sin(offset.x * tau)) * sqrt(offset.y);

        // 手动双线性 PCF
        vec2 samples = vec2(
            textureShadowPCF(shadowtex0, pos + vec3(offset * softSigma, 0.0)),
            textureShadowPCF(shadowtex1, pos + vec3(offset * softSigma, 0.0))
        );

        shadowSum += samples.y;

        // 彩色阴影处理
        if (samples.x < samples.y) {
            vec4 colorSample = texture(shadowcolor0, pos.xy + offset * softSigma);
            colorSum += colorSample.rgb * 4.0;
        }
    }
    // ... 边缘锐化 + 颜色混合
}
```

### 13.3 与不透明路径的区别

| 特性 | 不透明 (deferred3) | 半透明 (gbuffers_translucent) |
|------|-------------------|------------------------------|
| 阴影位置计算 | deferred 阶段 (片元) | 顶点着色器 |
| PCF 方式 | 硬件 `sampler2DShadow` + `textureGather` | 手动 `texelFetch` + `step` |
| VPS 预计算 | 独立 Pass (deferred2) | 片元着色器内联计算 |
| 彩色阴影 | 支持 (双深度比较) | 支持 (双深度比较) |

---

## 14. 性能配置与用户设置

### 14.1 性能预设 ([shaders.properties](shaders.properties))

| 预设 | 分辨率 | PCF 迭代 | VPS | 接触阴影 | 云影 |
|------|--------|----------|-----|----------|------|
| Low | 1024 | 9 | 关 | 关 | 关 |
| Medium | 1536 | 12 | 关 | 开 | 关 |
| High | 2048 | 12 | 开 | 开 | 关 |
| Ultra | 2048 | 15 | 开 | 开 | 关 |
| Extreme | 4096 | 15 | 开 | 开 | 开 |

### 14.2 用户可调参数 ([settings.glsl](settings.glsl))

| 参数 | 默认值 | 范围 | 说明 |
|------|--------|------|------|
| `shadowMapResolution` | 2048 | 512~16384 | 阴影贴图分辨率 |
| `shadowPenumbraScale` | 1.0 | 0.1~3.0 | 半影模糊半径缩放 |
| `shadowVPSEnabled` | 开 | 开/关 | 可变半影阴影 |
| `shadowFilterIterations` | 15 | 6~30 | PCF 采样数 |
| `contactShadowsEnabled` | 开 | 开/关 | 接触阴影 |
| `subsurfaceScatterMode` | 2 | 0~3 | SSS 模式 |
| `subsurfaceScatteringEnabled` | 开 | 开/关 | SSS 总开关 |

---

## 15. 关键文件索引

| 主题 | 文件路径 |
|------|----------|
| **阴影常量** | `lib/shadowconst.glsl` |
| **阴影扭曲** | `lib/light/warp.glsl` |
| **接触阴影** | `lib/light/contactShadow.glsl` |
| **Shadow Map 顶点** | `program/shadow/vertex.vsh` |
| **Shadow Map 片元** | `program/shadow/fragment.fsh` |
| **Shadow Map 后处理** | `program/shadow/comp0.fsh` |
| **VPS 预计算** | `world0/deferred2.fsh` |
| **主阴影采样 + 光照** | `world0/deferred3.fsh` |
| **半透明阴影** | `world0/gbuffers_translucent.fsh` + `.vsh` |
| **体积雾阴影** | `world0/composite.fsh` |
| **云影生成** | `world0/prepare2.fsh` |
| **用户设置** | `settings.glsl` |
| **内部常量** | `lib/internal.glsl` |
