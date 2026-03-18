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
        src/renderer/Shader.h)
target_link_libraries(mecraft PRIVATE glfw glm::glm)