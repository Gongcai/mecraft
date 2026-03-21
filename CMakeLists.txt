cmake_minimum_required(VERSION 4.1)
project(mecraft)


set(CMAKE_CXX_STANDARD 17)
find_package(glfw3 REQUIRED)
find_package(glm CONFIG REQUIRED)
add_executable(mecraft main.cpp
        glad.c
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
        src/renderer/Renderer.h)
include_directories(assets/third_party)
target_link_libraries(mecraft PRIVATE glfw glm::glm)
target_compile_definitions(mecraft PRIVATE GLFW_INCLUDE_NONE)
