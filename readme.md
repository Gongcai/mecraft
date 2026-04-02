# 🎮 Minecraft 风格体素游戏 — 架构与模块设计文档

> **技术栈**: C++17 · OpenGL 3.3 Core · GLFW · GLAD · GLM · stb_image  
> **构建系统**: CMake 4.1  

---

## 目录

1. [项目总体架构](#1-项目总体架构)
2. [核心模块设计](#2-核心模块设计)
   - 2.1 [游戏循环 (Game Loop)](#21-游戏循环)
   - 2.2 [窗口管理 (Window)](#22-窗口管理)
   - 2.3 [输入管理 (Input Manager)](#23-输入管理)
   - 2.4 [渲染引擎 (Renderer)](#24-渲染引擎)
   - 2.5 [资源管理 (Resource Manager)](#25-资源管理)
   - 2.6 [摄像机 (Camera)](#26-摄像机)
   - 2.7 [方块系统 (Block System)](#27-方块系统)
   - 2.8 [区块系统 (Chunk System)](#28-区块系统)
   - 2.9 [区块网格生成 (Chunk Meshing)](#29-区块网格生成)
   - 2.10 [世界管理 (World)](#210-世界管理)
   - 2.11 [地形生成 (Terrain Generation)](#211-地形生成)
   - 2.12 [玩家 (Player)](#212-玩家)
   - 2.13 [物理与碰撞 (Physics & Collision)](#213-物理与碰撞)
   - 2.14 [光照系统 (Lighting)](#214-光照系统)
   - 2.15 [UI / HUD](#215-ui--hud)
   - 2.16 [存档系统 (Save / Load)](#216-存档系统)
3. [推荐目录结构](#3-推荐目录结构)
4. [第三方库依赖](#4-第三方库依赖)
5. [关键数据结构](#5-关键数据结构)
6. [渲染管线流程](#6-渲染管线流程)
7. [开发阶段规划](#7-开发阶段规划)

---

## 1. 项目总体架构

```
┌─────────────────────────────────────────────────────────┐
│                        Game                             │
│  (拥有游戏主循环, 管理所有子系统的生命周期)                   │
└──────┬──────┬──────┬──────┬──────┬──────┬───────┬───────┘
       │      │      │      │      │      │       │
       ▼      ▼      ▼      ▼      ▼      ▼       ▼
   Window  Renderer World  Player Physics  UI   SaveSystem
     │      InputMgr  │      │      │       │
     │        │       │      ▼      │       │
     │        │       │   Camera    │       │
     │        │       │             │       │
     │        ▼       ▼             │       │
     │   ResourceMgr  │             │       │
     │   (Shader,     │             │       │
     │    Texture,    ▼             │       │
     │    Atlas)   TerrainGen       │       │
     │                │             │       │
     │                ▼             │       │
     │         ┌──────────┐         │       │
     │         │  Chunk   │◄────────┘       │
     │         │ (Blocks, │                 │
     │         │  Mesh,   │                 │
     │         │  Light)  │                 │
     │         └──────────┘                 │
     │              │                       │
     │              ▼                       │
     │        ChunkMesher                   │
     │         (网格构建)                    │
     └──────────────────────────────────────┘
           Window 事件流 → InputMgr 事件流
```

### 模块交互关系

| 生产者 | 消费者 | 数据 / 事件 |
|--------|--------|------------|
| `Window` | `Game` | 窗口尺寸变化, 关闭事件 |
| `Window` | `InputManager` | GLFW 原始回调事件 |
| `InputManager` | `Player`, `Camera`, `Game` | 键盘/鼠标状态查询 |
| `TerrainGen` | `Chunk` | 方块数据 (BlockID 三维数组) |
| `Chunk` | `ChunkMesher` | 方块数据 → 顶点数据 |
| `ChunkMesher` | `Renderer` | VAO / VBO (GPU 网格) |
| `Camera` | `Renderer` | View / Projection 矩阵 |
| `Player` | `Camera`, `Physics` | 位置, 速度, 视角 |
| `Physics` | `Player` | 碰撞响应, 地面检测 |
| `World` | `Chunk[]` | 区块加载/卸载调度 |
| `ResourceMgr` | `Renderer` | Shader 程序, 纹理 Atlas |

---

## 2. 核心模块设计

### 2.1 游戏循环

**职责**: 以固定时间步长驱动逻辑更新，以可变帧率驱动渲染。

```
┌─► 计算 deltaTime
│   ├─► accumulator += deltaTime
│   ├─► while (accumulator >= TICK_RATE):   // 固定步长 (1/60s)
│   │       input.update()                  // 刷新输入快照
│   │       processInput(input)
│   │       player.update(TICK_RATE)
│   │       physics.step(TICK_RATE)
│   │       world.update(playerPos)         // 加载/卸载区块
│   │       accumulator -= TICK_RATE
│   ├─► renderer.render(world, camera, interpolation)
│   └─► window.swapBuffers()
└───────────────────────────────────────
```

```cpp
class Game {
public:
    void init(int width, int height, const char* title);
    void run();       // 主循环
    void shutdown();

private:
    static constexpr double TICK_RATE = 1.0 / 60.0;

    Window        m_window;
    InputManager  m_input;    // 独立输入管理器
    Renderer      m_renderer;
    ResourceMgr   m_resources;
    World         m_world;
    Player        m_player;
    Physics       m_physics;
    UI            m_ui;
    SaveSystem    m_save;

    double m_lastFrameTime = 0.0;
};
```

---

### 2.2 窗口管理

**职责**: 封装 GLFW 窗口创建、OpenGL 上下文初始化、缓冲区交换，**不处理输入逻辑**。

```cpp
class Window {
public:
    bool init(int width, int height, const char* title);
    void destroy();

    bool shouldClose() const;
    void swapBuffers();
    void pollEvents();       // 仅驱动 GLFW 事件泵, 由 InputManager 消费

    int getWidth() const;
    int getHeight() const;
    float getAspectRatio() const;
    void setTitle(const std::string& title);

    // 供 InputManager 注册回调时使用
    GLFWwindow* getHandle() const;

private:
    GLFWwindow* m_window = nullptr;
    int m_width, m_height;

    // 窗口级回调 (仅处理窗口本身的事件)
    static void framebufferSizeCallback(GLFWwindow* w, int width, int height);
};
```

**关键设计要点**:
- `Window` 只负责 OpenGL 上下文生命周期和帧缓冲管理
- 通过 `getHandle()` 将 `GLFWwindow*` 暴露给 `InputManager`，由后者注册所有输入回调
- 窗口尺寸变化通过 `framebufferSizeCallback` 更新内部 `m_width / m_height`

---

### 2.3 输入管理

**职责**: 注册并处理所有键盘、鼠标输入回调，提供帧级输入状态查询接口。

```cpp
class InputManager {
public:
    // 传入 Window 句柄以注册 GLFW 回调
    void init(GLFWwindow* windowHandle);

    // 每逻辑帧开始前调用：将当前帧状态备份为"上一帧"
    void update();

    // ── 键盘 ──
    bool isKeyHeld(int key) const;         // 持续按住
    bool isKeyJustPressed(int key) const;  // 本帧刚按下
    bool isKeyJustReleased(int key) const; // 本帧刚释放

    // ── 鼠标按键 ──
    bool isMouseButtonHeld(int button) const;
    bool isMouseButtonJustPressed(int button) const;

    // ── 鼠标移动 ──
    glm::vec2 getMousePosition() const;
    glm::vec2 getMouseDelta() const;       // 本帧相对位移

    // ── 鼠标模式 ──
    void captureMouse(bool capture);       // true → GLFW_CURSOR_DISABLED

private:
    GLFWwindow* m_handle = nullptr;

    // 键盘状态双缓冲
    bool m_keys[GLFW_KEY_LAST + 1]     = {};
    bool m_keysPrev[GLFW_KEY_LAST + 1] = {};

    // 鼠标按键双缓冲
    bool m_mouseButtons[GLFW_MOUSE_BUTTON_LAST + 1]     = {};
    bool m_mouseButtonsPrev[GLFW_MOUSE_BUTTON_LAST + 1] = {};

    // 鼠标位置
    double m_mouseX = 0, m_mouseY = 0;
    double m_lastMouseX = 0, m_lastMouseY = 0;
    bool   m_firstMouse = true;

    // GLFW 回调 (static → 通过 userPointer 转发到实例)
    static void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* w, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* w, double xoffset, double yoffset);
};
```

**关键设计要点**:
- `InputManager::init()` 接收 `Window::getHandle()` 返回的 `GLFWwindow*`，自行调用 `glfwSetWindowUserPointer` 并注册所有输入回调
- 鼠标使用 Raw Input 模式：`captureMouse(true)` 内部调用 `glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED)`
- `update()` 在每个逻辑 tick 开始时将 `m_keys` 拷贝到 `m_keysPrev`，实现"刚按下 / 刚释放"检测
- `Player` 和 `Camera` 只依赖 `InputManager`，不再直接持有 `Window` 引用

**与 Window 的协作时序**:
```
每帧:
  window.pollEvents()      ← 驱动 GLFW 事件泵, 触发回调写入 InputManager 状态
  input.update()           ← 备份上一帧快照
  player.update(dt, input) ← 消费输入状态
```

---

### 2.4 渲染引擎

**职责**: 管理 OpenGL 状态、Shader 绑定、纹理绑定、绘制区块网格、天空盒。

```cpp
class Renderer {
public:
    void init();
    void shutdown();

    void beginFrame(const Camera& camera);   // 设置 VP 矩阵, 清屏
    void renderWorld(const World& world, const Camera& camera);
    void renderUI(const UI& ui);
    void endFrame();

    // 视锥剔除
    void updateFrustum(const glm::mat4& viewProj);
    bool isChunkInFrustum(const glm::vec3& chunkMin, const glm::vec3& chunkMax) const;

private:
    Shader m_chunkShader;
    Shader m_uiShader;

    glm::mat4 m_projection;
    glm::mat4 m_view;

    // 视锥体6个平面
    std::array<glm::vec4, 6> m_frustumPlanes;
};

class Shader {
public:
    void loadFromFile(const std::string& vertPath, const std::string& fragPath);
    void use() const;
    void setMat4(const std::string& name, const glm::mat4& mat) const;
    void setVec3(const std::string& name, const glm::vec3& vec) const;
    void setInt(const std::string& name, int value) const;
    void setFloat(const std::string& name, float value) const;

private:
    GLuint m_programID = 0;
    std::unordered_map<std::string, GLint> m_uniformCache;

    GLuint compileShader(GLenum type, const std::string& source);
    void checkCompileErrors(GLuint shader, const std::string& type);
};
```

**关键设计要点**:
- 启用 **背面剔除** (`GL_CULL_FACE`)、**深度测试** (`GL_DEPTH_TEST`)
- 半透明方块（水、玻璃）单独排序，从远到近渲染 (Alpha Blending Pass)
- Uniform 缓存：首次查询后哈希存储 location，避免重复 `glGetUniformLocation`

---

### 2.5 资源管理

**职责**: 加载和管理纹理、Shader、纹理图集 (Texture Atlas)。

```cpp
class ResourceMgr {
public:
    void init();
    void shutdown();

    Shader& loadShader(const std::string& name, 
                       const std::string& vertPath, 
                       const std::string& fragPath);
    Shader& getShader(const std::string& name);

    GLuint loadTexture(const std::string& path, bool alpha = false);
    GLuint getTexture(const std::string& name) const;

    // 纹理图集
    void buildTextureAtlas(const std::string& directory, int tileSize = 16);
    const TextureAtlas& getAtlas() const;

private:
    std::unordered_map<std::string, Shader> m_shaders;
    std::unordered_map<std::string, GLuint> m_textures;
    TextureAtlas m_atlas;
};

struct TextureAtlas {
    GLuint textureID = 0;
    int atlasWidth  = 0;     // 像素宽度
    int atlasHeight = 0;     // 像素高度
    int tileSize    = 16;    // 每个贴图块的像素尺寸
    int tilesPerRow = 0;

    // 根据 tile 索引计算 UV 坐标 (左下, 右上)
    std::pair<glm::vec2, glm::vec2> getUV(int tileIndex) const;
};
```

**纹理图集工作流**:
1. 将所有方块贴图 (16×16) 排列到一张大纹理上 (如 256×256)
2. 使用 `stb_image` 逐个加载小贴图，拼接到大图缓冲区
3. 上传到 GPU，设置 `GL_NEAREST` 过滤 (像素风格)
4. 渲染时通过 tile 索引计算 UV，避免纹理切换

---

### 2.6 摄像机

**职责**: 第一人称视角，提供 View 和 Projection 矩阵。

```cpp
class Camera {
public:
    Camera(glm::vec3 position = {0, 80, 0}, float yaw = -90.0f, float pitch = 0.0f);

    void processMouseMovement(float xOffset, float yOffset);
    void setPosition(const glm::vec3& pos);

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix(float aspect) const;

    glm::vec3 getPosition() const;
    glm::vec3 getFront() const;
    glm::vec3 getRight() const;
    glm::vec3 getUp() const;

    // 射线拾取 — 返回视线方向
    Ray getPickRay() const;

    float fov   = 70.0f;
    float nearPlane = 0.1f;
    float farPlane  = 500.0f;
    float sensitivity = 0.1f;

private:
    glm::vec3 m_position;
    glm::vec3 m_front;
    glm::vec3 m_up;
    glm::vec3 m_right;
    glm::vec3 m_worldUp = {0, 1, 0};

    float m_yaw, m_pitch;

    void updateVectors();
};
```

---

### 2.7 方块系统

**职责**: 定义方块种类、属性、纹理映射。

```cpp
using BlockID = uint8_t;

// 方块类型枚举
namespace BlockType {
    constexpr BlockID AIR         = 0;
    constexpr BlockID STONE       = 1;
    constexpr BlockID DIRT        = 2;
    constexpr BlockID GRASS       = 3;
    constexpr BlockID SAND        = 4;
    constexpr BlockID WATER       = 5;
    constexpr BlockID WOOD        = 6;
    constexpr BlockID LEAVES      = 7;
    constexpr BlockID COAL_ORE    = 8;
    constexpr BlockID IRON_ORE    = 9;
    constexpr BlockID DIAMOND_ORE = 10;
    constexpr BlockID BEDROCK     = 11;
    // ... 扩展
    constexpr BlockID COUNT       = 64;
}

struct BlockDef {
    const char* name;            // "Grass"
    bool  isSolid       = true;  // 是否可碰撞
    bool  isTransparent = false; // 是否透明 (水、玻璃、树叶)
    bool  isLightSource = false; // 是否发光
    uint8_t lightLevel  = 0;     // 光照强度 0-15

    // 六面纹理的 Atlas tile 索引 (上, 下, 前, 后, 左, 右)
    // 如果六面相同，可全部设为同一值
    int texTop, texBottom, texFront, texBack, texLeft, texRight;
};

class BlockRegistry {
public:
    static void init();   // 注册所有方块定义
    static const BlockDef& get(BlockID id);

private:
    static std::array<BlockDef, BlockType::COUNT> s_blocks;
};
```

**设计要点**:
- `BlockID` 使用 `uint8_t`，单区块 16³ = 4096 只需 4KB
- 六面可以有不同纹理（如草方块：上=草地、侧面=泥土+草边、下=泥土）
- 通过静态注册表查询，避免每个 Block 实例存储属性

---

### 2.8 区块系统

**职责**: 管理固定大小的方块三维数组，维护网格和光照状态。

```cpp
class Chunk {
public:
    static constexpr int SIZE_X = 16;
    static constexpr int SIZE_Y = 256;
    static constexpr int SIZE_Z = 16;

    Chunk(int chunkX, int chunkZ);
    ~Chunk();

    // 方块存取
    BlockID getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, BlockID id);

    // 世界坐标 → 区块内坐标
    static glm::ivec3 worldToLocal(int wx, int wy, int wz);
    // 区块坐标 → 世界坐标
    glm::ivec3 getWorldOffset() const;

    // 网格
    bool isDirty() const;       // 是否需要重建网格
    void markDirty();
    void setMesh(ChunkMesh&& mesh);
    const ChunkMesh& getMesh() const;

    // 光照
    uint8_t getSunlight(int x, int y, int z) const;
    void setSunlight(int x, int y, int z, uint8_t level);
    uint8_t getBlockLight(int x, int y, int z) const;
    void setBlockLight(int x, int y, int z, uint8_t level);

    // 邻居引用 (用于跨区块面剔除和光照传播)
    Chunk* neighbors[4] = {};  // +X, -X, +Z, -Z

    int m_chunkX, m_chunkZ;    // 区块坐标 (非世界坐标)

private:
    // 方块数据 — 平铺一维数组 (index = x + z*SIZE_X + y*SIZE_X*SIZE_Z)
    std::array<BlockID, SIZE_X * SIZE_Y * SIZE_Z> m_blocks;

    // 光照数据 — 每字节存两个 4-bit 值 (高4位=阳光, 低4位=方块光)
    std::array<uint8_t, SIZE_X * SIZE_Y * SIZE_Z> m_lightMap;

    ChunkMesh m_mesh;
    bool m_dirty = true;
};

struct ChunkMesh {
    GLuint vao = 0, vbo = 0;
    uint32_t vertexCount = 0;

    // 透明面单独存储
    GLuint transparentVao = 0, transparentVbo = 0;
    uint32_t transparentVertexCount = 0;

    void upload(const std::vector<BlockVertex>& vertices);
    void uploadTransparent(const std::vector<BlockVertex>& transparentVerts);
    void destroy();
};
```

**内存布局**:
- 方块数据: `16 × 256 × 16 × 1 byte = 64 KB / chunk`
- 光照数据: `16 × 256 × 16 × 1 byte = 64 KB / chunk`
- 总计约 128 KB/chunk，渲染距离 16 chunks → ~1024 个区块 → ~128 MB

---

### 2.9 区块网格生成

**职责**: 将区块方块数据转化为可渲染的顶点数据，实现面剔除。

```cpp
struct BlockVertex {
    // 位置 (3 float) + UV (2 float) + 法线 (压缩为 1 byte) + 光照 (1 byte) + AO (1 byte)
    float x, y, z;
    float u, v;
    uint8_t normal;     // 0-5 对应 6 个面方向
    uint8_t light;      // 高4位阳光 + 低4位方块光
    uint8_t ao;         // 环境光遮蔽等级 0-3
};

class ChunkMesher {
public:
    // 主入口：生成区块网格
    static void generateMesh(Chunk& chunk);

private:
    // 面剔除：只有暴露面 (邻接方块为空气或透明) 才生成
    static bool shouldRenderFace(const Chunk& chunk, int x, int y, int z, 
                                  int nx, int ny, int nz);

    // 向缓冲区添加一个面的 4 个顶点 (6 个索引)
    static void addFace(std::vector<BlockVertex>& vertices,
                        const glm::vec3& pos,
                        int face,           // 0=Top, 1=Bottom, 2=Front, 3=Back, 4=Left, 5=Right
                        const BlockDef& def,
                        const TextureAtlas& atlas,
                        uint8_t lightLevel,
                        uint8_t aoLevel);

    // 计算单个顶点的 AO 等级 (0-3)
    static uint8_t calcAO(bool side1, bool side2, bool corner);
};
```

**面剔除算法**:
```
对于区块中每个非 AIR 方块 (x, y, z):
    对于 6 个面方向 (dx, dy, dz):
        邻居 = getBlock(x+dx, y+dy, z+dz)   // 可能跨区块查询
        如果 邻居是 AIR 或 邻居是透明 且 当前不是透明:
            生成该面的 4 个顶点 + 2 个三角形
```

**AO (Ambient Occlusion) 计算**:
```
对于每个面的 4 个顶点:
    检查该顶点周围的 3 个方块 (side1, side2, corner)
    AO = calcAO(side1.isSolid, side2.isSolid, corner.isSolid)
    // AO=0 最暗, AO=3 最亮
```

**进阶优化 — Greedy Meshing** (可选):
- 将同类型、同光照、同AO的相邻面合并为一个大矩形
- 可减少 50-80% 的顶点数，显著提升渲染性能

---

### 2.10 世界管理

**职责**: 管理所有活跃区块的加载、卸载、查询。

```cpp
class World {
public:
    void init(uint32_t seed);
    void update(const glm::vec3& playerPos);  // 每帧调用

    // 方块操作 (世界坐标)
    BlockID getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, BlockID id);

    // 射线拾取：返回命中的方块位置和放置位置
    bool raycast(const Ray& ray, float maxDist,
                 glm::ivec3& hitBlock, glm::ivec3& placeBlock) const;

    // 获取所有需要渲染的区块
    const auto& getActiveChunks() const { return m_chunks; }

    int getRenderDistance() const { return m_renderDistance; }
    void setRenderDistance(int dist);

private:
    // 区块存储: key = (chunkX, chunkZ) 打包为 int64_t
    std::unordered_map<int64_t, std::unique_ptr<Chunk>> m_chunks;

    TerrainGenerator m_terrainGen;
    int m_renderDistance = 8;   // 以区块为单位
    uint32_t m_seed;

    // 区块坐标打包
    static int64_t chunkKey(int cx, int cz);

    // 加载/卸载
    void loadChunk(int cx, int cz);
    void unloadChunk(int cx, int cz);

    // 区块加载队列 (按距离排序, 近的优先)
    std::vector<glm::ivec2> m_loadQueue;
    void updateLoadQueue(int playerChunkX, int playerChunkZ);
};
```

**区块加载策略**:
1. 每帧计算玩家所在的区块坐标 `(pcx, pcz)`
2. 遍历渲染距离内所有 `(cx, cz)`，未加载的加入 `m_loadQueue`
3. `m_loadQueue` 按到玩家距离升序排序
4. 每帧最多加载 N 个区块 (如 2-4 个)，避免卡顿
5. 超出渲染距离 + 2 的区块卸载 (序列化后释放)

**未来优化**:
- 区块生成/网格构建移至工作线程 (`std::async` 或线程池)

---

### 2.11 地形生成

**职责**: 基于噪声算法程序化生成世界地形。

```cpp
class TerrainGenerator {
public:
    void init(uint32_t seed);

    // 填充一个区块的方块数据
    void generateChunk(Chunk& chunk);

private:
    uint32_t m_seed;
    FastNoiseLite m_continentNoise;   // 大陆轮廓
    FastNoiseLite m_detailNoise;      // 细节起伏
    FastNoiseLite m_caveNoise;        // 洞穴 (3D)
    FastNoiseLite m_biomeNoise;       // 生物群系

    // 计算某 (x, z) 处的地面高度
    int getHeight(int worldX, int worldZ) const;

    // 生物群系判定
    BiomeType getBiome(int worldX, int worldZ) const;

    // 装饰：树木、花草
    void decorateChunk(Chunk& chunk);

    // 洞穴雕刻
    void carveCaves(Chunk& chunk);

    // 矿石分布
    void placeOres(Chunk& chunk);
};

enum class BiomeType {
    Plains,
    Desert,
    Forest,
    Mountains,
    Ocean
};
```

**地形生成流程**:
```
1. 高度图生成:
   height(x,z) = BASE_HEIGHT 
                + continentNoise(x, z) * 40
                + detailNoise(x, z)  * 10

2. 基础填充:
   for y = 0 to height:
       if y == 0:              BEDROCK
       else if y < height - 4: STONE
       else if y < height:     DIRT
       else:                   GRASS (根据biome可能是SAND)

3. 水面填充:
   for y = height+1 to SEA_LEVEL:
       WATER

4. 洞穴雕刻:
   if caveNoise3D(x, y, z) > THRESHOLD:
       AIR

5. 矿石放置:
   在 STONE 区域按概率和高度范围放置矿石

6. 装饰:
   在地表放置树木 (5-7格高的木头+树叶球)
```

**推荐噪声库**: [FastNoiseLite](https://github.com/Auburn/FastNoiseLite) — 单头文件，MIT 协议，支持 Perlin、Simplex、Cellular 等。

---

### 2.12 玩家

**职责**: 管理玩家状态、移动、与世界交互。

```cpp
class Player {
public:
    void init(const glm::vec3& spawnPos);
    // 依赖 InputManager 而非 Window
    void update(float dt, const InputManager& input, World& world);

    glm::vec3 getPosition() const;
    glm::vec3 getEyePosition() const;   // position + {0, eyeHeight, 0}
    Camera& getCamera();

    // 方块交互
    void breakBlock(World& world);       // 左键
    void placeBlock(World& world);       // 右键
    void selectBlock(int slot);          // 数字键 1-9

    AABB getCollider() const;

private:
    glm::vec3 m_position;
    glm::vec3 m_velocity = {0, 0, 0};
    Camera    m_camera;

    float m_eyeHeight   = 1.62f;   // Minecraft 标准眼高
    float m_playerWidth = 0.6f;
    float m_playerHeight = 1.8f;

    float m_walkSpeed  = 4.317f;   // 方块/秒
    float m_sprintSpeed = 5.612f;
    float m_jumpForce  = 8.0f;

    bool  m_onGround = false;
    bool  m_sprinting = false;

    BlockID m_hotbar[9] = {};
    int     m_selectedSlot = 0;

    void handleMovement(float dt, const InputManager& input);
    void handleMouseLook(const InputManager& input);
    void handleBlockInteraction(const InputManager& input, World& world);
};
```

---

### 2.13 物理与碰撞

**职责**: AABB 碰撞检测、重力模拟、射线投射。

```cpp
struct AABB {
    glm::vec3 min, max;

    AABB(const glm::vec3& center, float halfW, float halfH);
    bool intersects(const AABB& other) const;
    AABB expanded(const glm::vec3& delta) const;  // 沿运动方向扩展
};

struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
};

struct RayHit {
    bool hit = false;
    glm::ivec3 blockPos;
    glm::ivec3 normal;   // 命中面的法线 → 用于计算放置位置
    float distance;
};

class Physics {
public:
    static constexpr float GRAVITY = -28.0f;  // m/s² (略快于真实重力, 更好的游戏手感)

    // 对玩家施加重力并解决碰撞
    void step(Player& player, const World& world, float dt);

    // 射线投射 (用于方块选取)
    static RayHit raycast(const World& world, const Ray& ray, float maxDist);

private:
    // 沿单轴移动并检测碰撞，返回实际可移动距离
    float sweepAxis(const AABB& box, const World& world, 
                    int axis, float delta);

    // 收集玩家 AABB 扩展范围内的所有固体方块 AABB
    std::vector<AABB> getBlockColliders(const World& world, const AABB& broadphase);
};
```

**碰撞解决算法 (Sweep and Resolve)**:
```
1. 将速度分解为 3 个轴 (X, Y, Z)
2. 按 Y → X → Z 顺序逐轴处理:
   a. 沿该轴扩展 AABB
   b. 检测与所有固体方块的重叠
   c. 如果有碰撞，截断该轴速度
   d. 更新位置
3. 如果 Y 轴碰撞且向下 → onGround = true
```

**射线投射 (DDA 算法)**:
- 使用 3D DDA (Digital Differential Analyzer) 沿射线步进
- 每步检查当前体素是否为固体
- 记录进入面的法线 → 用于确定方块放置位置
- 最大距离 5-6 个方块

---

### 2.14 光照系统

**职责**: 计算天光传播和方块光源传播。

```cpp
class LightEngine {
public:
    // 初始化区块的阳光 (从顶部向下传播)
    static void initSunlight(Chunk& chunk);

    // 放置方块后更新光照
    static void onBlockPlace(World& world, int x, int y, int z);

    // 移除方块后更新光照
    static void onBlockRemove(World& world, int x, int y, int z);

private:
    // BFS 光照传播
    static void propagateLight(World& world, 
                                std::queue<glm::ivec3>& lightQueue,
                                bool isSunlight);

    // BFS 光照移除
    static void removeLight(World& world,
                            std::queue<std::pair<glm::ivec3, uint8_t>>& removeQueue,
                            std::queue<glm::ivec3>& propagateQueue,
                            bool isSunlight);
};
```

**光照传播算法 (BFS)**:
```
1. 阳光传播:
   - 从 Y=255 向下扫描，遇到第一个不透明方块前，阳光=15
   - 阳光向四周水平扩散，每格衰减 1

2. 方块光传播:
   - 光源方块入队 (如火把 lightLevel=14)
   - BFS: 取出队头 (x,y,z,level)
   - 检查6个邻居，如果邻居光照 < level-1 且非不透明:
       设置邻居光照 = level - 1
       邻居入队

3. 光照移除 (方块放置遮挡时):
   - 被遮挡位置入移除队列
   - BFS: 如果邻居光照 < 当前, 邻居也入移除队列
   - 如果邻居光照 >= 当前, 入传播队列 (重新传播)
```

**Shader 中的光照应用**:
```glsl
float sunlight = float(lightByte >> 4) / 15.0;
float blockLight = float(lightByte & 0xF) / 15.0;
float light = max(sunlight * dayFactor, blockLight);
float ao = aoLevels[aoValue];  // {0.4, 0.6, 0.8, 1.0}
fragColor = texColor * vec4(vec3(light * ao), 1.0);
```

---

### 2.15 UI / HUD

**职责**: 渲染 2D 覆盖层 — 准星、快捷栏、调试信息。

```cpp
class UI {
public:
    void init(const ResourceMgr& resources);
    void shutdown();

    void render(const Renderer& renderer, const Player& player, 
                const Window& window, float fps);

    bool showDebugInfo = false;

private:
    GLuint m_quadVAO = 0, m_quadVBO = 0;  // 通用2D quad

    void renderCrosshair(int screenW, int screenH);
    void renderHotbar(const Player& player, int screenW, int screenH);
    void renderDebugText(float fps, const glm::vec3& pos, int screenW, int screenH);

    // 简易文字渲染 (位图字体)
    void renderText(const std::string& text, float x, float y, float scale);
};
```

**实现方案**:
- 准星：屏幕中心画两条短线 (或一个小十字纹理)
- 快捷栏：屏幕底部 9 格方块图标
- 调试信息：左上角显示 FPS、坐标、区块数量、面数
- 文字渲染：使用位图字体纹理 (ASCII 字符集)，通过 `glDrawArrays` 逐字渲染

---

### 2.16 存档系统

**职责**: 世界数据的持久化存储与读取。

```cpp
class SaveSystem {
public:
    void init(const std::string& worldName);

    // 保存单个区块
    void saveChunk(const Chunk& chunk);
    
    // 加载单个区块 (如果存在返回 true)
    bool loadChunk(Chunk& chunk);

    // 保存玩家数据
    void savePlayer(const Player& player);
    bool loadPlayer(Player& player);

    // 保存世界元数据 (种子, 时间等)
    void saveWorldMeta(uint32_t seed, float dayTime);
    bool loadWorldMeta(uint32_t& seed, float& dayTime);

private:
    std::string m_savePath;   // "saves/<worldName>/"

    // 区块文件路径: "saves/<worldName>/chunks/c_<cx>_<cz>.dat"
    std::string getChunkPath(int cx, int cz) const;
};
```

**存储格式**:
```
区块文件 (二进制):
┌─────────────────────────────┐
│ Magic Number (4 bytes)      │  "MCCK"
│ Version      (4 bytes)      │  1
│ ChunkX       (4 bytes)      │
│ ChunkZ       (4 bytes)      │
│ Data Length   (4 bytes)      │  压缩后长度
│ Block Data   (N bytes)      │  RLE 压缩的方块数据
│ Light Data   (N bytes)      │  RLE 压缩的光照数据
└─────────────────────────────┘
```

**压缩**: 使用简单的 RLE (Run-Length Encoding) 压缩方块数据。大面积相同方块 (如空气、石头) 压缩率很高。

---

## 3. 推荐目录结构

```
untitled/
├── CMakeLists.txt
├── DESIGN.md
├── assets/
│   ├── shaders/
│   │   ├── chunk.vert
│   │   ├── chunk.frag
│   │   ├── ui.vert
│   │   └── ui.frag
│   ├── textures/
│   │   ├── blocks/                # 方块贴图 (16x16 PNG)
│   │   │   ├── grass_top.png
│   │   │   ├── grass_side.png
│   │   │   ├── dirt.png
│   │   │   ├── stone.png
│   │   │   └── ...
│   │   ├── ui/
│   │   │   ├── crosshair.png
│   │   │   ├── hotbar.png
│   │   │   └── font.png
│   │   └── skybox/
│   └── saves/                     # 存档目录
├── third_party/                   # 第三方库 (header-only)
│   ├── glad/
│   │   ├── glad.h
│   │   └── glad.c
│   ├── stb/
│   │   └── stb_image.h
│   ├── glm/                       # (如果不用系统安装的)
│   └── FastNoiseLite.h
├── src/
│   ├── main.cpp
│   ├── core/
│   │   ├── Game.h / Game.cpp
│   │   ├── Window.h / Window.cpp          # 仅窗口与上下文管理
│   │   ├── InputManager.h / InputManager.cpp  # 独立输入管理
│   │   └── Camera.h / Camera.cpp
│   ├── renderer/
│   │   ├── Renderer.h / Renderer.cpp
│   │   ├── Shader.h / Shader.cpp
│   │   └── TextureAtlas.h / TextureAtlas.cpp
│   ├── world/
│   │   ├── World.h / World.cpp
│   │   ├── Chunk.h / Chunk.cpp
│   │   ├── ChunkMesher.h / ChunkMesher.cpp
│   │   ├── Block.h / Block.cpp
│   │   └── TerrainGenerator.h / TerrainGenerator.cpp
│   ├── physics/
│   │   ├── Physics.h / Physics.cpp
│   │   ├── AABB.h
│   │   └── Ray.h
│   ├── lighting/
│   │   └── LightEngine.h / LightEngine.cpp
│   ├── player/
│   │   └── Player.h / Player.cpp
│   ├── ui/
│   │   └── UI.h / UI.cpp
│   ├── resource/
│   │   └── ResourceMgr.h / ResourceMgr.cpp
│   └── save/
│       └── SaveSystem.h / SaveSystem.cpp
└── tests/
    ├── test_noise.cpp
    └── test_chunk.cpp
```

---

## 4. 第三方库依赖

| 库 | 用途 | 集成方式 | 许可证 |
|----|------|---------|--------|
| **GLFW 3.3+** | 窗口创建、OpenGL 上下文、输入处理 | 系统包 / 子模块 | Zlib |
| **GLAD** | OpenGL 函数加载器 | 源码集成 (glad.c) | MIT |
| **GLM** | 数学库 (向量、矩阵、变换) | Header-only / 系统包 | MIT |
| **stb_image** | 图片加载 (PNG/JPG → 像素数据) | 单头文件 | MIT / Public Domain |
| **FastNoiseLite** | 噪声生成 (Perlin, Simplex, Cellular) | 单头文件 | MIT |

---

## 5. 关键数据结构

```cpp
// ===== 方块顶点 =====
struct BlockVertex {
    float x, y, z;       // 位置 (12 bytes)
    float u, v;           // UV   (8 bytes)
    uint8_t normal;       // 面方向 0-5 (1 byte)
    uint8_t sunlight;     // 阳光等级 0-15 (1 byte)
    uint8_t blockLight;   // 方块光等级 0-15 (1 byte)
    uint8_t ao;           // AO等级 0-3 (1 byte)
};
// 总计 24 bytes/vertex, 4 vertex/face, 最多 6 face/block

// ===== 射线 =====
struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
};

// ===== AABB =====
struct AABB {
    glm::vec3 min;
    glm::vec3 max;
};

// ===== 区块坐标 =====
struct ChunkCoord {
    int x, z;
    bool operator==(const ChunkCoord& o) const { return x == o.x && z == o.z; }
};
struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const {
        return std::hash<int>()(c.x) ^ (std::hash<int>()(c.z) << 16);
    }
};

// ===== 世界坐标 → 区块坐标转换 =====
inline ChunkCoord worldToChunk(int wx, int wz) {
    return { wx >> 4, wz >> 4 };  // 等价于 / 16 (对负数也正确)
}
inline int worldToLocal(int w) {
    return w & 15;  // 等价于 % 16 (对负数也正确)
}
```

---

## 6. 渲染管线流程

```
每帧渲染流程:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

1. 清屏
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)

2. 计算矩阵
   View       = camera.getViewMatrix()
   Projection = camera.getProjectionMatrix(aspect)
   ViewProj   = Projection * View

3. 更新视锥体
   extractFrustumPlanes(ViewProj)

4. ===== 不透明 Pass =====
   绑定 chunk shader
   设置 VP uniform, 纹理 atlas, 光照参数
   启用深度写入, 禁用混合
   for each chunk in world.activeChunks:
       if !isChunkInFrustum(chunk.bounds): continue
       if chunk.isDirty(): ChunkMesher::generateMesh(chunk)
       Model = translate(chunk.worldOffset)
       设置 Model uniform
       绑定 chunk.mesh.vao
       glDrawArrays(GL_TRIANGLES, 0, chunk.mesh.vertexCount)

5. ===== 透明 Pass =====
   启用混合 (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
   禁用深度写入 (glDepthMask(GL_FALSE))
   按距离从远到近排序透明区块
   for each chunk (sorted):
       绑定 chunk.mesh.transparentVao
       glDrawArrays(GL_TRIANGLES, 0, chunk.mesh.transparentVertexCount)
   恢复深度写入

6. ===== UI Pass =====
   禁用深度测试
   设置正交投影
   renderCrosshair()
   renderHotbar()
   if debugMode: renderDebugText()
   恢复深度测试

7. 交换缓冲区
   glfwSwapBuffers(window)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

**Shader 设计要点**:

**chunk.vert**:
```glsl
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in float aNormal;
layout(location = 3) in float aSunlight;
layout(location = 4) in float aBlockLight;
layout(location = 5) in float aAO;

uniform mat4 model;
uniform mat4 viewProj;

out vec2 vUV;
out float vLight;
out float vAO;

void main() {
    gl_Position = viewProj * model * vec4(aPos, 1.0);
    vUV = aUV;
    float sun = aSunlight / 15.0;
    float block = aBlockLight / 15.0;
    vLight = max(sun, block);
    vAO = aAO;
}
```

**chunk.frag**:
```glsl
#version 330 core
in vec2 vUV;
in float vLight;
in float vAO;

uniform sampler2D texAtlas;

out vec4 FragColor;

const float aoLevels[4] = float[](0.4, 0.6, 0.8, 1.0);

void main() {
    vec4 texColor = texture(texAtlas, vUV);
    if (texColor.a < 0.1) discard;  // Alpha test
    float ao = aoLevels[int(vAO)];
    float brightness = vLight * ao;
    brightness = max(brightness, 0.05);  // 最低环境光
    FragColor = vec4(texColor.rgb * brightness, texColor.a);
}
```

---

## 7. 开发阶段规划

### 阶段 1: 基础框架 🏗️ (第 1-2 周)

**目标**: 窗口 + 摄像机 + 渲染单个方块

- [ ] 封装 `Window` 类 (GLFW 窗口 + OpenGL 上下文, 不含输入逻辑)
- [ ] 封装 `InputManager` 类 (注册 GLFW 回调, 提供键盘/鼠标状态查询)
- [ ] 实现 `Shader` 类 (加载/编译/绑定/Uniform 缓存)
- [ ] 实现 `Camera` 类 (第一人称, 通过 `InputManager` 控制鼠标视角)
- [ ] 使用 `stb_image` 加载纹理并绑定到 OpenGL
- [ ] 手动构建一个立方体的 VAO/VBO 并渲染
- [ ] 实现 `Game` 主循环 (固定步长 + 可变帧率)

**产出**: 可以在 3D 空间中自由飞行观察一个带纹理的方块，WASD + 鼠标控制视角。

---

### 阶段 2: 方块与区块系统 🧱 (第 3-4 周)

**目标**: 渲染一个完整的区块，实现面剔除

- [ ] 实现 `BlockRegistry` + `BlockDef`，注册基础方块类型 (AIR, STONE, DIRT, GRASS 等)
- [ ] 实现 `TextureAtlas`，将所有方块贴图拼合为一张大纹理
- [ ] 实现 `ResourceMgr`，统一管理 Shader 和纹理加载
- [ ] 实现 `Chunk` 类 (方块数据存储, `getBlock` / `setBlock`)
- [ ] 实现 `ChunkMesher`，遍历方块生成顶点，执行相邻面剔除
- [ ] 手动填充一个测试区块 (全石头或棋盘格)，渲染并验证面剔除正确性
- [ ] 验证 Atlas UV 坐标计算正确（每个面贴正确纹理）

**产出**: 屏幕上出现一个 16×256×16 的区块，面剔除生效，纹理正确。

---

### 阶段 3: 世界与地形生成 🌍 (第 5-6 周)

**目标**: 程序化生成无限世界，实现区块动态加载/卸载

- [ ] 集成 `FastNoiseLite`，实现 `TerrainGenerator`
  - [ ] 高度图生成 (Perlin 噪声叠加)
  - [ ] 基础分层填充 (BEDROCK / STONE / DIRT / GRASS)
  - [ ] 水面填充 (SEA_LEVEL 以下 WATER 方块)
- [ ] 实现 `World` 类
  - [ ] `unordered_map` 存储活跃区块
  - [ ] `update(playerPos)` 驱动区块加载/卸载
  - [ ] `getBlock` / `setBlock` 跨区块查询
- [ ] 每帧最多加载 2-4 个区块，避免卡顿
- [ ] 实现 `Renderer::renderWorld`，遍历所有活跃区块提交绘制
- [ ] 调整 `Camera` 初始位置至地表上方

**产出**: 可以在程序化生成的地形上自由飞行，地形随视野动态加载。

---

### 阶段 4: 玩家与物理 🏃 (第 7-8 周)

**目标**: 玩家站立在地面上，能正常行走、跳跃

- [ ] 实现 `AABB` 结构体和辅助函数
- [ ] 实现 `Physics::step` — 重力 + 逐轴 Sweep 碰撞解决
- [ ] 实现 `Player` 类
  - [ ] `handleMovement` — WASD 行走/奔跑，Space 跳跃
  - [ ] `handleMouseLook` — 委托给 `Camera::processMouseMovement`
  - [ ] `getEyePosition` — 用于摄像机跟随
- [ ] 将 Camera 从自由飞行模式切换为跟随 Player
- [ ] 实现 `Physics::raycast` (DDA 算法)，用于选取方块 (先用红框高亮显示)
- [ ] 测试边界情况：从高处落下、穿墙检测

**产出**: 玩家能站在地面上行走、跳跃，不会穿透方块，视线能选中方块。

---

### 阶段 5: 方块交互与存档 ⛏️ (第 9-10 周)

**目标**: 左键破坏方块，右键放置方块，进度可以保存

- [ ] 完善 `Player::handleBlockInteraction`
  - [ ] 左键: 调用 `World::setBlock` 设为 AIR，标记区块 dirty
  - [ ] 右键: 沿射线法线方向放置选中的方块
  - [ ] 数字键 1-9 切换快捷栏选中方块
- [ ] 触发区块 dirty 后，下一帧重建网格 (含边界区块)
- [ ] 实现 `UI` 类
  - [ ] 屏幕中心准星
  - [ ] 底部快捷栏 (9 格图标)
  - [ ] 方块选中高亮（线框渲染）
- [ ] 实现 `SaveSystem`
  - [ ] 区块二进制序列化 + RLE 压缩
  - [ ] 玩家位置/背包保存
  - [ ] 游戏退出时自动保存，启动时自动读取

**产出**: 完整的挖掘/放置体验，进度可以持久化保存。

---

### 阶段 6: 光照系统 💡 (第 11-12 周)

**目标**: 实现天光和方块光源的 BFS 传播，Shader 中应用光照和 AO

- [ ] 实现 `LightEngine`
  - [ ] `initSunlight` — 区块加载后从顶部向下初始化天光
  - [ ] `propagateLight` — BFS 光照扩散
  - [ ] `onBlockPlace` / `onBlockRemove` — 增量更新光照
- [ ] 在 `ChunkMesher` 中读取光照数据，写入 `BlockVertex.sunlight` / `blockLight`
- [ ] 实现顶点 AO 计算 (`calcAO`，检查顶点周围 3 个邻居)
- [ ] 更新 `chunk.vert` / `chunk.frag`，应用光照公式和 AO 等级
- [ ] 添加简单的昼夜系数 `dayFactor` uniform，测试昼夜效果

**产出**: 地下黑暗，洞穴阴影自然，方块角落有 AO 阴影，游戏观感大幅提升。

---

### 阶段 7: 内容扩充与优化 🚀 (第 13-16 周)

**目标**: 丰富游戏内容，提升性能到流畅水平

**地形内容** (第 13 周):
- [ ] `TerrainGenerator` 添加洞穴雕刻 (3D 噪声)
- [ ] 矿石分布 (按高度和概率放置 COAL/IRON/DIAMOND 矿石)
- [ ] 生物群系判定 (沙漠、森林、平原)
- [ ] 树木生成 (地表装饰)

**渲染优化** (第 14 周):
- [ ] 视锥体剔除 (`Renderer::isChunkInFrustum`)，减少无效 draw call
- [ ] 透明方块单独 Pass (水、玻璃从远到近渲染)
- [ ] 天空盒 (纯色渐变或 cubemap)

**多线程优化** (第 15 周):
- [ ] 区块生成和网格构建移至工作线程 (`std::async` 或简单线程池)
- [ ] 主线程只负责 GPU 上传和渲染
- [ ] 解决多线程访问 `World` 的竞态问题 (互斥锁或无锁队列)

**调试与收尾** (第 16 周):
- [ ] 实现 `UI` 调试信息 (FPS、坐标、已加载区块数、顶点数)
- [ ] Greedy Meshing (可选，合并同类面减少顶点)
- [ ] 性能 Profiling，找出瓶颈并优化
- [ ] 代码整理、补充注释

**产出**: 具备完整 Minecraft 基础玩法的体素游戏，渲染距离 8 以上流畅运行。

---

### 各阶段依赖关系

```
阶段1 (Window + Input + Camera + Shader)
  └─► 阶段2 (Block + Chunk + Mesher + Atlas)
        └─► 阶段3 (World + TerrainGen + 多区块渲染)
              └─► 阶段4 (Player + Physics + 碰撞)
                    └─► 阶段5 (交互 + UI + Save)
                          └─► 阶段6 (Light + AO + Shader)
                                └─► 阶段7 (内容 + 优化 + 多线程)
```

> 💡 **建议**: 每个阶段结束后提交一个 Git tag，方便回退和对比。阶段 1-4 是核心骨架，务必保证代码质量再推进；阶段 5-7 可以并行穿插进行。

---

### 进阶优化 (后续迭代) 🚀

