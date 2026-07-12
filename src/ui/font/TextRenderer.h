#pragma once

#include "GlyphAtlas.h"
#include "../../renderer/rhi/RhiHandles.h"
#include "../../renderer/rhi/RhiTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class RhiCommandList;
class RhiDevice;
struct UIRenderContext;

class TextRenderer {
public:
    struct TextMetrics {
        float width = 0.0f;
        float height = 0.0f;
    };

    bool init(RhiDevice& rhiDevice, const char* fontPath);
    void shutdown();

    // Starts the CPU collection phase for one UI pass.
    void beginFrameCollection(float screenWidth, float screenHeight);

    // Collects a text request or records its prepared draw according to context.phase.
    void draw(const UIRenderContext& context,
              const std::string& text,
              float x,
              float y,
              float scale,
              const std::array<float, 4>& color) const;

    // Freezes the atlas, uploads resources, and builds immutable draw ranges.
    bool prepareFrame(RhiCommandList& commandList);
    void beginFrameRecording();
    [[nodiscard]] bool endFrameRecording() const;

    [[nodiscard]] TextMetrics measureText(const std::string& text, float scale) const;
    [[nodiscard]] const GlyphAtlas& atlas() const { return m_atlas; }

private:
    struct Vertex {
        float x = 0.0f;
        float y = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    struct DrawRequest {
        std::string text;
        float x = 0.0f;
        float y = 0.0f;
        float scale = 1.0f;
        std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
        RhiRect2D scissor;
        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
    };

    bool createPipelineResources();
    bool ensureVertexCapacity(uint64_t requiredBytes);
    bool rebuildAtlasBinding();
    bool generateRequestVertices(DrawRequest& request);
    void recordPreparedRequest(const UIRenderContext& context,
                               const std::string& text,
                               float x,
                               float y,
                               float scale,
                               const std::array<float, 4>& color) const;
    [[nodiscard]] static RhiRect2D resolveScissor(const UIRenderContext& context);

    RhiDevice* m_rhiDevice = nullptr;
    mutable GlyphAtlas m_atlas;
    RhiShaderHandle m_vertexShader;
    RhiShaderHandle m_fragmentShader;
    RhiBindGroupLayoutHandle m_bindGroupLayout;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiPipelineHandle m_pipeline;
    RhiSamplerHandle m_sampler;
    RhiTextureViewHandle m_atlasView;
    RhiBindGroupHandle m_atlasBindGroup;
    RhiTextureHandle m_boundAtlasTexture;
    RhiBufferHandle m_vertexBuffer;
    uint64_t m_vertexCapacity = 0;
    RhiResourceState m_vertexBufferState = RhiResourceState::VertexBuffer;
    float m_screenWidth = 1.0f;
    float m_screenHeight = 1.0f;
    mutable std::vector<DrawRequest> m_requests;
    std::vector<Vertex> m_vertices;
    mutable size_t m_recordIndex = 0;
    bool m_collecting = false;
    bool m_prepared = false;
    mutable bool m_recording = false;
};
