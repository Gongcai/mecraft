#pragma once

#include <cstdint>

#include "Tween.h"

class Shader;
class ResourceMgr;

class ScreenTransition {
public:
    ScreenTransition() = default;
    ~ScreenTransition();

    void init(ResourceMgr& resourceMgr);
    void shutdown();

    void startFadeOut(float duration);
    void startFadeIn(float duration);

    void tick(float dt);
    void render(int screenW, int screenH) const;

    [[nodiscard]] bool isDone() const { return m_alphaTween.isDone(); }
    [[nodiscard]] bool isActive() const { return m_alphaTween.isRunning(); }

private:
    void initMesh();
    void cleanupMesh();

    Shader* m_shader = nullptr;
    uint32_t m_vao = 0;
    uint32_t m_vbo = 0;
    Tween<float> m_alphaTween;
};
