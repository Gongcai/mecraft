 Mecraft: Modern Voxel Engine

!https://img.shields.io/badge/Language-C++17-blue.svg?style=flat-square
!https://img.shields.io/badge/Renderer-OpenGL%203.3%20Core-orange.svg?style=flat-square
!https://img.shields.io/badge/Build-CMake%204.1-yellow.svg?style=flat-square
!https://img.shields.io/badge/License-MIT-green.svg?style=flat-square

Welcome to Mecraft! This is a high-performance, Minecraft-style voxel game engine written from scratch in modern C++17 and OpenGL 3.3 Core. It features a highly modular ECS-like architecture, advanced rendering techniques, and a robust physics engine. Whether you are a graphics enthusiast, a game developer, or just curious about how voxel engines work under the hood, this project serves as an excellent reference.

Current Status: Actively developed. Latest feature: Steve Character Rendering & Animation System ✅

🚀 Features

🎨 Rendering Engine

• Zero-Overdraw Meshing: Implements advanced face culling and greedy meshing algorithms to minimize draw calls.

• Texture Atlas Management: Automatically packs 16x16 block textures into a single dynamic atlas, eliminating texture switching overhead.

• Custom Fragment Shaders: Supports multi-light blending (sunlight + block light) with real-time shadows and smooth AO (Ambient Occlusion).

• Frustum Culling: Skips rendering for off-screen chunks to boost performance.

🌍 World & Terrain

• Infinite Procedural Generation: Powered by FastNoiseLite, featuring multi-octave Perlin noise for realistic continents, biomes, and caves.

• Smart Chunk Management: Dynamic loading/unloading based on player proximity, with distance-sorted priority queues.

• Robust Save/Load System: Utilizes RLE (Run-Length Encoding) compression for efficient disk storage of massive voxel worlds.

⚙️ Core Systems

• Decoupled Input & Windowing: A pure GLFW abstraction layer with a dedicated InputManager handling raw input, mouse capture, and callback forwarding.

• Sweep & Prune Physics: Custom AABB collision detection with axis-separated velocity solving.

• DDA Raycasting: High-precision block picking and placement system for player interactions.

🏛️ Architecture Overview

Mecraft is built on a clean, decoupled architecture. The Game class orchestrates all subsystems, ensuring a clear separation of concerns between logic, rendering, and I/O.
┌─────────────────────────────────────────────────────────┐
│                      Game (Core Loop)                   │
│  (Fixed-timestep logic @ 60Hz | Variable-rate rendering)│
└──────┬──────┬──────┬──────┬──────┬──────┬───────┬───────┘
       │      │      │      │      │      │       │
       ▼      ▼      ▼      ▼      ▼      ▼       ▼
   Window  Input  Render  World  Player Physics  Resource
    (GLFW) (Mgr)  Engine  Mgr    Ctrl   Engine    Mgr
       ▲      │      │      │       ▲       │         │
       │      └──────┼──────┘       │       └─────────┘
       │             ▼              │
       └──────── Camera      ┌──────┴──────┐
                             │ Chunk System │
                             │ (Blocks, Mesh│
                             │  Light, AABB)│
                             └─────────────┘


🛠️ Tech Stack

Library Purpose Integration

GLFW Windowing, OpenGL Context, Input System / Submodule

GLAD OpenGL Function Loader Source (Single-file)

GLM Mathematics (Vectors, Matrices) Header-only

stb_image Image Loading (PNG/JPG) Single-header

FastNoiseLite Procedural Noise Generation Single-header

📂 Project Structure

mecraft/
├── assets/          # Shaders, block textures, UI elements
├── src/             # Core source code
│   ├── core/        # Game loop, Window, InputManager
│   ├── renderer/    # OpenGL abstraction, Shaders, Texture Atlas
│   ├── world/       # Chunk, World, TerrainGen, Block registry
│   ├── physics/     # AABB, Raycasting, Collision detection
│   ├── player/      # Player controller, Camera
│   └── save/        # Save system (RLE compression)
├── tests/           # Unit tests (e.g., noise, chunk logic)
└── CMakeLists.txt   # Build configuration


🚦 Getting Started

Prerequisites

• A C++17 compatible compiler (GCC, Clang, or MSVC)

• CMake 4.1+

• Git

Build & Run

1. Clone the repository:
   git clone https://github.com/Gongcai/mecraft.git
   cd mecraft
   

2. Configure and build with CMake:
   mkdir build
   cd build
   cmake ..
   make -j4
   

3. Run the executable:
   ./mecraft
   

🛣️ Roadmap
Phase 1: Core Framework (Window, Input, Camera, Shader)

Phase 2: Voxel Rendering (Block/Chunk system, Face Culling, Texture Atlas)

Phase 3: World Engine (Terrain Generation, Chunk streaming, Save/Load)

Phase 4: Interaction (Physics, Raycasting, Block placement/destruction)

Phase 5: Advanced Entities (Steve animation system refinement, AI Mobs)

Phase 6: Polishing (UI/HUD overhaul, Audio engine, Particle effects)

🤝 Contributing

Contributions are welcome! If you have ideas for optimizations (like multi-threaded chunk meshing) or new features, feel free to fork the repo and submit a pull request. 

1. Fork it (<https://github.com/Gongcai/mecraft/fork>)
2. Create your feature branch (git checkout -b feature/AmazingFeature)
3. Commit your changes (git commit -m 'Add some AmazingFeature')
4. Push to the branch (git push origin feature/AmazingFeature)
5. Open a Pull Request
