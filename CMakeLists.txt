cmake_minimum_required(VERSION 3.20) # 建议使用 3.20，4.1 尚未正式发布
project(mecraft LANGUAGES C CXX)
if (MSVC)
    add_compile_options(/utf-8)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# 1. 查找依赖 (统一管理)
# ---------------------------------------------------------------------------
find_package(glfw3 REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenAL CONFIG REQUIRED)
find_package(Freetype CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# 2. 定义源文件变量 (分类管理，方便增删)
# ---------------------------------------------------------------------------
# 第三方库 (Glad, ImGui)
set(THIRD_PARTY_SOURCES
        assets/third_party/glad/glad.c
        assets/third_party/imgui/imgui.cpp
        assets/third_party/imgui/imgui_demo.cpp
        assets/third_party/imgui/imgui_draw.cpp
        assets/third_party/imgui/imgui_impl_glfw.cpp
        assets/third_party/imgui/imgui_impl_opengl3.cpp
        assets/third_party/imgui/imgui_tables.cpp
        assets/third_party/imgui/imgui_widgets.cpp
        assets/third_party/FastNoise/FastNoiseLite.h
)

# 核心逻辑 (不包含 main.cpp 和测试代码)
set(CORE_SOURCES
        src/core/Window.cpp
        src/core/Camera.cpp
        src/core/InputManager.cpp
        src/core/Game.cpp
        src/core/Time.cpp
        src/core/InputContextManager.cpp
        src/core/GameStateMachine.cpp
        src/player/Player.cpp
        src/player/ActionMap.cpp
        src/player/Inventory.cpp
        src/renderer/Shader.cpp
        src/renderer/Renderer.cpp
        src/renderer/PostProcessRenderer.cpp
        src/renderer/TestCube.cpp
        src/renderer/stb_image.cpp
        src/renderer/ChunkMesher.cpp
        src/renderer/ChunkMeshingService.cpp
        src/resource/ResourceMgr.cpp
        src/world/Block.cpp
        src/world/Chunk.cpp
        src/world/TerrainGenerator.cpp
        src/world/World.cpp
        src/physics/PhysicsSystem.cpp
        src/particle/ParticleSystem.cpp
        src/audio/AudioEngine.cpp
        src/audio/AudioClip.cpp
        src/audio/AudioSource.cpp
        src/audio/AudioListener.cpp
        src/ui/Dashboard.cpp
        src/ui/UIRenderer.cpp
)

# ---------------------------------------------------------------------------
# 3. 构建核心静态库 (项目的“引擎”部分)
# ---------------------------------------------------------------------------
add_library(mecraft_core STATIC ${CORE_SOURCES} ${THIRD_PARTY_SOURCES})

# 设置包含路径：PUBLIC 确保链接此库的其他目标也能找到这些头文件
target_include_directories(mecraft_core PUBLIC
        src
        assets/third_party
)

# 统一链接依赖
target_link_libraries(mecraft_core PUBLIC
        glfw
        glm::glm
        nlohmann_json::nlohmann_json
        OpenAL::OpenAL
        Freetype::Freetype
)

# 统一宏定义
target_compile_definitions(mecraft_core PUBLIC GLFW_INCLUDE_NONE)


add_executable(mecraft main.cpp)
target_link_libraries(mecraft PRIVATE mecraft_core)


enable_testing()

# 辅助宏：减少重复代码
macro(add_mecraft_test TEST_NAME SOURCE_FILE)
    add_executable(${TEST_NAME} ${SOURCE_FILE})
    target_link_libraries(${TEST_NAME} PRIVATE mecraft_core)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endmacro()

# 物理测试 (需要特殊宏定义)
add_executable(physics_mvp_test tests/physics_mvp_test.cpp)
target_link_libraries(physics_mvp_test PRIVATE mecraft_core)
target_compile_definitions(physics_mvp_test PRIVATE MECRAFT_NO_TEXTURES)
add_test(NAME physics_mvp_test COMMAND physics_mvp_test)

# 其他简单测试
add_mecraft_test(world_flat_test tests/world_flat_test.cpp)
add_mecraft_test(audio_test tests/audio_test.cpp)
add_mecraft_test(terrain_perf_test tests/terrain_perf_test.cpp)