#pragma once

#include "Tween.h"
#include "renderer/rhi/RhiHandles.h"

struct GameResources;
class RhiCommandList;
class RhiDevice;

class ScreenTransition {
public:
    ScreenTransition() = default;
    ~ScreenTransition();

    void init(GameResources& resources, RhiDevice& rhiDevice);
    void shutdown();

    void startFadeOut(float duration);
    void startFadeIn(float duration);

    void tick(float dt);
    void render(int screenW, int screenH, RhiCommandList& commandList) const;

    [[nodiscard]] bool isDone() const { return m_alphaTween.isDone(); }
    [[nodiscard]] bool isActive() const { return m_alphaTween.isRunning(); }

private:
    void initMesh();
    void cleanupMesh();

    RhiDevice* m_rhiDevice = nullptr;
    RhiBufferHandle m_vertexBuffer;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_fragmentShader;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiPipelineHandle m_pipeline;
    Tween<float> m_alphaTween;
};
