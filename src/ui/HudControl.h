#pragma once

#include <vector>
#include <glad/glad.h>
#include "IUIControl.h"

class ResourceMgr;
class Shader;
struct TextureAtlas;

class HudControl : public IUIControl {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    void render(const UIRenderContext& context) const override;
    UIEventResult onInput(const UIInputEvent& event) override;
    [[nodiscard]] bool isVisible() const override;

    void setVisible(bool visible);

private:
    void initMesh();
    void cleanupMesh();

    // Appends icon quads for one stat row into the vertex buffer.
    // Icons are drawn left-to-right starting at (startX, startY).
    // current/max determine how many full and half icons to draw.
    // fullIndex/halfIndex are atlas tile indices for the full and half icon.
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
    bool m_visible = true;

    // Cached atlas icon indices (resolved once in init).
    int m_heartFull = -1;
    int m_heartHalf = -1;
    int m_armorFull = -1;
    int m_armorHalf = -1;
    int m_foodFull = -1;
    int m_foodHalf = -1;
};
