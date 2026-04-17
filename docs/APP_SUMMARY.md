# Mecraft

## What It Is
Mecraft is a desktop C++17 voxel sandbox game inspired by Minecraft, built with OpenGL 3.3, GLFW, OpenAL, and CMake. Repo evidence shows a single-player prototype/engine with procedural terrain, block interaction, UI overlays, audio, and automated tests.

## Who It's For
- Primary persona: a player or graphics/game-dev hobbyist who wants a Minecraft-style sandbox and is comfortable running a native desktop build.

## What It Does
- Generates a chunked voxel world with terrain biomes, streaming, and adjustable render distance.
- Supports block targeting, breaking, placing, and light-aware world updates.
- Includes player movement, physics/collision, inventory, hotbar, and held-item preview.
- Provides crafting, item/block registries, and asset-driven configs in `assets/config`.
- Renders world, particles, drops, outlines, fog, and post-processing effects.
- Plays BGM, positional audio, footsteps, and fall/hurt sounds via OpenAL.
- Ships broad test coverage for world, meshing, UI, crafting, physics, audio, and performance paths.

## How It Works
- `main.cpp` creates `Game`; `Game::init()` wires `Window`, `InputManager`, `ResourceMgr`, `World`, `Player`, `Renderer`, `UIRenderer`, `AudioEngine`, `BgmSystem`, `ParticleSystem`, `DropSystem`, and `CraftingSystem`.
- Input flows from GLFW window events into `InputManager` and `InputContextManager`, then through `GameStateMachine` states such as `GameplayState`, `InventoryState`, `CommandState`, and `UIState`.
- `World` owns active chunks, terrain generation, light propagation, day/night state, chunk load/unload queues, and raycast/block queries around the player.
- `Renderer` drains world data, submits async chunk meshing jobs through `ChunkMeshingService` + `ThreadPool`, uploads chunk meshes, and renders world passes before post-process/UI composition.
- Assets and data flow from `assets/shaders`, `assets/textures`, `assets/sounds`, `assets/bgm`, and `assets/config/*.json|txt` into `ResourceMgr`, registries, and gameplay/UI systems.

## How To Run
1. Install build/runtime dependencies required by `CMakeLists.txt`: CMake 3.20+, a C++17 compiler, `glfw3`, `glm`, `nlohmann_json`, `OpenAL`, and `Freetype`. Dependency bootstrap method: Not found in repo.
2. Configure and build:
   `cmake -S . -B build`
   `cmake --build build --config Release`
3. Run the built app from the build output so the Windows asset path `../assets` resolves correctly, for example:
   `.\build\Release\mecraft.exe`
4. Optional validation:
   `ctest --test-dir build -C Release`

Packaging/distribution instructions: Not found in repo.
