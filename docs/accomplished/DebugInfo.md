
建议做一层很薄的 **RenderDoc/KHR_debug 标注层**。RelWithDebInfo 已经定义了 `MECRAFT_DEBUG`，所以这套东西可以只在 Debug/RelWithDebInfo 生效，Release 里编译为空。

**核心办法**

用 OpenGL `KHR_debug`：

- `glPushDebugGroup` / `glPopDebugGroup`：给 pass、子阶段、draw batch 分组
- `glObjectLabel`：给 FBO、texture、buffer、VAO、shader program 命名
- 可选 `glDebugMessageInsert`：在时间线上插入一次性事件，比如“Water MDI commands=37”

RenderDoc 会把这些名字显示在 Event Browser 和 Resource Inspector 里，抓帧会好看很多。

**建议封装**

新增类似：

`src/renderer/debug/RenderDebugLabels.h/.cpp`

接口保持小：

```cpp
namespace renderer::debug {

bool labelsEnabled();

void pushGroup(const char* name);
void popGroup();
void insertEvent(const char* name);

void labelTexture(GLuint id, const char* name);
void labelBuffer(GLuint id, const char* name);
void labelFramebuffer(GLuint id, const char* name);
void labelVertexArray(GLuint id, const char* name);
void labelProgram(GLuint id, const char* name);

class ScopedDebugGroup {
public:
    explicit ScopedDebugGroup(const char* name);
    ~ScopedDebugGroup();
    ScopedDebugGroup(const ScopedDebugGroup&) = delete;
    ScopedDebugGroup& operator=(const ScopedDebugGroup&) = delete;
};

}
```

实现上用：

```cpp
#ifdef MECRAFT_DEBUG
if (glPushDebugGroup) ...
if (glObjectLabel && id != 0) ...
#else
// no-op
#endif
```

不建议每个 drawcall 都插 marker，MDI 后 drawcall 少了，但 marker 太多仍会让 RenderDoc 时间线噪声很大。按 pass/子阶段/批次类型标注即可。

**优先接入点**

1. **Frame 顶层**
   - `RenderScene::renderFrame`
   - label: `Frame.NewPipeline` / `Frame.LegacyPipeline`
   - 带 frame index 更好：`Frame 12345 NewPipeline`

2. **DeferredPipeline pass 顺序**
   - `SkyCapture`
   - `GBuffer`
   - `Shadow`
   - `SSAO`
   - `DeferredLighting`
   - `Reflection`
   - `Cloud`
   - `WaterComposite.PreTAA`
   - `Volumetric`
   - `TemporalResolve`
   - `WaterComposite.PostTAAFallback`
   - `PostProcess`

3. **Terrain / MDI flush**
   - `WorldRenderBuffer::flushOpaque()` → `Terrain.Opaque.MDI commands=N`
   - `flushCutout()` → `Terrain.Cutout.MDI commands=N`
   - `flushTransparent()` → `Terrain.Transparent.MDI commands=N`
   - `flushWater()` → `Water.MDI commands=N`

4. **ShadowPass**
   - `Shadow.CSM.Cascade0.Opaque`
   - `Shadow.CSM.Cascade0.Transparent`
   - `Shadow.CSM.Cascade1...`
   - 透明阴影尤其值得标注，因为水/玻璃/叶子都容易混。

5. **WaterCompositePass**
   - 外层：`WaterComposite.PreTAA` / `WaterComposite.PostTAA`
   - 内层：`WaterComposite.BindInputs`
   - 内层：`WaterComposite.DrawWater.MDI commands=N`
   - fallback：`WaterComposite.DrawWater.CPU draws=N`

6. **资源命名**
   - `DeferredTargets.GBufferColor`
   - `DeferredTargets.GBufferDepth`
   - `DeferredTargets.SceneResolved`
   - `DeferredTargets.TransparentComposite`
   - `ShadowTargets.CSMDepth`
   - `ShadowTargets.CSMDepthAll`
   - `ShadowTargets.CSMTransparentColor0`
   - `WorldRenderBuffer.OpaqueVBO`
   - `WorldRenderBuffer.TransparentVBO`
   - `WorldRenderBuffer.WaterIndirectBuffer`

**开关建议**

做一个运行时开关，避免平时 RelWithDebInfo 抓性能时 marker 太多：

- CMake 宏：`MECRAFT_RENDERDOC_LABELS`
- 或配置项：`debug.renderLabels`
- 推荐默认：`MECRAFT_DEBUG` 下可用，但运行时默认开，后面如果觉得吵再接 Dashboard 开关。

也可以用环境变量最省事：

```cpp
MEC_RENDER_LABELS=0
MEC_RENDER_LABELS=1
```

第一次调用时缓存结果。

**实现顺序**

1. 新增 `RenderDebugLabels` no-op 封装。
2. 替换已有本地 `pushDebugGroup/popDebugGroup`，比如 `FirstPersonHeldItemRenderer`、`HumanoidRenderer`、`HeldItemPreviewControl`。
3. 给 `WorldRenderBuffer` 四个 flush 加 group/event。
4. 给 `WaterCompositePass` 和 `ShadowPass` 加最关键分组。
5. 给 render targets / world buffer 的 GL 对象加 `glObjectLabel`。
6. 抓一次 RenderDoc，看 Event Browser 是否按 pass 树状展开清楚，再决定是否继续细化到更多 pass。

**注意点**

- group 必须 RAII，避免早退导致 `glPopDebugGroup` 不执行。
- label 字符串生命周期没要求，OpenGL 会复制。
- 动态字符串可以用 `std::string`，传 `c_str()` 即可。
- `glObjectLabel` 的 `identifier` 要对应类型：`GL_TEXTURE`、`GL_BUFFER`、`GL_FRAMEBUFFER`、`GL_VERTEX_ARRAY`、`GL_PROGRAM`。
- 不要在每个 chunk/subchunk 上打 group，RenderDoc 会爆炸；最多在 MDI flush 级别标 commands 数量。
- `glDebugMessageCallback` 暂时不是必须；RenderDoc 抓 API error 已经够用了。后续想在控制台输出 GL error，再单独做。