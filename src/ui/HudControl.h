#pragma once

#include <vector>
#include <glad/glad.h>
#include "UIWidget.h"

class ResourceMgr;
class Shader;
struct TextureAtlas;

class HudControl : public UIWidget {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

protected:
    void renderSelf(const UIRenderContext& context) const override;

private:
    void initMesh();
    void cleanupMesh();

    void appendIconRow(std::vector<float>& verts,
                       const TextureAtlas& atlas,
                       float startX, float startY,
                       int current, int max,
                       int fullIndex, int halfIndex,
                       float iconSize) const;

    Shader* m_inventoryShader = nullptr;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    ResourceMgr* m_resourceMgr = nullptr;

    // Cached atlas icon indices (resolved once in init).
    int m_heartFull = -1;
    int m_heartHalf = -1;
    int m_armorFull = -1;
    int m_armorHalf = -1;
    int m_foodFull = -1;
    int m_foodHalf = -1;
};
