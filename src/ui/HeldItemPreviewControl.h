#pragma once

#include <cstdint>
#include <unordered_map>

#include <glad/glad.h>

#include "IUIControl.h"
#include "../world/Block.h"

class Shader;

struct HeldItemPreviewLayout {
    float centerXRatio = 0.686f;
    float centerYRatio = 0.047f;
    float sizeRatio = 0.218f;
    float pitchDegrees = 20.7f;
    float yawDegrees = -43.7f;
    float swayAmplitudeX = 0.0296f;
    float swayAmplitudeY = 0.020f;
};

class HeldItemPreviewControl : public IUIControl {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    void render(const UIRenderContext& context) const override;
    UIEventResult onInput(const UIInputEvent& event) override;
    [[nodiscard]] bool isVisible() const override;

    void setVisible(bool visible);
    void setLayout(const HeldItemPreviewLayout& layout);
    [[nodiscard]] const HeldItemPreviewLayout& getLayout() const;
    void triggerActionAnimation();

private:
    struct Mesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        uint32_t vertexCount = 0;
    };

    Mesh* getOrCreateBlockMesh(BlockID blockId);
    Mesh buildBlockMesh(BlockID blockId) const;
    static void destroyMesh(Mesh& mesh);

    ResourceMgr* m_resourceMgr = nullptr;
    Shader* m_shader = nullptr;
    mutable std::unordered_map<BlockID, Mesh> m_blockMeshes;
    HeldItemPreviewLayout m_layout;
    mutable bool m_hasPrevSample = false;
    mutable float m_prevTimeSeconds = 0.0f;
    mutable float m_motionBlend = 0.0f;
    mutable float m_swayX = 0.0f;
    mutable float m_swayY = 0.0f;
    mutable bool m_actionAnimActive = false;
    mutable float m_actionAnimElapsed = 0.0f;
    bool m_visible = true;
};
