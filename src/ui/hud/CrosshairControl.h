#pragma once

#include <array>

#include <cstdint>

#include "renderer/rhi/RhiHandles.h"
#include "../core/UIWidget.h"

class ResourceMgr;
class RhiDevice;

class CrosshairControl : public UIWidget
{
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;

    void setSize(float size);
    [[nodiscard]] float getSize() const;

    void setColor(const std::array<float, 4>& color);
    [[nodiscard]] const std::array<float, 4>& getColor() const;

protected:
    void renderSelf(const UIRenderContext& ctx) const override;

private:
    void initMesh();
    void rebuildMesh();
    void cleanupMesh();

    RhiDevice* m_rhiDevice = nullptr;
    RhiBufferHandle m_vertexBuffer;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_fragmentShader;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiPipelineHandle m_pipeline;
    int m_vertexCount = 0;
    float m_size = 1.0f;
    std::array<float, 4> m_color {1.0f, 1.0f, 1.0f, 1.0f};
};
