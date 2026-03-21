//
// Created by Caiwe on 2026/3/21.
//
#include "Game.h"


void Game::init(int width, int height, const char *title) {
    if (m_window.init(width, height, title)) {
        m_input.init(m_window.getHandle());
        m_input.captureMouse(true);
    }
    else {
        std::cerr << "Error while initializing the window." << std::endl;
    }
    Time::init();
    m_testCube = new TestCube({0.0f,0.0f,0.0f});
    glEnable(GL_DEPTH_TEST);
}

void Game::run() {

    while (!m_window.shouldClose()) {
        m_window.pollEvents();
        Time::update();
        m_input.update();

        const InputSnapshot& input = m_input.snapshot();
        if (input.isKeyJustPressed(GLFW_KEY_ESCAPE)) {
            break;
        }
        const InputSnapshot& frameInput = m_input.snapshot();
        const glm::vec2 mouseDelta = frameInput.mouseDelta;

        // 1. ESC 退出
        if (frameInput.isKeyJustPressed(GLFW_KEY_ESCAPE)) break;

 


        m_camera.processMouseMovement(mouseDelta.x, -mouseDelta.y); // y 通常需要反向


        // 4. 摄像机移动
        float moveSpeed = 5.0f * Time::deltaTime;
        if (frameInput.isKeyHeld(GLFW_KEY_W)) m_camera.setPosition(m_camera.getPosition() + m_camera.getFront() * moveSpeed);
        if (frameInput.isKeyHeld(GLFW_KEY_S)) m_camera.setPosition(m_camera.getPosition() - m_camera.getFront() * moveSpeed);
        if (frameInput.isKeyHeld(GLFW_KEY_A)) m_camera.setPosition(m_camera.getPosition() - m_camera.getRight() * moveSpeed);
        if (frameInput.isKeyHeld(GLFW_KEY_D)) m_camera.setPosition(m_camera.getPosition() + m_camera.getRight() * moveSpeed);

        // 5. 点击左键打印拾取射线
        if (frameInput.isMouseButtonJustPressed(GLFW_MOUSE_BUTTON_LEFT)) {
            Ray ray = m_camera.getPickRay();
            std::cout << "[Ray] Origin: (" << ray.origin.x << ", " << ray.origin.y << ", " << ray.origin.z
                      << ") Dir: (" << ray.direction.x << ", " << ray.direction.y << ", " << ray.direction.z << ")" << std::endl;
        }

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 projection = m_camera.getProjectionMatrix(m_window.getAspectRatio());
        glm::mat4 view = m_camera.getViewMatrix();

        m_testCube->setViewProjection(projection * view);
        m_testCube->draw();


        m_window.swapBuffers();
    }
}

void Game::shutdown() {
    return;
}
