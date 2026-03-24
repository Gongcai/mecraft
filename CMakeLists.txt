cmake_minimum_required(VERSION 4.1  )
project(mecraft)


set(CMAKE_CXX_STANDARD 17)
find_package(glfw3 REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
add_executable(mecraft main.cpp
        assets/third_party/glad/glad.c
        src/core/Window.cpp
        src/core/Window.h
        src/core/Camera.cpp
        src/core/Camera.h
        src/core/InputManager.cpp
        src/core/InputManager.h
        src/renderer/Shader.cpp
        src/renderer/Shader.h
        src/core/Game.cpp
        src/core/Game.h
        src/core/Time.cpp
        src/core/Time.h
        src/player/Player.cpp
        src/player/Player.h
        src/renderer/TestCube.cpp
        src/renderer/TestCube.h
        assets/third_party/stb/stb_image.h
        src/renderer/stb_image.cpp
        src/resource/ResourceMgr.cpp
        src/resource/ResourceMgr.h
        src/renderer/Renderer.cpp
        src/renderer/Renderer.h
        src/player/ActionMap.cpp
        src/player/ActionMap.h
        src/core/InputContextManager.h
        src/core/InputContextManager.cpp
        src/core/IGameState.h
        src/core/GameStateMachine.h
        src/core/GameStateMachine.cpp
        src/core/states/GameplayState.h
        src/core/states/UIState.h
        src/world/Block.cpp
        src/world/Block.h
        src/world/Chunk.cpp
        src/world/Chunk.h
        src/renderer/ChunkMesher.cpp
        src/renderer/ChunkMesher.h
        assets/third_party/glad/glad.h
        src/world/World.cpp
        src/world/World.h
        "src/physics/Ray.h")
include_directories(assets/third_party)
target_link_libraries(mecraft PRIVATE glfw glm::glm)
target_link_directories(mecraft PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(mecraft PRIVATE GLFW_INCLUDE_NONE)

enable_testing()
add_executable(world_flat_test
        tests/world_flat_test.cpp
        src/world/World.cpp
        src/world/Chunk.cpp
        assets/third_party/glad/glad.c)
target_include_directories(world_flat_test PRIVATE assets/third_party)
target_link_libraries(world_flat_test PRIVATE glm::glm)
add_test(NAME world_flat_test COMMAND world_flat_test)

