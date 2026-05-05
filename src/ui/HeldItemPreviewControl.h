#pragma once

#include <cstdint>
#include <unordered_map>

#include <glad/glad.h>

#include "UIWidget.h"
#include "../world/Block.h"
#include "../item/Item.h"

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

class HeldItemPreviewControl : public UIWidget {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    void setLayout(const HeldItemPreviewLayout& layout);
    [[nodiscard]] const HeldItemPreviewLayout& getLayout() const;
    void triggerActionAnimation();
    void setActionAnimationActive(bool active);

protected:
    void renderSelf(const UIRenderContext& context) const override;

private:
    struct Mesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        uint32_t vertexCount = 0;
    };

    Mesh* getOrCreateBlockMesh(BlockID blockId);
    Mesh buildBlockMesh(BlockID blockId) const;
    Mesh* getOrCreateItemMesh(ItemID itemId);
    Mesh buildItemMesh(ItemID itemId) const;
    static void destroyMesh(Mesh& mesh);

    ResourceMgr* m_resourceMgr = nullptr;
    Shader* m_shader = nullptr;
    Shader* m_itemShader = nullptr;
    mutable std::unordered_map<BlockID, Mesh> m_blockMeshes;
    mutable std::unordered_map<ItemID, Mesh> m_itemMeshes;
    HeldItemPreviewLayout m_layout;
    mutable bool m_hasPrevSample = false;
    mutable float m_prevTimeSeconds = 0.0f;
    mutable float m_motionBlend = 0.0f;
    mutable float m_swayX = 0.0f;
    mutable float m_swayY = 0.0f;
    mutable bool m_actionAnimActive = false;
    mutable bool m_actionAnimContinuous = false;
    mutable float m_actionAnimElapsed = 0.0f;
};
