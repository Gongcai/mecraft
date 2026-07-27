#include "ImGuiRhiRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>

#include "imgui_impl_glfw.h"

#include "engine/platform/Window.h"
#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiShaderSourceLoader.h"

namespace {
constexpr uint64_t kInitialVertexBufferCapacity = 64u * 1024u;
constexpr uint64_t kInitialIndexBufferCapacity = 16u * 1024u;

struct ImGuiPushConstants {
    float scale[2];
    float translate[2];
};

static_assert(sizeof(ImGuiPushConstants) == 16u);
static_assert(sizeof(ImDrawIdx) == sizeof(uint16_t) ||
              sizeof(ImDrawIdx) == sizeof(uint32_t));

[[nodiscard]] uint64_t alignedUploadSize(const uint64_t size) {
    return (size + 3u) & ~uint64_t{3u};
}

[[nodiscard]] bool checkedMultiply(const uint64_t lhs,
                                   const uint64_t rhs,
                                   uint64_t& result) {
    if (lhs != 0u && rhs > std::numeric_limits<uint64_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

[[nodiscard]] uint64_t grownCapacity(const uint64_t current,
                                     const uint64_t required) {
    uint64_t capacity = std::max<uint64_t>(current, 4u);
    while (capacity < required) {
        if (capacity > std::numeric_limits<uint64_t>::max() / 2u) {
            return required;
        }
        capacity *= 2u;
    }
    return capacity;
}
} // namespace

ImGuiRhiRenderer::~ImGuiRhiRenderer() {
    shutdown();
}

bool ImGuiRhiRenderer::init(const Window& window,
                            RhiDevice& rhiDevice,
                            const bool dockingEnabled,
                            std::string iniFile) {
    shutdown();
    IMGUI_CHECKVERSION();
    m_context = ImGui::CreateContext();
    if (m_context == nullptr) {
        return false;
    }
    ImGui::SetCurrentContext(m_context);
    m_iniFile = std::move(iniFile);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    if (dockingEnabled) {
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    }
    io.IniFilename = m_iniFile.empty() ? nullptr : m_iniFile.c_str();
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendRendererName = "mecraft_rhi";
    io.BackendRendererUserData = this;
    if (!ImGui_ImplGlfw_InitForOther(window.getHandle(), true)) {
        shutdown();
        return false;
    }
    m_platformInitialized = true;
    m_rhiDevice = &rhiDevice;
    if (!createRhiResources()) {
        shutdown();
        return false;
    }
    ImGui::StyleColorsDark();
    return true;
}

void ImGuiRhiRenderer::shutdown() {
    if (m_context != nullptr) {
        ImGui::SetCurrentContext(m_context);
        ImGui::DestroyPlatformWindows();
    }
    destroyRhiResources();
    if (m_context != nullptr) {
        ImGuiIO& io = ImGui::GetIO();
        io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendRendererName = nullptr;
        io.BackendRendererUserData = nullptr;
    }
    if (m_platformInitialized) {
        ImGui_ImplGlfw_Shutdown();
        m_platformInitialized = false;
    }
    if (m_context != nullptr) {
        ImGui::DestroyContext(m_context);
        m_context = nullptr;
    }
    m_iniFile.clear();
    m_rhiDevice = nullptr;
    m_frameStarted = false;
    m_framePrepared = false;
}

bool ImGuiRhiRenderer::beginFrame(const int framebufferWidth,
                                  const int framebufferHeight) {
    if (m_context == nullptr || m_rhiDevice == nullptr ||
        framebufferWidth <= 0 || framebufferHeight <= 0 || m_frameStarted) {
        return false;
    }
    ImGui::SetCurrentContext(m_context);
    ImGui_ImplGlfw_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    if (!std::isfinite(io.DisplayFramebufferScale.x) ||
        !std::isfinite(io.DisplayFramebufferScale.y) ||
        io.DisplayFramebufferScale.x <= 0.0f ||
        io.DisplayFramebufferScale.y <= 0.0f) {
        return false;
    }
    io.DisplaySize = {
        static_cast<float>(framebufferWidth) / io.DisplayFramebufferScale.x,
        static_cast<float>(framebufferHeight) / io.DisplayFramebufferScale.y};
    m_framebufferWidth = framebufferWidth;
    m_framebufferHeight = framebufferHeight;
    m_framePrepared = false;
    ImGui::NewFrame();
    m_frameStarted = true;
    return true;
}

bool ImGuiRhiRenderer::prepareDrawData(RhiCommandList& commandList) {
    if (!m_frameStarted || m_context == nullptr) {
        return false;
    }
    ImGui::SetCurrentContext(m_context);
    ImGui::Render();
    m_frameStarted = false;
    const ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData == nullptr || !buildPreparedDraws(*drawData) ||
        !uploadDrawBuffers(commandList)) {
        return false;
    }
    m_framePrepared = true;
    return true;
}

ImTextureID ImGuiRhiRenderer::registerTexture(
    const RhiTextureViewHandle textureView,
    const RhiSamplerHandle sampler) {
    if (m_rhiDevice == nullptr || !textureView.isValid() || !sampler.isValid()) {
        return ImTextureID_Invalid;
    }
    RhiBindGroupDesc desc;
    desc.layout = m_bindGroupLayout;
    RhiBindGroupEntry entry;
    entry.binding = 0u;
    entry.resource.combinedTextureSampler = {textureView, sampler};
    desc.entries.push_back(entry);
    const RhiBindGroupHandle bindGroup = m_rhiDevice->createBindGroup(desc);
    if (!bindGroup.isValid()) {
        return ImTextureID_Invalid;
    }
    const ImTextureID id = static_cast<ImTextureID>(m_nextTextureId++);
    m_textureBindings.push_back({id, bindGroup, false});
    return id;
}

void ImGuiRhiRenderer::unregisterTexture(const ImTextureID textureId) {
    const auto it = std::find_if(
        m_textureBindings.begin(), m_textureBindings.end(),
        [textureId](const TextureBinding& binding) {
            return binding.id == textureId && !binding.font;
        });
    if (it == m_textureBindings.end()) {
        return;
    }
    if (m_rhiDevice != nullptr && it->bindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(it->bindGroup);
    }
    m_textureBindings.erase(it);
}

bool ImGuiRhiRenderer::createRhiResources() {
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    int bytesPerPixel = 0;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->GetTexDataAsRGBA32(
        &fontPixels, &fontWidth, &fontHeight, &bytesPerPixel);
    if (fontPixels == nullptr || fontWidth <= 0 || fontHeight <= 0 ||
        bytesPerPixel != 4) {
        return false;
    }
    uint64_t fontByteSize = 0u;
    if (!checkedMultiply(static_cast<uint64_t>(fontWidth),
                         static_cast<uint64_t>(fontHeight), fontByteSize) ||
        !checkedMultiply(fontByteSize,
                         static_cast<uint64_t>(bytesPerPixel), fontByteSize) ||
        fontByteSize > std::numeric_limits<size_t>::max()) {
        return false;
    }
    RhiTextureDesc textureDesc;
    textureDesc.debugName = "ImGui.FontAtlas";
    textureDesc.format = RhiTextureFormat::Rgba8Unorm;
    textureDesc.width = static_cast<uint32_t>(fontWidth);
    textureDesc.height = static_cast<uint32_t>(fontHeight);
    textureDesc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                        rhiFlag(RhiTextureUsage::TransferDst);
    RhiTextureInitialData initialData;
    initialData.pixels = fontPixels;
    initialData.sizeBytes = static_cast<size_t>(fontByteSize);
    initialData.finalState = RhiResourceState::ShaderRead;
    m_fontTexture = m_rhiDevice->createTexture(textureDesc, &initialData);
    RhiTextureViewDesc viewDesc;
    viewDesc.texture = m_fontTexture;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = RhiTextureFormat::Rgba8Unorm;
    m_fontTextureView = m_rhiDevice->createTextureView(viewDesc);
    RhiSamplerDesc samplerDesc;
    samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    m_fontSampler = m_rhiDevice->createSampler(samplerDesc);

    const std::optional<std::string> vertexSource =
        renderer::rhi::loadShaderSource("assets/shaders/imgui_rhi.vert");
    const std::optional<std::string> fragmentSource =
        renderer::rhi::loadShaderSource("assets/shaders/imgui_rhi.frag");
    if (!vertexSource || !fragmentSource) {
        return false;
    }
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "ImGui.Vertex";
    shaderDesc.stage = RhiShaderStage::Vertex;
    shaderDesc.source = vertexSource->c_str();
    shaderDesc.sourceSize = vertexSource->size();
    m_vertexShader = m_rhiDevice->createShader(shaderDesc);
    shaderDesc.debugName = "ImGui.Fragment";
    shaderDesc.stage = RhiShaderStage::Fragment;
    shaderDesc.source = fragmentSource->c_str();
    shaderDesc.sourceSize = fragmentSource->size();
    m_fragmentShader = m_rhiDevice->createShader(shaderDesc);

    RhiBindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.debugName = "ImGui.BindGroupLayout";
    bindGroupLayoutDesc.entries.push_back({
        0u, RhiBindingType::CombinedTextureSampler,
        rhiFlag(RhiShaderStage::Fragment), 1u});
    m_bindGroupLayout = m_rhiDevice->createBindGroupLayout(bindGroupLayoutDesc);
    RhiPipelineLayoutDesc layoutDesc;
    layoutDesc.debugName = "ImGui.PipelineLayout";
    layoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout);
    layoutDesc.pushConstantBytes = sizeof(ImGuiPushConstants);
    layoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Vertex);
    m_pipelineLayout = m_rhiDevice->createPipelineLayout(layoutDesc);
    RhiGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.debugName = "ImGui.Pipeline";
    pipelineDesc.vertexShader = m_vertexShader;
    pipelineDesc.fragmentShader = m_fragmentShader;
    pipelineDesc.layout = m_pipelineLayout;
    pipelineDesc.vertexInput.bindings.push_back(
        {0u, sizeof(ImDrawVert), RhiVertexInputRate::Vertex});
    pipelineDesc.vertexInput.attributes = {
        {0u, 0u, RhiVertexFormat::Float2,
         static_cast<uint32_t>(offsetof(ImDrawVert, pos))},
        {1u, 0u, RhiVertexFormat::Float2,
         static_cast<uint32_t>(offsetof(ImDrawVert, uv))},
        {2u, 0u, RhiVertexFormat::Uint,
         static_cast<uint32_t>(offsetof(ImDrawVert, col))}};
    pipelineDesc.raster.cullMode = RhiCullMode::None;
    pipelineDesc.raster.scissorEnabled = true;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    RhiBlendAttachmentState blend;
    blend.blendEnabled = true;
    blend.srcColor = RhiBlendFactor::SrcAlpha;
    blend.dstColor = RhiBlendFactor::OneMinusSrcAlpha;
    blend.srcAlpha = RhiBlendFactor::One;
    blend.dstAlpha = RhiBlendFactor::OneMinusSrcAlpha;
    pipelineDesc.blend.attachments.push_back(blend);
    pipelineDesc.colorFormats.push_back(m_rhiDevice->swapchainColorFormat());
    pipelineDesc.depthFormat = m_rhiDevice->swapchainDepthStencilFormat();
    m_pipeline = m_rhiDevice->createGraphicsPipeline(pipelineDesc);

    RhiBufferDesc bufferDesc;
    bufferDesc.debugName = "ImGui.Vertices";
    bufferDesc.size = kInitialVertexBufferCapacity;
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    bufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    bufferDesc.initialState = RhiResourceState::VertexBuffer;
    m_vertexBuffer = m_rhiDevice->createBuffer(bufferDesc, nullptr, 0u);
    m_vertexBufferCapacity = kInitialVertexBufferCapacity;
    m_vertexBufferState = RhiResourceState::VertexBuffer;
    bufferDesc.debugName = "ImGui.Indices";
    bufferDesc.size = kInitialIndexBufferCapacity;
    bufferDesc.usage = rhiFlag(RhiBufferUsage::Index) |
                       rhiFlag(RhiBufferUsage::TransferDst);
    bufferDesc.initialState = RhiResourceState::IndexBuffer;
    m_indexBuffer = m_rhiDevice->createBuffer(bufferDesc, nullptr, 0u);
    m_indexBufferCapacity = kInitialIndexBufferCapacity;
    m_indexBufferState = RhiResourceState::IndexBuffer;

    if (!m_fontTexture.isValid() || !m_fontTextureView.isValid() ||
        !m_fontSampler.isValid() || !m_vertexShader.isValid() ||
        !m_fragmentShader.isValid() || !m_bindGroupLayout.isValid() ||
        !m_pipelineLayout.isValid() || !m_pipeline.isValid() ||
        !m_vertexBuffer.isValid() || !m_indexBuffer.isValid()) {
        return false;
    }
    const ImTextureID fontId = registerTexture(m_fontTextureView, m_fontSampler);
    if (fontId == ImTextureID_Invalid) {
        return false;
    }
    m_textureBindings.back().font = true;
    io.Fonts->SetTexID(fontId);
    return true;
}

void ImGuiRhiRenderer::destroyRhiResources() {
    if (m_context != nullptr) {
        ImGui::GetIO().Fonts->SetTexID(ImTextureID_Invalid);
    }
    if (m_rhiDevice != nullptr) {
        for (const TextureBinding& binding : m_textureBindings) {
            if (binding.bindGroup.isValid()) {
                m_rhiDevice->destroyBindGroup(binding.bindGroup);
            }
        }
        if (m_indexBuffer.isValid()) m_rhiDevice->destroyBuffer(m_indexBuffer);
        if (m_vertexBuffer.isValid()) m_rhiDevice->destroyBuffer(m_vertexBuffer);
        if (m_pipeline.isValid()) m_rhiDevice->destroyPipeline(m_pipeline);
        if (m_pipelineLayout.isValid()) m_rhiDevice->destroyPipelineLayout(m_pipelineLayout);
        if (m_bindGroupLayout.isValid()) m_rhiDevice->destroyBindGroupLayout(m_bindGroupLayout);
        if (m_fragmentShader.isValid()) m_rhiDevice->destroyShader(m_fragmentShader);
        if (m_vertexShader.isValid()) m_rhiDevice->destroyShader(m_vertexShader);
        if (m_fontSampler.isValid()) m_rhiDevice->destroySampler(m_fontSampler);
        if (m_fontTextureView.isValid()) m_rhiDevice->destroyTextureView(m_fontTextureView);
        if (m_fontTexture.isValid()) m_rhiDevice->destroyTexture(m_fontTexture);
    }
    m_textureBindings.clear();
    m_fontTexture = {};
    m_fontTextureView = {};
    m_fontSampler = {};
    m_vertexShader = {};
    m_fragmentShader = {};
    m_bindGroupLayout = {};
    m_pipelineLayout = {};
    m_pipeline = {};
    m_vertexBuffer = {};
    m_indexBuffer = {};
    m_vertexBufferCapacity = 0u;
    m_indexBufferCapacity = 0u;
    m_vertexBufferState = RhiResourceState::Undefined;
    m_indexBufferState = RhiResourceState::Undefined;
    m_vertices.clear();
    m_indexBytes.clear();
    m_preparedDraws.clear();
    m_nextTextureId = 1u;
}

RhiBindGroupHandle ImGuiRhiRenderer::findBindGroup(
    const ImTextureID textureId) const {
    const auto it = std::find_if(
        m_textureBindings.begin(), m_textureBindings.end(),
        [textureId](const TextureBinding& binding) {
            return binding.id == textureId;
        });
    return it == m_textureBindings.end() ? RhiBindGroupHandle{} : it->bindGroup;
}

bool ImGuiRhiRenderer::buildPreparedDraws(const ImDrawData& drawData) {
    m_vertices.clear();
    m_indexBytes.clear();
    m_preparedDraws.clear();
    m_displayPos = drawData.DisplayPos;
    m_displaySize = drawData.DisplaySize;
    if (drawData.TotalVtxCount < 0 || drawData.TotalIdxCount < 0 ||
        m_displaySize.x <= 0.0f || m_displaySize.y <= 0.0f) {
        return false;
    }
    m_vertices.reserve(static_cast<size_t>(drawData.TotalVtxCount));
    uint64_t totalIndexBytes = 0u;
    if (!checkedMultiply(static_cast<uint64_t>(drawData.TotalIdxCount),
                         sizeof(ImDrawIdx), totalIndexBytes) ||
        totalIndexBytes > std::numeric_limits<size_t>::max() - 3u) {
        return false;
    }
    m_indexBytes.resize(static_cast<size_t>(alignedUploadSize(totalIndexBytes)), 0u);
    uint64_t globalVertexOffset = 0u;
    uint64_t globalIndexOffset = 0u;
    uint64_t indexByteOffset = 0u;
    for (const ImDrawList* drawList : drawData.CmdLists) {
        if (drawList == nullptr) {
            return false;
        }
        m_vertices.insert(
            m_vertices.end(), drawList->VtxBuffer.begin(), drawList->VtxBuffer.end());
        const uint64_t drawListIndexBytes =
            static_cast<uint64_t>(drawList->IdxBuffer.Size) * sizeof(ImDrawIdx);
        if (drawListIndexBytes != 0u) {
            std::memcpy(m_indexBytes.data() + indexByteOffset,
                        drawList->IdxBuffer.Data,
                        static_cast<size_t>(drawListIndexBytes));
        }
        for (const ImDrawCmd& command : drawList->CmdBuffer) {
            if (command.UserCallback != nullptr) {
                if (command.UserCallback != ImDrawCallback_ResetRenderState) {
                    return false;
                }
                PreparedDraw reset;
                reset.resetState = true;
                m_preparedDraws.push_back(reset);
                continue;
            }
            if (command.ElemCount == 0u) {
                continue;
            }
            const RhiBindGroupHandle bindGroup = findBindGroup(command.GetTexID());
            if (!bindGroup.isValid() ||
                command.IdxOffset > static_cast<unsigned>(drawList->IdxBuffer.Size) ||
                command.ElemCount > static_cast<unsigned>(drawList->IdxBuffer.Size) -
                                        command.IdxOffset ||
                command.VtxOffset > static_cast<unsigned>(drawList->VtxBuffer.Size)) {
                return false;
            }
            const float clipMinX =
                (command.ClipRect.x - drawData.DisplayPos.x) * drawData.FramebufferScale.x;
            const float clipMinY =
                (command.ClipRect.y - drawData.DisplayPos.y) * drawData.FramebufferScale.y;
            const float clipMaxX =
                (command.ClipRect.z - drawData.DisplayPos.x) * drawData.FramebufferScale.x;
            const float clipMaxY =
                (command.ClipRect.w - drawData.DisplayPos.y) * drawData.FramebufferScale.y;
            if (!std::isfinite(clipMinX) || !std::isfinite(clipMinY) ||
                !std::isfinite(clipMaxX) || !std::isfinite(clipMaxY)) {
                return false;
            }
            const float minX = std::clamp(
                clipMinX, 0.0f, static_cast<float>(m_framebufferWidth));
            const float minY = std::clamp(
                clipMinY, 0.0f, static_cast<float>(m_framebufferHeight));
            const float maxX = std::clamp(
                clipMaxX, 0.0f, static_cast<float>(m_framebufferWidth));
            const float maxY = std::clamp(
                clipMaxY, 0.0f, static_cast<float>(m_framebufferHeight));
            if (maxX <= minX || maxY <= minY) {
                continue;
            }
            const int32_t scissorMinX = static_cast<int32_t>(std::floor(minX));
            const int32_t scissorMaxX = static_cast<int32_t>(std::ceil(maxX));
            const int32_t scissorMinY = static_cast<int32_t>(std::floor(minY));
            const int32_t scissorMaxY = static_cast<int32_t>(std::ceil(maxY));
            const uint64_t firstIndex = globalIndexOffset + command.IdxOffset;
            const uint64_t vertexOffset = globalVertexOffset + command.VtxOffset;
            if (firstIndex > std::numeric_limits<uint32_t>::max() ||
                vertexOffset > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
                return false;
            }
            PreparedDraw draw;
            draw.scissor = {
                scissorMinX,
                m_framebufferHeight - scissorMaxY,
                static_cast<uint32_t>(scissorMaxX - scissorMinX),
                static_cast<uint32_t>(scissorMaxY - scissorMinY)};
            draw.bindGroup = bindGroup;
            draw.indexCount = command.ElemCount;
            draw.firstIndex = static_cast<uint32_t>(firstIndex);
            draw.vertexOffset = static_cast<int32_t>(vertexOffset);
            m_preparedDraws.push_back(draw);
        }
        globalVertexOffset += static_cast<uint64_t>(drawList->VtxBuffer.Size);
        globalIndexOffset += static_cast<uint64_t>(drawList->IdxBuffer.Size);
        indexByteOffset += drawListIndexBytes;
    }
    return globalVertexOffset == static_cast<uint64_t>(drawData.TotalVtxCount) &&
           globalIndexOffset == static_cast<uint64_t>(drawData.TotalIdxCount) &&
           indexByteOffset == totalIndexBytes;
}

bool ImGuiRhiRenderer::uploadDrawBuffers(RhiCommandList& commandList) {
    if (m_vertices.empty() && m_indexBytes.empty()) {
        return true;
    }
    uint64_t vertexBytes = 0u;
    if (!checkedMultiply(m_vertices.size(), sizeof(ImDrawVert), vertexBytes) ||
        vertexBytes == 0u || m_indexBytes.empty()) {
        return false;
    }
    const auto ensureCapacity = [this](
        RhiBufferHandle& buffer, uint64_t& capacity, RhiResourceState& state,
        const uint64_t required, const RhiBufferUsage usage, const char* name) {
        if (required <= capacity) {
            return true;
        }
        RhiBufferDesc desc;
        desc.debugName = name;
        desc.size = grownCapacity(capacity, required);
        desc.usage = rhiFlag(usage) | rhiFlag(RhiBufferUsage::TransferDst);
        desc.memoryUsage = RhiMemoryUsage::GpuOnly;
        const RhiResourceState functionalState = usage == RhiBufferUsage::Vertex
            ? RhiResourceState::VertexBuffer : RhiResourceState::IndexBuffer;
        desc.initialState = functionalState;
        const RhiBufferHandle replacement =
            m_rhiDevice->createBuffer(desc, nullptr, 0u);
        if (!replacement.isValid()) {
            return false;
        }
        if (buffer.isValid()) {
            m_rhiDevice->destroyBuffer(buffer);
        }
        buffer = replacement;
        capacity = desc.size;
        state = functionalState;
        return true;
    };
    if (!ensureCapacity(m_vertexBuffer, m_vertexBufferCapacity, m_vertexBufferState,
                        vertexBytes, RhiBufferUsage::Vertex, "ImGui.Vertices") ||
        !ensureCapacity(m_indexBuffer, m_indexBufferCapacity, m_indexBufferState,
                        m_indexBytes.size(), RhiBufferUsage::Index, "ImGui.Indices")) {
        return false;
    }
    commandList.bufferBarrier(
        {m_vertexBuffer, m_vertexBufferState, RhiResourceState::TransferDst});
    commandList.updateBuffer(
        m_vertexBuffer, 0u, m_vertices.data(), static_cast<size_t>(vertexBytes));
    commandList.bufferBarrier(
        {m_vertexBuffer, RhiResourceState::TransferDst, RhiResourceState::VertexBuffer});
    m_vertexBufferState = RhiResourceState::VertexBuffer;
    commandList.bufferBarrier(
        {m_indexBuffer, m_indexBufferState, RhiResourceState::TransferDst});
    commandList.updateBuffer(
        m_indexBuffer, 0u, m_indexBytes.data(), m_indexBytes.size());
    commandList.bufferBarrier(
        {m_indexBuffer, RhiResourceState::TransferDst, RhiResourceState::IndexBuffer});
    m_indexBufferState = RhiResourceState::IndexBuffer;
    return true;
}

void ImGuiRhiRenderer::bindRenderState(RhiCommandList& commandList) const {
    ImGuiPushConstants constants;
    constants.scale[0] = 2.0f / m_displaySize.x;
    constants.scale[1] = -2.0f / m_displaySize.y;
    constants.translate[0] = -1.0f - m_displayPos.x * constants.scale[0];
    constants.translate[1] = 1.0f - m_displayPos.y * constants.scale[1];
    commandList.setViewport({
        0.0f, 0.0f, static_cast<float>(m_framebufferWidth),
        static_cast<float>(m_framebufferHeight), 0.0f, 1.0f});
    commandList.setGraphicsPipeline(m_pipeline);
    commandList.setVertexBuffer(0u, m_vertexBuffer, 0u);
    commandList.setIndexBuffer(
        m_indexBuffer,
        sizeof(ImDrawIdx) == sizeof(uint16_t)
            ? RhiIndexFormat::Uint16 : RhiIndexFormat::Uint32,
        0u);
    commandList.pushConstants(
        &constants, sizeof(constants), rhiFlag(RhiShaderStage::Vertex));
}

void ImGuiRhiRenderer::recordDraws(RhiCommandList& commandList) const {
    if (!m_framePrepared) {
        std::abort();
    }
    if (m_preparedDraws.empty()) {
        return;
    }
    bindRenderState(commandList);
    for (const PreparedDraw& draw : m_preparedDraws) {
        if (draw.resetState) {
            bindRenderState(commandList);
            continue;
        }
        commandList.setBindGroup(0u, draw.bindGroup);
        commandList.setScissor(draw.scissor);
        commandList.drawIndexed(
            draw.indexCount, 1u, draw.firstIndex, draw.vertexOffset, 0u);
    }
}
