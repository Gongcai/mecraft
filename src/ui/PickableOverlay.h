#pragma once

class ResourceMgr;
class Shader;

#include "Pickable.h"

class PickableOverlay
{
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown();

    void render(const Pickable::SlotInfo* slots, int count, float mouseX, float mouseY) const;

private:
    ResourceMgr* m_resourceMgr = nullptr;
    Shader* m_crosshairShader = nullptr;
    Shader* m_inventoryShader = nullptr;
    Pickable::MeshHandles m_mesh;
};

