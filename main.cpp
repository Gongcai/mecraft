#include "src/core/Window.h"
#include "src/core/InputManager.h"
#include "src/core/Camera.h"
#include "src/renderer/Shader.h"

#include <iostream>

int main() {
    Window window;
    if (!window.init(1280, 720, "Voxel Engine Test")) {
        return -1;
    }

    InputManager input;
    input.init(window.getHandle());
    input.captureMouse(true);

    Camera camera(glm::vec3(0.0f, 2.0f, 5.0f));
    Shader chunkShader("../assets/shaders/chunk.vs", "../assets/shaders/chunk.fs");

    // 定义一个简单的立方体顶点数据 (位置, UV, NormalIndex, Sunlight, BlockLight, AO)
    // 根据 chunk.vs: layout 0=pos, 1=uv, 2=normal, 3=sun, 4=block, 5=ao
    float vertices[] = {
        // Position           // UV      // Norm // Sun  // Block // AO
        -0.5f, -0.5f, -0.5f,  0.0f, 0.0f, 0.0f,  15.0f,  0.0f,    3.0f,
         0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,  15.0f,  0.0f,    3.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 0.0f,  15.0f,  0.0f,    3.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 0.0f,  15.0f,  0.0f,    3.0f,
        -0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f,  15.0f,  0.0f,    3.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, 0.0f, 0.0f,  15.0f,  0.0f,    3.0f,

        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f,  15.0f,  0.0f,    3.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 1.0f,  15.0f,  0.0f,    3.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 1.0f,  15.0f,  0.0f,    3.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 1.0f,  15.0f,  0.0f,    3.0f,
        -0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 1.0f,  15.0f,  0.0f,    3.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f,  15.0f,  0.0f,    3.0f,

        -0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 2.0f,  15.0f,  0.0f,    3.0f,
        -0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 2.0f,  15.0f,  0.0f,    3.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 2.0f,  15.0f,  0.0f,    3.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 2.0f,  15.0f,  0.0f,    3.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 2.0f,  15.0f,  0.0f,    3.0f,
        -0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 2.0f,  15.0f,  0.0f,    3.0f,

         0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 3.0f,  15.0f,  0.0f,    3.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 3.0f,  15.0f,  0.0f,    3.0f,
         0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 3.0f,  15.0f,  0.0f,    3.0f,
         0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 3.0f,  15.0f,  0.0f,    3.0f,
         0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 3.0f,  15.0f,  0.0f,    3.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 3.0f,  15.0f,  0.0f,    3.0f,

        -0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 4.0f,  15.0f,  0.0f,    3.0f,
         0.5f, -0.5f, -0.5f,  1.0f, 1.0f, 4.0f,  15.0f,  0.0f,    3.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 4.0f,  15.0f,  0.0f,    3.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 4.0f,  15.0f,  0.0f,    3.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 4.0f,  15.0f,  0.0f,    3.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 4.0f,  15.0f,  0.0f,    3.0f,

        -0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 5.0f,  15.0f,  0.0f,    3.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 5.0f,  15.0f,  0.0f,    3.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 5.0f,  15.0f,  0.0f,    3.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 5.0f,  15.0f,  0.0f,    3.0f,
        -0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 5.0f,  15.0f,  0.0f,    3.0f,
        -0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 5.0f,  15.0f,  0.0f,    3.0f
    };

    unsigned int VBO, VAO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // aPos (layout 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // aUV (layout 1)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // aNormalIndex (layout 2)
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    // aSunlight (layout 3)
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    // aBlockLight (layout 4)
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(7 * sizeof(float)));
    glEnableVertexAttribArray(4);
    // aAO (layout 5)
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(5);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    bool isCursorCaptured = true;

    while (!window.shouldClose()) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        window.pollEvents();

        // Debug
        // double mx, my;
        // glfwGetCursorPos(window.getHandle(), &mx, &my);
        // std::cout << "Main loop poll: " << mx << ", " << my << std::endl;

        input.update();
        const glm::vec2 mouseDelta = input.getMouseDelta();

        // 1. ESC 退出
        if (input.isKeyJustPressed(GLFW_KEY_ESCAPE)) break;

        // 2. 空格键盘切换鼠标捕获
        if (input.isKeyJustPressed(GLFW_KEY_F)) {
            isCursorCaptured = !isCursorCaptured;
            input.captureMouse(isCursorCaptured);
        }


        camera.processMouseMovement(mouseDelta.x, -mouseDelta.y); // y 通常需要反向


        // 4. 摄像机移动
        float moveSpeed = 5.0f * deltaTime;
        if (input.isKeyHeld(GLFW_KEY_W)) camera.setPosition(camera.getPosition() + camera.getFront() * moveSpeed);
        if (input.isKeyHeld(GLFW_KEY_S)) camera.setPosition(camera.getPosition() - camera.getFront() * moveSpeed);
        if (input.isKeyHeld(GLFW_KEY_A)) camera.setPosition(camera.getPosition() - camera.getRight() * moveSpeed);
        if (input.isKeyHeld(GLFW_KEY_D)) camera.setPosition(camera.getPosition() + camera.getRight() * moveSpeed);

        // 5. 点击左键打印拾取射线
        if (input.isMouseButtonJustPressed(GLFW_MOUSE_BUTTON_LEFT)) {
            Ray ray = camera.getPickRay();
            std::cout << "[Ray] Origin: (" << ray.origin.x << ", " << ray.origin.y << ", " << ray.origin.z
                      << ") Dir: (" << ray.direction.x << ", " << ray.direction.y << ", " << ray.direction.z << ")" << std::endl;
        }

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        chunkShader.use();
        glm::mat4 projection = camera.getProjectionMatrix(window.getAspectRatio());
        glm::mat4 view = camera.getViewMatrix();
        chunkShader.setMat4("viewProj", projection * view);

        glm::mat4 model = glm::mat4(1.0f);
        chunkShader.setMat4("model", model);

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);

        window.swapBuffers();
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    window.destroy();
    return 0;
}