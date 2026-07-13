#include "TextRenderer.h"

#include "../../renderer/rhi/RhiCommandList.h"
#include "../../renderer/rhi/RhiDescriptor.h"
#include "../../renderer/rhi/RhiDevice.h"
#include "../../renderer/rhi/RhiPipeline.h"
#include "../../renderer/rhi/RhiResources.h"
#include "../../renderer/rhi/RhiShaderSourceLoader.h"
#include "../core/UIRenderContext.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>

#include <glm/vec4.hpp>

namespace {

bool sameHandle(const RhiTextureHandle lhs, const RhiTextureHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

bool decodeUtf8Codepoint(const char*& cursor, const char* end, uint32_t& codepoint) {
    if (cursor >= end) {
        return false;
    }
    const auto first = static_cast<unsigned char>(*cursor);
    if (first < 0x80u) {
        codepoint = first;
        ++cursor;
        return true;
    }
    uint32_t value = 0u;
    uint32_t minimum = 0u;
    int continuationCount = 0;
    if ((first & 0xe0u) == 0xc0u) {
        value = first & 0x1fu;
        minimum = 0x80u;
        continuationCount = 1;
    } else if ((first & 0xf0u) == 0xe0u) {
        value = first & 0x0fu;
        minimum = 0x800u;
        continuationCount = 2;
    } else if ((first & 0xf8u) == 0xf0u) {
        value = first & 0x07u;
        minimum = 0x10000u;
        continuationCount = 3;
    } else {
        return false;
    }
    if (end - cursor <= continuationCount) {
        return false;
    }
    for (int index = 1; index <= continuationCount; ++index) {
        const auto byte = static_cast<unsigned char>(cursor[index]);
        if ((byte & 0xc0u) != 0x80u) {
            return false;
        }
        value = (value << 6u) | (byte & 0x3fu);
    }
    if (value < minimum || value > 0x10ffffu ||
        (value >= 0xd800u && value <= 0xdfffu)) {
        return false;
    }
    cursor += continuationCount + 1;
    codepoint = value;
    return true;
}

} // namespace

bool TextRenderer::init(RhiDevice& rhiDevice, const char* fontPath) {
    shutdown();
    m_rhiDevice = &rhiDevice;
    if (!m_atlas.init(rhiDevice, fontPath, 32) || !createPipelineResources()) {
        shutdown();
        return false;
    }
    return ensureVertexCapacity(64u * 1024u);
}

void TextRenderer::shutdown() {
    if (m_rhiDevice != nullptr) {
        if (m_vertexBuffer.isValid()) m_rhiDevice->destroyBuffer(m_vertexBuffer);
        if (m_atlasBindGroup.isValid()) m_rhiDevice->destroyBindGroup(m_atlasBindGroup);
        if (m_atlasView.isValid()) m_rhiDevice->destroyTextureView(m_atlasView);
        if (m_sampler.isValid()) m_rhiDevice->destroySampler(m_sampler);
        if (m_pipeline.isValid()) m_rhiDevice->destroyPipeline(m_pipeline);
        if (m_pipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_pipelineLayout);
        if (m_bindGroupLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_bindGroupLayout);
        if (m_fragmentShader.isValid()) m_rhiDevice->destroyShader(m_fragmentShader);
        if (m_vertexShader.isValid()) m_rhiDevice->destroyShader(m_vertexShader);
    }
    m_atlas.shutdown();
    m_rhiDevice = nullptr;
    m_vertexShader = {};
    m_fragmentShader = {};
    m_bindGroupLayout = {};
    m_pipelineLayout = {};
    m_pipeline = {};
    m_sampler = {};
    m_atlasView = {};
    m_atlasBindGroup = {};
    m_boundAtlasTexture = {};
    m_vertexBuffer = {};
    m_vertexCapacity = 0;
    m_vertexBufferState = RhiResourceState::Undefined;
    m_requests.clear();
    m_vertices.clear();
    m_recordIndex = 0;
    m_collecting = false;
    m_prepared = false;
    m_recording = false;
}

bool TextRenderer::createPipelineResources() {
    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/ui_text_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/ui_text_rhi.frag");
    if (!vertexSource || !fragmentSource) {
        return false;
    }

    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "UiText.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_vertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "UiText.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = m_rhiDevice->createShader(shaderDesc);

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "UiText.BindGroupLayout";
    bindGroupLayoutDesc.entries.push_back({0u, RhiBindingType::CombinedTextureSampler,
                                           rhiFlag(RhiShaderStage::Fragment), 1u});
    m_bindGroupLayout = m_rhiDevice->createBindGroupLayout(bindGroupLayoutDesc);

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "UiText.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes = sizeof(glm::vec4);
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex);
    m_pipelineLayout = m_rhiDevice->createPipelineLayout(pipelineLayoutDesc);

    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "UiText.Pipeline";
    pipelineDesc.vertexShader = m_vertexShader;
    pipelineDesc.fragmentShader = m_fragmentShader;
    pipelineDesc.layout = m_pipelineLayout;
    pipelineDesc.vertexInput.bindings.push_back({0u, sizeof(Vertex), RhiVertexInputRate::Vertex});
    pipelineDesc.vertexInput.attributes.push_back({0u, 0u, RhiVertexFormat::Float2, offsetof(Vertex, x)});
    pipelineDesc.vertexInput.attributes.push_back({1u, 0u, RhiVertexFormat::Float2, offsetof(Vertex, u)});
    pipelineDesc.vertexInput.attributes.push_back({2u, 0u, RhiVertexFormat::Float4, offsetof(Vertex, r)});
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.raster.scissorEnabled = true;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.colorFormats.push_back(m_rhiDevice->swapchainColorFormat());
    pipelineDesc.depthFormat = m_rhiDevice->swapchainDepthStencilFormat();
    RhiBlendAttachmentState blend;
    blend.blendEnabled = true;
    blend.srcColor = RhiBlendFactor::SrcAlpha;
    blend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    blend.srcAlpha = RhiBlendFactor::One;
    blend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    pipelineDesc.blend.attachments.push_back(blend);
    m_pipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);

    RhiSamplerDesc samplerDesc;
    samplerDesc.minFilter = RhiFilter::Linear;
    samplerDesc.magFilter = RhiFilter::Linear;
    samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    samplerDesc.addressU = RhiAddressMode::ClampToEdge;
    samplerDesc.addressV = RhiAddressMode::ClampToEdge;
    samplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_sampler = m_rhiDevice->createSampler(samplerDesc);

    return m_vertexShader.isValid() && m_fragmentShader.isValid() &&
           m_bindGroupLayout.isValid() && m_pipelineLayout.isValid() &&
           m_pipeline.isValid() && m_sampler.isValid();
}

bool TextRenderer::ensureVertexCapacity(const uint64_t requiredBytes) {
    if (m_rhiDevice == nullptr || requiredBytes == 0u) {
        return false;
    }
    if (m_vertexBuffer.isValid() && requiredBytes <= m_vertexCapacity) {
        return true;
    }
    uint64_t newCapacity = std::max<uint64_t>(64u * 1024u, m_vertexCapacity);
    while (newCapacity < requiredBytes) {
        if (newCapacity > std::numeric_limits<uint64_t>::max() / 2u) {
            return false;
        }
        newCapacity *= 2u;
    }
    RhiBufferDesc desc;
    desc.debugName = "UiText.DynamicVertexBuffer";
    desc.size = newCapacity;
    desc.usage = rhiFlag(RhiBufferUsage::Vertex) | rhiFlag(RhiBufferUsage::TransferDst);
    desc.memoryUsage = RhiMemoryUsage::GpuOnly;
    desc.initialState = RhiResourceState::VertexBuffer;
    const RhiBufferHandle newBuffer = m_rhiDevice->createBuffer(desc, nullptr, 0u);
    if (!newBuffer.isValid()) {
        return false;
    }
    if (m_vertexBuffer.isValid()) {
        m_rhiDevice->destroyBuffer(m_vertexBuffer);
    }
    m_vertexBuffer = newBuffer;
    m_vertexCapacity = newCapacity;
    m_vertexBufferState = RhiResourceState::VertexBuffer;
    return true;
}

void TextRenderer::beginFrameCollection(const float screenWidth, const float screenHeight) {
    if (m_rhiDevice == nullptr || screenWidth <= 0.0f || screenHeight <= 0.0f || m_recording) {
        std::abort();
    }
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    m_requests.clear();
    m_vertices.clear();
    m_recordIndex = 0;
    m_collecting = true;
    m_prepared = false;
}

RhiRect2D TextRenderer::resolveScissor(const UIRenderContext& context) {
    if (context.hasScissor) {
        return context.scissor;
    }
    const float pixelScale = context.pixelScale();
    return {
        0,
        0,
        static_cast<uint32_t>(std::max(1l, std::lround(static_cast<float>(context.screenWidth) * pixelScale))),
        static_cast<uint32_t>(std::max(1l, std::lround(static_cast<float>(context.screenHeight) * pixelScale)))
    };
}

void TextRenderer::draw(const UIRenderContext& context,
                        const std::string& text,
                        const float x,
                        const float y,
                        const float scale,
                        const std::array<float, 4>& color) const {
    if (text.empty()) {
        return;
    }
    if (context.phase == UIRenderPhase::CollectText) {
        if (!m_collecting || m_prepared) {
            std::abort();
        }
        DrawRequest request;
        request.text = text;
        request.x = x;
        request.y = y;
        request.scale = scale;
        request.color = color;
        request.scissor = resolveScissor(context);
        m_requests.push_back(std::move(request));
        return;
    }
    recordPreparedRequest(context, text, x, y, scale, color);
}

bool TextRenderer::prepareFrame(RhiCommandList& commandList) {
    if (!m_collecting || m_prepared || m_rhiDevice == nullptr) {
        return false;
    }
    for (const DrawRequest& request : m_requests) {
        if (!m_atlas.ensureGlyphs(request.text)) {
            return false;
        }
    }
    if (m_atlas.requiresTextureRecreation()) {
        if (m_atlasBindGroup.isValid()) {
            m_rhiDevice->destroyBindGroup(m_atlasBindGroup);
            m_atlasBindGroup = {};
        }
        if (m_atlasView.isValid()) {
            m_rhiDevice->destroyTextureView(m_atlasView);
            m_atlasView = {};
        }
        m_boundAtlasTexture = {};
    }
    if (!m_requests.empty() && !m_atlas.prepareUpload(commandList)) {
        return false;
    }

    m_vertices.clear();
    for (DrawRequest& request : m_requests) {
        if (!generateRequestVertices(request)) {
            return false;
        }
    }
    if (!m_vertices.empty()) {
        const uint64_t byteSize = static_cast<uint64_t>(m_vertices.size()) * sizeof(Vertex);
        if (!ensureVertexCapacity(byteSize)) {
            return false;
        }
        commandList.bufferBarrier({m_vertexBuffer, m_vertexBufferState, RhiResourceState::TransferDst});
        commandList.updateBuffer(m_vertexBuffer, 0u, m_vertices.data(), static_cast<size_t>(byteSize));
        commandList.bufferBarrier({m_vertexBuffer, RhiResourceState::TransferDst,
                                   RhiResourceState::VertexBuffer});
        m_vertexBufferState = RhiResourceState::VertexBuffer;
        if (!rebuildAtlasBinding()) {
            return false;
        }
    }
    m_collecting = false;
    m_prepared = true;
    return true;
}

bool TextRenderer::rebuildAtlasBinding() {
    const RhiTextureHandle texture = m_atlas.textureHandle();
    if (!texture.isValid()) {
        return false;
    }
    if (m_atlasBindGroup.isValid() && sameHandle(texture, m_boundAtlasTexture)) {
        return true;
    }
    if (m_atlasBindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_atlasBindGroup);
        m_atlasBindGroup = {};
    }
    if (m_atlasView.isValid()) {
        m_rhiDevice->destroyTextureView(m_atlasView);
        m_atlasView = {};
    }
    RhiTextureViewDesc viewDesc;
    viewDesc.texture = texture;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = RhiTextureFormat::R8Unorm;
    m_atlasView = m_rhiDevice->createTextureView(viewDesc);
    if (!m_atlasView.isValid()) {
        return false;
    }
    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_bindGroupLayout;
    RhiBindGroupEntry entry;
    entry.binding = 0u;
    entry.resource.combinedTextureSampler.textureView = m_atlasView;
    entry.resource.combinedTextureSampler.sampler = m_sampler;
    bindGroupDesc.entries.push_back(entry);
    m_atlasBindGroup = m_rhiDevice->createBindGroup(bindGroupDesc);
    if (!m_atlasBindGroup.isValid()) {
        return false;
    }
    m_boundAtlasTexture = texture;
    return true;
}

bool TextRenderer::generateRequestVertices(DrawRequest& request) {
    request.firstVertex = static_cast<uint32_t>(m_vertices.size());
    request.vertexCount = 0u;
    if (request.scale <= 0.0f) {
        return false;
    }
    const float renderScale = request.scale;
    const float pixelScale = (8.0f * renderScale) / static_cast<float>(m_atlas.pixelHeight());
    const float lineHeight = static_cast<float>(m_atlas.lineHeight()) * pixelScale;
    const float descent = static_cast<float>(m_atlas.descent()) * pixelScale;
    const float originX = std::round(request.x);
    float cursorX = originX;
    float cursorY = std::round(request.y + descent);

    const char* cursor = request.text.data();
    const char* const end = cursor + request.text.size();
    auto append = [&](const float x, const float y, const float u, const float v) {
        m_vertices.push_back({x, y, u, v, request.color[0], request.color[1],
                              request.color[2], request.color[3]});
    };
    while (cursor < end) {
        if (*cursor == '\n') {
            cursorX = originX;
            cursorY -= lineHeight;
            ++cursor;
            continue;
        }
        if (*cursor == '\r') {
            ++cursor;
            continue;
        }
        uint32_t codepoint = 0u;
        if (!decodeUtf8Codepoint(cursor, end, codepoint)) {
            return false;
        }
        const GlyphInfo* glyph = m_atlas.findGlyph(codepoint);
        if (glyph == nullptr) {
            return false;
        }
        const float x = std::round(cursorX + static_cast<float>(glyph->bearingX) * pixelScale);
        const float y = std::round(cursorY -
                                   static_cast<float>(glyph->bitmapHeight - glyph->bearingY) * pixelScale);
        const float width = static_cast<float>(glyph->bitmapWidth) * pixelScale;
        const float height = static_cast<float>(glyph->bitmapHeight) * pixelScale;
        if (glyph->bitmapWidth > 0 && glyph->bitmapHeight > 0) {
            append(x, y, glyph->uvMinX, glyph->uvMinY);
            append(x + width, y, glyph->uvMaxX, glyph->uvMinY);
            append(x + width, y + height, glyph->uvMaxX, glyph->uvMaxY);
            append(x, y, glyph->uvMinX, glyph->uvMinY);
            append(x + width, y + height, glyph->uvMaxX, glyph->uvMaxY);
            append(x, y + height, glyph->uvMinX, glyph->uvMaxY);
        }
        cursorX += static_cast<float>(glyph->advanceX >> 6) * pixelScale;
    }
    request.vertexCount = static_cast<uint32_t>(m_vertices.size()) - request.firstVertex;
    return true;
}

void TextRenderer::beginFrameRecording() {
    if (!m_prepared || m_recording) {
        std::abort();
    }
    m_recordIndex = 0u;
    m_recording = true;
}

void TextRenderer::recordPreparedRequest(const UIRenderContext& context,
                                         const std::string& text,
                                         const float x,
                                         const float y,
                                         const float scale,
                                         const std::array<float, 4>& color) const {
    if (!m_recording || context.commandList == nullptr || m_recordIndex >= m_requests.size()) {
        std::abort();
    }
    const DrawRequest& request = m_requests[m_recordIndex++];
    if (request.text != text || request.x != x || request.y != y ||
        request.scale != scale || request.color != color) {
        std::abort();
    }
    if (request.vertexCount == 0u) {
        return;
    }
    RhiCommandList& commandList = *context.commandList;
    commandList.setScissor(request.scissor);
    commandList.setGraphicsPipeline(m_pipeline);
    commandList.setBindGroup(0u, m_atlasBindGroup);
    commandList.setVertexBuffer(0u, m_vertexBuffer, 0u);
    const glm::vec4 pushConstants(m_screenWidth, m_screenHeight, 0.0f, 0.0f);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants), rhiFlag(RhiShaderStage::Vertex));
    commandList.draw(request.vertexCount, 1u, request.firstVertex, 0u);
}

bool TextRenderer::endFrameRecording() const {
    if (!m_recording) {
        return false;
    }
    m_recording = false;
    return m_recordIndex == m_requests.size();
}

TextRenderer::TextMetrics TextRenderer::measureText(const std::string& text,
                                                    const float scale) const {
    TextMetrics result;
    if (text.empty()) {
        return result;
    }
    if (scale <= 0.0f) {
        std::abort();
    }
    if (m_prepared) {
        const char* cursor = text.data();
        const char* const end = cursor + text.size();
        while (cursor < end) {
            if (*cursor == '\n' || *cursor == '\r') {
                ++cursor;
                continue;
            }
            uint32_t codepoint = 0u;
            if (!decodeUtf8Codepoint(cursor, end, codepoint) ||
                m_atlas.findGlyph(codepoint) == nullptr) {
                std::abort();
            }
        }
    } else if (!m_atlas.ensureGlyphs(text)) {
        std::abort();
    }

    const float renderScale = scale;
    const float pixelScale = (8.0f * renderScale) / static_cast<float>(m_atlas.pixelHeight());
    const float lineHeight = static_cast<float>(m_atlas.lineHeight()) * pixelScale;
    float maximumWidth = 0.0f;
    float currentWidth = 0.0f;
    int lineCount = 1;
    const char* cursor = text.data();
    const char* const end = cursor + text.size();
    while (cursor < end) {
        if (*cursor == '\n') {
            maximumWidth = std::max(maximumWidth, currentWidth);
            currentWidth = 0.0f;
            ++lineCount;
            ++cursor;
            continue;
        }
        if (*cursor == '\r') {
            ++cursor;
            continue;
        }
        uint32_t codepoint = 0u;
        if (!decodeUtf8Codepoint(cursor, end, codepoint)) {
            std::abort();
        }
        const GlyphInfo* glyph = m_atlas.findGlyph(codepoint);
        if (glyph == nullptr) {
            std::abort();
        }
        currentWidth += static_cast<float>(glyph->advanceX >> 6) * pixelScale;
    }
    result.width = std::max(maximumWidth, currentWidth);
    result.height = lineHeight * static_cast<float>(lineCount);
    return result;
}
