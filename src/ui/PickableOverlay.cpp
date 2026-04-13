#include "PickableOverlay.h"

#include <glad/glad.h>

#include "../renderer/Shader.h"
#include "../resource/ResourceMgr.h"

void PickableOverlay::init(ResourceMgr& resourceMgr)
{
    m_resourceMgr = &resourceMgr;
    m_crosshairShader = resourceMgr.getShader("crosshair");
    m_inventoryShader = resourceMgr.getShader("inventory");
    Pickable::initMesh(m_mesh);
}

void PickableOverlay::shutdown()
{
    Pickable::shutdownMesh(m_mesh);
    m_resourceMgr = nullptr;
    m_crosshairShader = nullptr;
    m_inventoryShader = nullptr;
}

void PickableOverlay::render(const Pickable::SlotInfo* slots, int count, float mouseX, float mouseY) const
{
    if (!m_resourceMgr || !slots || count <= 0) {
        return;
    }

    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int screenW = viewport[2];
    const int screenH = viewport[3];
    if (screenW <= 0 || screenH <= 0) {
        return;
    }

    const TextureAtlas& itemIconAtlas = m_resourceMgr->getItemIconAtlas();
    Pickable::RenderParams params;
    const int hoveredIndex = Pickable::hitTest(slots, count, mouseX, mouseY);

    Pickable::render(slots,
                     count,
                     hoveredIndex,
                     screenW,
                     screenH,
                     params,
                     m_crosshairShader,
                     m_inventoryShader,
                     m_mesh,
                     *m_resourceMgr,
                     itemIconAtlas,
                     m_resourceMgr->getItemTextureAtlas());
}

