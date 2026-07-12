#include "renderer/rhi/gl/GlRhiDevice.h"

#include "renderer/rhi/RhiHandleAllocator.h"
#include "renderer/rhi/gl/GlRhiTextureRegistry.h"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr GLuint kRhiPushConstantBinding = 15u;

struct GlFormatInfo {
    GLenum internalFormat = GL_RGBA8;
    GLenum externalFormat = GL_RGBA;
    GLenum type = GL_UNSIGNED_BYTE;
    bool depth = false;
    bool stencil = false;
};

[[nodiscard]] const char* rhiDebugName(const char* name) {
    return name != nullptr ? name : "";
}

void logRhiError(const char* message) {
    std::cerr << "GlRhiDevice: " << message << '\n';
}

void logFramebufferStatus(const char* context, const GLenum status) {
    std::cerr << "GlRhiDevice: " << context << " framebuffer status=0x"
              << std::hex << status << std::dec << '\n';
}

void labelGlObject(const GLenum identifier, const GLuint name, const char* label) {
    if (name == 0u || label == nullptr || label[0] == '\0' || !GLAD_GL_VERSION_4_3) {
        return;
    }
    glObjectLabel(identifier, name, -1, label);
}

void clearFramebufferColor(const GLuint framebuffer, const GLint drawBuffer, const float* color) {
    if (framebuffer == 0u) {
        glClearBufferfv(GL_COLOR, drawBuffer, color);
        return;
    }
    glClearNamedFramebufferfv(framebuffer, GL_COLOR, drawBuffer, color);
}

void clearFramebufferDepth(const GLuint framebuffer, const float depth) {
    if (framebuffer == 0u) {
        glClearBufferfv(GL_DEPTH, 0, &depth);
        return;
    }
    glClearNamedFramebufferfv(framebuffer, GL_DEPTH, 0, &depth);
}

void clearFramebufferDepthStencil(const GLuint framebuffer, const float depth, const uint32_t stencil) {
    if (framebuffer == 0u) {
        glClearBufferfi(GL_DEPTH_STENCIL, 0, depth, static_cast<GLint>(stencil));
        return;
    }
    glClearNamedFramebufferfi(framebuffer, GL_DEPTH_STENCIL, 0, depth, static_cast<GLint>(stencil));
}

void invalidateFramebufferData(const GLuint framebuffer, const std::vector<GLenum>& attachments) {
    if (attachments.empty()) {
        return;
    }
    if (framebuffer == 0u) {
        glInvalidateFramebuffer(GL_FRAMEBUFFER, static_cast<GLsizei>(attachments.size()), attachments.data());
        return;
    }
    glInvalidateNamedFramebufferData(framebuffer, static_cast<GLsizei>(attachments.size()), attachments.data());
}

template <typename Handle>
[[nodiscard]] bool sameHandle(const Handle a, const Handle b) {
    return a.index == b.index && a.generation == b.generation;
}

[[nodiscard]] GLenum toGlShaderStage(const RhiShaderStage stage) {
    switch (stage) {
        case RhiShaderStage::Vertex: return GL_VERTEX_SHADER;
        case RhiShaderStage::Fragment: return GL_FRAGMENT_SHADER;
        case RhiShaderStage::Compute: return GL_COMPUTE_SHADER;
    }
    return 0u;
}

[[nodiscard]] GLenum toGlTextureTarget(const RhiTextureDimension dimension) {
    switch (dimension) {
        case RhiTextureDimension::Texture2D: return GL_TEXTURE_2D;
        case RhiTextureDimension::Texture2DArray: return GL_TEXTURE_2D_ARRAY;
        case RhiTextureDimension::Texture3D: return GL_TEXTURE_3D;
        case RhiTextureDimension::Cube: return GL_TEXTURE_CUBE_MAP;
    }
    return 0u;
}

[[nodiscard]] GLenum toGlTextureViewTarget(const RhiTextureViewType viewType) {
    switch (viewType) {
        case RhiTextureViewType::Texture2D: return GL_TEXTURE_2D;
        case RhiTextureViewType::Texture2DArray: return GL_TEXTURE_2D_ARRAY;
        case RhiTextureViewType::Texture3D: return GL_TEXTURE_3D;
        case RhiTextureViewType::Cube: return GL_TEXTURE_CUBE_MAP;
    }
    return 0u;
}

[[nodiscard]] bool toGlFormatInfo(const RhiTextureFormat format, GlFormatInfo& out) {
    switch (format) {
        case RhiTextureFormat::R8Unorm:
            out = {GL_R8, GL_RED, GL_UNSIGNED_BYTE, false, false};
            return true;
        case RhiTextureFormat::Rg8Unorm:
            out = {GL_RG8, GL_RG, GL_UNSIGNED_BYTE, false, false};
            return true;
        case RhiTextureFormat::Rgba8Unorm:
            out = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, false, false};
            return true;
        case RhiTextureFormat::Rgba8Srgb:
            out = {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, false, false};
            return true;
        case RhiTextureFormat::Rg16Float:
            out = {GL_RG16F, GL_RG, GL_HALF_FLOAT, false, false};
            return true;
        case RhiTextureFormat::Rgba16Float:
            out = {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, false, false};
            return true;
        case RhiTextureFormat::Rgba32Float:
            out = {GL_RGBA32F, GL_RGBA, GL_FLOAT, false, false};
            return true;
        case RhiTextureFormat::R16Float:
            out = {GL_R16F, GL_RED, GL_HALF_FLOAT, false, false};
            return true;
        case RhiTextureFormat::R32Float:
            out = {GL_R32F, GL_RED, GL_FLOAT, false, false};
            return true;
        case RhiTextureFormat::Depth16:
            out = {GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, true, false};
            return true;
        case RhiTextureFormat::Depth24:
            out = {GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, true, false};
            return true;
        case RhiTextureFormat::Depth24Stencil8:
            out = {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, true, true};
            return true;
        case RhiTextureFormat::Depth32Float:
            out = {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, true, false};
            return true;
        case RhiTextureFormat::Undefined:
            return false;
    }
    return false;
}

[[nodiscard]] size_t textureFormatSizeBytes(const RhiTextureFormat format) {
    switch (format) {
        case RhiTextureFormat::R8Unorm: return 1u;
        case RhiTextureFormat::Rg8Unorm: return 2u;
        case RhiTextureFormat::Rgba8Unorm:
        case RhiTextureFormat::Rgba8Srgb:
        case RhiTextureFormat::R32Float:
        case RhiTextureFormat::Depth24:
        case RhiTextureFormat::Depth24Stencil8:
        case RhiTextureFormat::Depth32Float: return 4u;
        case RhiTextureFormat::R16Float:
        case RhiTextureFormat::Depth16: return 2u;
        case RhiTextureFormat::Rg16Float: return 4u;
        case RhiTextureFormat::Rgba16Float: return 8u;
        case RhiTextureFormat::Rgba32Float: return 16u;
        case RhiTextureFormat::Undefined: return 0u;
    }
    return 0u;
}

[[nodiscard]] GLenum toGlMinFilter(const RhiFilter filter, const RhiMipmapMode mipmapMode) {
    if (filter == RhiFilter::Nearest) {
        return mipmapMode == RhiMipmapMode::Nearest ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_LINEAR;
    }
    return mipmapMode == RhiMipmapMode::Nearest ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
}

[[nodiscard]] GLenum toGlMagFilter(const RhiFilter filter) {
    return filter == RhiFilter::Nearest ? GL_NEAREST : GL_LINEAR;
}

[[nodiscard]] GLenum toGlAddressMode(const RhiAddressMode mode) {
    switch (mode) {
        case RhiAddressMode::Repeat: return GL_REPEAT;
        case RhiAddressMode::MirroredRepeat: return GL_MIRRORED_REPEAT;
        case RhiAddressMode::ClampToEdge: return GL_CLAMP_TO_EDGE;
        case RhiAddressMode::ClampToBorder: return GL_CLAMP_TO_BORDER;
    }
    return GL_CLAMP_TO_EDGE;
}

[[nodiscard]] std::array<GLfloat, 4> toGlBorderColor(const RhiBorderColor color) {
    switch (color) {
        case RhiBorderColor::TransparentBlack: return {0.0f, 0.0f, 0.0f, 0.0f};
        case RhiBorderColor::OpaqueBlack: return {0.0f, 0.0f, 0.0f, 1.0f};
        case RhiBorderColor::OpaqueWhite: return {1.0f, 1.0f, 1.0f, 1.0f};
    }
    return {0.0f, 0.0f, 0.0f, 0.0f};
}

[[nodiscard]] GLenum toGlCompareOp(const RhiCompareOp op) {
    switch (op) {
        case RhiCompareOp::Never: return GL_NEVER;
        case RhiCompareOp::Less: return GL_LESS;
        case RhiCompareOp::Equal: return GL_EQUAL;
        case RhiCompareOp::LessOrEqual: return GL_LEQUAL;
        case RhiCompareOp::Greater: return GL_GREATER;
        case RhiCompareOp::NotEqual: return GL_NOTEQUAL;
        case RhiCompareOp::GreaterOrEqual: return GL_GEQUAL;
        case RhiCompareOp::Always: return GL_ALWAYS;
    }
    return GL_ALWAYS;
}

[[nodiscard]] GLenum toGlTopology(const RhiPrimitiveTopology topology) {
    switch (topology) {
        case RhiPrimitiveTopology::TriangleList: return GL_TRIANGLES;
        case RhiPrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
        case RhiPrimitiveTopology::LineList: return GL_LINES;
        case RhiPrimitiveTopology::LineStrip: return GL_LINE_STRIP;
    }
    return GL_TRIANGLES;
}

[[nodiscard]] GLenum toGlBlendFactor(const RhiBlendFactor factor) {
    switch (factor) {
        case RhiBlendFactor::Zero: return GL_ZERO;
        case RhiBlendFactor::One: return GL_ONE;
        case RhiBlendFactor::SrcAlpha: return GL_SRC_ALPHA;
        case RhiBlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
        case RhiBlendFactor::SrcColor: return GL_SRC_COLOR;
        case RhiBlendFactor::OneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
        case RhiBlendFactor::DstAlpha: return GL_DST_ALPHA;
        case RhiBlendFactor::OneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
        case RhiBlendFactor::DstColor: return GL_DST_COLOR;
        case RhiBlendFactor::OneMinusDstColor: return GL_ONE_MINUS_DST_COLOR;
    }
    return GL_ONE;
}

[[nodiscard]] GLenum toGlBlendOp(const RhiBlendOp op) {
    switch (op) {
        case RhiBlendOp::Add: return GL_FUNC_ADD;
        case RhiBlendOp::Subtract: return GL_FUNC_SUBTRACT;
        case RhiBlendOp::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
        case RhiBlendOp::Min: return GL_MIN;
        case RhiBlendOp::Max: return GL_MAX;
    }
    return GL_FUNC_ADD;
}

struct GlVertexFormatInfo {
    GLint componentCount = 3;
    GLenum type = GL_FLOAT;
    bool integer = false;
    bool normalized = false;
};

[[nodiscard]] GlVertexFormatInfo toGlVertexFormat(const RhiVertexFormat format) {
    switch (format) {
        case RhiVertexFormat::Float: return {1, GL_FLOAT, false, false};
        case RhiVertexFormat::Float2: return {2, GL_FLOAT, false, false};
        case RhiVertexFormat::Float3: return {3, GL_FLOAT, false, false};
        case RhiVertexFormat::Float4: return {4, GL_FLOAT, false, false};
        case RhiVertexFormat::Uint: return {1, GL_UNSIGNED_INT, true, false};
        case RhiVertexFormat::Uint2: return {2, GL_UNSIGNED_INT, true, false};
        case RhiVertexFormat::Uint3: return {3, GL_UNSIGNED_INT, true, false};
        case RhiVertexFormat::Uint4: return {4, GL_UNSIGNED_INT, true, false};
        case RhiVertexFormat::Sint8: return {1, GL_BYTE, true, false};
        case RhiVertexFormat::Unorm8: return {1, GL_UNSIGNED_BYTE, false, true};
        case RhiVertexFormat::Uint8: return {1, GL_UNSIGNED_BYTE, true, false};
        case RhiVertexFormat::Uint16: return {1, GL_UNSIGNED_SHORT, true, false};
    }
    return {3, GL_FLOAT, false, false};
}

[[nodiscard]] uint64_t indexElementSize(const RhiIndexFormat format) {
    return format == RhiIndexFormat::Uint16 ? 2u : 4u;
}

[[nodiscard]] GLbitfield barrierBitsForState(const RhiResourceState state) {
    switch (state) {
        case RhiResourceState::RenderTarget: return GL_FRAMEBUFFER_BARRIER_BIT;
        case RhiResourceState::DepthWrite: return GL_FRAMEBUFFER_BARRIER_BIT;
        case RhiResourceState::DepthRead: return GL_TEXTURE_FETCH_BARRIER_BIT;
        case RhiResourceState::ShaderRead: return GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT;
        case RhiResourceState::ShaderWrite: return GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT;
        case RhiResourceState::TransferSrc: return GL_TEXTURE_UPDATE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT;
        case RhiResourceState::TransferDst: return GL_TEXTURE_UPDATE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT;
        case RhiResourceState::VertexBuffer: return GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT;
        case RhiResourceState::IndexBuffer: return GL_ELEMENT_ARRAY_BARRIER_BIT;
        case RhiResourceState::IndirectArgument: return GL_COMMAND_BARRIER_BIT;
        case RhiResourceState::UniformBuffer: return GL_UNIFORM_BARRIER_BIT;
        case RhiResourceState::StorageBuffer: return GL_SHADER_STORAGE_BARRIER_BIT;
        case RhiResourceState::Undefined:
        case RhiResourceState::Present:
            return 0u;
    }
    return 0u;
}

[[nodiscard]] GLuint compileShaderObject(const RhiShaderDesc& desc) {
    const GLenum stage = toGlShaderStage(desc.stage);
    if (stage == 0u || desc.source == nullptr || desc.sourceSize == 0u) {
        logRhiError("shader creation requires a valid GLSL source stage");
        return 0u;
    }

    const GLuint shader = glCreateShader(stage);
    const auto sourceSize = static_cast<GLint>(desc.sourceSize);
    const char* source = desc.source;
    glShaderSource(shader, 1, &source, &sourceSize);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) {
        labelGlObject(GL_SHADER, shader, desc.debugName);
        return shader;
    }

    std::array<char, 2048> infoLog{};
    glGetShaderInfoLog(shader, static_cast<GLsizei>(infoLog.size()), nullptr, infoLog.data());
    std::cerr << "GlRhiDevice: shader compilation failed [" << rhiDebugName(desc.debugName)
              << "]\n" << infoLog.data() << '\n';
    glDeleteShader(shader);
    return 0u;
}

struct GlBufferRecord {
    GLuint buffer = 0u;
    RhiBufferDesc desc;
    bool active = false;
};

struct GlTextureRecord {
    GLuint texture = 0u;
    GLenum target = 0u;
    RhiTextureDesc desc;
    GlFormatInfo format;
    bool active = false;
};

struct GlResolvedTextureRecord {
    GLuint texture = 0u;
    GLenum target = 0u;
    RhiTextureDesc desc;
    GlFormatInfo format;
    bool valid = false;
};

struct GlTextureViewRecord {
    GLuint texture = 0u;
    GLenum target = 0u;
    RhiTextureViewDesc desc;
    RhiTextureFormat resolvedFormat = RhiTextureFormat::Undefined;
    GlFormatInfo format;
    bool swapchainBackbuffer = false;
    bool swapchainDepthStencil = false;
    bool ownsTexture = false;
    bool active = false;
};

struct GlSamplerRecord {
    GLuint sampler = 0u;
    RhiSamplerDesc desc;
    bool active = false;
};

struct GlShaderRecord {
    GLuint shader = 0u;
    RhiShaderStage stage = RhiShaderStage::Vertex;
    bool active = false;
};

struct GlBindGroupLayoutRecord {
    RhiBindGroupLayoutDesc desc;
    bool active = false;
};

struct GlPipelineLayoutRecord {
    RhiPipelineLayoutDesc desc;
    bool active = false;
};

struct GlPipelineRecord {
    GLuint program = 0u;
    GLuint vertexArray = 0u;
    bool compute = false;
    RhiGraphicsPipelineDesc graphicsDesc;
    RhiComputePipelineDesc computeDesc;
    bool active = false;
};

struct GlBindGroupRecord {
    RhiBindGroupDesc desc;
    bool active = false;
};

struct GlQueryPoolRecord {
    std::vector<GLuint> queries;
    bool active = false;
};

struct GlFramebufferRecord {
    GLuint framebuffer = 0u;
    std::vector<RhiTextureViewHandle> colorViews;
    RhiTextureViewHandle depthView;
    bool active = false;
};

template <typename Handle, typename Record>
[[nodiscard]] Record* recordForHandle(RhiHandleAllocator<Handle>& allocator,
                                      std::vector<Record>& records,
                                      const Handle handle) {
    if (!allocator.isAlive(handle)) {
        return nullptr;
    }

    const uint32_t slot = handle.index - 1u;
    if (slot >= records.size() || !records[slot].active) {
        return nullptr;
    }
    return &records[slot];
}

template <typename Handle, typename Record>
[[nodiscard]] const Record* recordForHandle(const RhiHandleAllocator<Handle>& allocator,
                                            const std::vector<Record>& records,
                                            const Handle handle) {
    if (!allocator.isAlive(handle)) {
        return nullptr;
    }

    const uint32_t slot = handle.index - 1u;
    if (slot >= records.size() || !records[slot].active) {
        return nullptr;
    }
    return &records[slot];
}

} // namespace

struct GlRhiDeviceData {
    RhiHandleAllocator<RhiBufferHandle> buffers;
    RhiHandleAllocator<RhiTextureHandle> textures;
    RhiHandleAllocator<RhiTextureViewHandle> textureViews;
    RhiHandleAllocator<RhiSamplerHandle> samplers;
    RhiHandleAllocator<RhiShaderHandle> shaders;
    RhiHandleAllocator<RhiBindGroupLayoutHandle> bindGroupLayouts;
    RhiHandleAllocator<RhiPipelineLayoutHandle> pipelineLayouts;
    RhiHandleAllocator<RhiPipelineHandle> pipelines;
    RhiHandleAllocator<RhiBindGroupHandle> bindGroups;
    RhiHandleAllocator<RhiQueryPoolHandle> queryPools;

    std::vector<GlBufferRecord> bufferRecords;
    std::vector<GlTextureRecord> textureRecords;
    std::vector<GlTextureViewRecord> textureViewRecords;
    std::vector<GlSamplerRecord> samplerRecords;
    std::vector<GlShaderRecord> shaderRecords;
    std::vector<GlBindGroupLayoutRecord> bindGroupLayoutRecords;
    std::vector<GlPipelineLayoutRecord> pipelineLayoutRecords;
    std::vector<GlPipelineRecord> pipelineRecords;
    std::vector<GlBindGroupRecord> bindGroupRecords;
    std::vector<GlQueryPoolRecord> queryPoolRecords;
    std::vector<GlFramebufferRecord> framebufferCache;

    RhiTextureViewHandle swapchainColorView;
    RhiTextureViewHandle swapchainDepthStencilView;
    RhiTextureFormat swapchainFormat = RhiTextureFormat::Rgba8Unorm;
    RhiTextureFormat swapchainDepthStencilFormat = RhiTextureFormat::Depth24;
    uint32_t swapchainWidth = 1u;
    uint32_t swapchainHeight = 1u;
    GLuint pushConstantBuffer = 0u;
    uint32_t pushConstantCapacity = 0u;
    GLuint currentFramebuffer = 0u;
    std::vector<GLenum> currentStoreDiscardAttachments;
    RhiIndexFormat indexFormat = RhiIndexFormat::Uint32;
    uint64_t indexOffset = 0u;
};

namespace {

struct GlBlitEndpoint {
    GLuint texture = 0u;
    GLenum target = 0u;
    RhiTextureDesc desc;
    GlFormatInfo format;
    uint32_t attachmentMip = 0u;
    bool swapchain = false;
    bool valid = false;
};

[[nodiscard]] uint32_t mipExtent(const uint32_t extent, const uint32_t mipLevel) {
    if (mipLevel >= 31u) {
        return 1u;
    }
    return std::max(1u, extent >> mipLevel);
}

[[nodiscard]] bool resolveTextureRecord(GlRhiDeviceData& data,
                                        const RhiTextureHandle handle,
                                        GlResolvedTextureRecord& resolved) {
    resolved = {};
    if (!handle.isValid()) {
        return false;
    }

    const GlTextureRecord* deviceRecord = recordForHandle(data.textures, data.textureRecords, handle);
    renderer::rhi::gl::GlRhiTextureRegistration registration;
    const bool registered = renderer::rhi::gl::textureRegistration(handle, registration);
    if (deviceRecord != nullptr && registered) {
        return false;
    }

    if (deviceRecord != nullptr) {
        resolved.texture = deviceRecord->texture;
        resolved.target = deviceRecord->target;
        resolved.desc = deviceRecord->desc;
        resolved.format = deviceRecord->format;
        resolved.valid = true;
        return true;
    }

    if (registered) {
        const GLenum target = toGlTextureTarget(registration.dimension);
        GlFormatInfo format;
        if (target == 0u || !toGlFormatInfo(registration.format, format)) {
            return false;
        }

        resolved.texture = registration.textureId;
        resolved.target = target;
        resolved.desc.debugName = nullptr;
        resolved.desc.dimension = registration.dimension;
        resolved.desc.format = registration.format;
        resolved.desc.width = registration.width;
        resolved.desc.height = registration.height;
        resolved.desc.depthOrLayers = registration.depthOrLayers;
        resolved.desc.mipLevels = registration.mipLevels;
        resolved.desc.sampleCount = registration.sampleCount;
        resolved.desc.usage = registration.usage;
        resolved.format = format;
        resolved.valid = true;
        return true;
    }

    return false;
}

[[nodiscard]] bool resolveTextureHandleForBlit(GlRhiDeviceData& data,
                                               const RhiTextureHandle handle,
                                               const uint32_t mipLevel,
                                               GlBlitEndpoint& endpoint) {
    GlResolvedTextureRecord resolved;
    if (!resolveTextureRecord(data, handle, resolved)) {
        logRhiError("blitTexture received an invalid texture handle");
        return false;
    }

    if (mipLevel >= resolved.desc.mipLevels) {
        return false;
    }

    endpoint.texture = resolved.texture;
    endpoint.target = resolved.target;
    endpoint.desc = resolved.desc;
    endpoint.desc.width = mipExtent(resolved.desc.width, mipLevel);
    endpoint.desc.height = mipExtent(resolved.desc.height, mipLevel);
    endpoint.desc.mipLevels = 1u;
    endpoint.format = resolved.format;
    endpoint.attachmentMip = mipLevel;
    endpoint.valid = true;
    return true;
}

[[nodiscard]] bool resolveTextureViewForBlit(GlRhiDeviceData& data,
                                             const RhiTextureViewHandle view,
                                             GlBlitEndpoint& endpoint) {
    if (!view.isValid()) {
        return false;
    }

    const GlTextureViewRecord* viewRecord =
        recordForHandle(data.textureViews, data.textureViewRecords, view);
    if (viewRecord == nullptr) {
        return false;
    }

    if (viewRecord->swapchainBackbuffer || viewRecord->swapchainDepthStencil) {
        endpoint.texture = 0u;
        endpoint.target = GL_TEXTURE_2D;
        endpoint.desc.dimension = RhiTextureDimension::Texture2D;
        endpoint.desc.format = viewRecord->resolvedFormat;
        endpoint.desc.width = data.swapchainWidth;
        endpoint.desc.height = data.swapchainHeight;
        endpoint.desc.depthOrLayers = 1u;
        endpoint.desc.mipLevels = 1u;
        endpoint.desc.sampleCount = 1u;
        endpoint.desc.usage = viewRecord->swapchainBackbuffer
            ? rhiFlag(RhiTextureUsage::Present) | rhiFlag(RhiTextureUsage::ColorAttachment)
            : rhiFlag(RhiTextureUsage::DepthStencilAttachment);
        endpoint.format = viewRecord->format;
        endpoint.attachmentMip = 0u;
        endpoint.swapchain = true;
        endpoint.valid = true;
        return true;
    }

    GlResolvedTextureRecord textureRecord;
    if (!resolveTextureRecord(data, viewRecord->desc.texture, textureRecord)) {
        return false;
    }

    endpoint.texture = viewRecord->texture;
    endpoint.target = viewRecord->target;
    endpoint.desc = textureRecord.desc;
    endpoint.desc.format = viewRecord->resolvedFormat;
    endpoint.desc.width = mipExtent(textureRecord.desc.width, viewRecord->desc.baseMip);
    endpoint.desc.height = mipExtent(textureRecord.desc.height, viewRecord->desc.baseMip);
    endpoint.desc.depthOrLayers = viewRecord->desc.layerCount;
    endpoint.desc.mipLevels = viewRecord->desc.mipCount;
    endpoint.format = viewRecord->format;
    endpoint.attachmentMip = 0u;
    endpoint.valid = true;
    return true;
}

[[nodiscard]] bool resolveBlitEndpoint(GlRhiDeviceData& data,
                                       const RhiTextureHandle texture,
                                       const RhiTextureViewHandle view,
                                       const uint32_t mipLevel,
                                       GlBlitEndpoint& endpoint) {
    const bool hasTexture = texture.isValid();
    const bool hasView = view.isValid();
    if (hasTexture == hasView) {
        return false;
    }

    return hasTexture
        ? resolveTextureHandleForBlit(data, texture, mipLevel, endpoint)
        : resolveTextureViewForBlit(data, view, endpoint);
}

} // namespace

GlRhiCommandList::GlRhiCommandList() = default;

void GlRhiCommandList::attachDevice(GlRhiDevice* device) {
    m_device = device;
}

void GlRhiCommandList::resetFrameState() {
    m_graphicsPipeline = {};
    m_computePipeline = {};
    m_rendering = false;
    if (m_device != nullptr && m_device->m_data) {
        m_device->m_data->currentFramebuffer = 0u;
        m_device->m_data->currentStoreDiscardAttachments.clear();
        m_device->m_data->indexFormat = RhiIndexFormat::Uint32;
        m_device->m_data->indexOffset = 0u;
    }
}

void GlRhiCommandList::beginDebugLabel(const char* name, const glm::vec4& color) {
    (void) color;
    if (name != nullptr && name[0] != '\0' && GLAD_GL_VERSION_4_3) {
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0u, -1, name);
    }
}

void GlRhiCommandList::endDebugLabel() {
    if (GLAD_GL_VERSION_4_3) {
        glPopDebugGroup();
    }
}

void GlRhiCommandList::insertDebugMarker(const char* name, const glm::vec4& color) {
    (void) color;
    if (name != nullptr && name[0] != '\0' && GLAD_GL_VERSION_4_3) {
        glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION,
                             GL_DEBUG_TYPE_MARKER,
                             0u,
                             GL_DEBUG_SEVERITY_NOTIFICATION,
                             -1,
                             name);
    }
}

void GlRhiCommandList::textureBarrier(const RhiTextureBarrier& barrier) {
    if (m_device == nullptr || !m_device->m_data ||
        recordForHandle(m_device->m_data->textures, m_device->m_data->textureRecords, barrier.texture) == nullptr) {
        logRhiError("textureBarrier received an invalid texture handle");
        return;
    }

    const GLbitfield bits = barrierBitsForState(barrier.newState);
    if (bits != 0u) {
        glMemoryBarrier(bits);
    }
}

void GlRhiCommandList::bufferBarrier(const RhiBufferBarrier& barrier) {
    if (m_device == nullptr || !m_device->m_data ||
        recordForHandle(m_device->m_data->buffers, m_device->m_data->bufferRecords, barrier.buffer) == nullptr) {
        logRhiError("bufferBarrier received an invalid buffer handle");
        return;
    }

    const GLbitfield bits = barrierBitsForState(barrier.newState);
    if (bits != 0u) {
        glMemoryBarrier(bits);
    }
}

void GlRhiCommandList::beginRendering(const RhiRenderingInfo& info) {
    if (m_device == nullptr || !m_device->m_data) {
        logRhiError("beginRendering requires an initialized device");
        return;
    }

    auto& data = *m_device->m_data;
    std::vector<RhiTextureViewHandle> colorViews;
    colorViews.reserve(info.colorAttachmentCount);
    for (uint32_t i = 0u; i < info.colorAttachmentCount; ++i) {
        const RhiTextureViewHandle view = info.colorAttachments[i].view;
        if (recordForHandle(data.textureViews, data.textureViewRecords, view) == nullptr) {
            logRhiError("beginRendering received an invalid color attachment view");
            return;
        }
        colorViews.push_back(view);
    }

    RhiTextureViewHandle depthView;
    const GlTextureViewRecord* depthViewRecord = nullptr;
    if (info.depthStencilAttachment != nullptr && info.depthStencilAttachment->view.isValid()) {
        depthView = info.depthStencilAttachment->view;
        depthViewRecord = recordForHandle(data.textureViews, data.textureViewRecords, depthView);
        if (depthViewRecord == nullptr) {
            logRhiError("beginRendering received an invalid depth attachment view");
            return;
        }
    }

    bool renderToSwapchain = false;
    for (const RhiTextureViewHandle view : colorViews) {
        const GlTextureViewRecord* viewRecord = recordForHandle(data.textureViews, data.textureViewRecords, view);
        if (viewRecord != nullptr && viewRecord->swapchainBackbuffer) {
            renderToSwapchain = true;
            break;
        }
    }
    if (renderToSwapchain && colorViews.size() != 1u) {
        logRhiError("beginRendering requires exactly one swapchain color attachment");
        return;
    }
    if (renderToSwapchain && depthView.isValid() &&
        (depthViewRecord == nullptr || !depthViewRecord->swapchainDepthStencil)) {
        logRhiError("beginRendering requires the swapchain depth-stencil view for swapchain depth output");
        return;
    }
    if (!renderToSwapchain && depthViewRecord != nullptr && depthViewRecord->swapchainDepthStencil) {
        logRhiError("beginRendering requires swapchain depth-stencil with swapchain color output");
        return;
    }

    GLuint framebuffer = 0u;
    if ((!colorViews.empty() || depthView.isValid()) && !renderToSwapchain) {
        GlFramebufferRecord* cached = nullptr;
        for (GlFramebufferRecord& record : data.framebufferCache) {
            if (record.active && sameHandle(record.depthView, depthView) &&
                record.colorViews.size() == colorViews.size()) {
                bool sameColors = true;
                for (size_t i = 0u; i < colorViews.size(); ++i) {
                    if (!sameHandle(record.colorViews[i], colorViews[i])) {
                        sameColors = false;
                        break;
                    }
                }
                if (sameColors) {
                    cached = &record;
                    break;
                }
            }
        }

        if (cached == nullptr) {
            GlFramebufferRecord record;
            record.colorViews = colorViews;
            record.depthView = depthView;
            record.active = true;
            glCreateFramebuffers(1, &record.framebuffer);
            for (uint32_t i = 0u; i < colorViews.size(); ++i) {
                const GlTextureViewRecord* viewRecord =
                    recordForHandle(data.textureViews, data.textureViewRecords, colorViews[i]);
                glNamedFramebufferTexture(record.framebuffer, GL_COLOR_ATTACHMENT0 + i, viewRecord->texture, 0);
            }
            if (depthView.isValid()) {
                const GlTextureViewRecord* viewRecord =
                    recordForHandle(data.textureViews, data.textureViewRecords, depthView);
                const GLenum attachment = viewRecord->format.stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
                glNamedFramebufferTexture(record.framebuffer, attachment, viewRecord->texture, 0);
            }

            std::vector<GLenum> drawBuffers;
            drawBuffers.reserve(colorViews.size());
            for (uint32_t i = 0u; i < colorViews.size(); ++i) {
                drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + i);
            }
            if (!drawBuffers.empty()) {
                glNamedFramebufferDrawBuffers(record.framebuffer,
                                              static_cast<GLsizei>(drawBuffers.size()),
                                              drawBuffers.data());
            } else {
                glNamedFramebufferDrawBuffer(record.framebuffer, GL_NONE);
                glNamedFramebufferReadBuffer(record.framebuffer, GL_NONE);
            }

            const GLenum framebufferStatus =
                glCheckNamedFramebufferStatus(record.framebuffer, GL_FRAMEBUFFER);
            if (framebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
                glDeleteFramebuffers(1, &record.framebuffer);
                logFramebufferStatus("beginRendering", framebufferStatus);
                return;
            }

            data.framebufferCache.push_back(record);
            cached = &data.framebufferCache.back();
        }
        framebuffer = cached->framebuffer;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    if (renderToSwapchain) {
        glDrawBuffer(GL_BACK);
        glReadBuffer(GL_BACK);
    }
    data.currentFramebuffer = framebuffer;
    data.currentStoreDiscardAttachments.clear();
    glViewport(info.renderArea.x,
               info.renderArea.y,
               static_cast<GLsizei>(info.renderArea.width),
               static_cast<GLsizei>(info.renderArea.height));

    for (uint32_t i = 0u; i < info.colorAttachmentCount; ++i) {
        if (info.colorAttachments[i].loadOp == RhiLoadOp::Clear) {
            clearFramebufferColor(framebuffer, static_cast<GLint>(i), info.colorAttachments[i].clearColor);
        }
        if (info.colorAttachments[i].storeOp == RhiStoreOp::DontCare) {
            data.currentStoreDiscardAttachments.push_back(GL_COLOR_ATTACHMENT0 + i);
        }
    }

    if (info.depthStencilAttachment != nullptr && depthView.isValid()) {
        const auto* attachment = info.depthStencilAttachment;
        const GlTextureViewRecord* viewRecord =
            recordForHandle(data.textureViews, data.textureViewRecords, depthView);
        if (attachment->depthLoadOp == RhiLoadOp::Clear) {
            if (viewRecord->format.stencil) {
                clearFramebufferDepthStencil(framebuffer, attachment->clearDepth, attachment->clearStencil);
            } else {
                clearFramebufferDepth(framebuffer, attachment->clearDepth);
            }
        }
        if (attachment->depthStoreOp == RhiStoreOp::DontCare) {
            data.currentStoreDiscardAttachments.push_back(
                viewRecord->format.stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT);
        }
    }

    m_rendering = true;
}

void GlRhiCommandList::endRendering() {
    if (m_device == nullptr || !m_device->m_data) {
        logRhiError("endRendering requires an initialized device");
        return;
    }

    auto& data = *m_device->m_data;
    invalidateFramebufferData(data.currentFramebuffer, data.currentStoreDiscardAttachments);
    glBindFramebuffer(GL_FRAMEBUFFER, 0u);
    data.currentFramebuffer = 0u;
    data.currentStoreDiscardAttachments.clear();
    m_rendering = false;
}

void GlRhiCommandList::clearDepthAttachment(const float depth, const RhiRect2D& rect) {
    if (!m_rendering) {
        logRhiError("clearDepthAttachment requires an active rendering scope");
        return;
    }
    glEnable(GL_SCISSOR_TEST);
    glScissor(rect.x, rect.y,
              static_cast<GLsizei>(rect.width),
              static_cast<GLsizei>(rect.height));
    glDepthMask(GL_TRUE);
    glClearBufferfv(GL_DEPTH, 0, &depth);
}

void GlRhiCommandList::setViewport(const RhiViewport& viewport) {
    glViewport(static_cast<GLint>(viewport.x),
               static_cast<GLint>(viewport.y),
               static_cast<GLsizei>(viewport.width),
               static_cast<GLsizei>(viewport.height));
    glDepthRange(viewport.minDepth, viewport.maxDepth);
}

void GlRhiCommandList::setScissor(const RhiRect2D& rect) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(rect.x, rect.y, static_cast<GLsizei>(rect.width), static_cast<GLsizei>(rect.height));
}

void GlRhiCommandList::setGraphicsPipeline(RhiPipelineHandle pipeline) {
    if (m_device == nullptr || !m_device->m_data) {
        logRhiError("setGraphicsPipeline requires an initialized device");
        return;
    }

    const GlPipelineRecord* record =
        recordForHandle(m_device->m_data->pipelines, m_device->m_data->pipelineRecords, pipeline);
    if (record == nullptr || record->compute) {
        logRhiError("setGraphicsPipeline received an invalid graphics pipeline");
        return;
    }

    const RhiGraphicsPipelineDesc& desc = record->graphicsDesc;
    glUseProgram(record->program);
    glBindVertexArray(record->vertexArray);

    if (desc.depthStencil.depthTestEnabled) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(toGlCompareOp(desc.depthStencil.depthCompare));
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(desc.depthStencil.depthWriteEnabled ? GL_TRUE : GL_FALSE);

    if (desc.raster.cullMode == RhiCullMode::None) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(desc.raster.cullMode == RhiCullMode::Front ? GL_FRONT : GL_BACK);
    }
    glFrontFace(desc.raster.frontFace == RhiFrontFace::CounterClockwise ? GL_CCW : GL_CW);
    if (desc.raster.depthBiasEnabled) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(desc.raster.depthBiasSlopeFactor,
                        desc.raster.depthBiasConstantFactor);
    } else {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    if (!desc.raster.scissorEnabled) {
        glDisable(GL_SCISSOR_TEST);
    }

    const size_t attachmentCount = std::max(desc.colorFormats.size(), desc.blend.attachments.size());
    for (size_t i = 0u; i < attachmentCount; ++i) {
        const bool hasBlend = i < desc.blend.attachments.size() && desc.blend.attachments[i].blendEnabled;
        if (hasBlend) {
            const RhiBlendAttachmentState& blend = desc.blend.attachments[i];
            glEnablei(GL_BLEND, static_cast<GLuint>(i));
            glBlendFuncSeparatei(static_cast<GLuint>(i),
                                 toGlBlendFactor(blend.srcColor),
                                 toGlBlendFactor(blend.dstColor),
                                 toGlBlendFactor(blend.srcAlpha),
                                 toGlBlendFactor(blend.dstAlpha));
            glBlendEquationSeparatei(static_cast<GLuint>(i),
                                     toGlBlendOp(blend.colorOp),
                                     toGlBlendOp(blend.alphaOp));
        } else {
            glDisablei(GL_BLEND, static_cast<GLuint>(i));
        }
    }

    m_graphicsPipeline = pipeline;
    m_computePipeline = {};
}

void GlRhiCommandList::setComputePipeline(RhiPipelineHandle pipeline) {
    if (m_device == nullptr || !m_device->m_data) {
        logRhiError("setComputePipeline requires an initialized device");
        return;
    }

    const GlPipelineRecord* record =
        recordForHandle(m_device->m_data->pipelines, m_device->m_data->pipelineRecords, pipeline);
    if (record == nullptr || !record->compute) {
        logRhiError("setComputePipeline received an invalid compute pipeline");
        return;
    }

    glUseProgram(record->program);
    m_computePipeline = pipeline;
    m_graphicsPipeline = {};
}

void GlRhiCommandList::setBindGroup(uint32_t setIndex, RhiBindGroupHandle bindGroup) {
    if (m_device == nullptr || !m_device->m_data) {
        logRhiError("setBindGroup requires an initialized device");
        return;
    }

    auto& data = *m_device->m_data;
    const GlBindGroupRecord* record = recordForHandle(data.bindGroups, data.bindGroupRecords, bindGroup);
    if (record == nullptr) {
        logRhiError("setBindGroup received an invalid bind group");
        return;
    }

    const GlBindGroupLayoutRecord* layoutRecord =
        recordForHandle(data.bindGroupLayouts, data.bindGroupLayoutRecords, record->desc.layout);
    if (layoutRecord == nullptr) {
        logRhiError("setBindGroup references an invalid layout");
        return;
    }

    const GlPipelineRecord* pipeline = nullptr;
    if (m_graphicsPipeline.isValid()) {
        pipeline = recordForHandle(data.pipelines, data.pipelineRecords, m_graphicsPipeline);
    } else if (m_computePipeline.isValid()) {
        pipeline = recordForHandle(data.pipelines, data.pipelineRecords, m_computePipeline);
    }
    if (pipeline == nullptr) {
        logRhiError("setBindGroup requires a bound pipeline");
        return;
    }
    const RhiPipelineLayoutHandle pipelineLayoutHandle =
        pipeline->compute ? pipeline->computeDesc.layout : pipeline->graphicsDesc.layout;
    const GlPipelineLayoutRecord* pipelineLayout =
        recordForHandle(data.pipelineLayouts, data.pipelineLayoutRecords, pipelineLayoutHandle);
    if (pipelineLayout == nullptr || setIndex >= pipelineLayout->desc.bindGroupLayouts.size() ||
        !sameHandle(pipelineLayout->desc.bindGroupLayouts[setIndex], record->desc.layout)) {
        logRhiError("setBindGroup is incompatible with the bound pipeline layout set");
        return;
    }

    for (const RhiBindGroupEntry& entry : record->desc.entries) {
        const auto layoutIt = std::find_if(layoutRecord->desc.entries.begin(),
                                           layoutRecord->desc.entries.end(),
                                           [&](const RhiBindGroupLayoutEntry& layoutEntry) {
                                               return layoutEntry.binding == entry.binding;
                                           });
        if (layoutIt == layoutRecord->desc.entries.end()) {
            logRhiError("setBindGroup entry is not declared by its layout");
            return;
        }

        switch (layoutIt->type) {
            case RhiBindingType::UniformBuffer: {
                const GlBufferRecord* buffer =
                    recordForHandle(data.buffers, data.bufferRecords, entry.resource.buffer.buffer);
                const uint64_t range = buffer != nullptr && entry.resource.buffer.range != 0u
                    ? entry.resource.buffer.range
                    : (buffer != nullptr && entry.resource.buffer.offset <= buffer->desc.size
                        ? buffer->desc.size - entry.resource.buffer.offset
                        : 0u);
                if (buffer == nullptr ||
                    (buffer->desc.usage & rhiFlag(RhiBufferUsage::Uniform)) == 0u ||
                    range == 0u || entry.resource.buffer.offset > buffer->desc.size ||
                    range > buffer->desc.size - entry.resource.buffer.offset) {
                    logRhiError("setBindGroup received an invalid uniform buffer binding");
                    return;
                }
                glBindBufferRange(GL_UNIFORM_BUFFER,
                                  entry.binding,
                                  buffer->buffer,
                                  static_cast<GLintptr>(entry.resource.buffer.offset),
                                  static_cast<GLsizeiptr>(range));
                break;
            }
            case RhiBindingType::StorageBuffer: {
                const GlBufferRecord* buffer =
                    recordForHandle(data.buffers, data.bufferRecords, entry.resource.buffer.buffer);
                const uint64_t range = buffer != nullptr && entry.resource.buffer.range != 0u
                    ? entry.resource.buffer.range
                    : (buffer != nullptr && entry.resource.buffer.offset <= buffer->desc.size
                        ? buffer->desc.size - entry.resource.buffer.offset
                        : 0u);
                if (buffer == nullptr ||
                    (buffer->desc.usage & rhiFlag(RhiBufferUsage::Storage)) == 0u ||
                    range == 0u || entry.resource.buffer.offset > buffer->desc.size ||
                    range > buffer->desc.size - entry.resource.buffer.offset) {
                    logRhiError("setBindGroup received an invalid storage buffer binding");
                    return;
                }
                glBindBufferRange(GL_SHADER_STORAGE_BUFFER,
                                  entry.binding,
                                  buffer->buffer,
                                  static_cast<GLintptr>(entry.resource.buffer.offset),
                                  static_cast<GLsizeiptr>(range));
                break;
            }
            case RhiBindingType::SampledTexture: {
                const GlTextureViewRecord* view =
                    recordForHandle(data.textureViews, data.textureViewRecords, entry.resource.textureView);
                if (view == nullptr) {
                    logRhiError("setBindGroup received an invalid sampled texture view");
                    return;
                }
                glBindTextureUnit(entry.binding, view->texture);
                break;
            }
            case RhiBindingType::StorageTexture: {
                const GlTextureViewRecord* view =
                    recordForHandle(data.textureViews, data.textureViewRecords, entry.resource.textureView);
                if (view == nullptr) {
                    logRhiError("setBindGroup received an invalid storage texture view");
                    return;
                }
                glBindImageTexture(entry.binding, view->texture, 0, GL_FALSE, 0, GL_READ_WRITE, view->format.internalFormat);
                break;
            }
            case RhiBindingType::Sampler: {
                const GlSamplerRecord* sampler =
                    recordForHandle(data.samplers, data.samplerRecords, entry.resource.sampler);
                if (sampler == nullptr) {
                    logRhiError("setBindGroup received an invalid sampler");
                    return;
                }
                glBindSampler(entry.binding, sampler->sampler);
                break;
            }
            case RhiBindingType::CombinedTextureSampler: {
                const GlTextureViewRecord* view =
                    recordForHandle(data.textureViews,
                                    data.textureViewRecords,
                                    entry.resource.combinedTextureSampler.textureView);
                const GlSamplerRecord* sampler =
                    recordForHandle(data.samplers,
                                    data.samplerRecords,
                                    entry.resource.combinedTextureSampler.sampler);
                if (view == nullptr || sampler == nullptr) {
                    logRhiError("setBindGroup received an invalid combined texture sampler");
                    return;
                }
                glBindTextureUnit(entry.binding, view->texture);
                glBindSampler(entry.binding, sampler->sampler);
                break;
            }
        }
    }
}

void GlRhiCommandList::setVertexBuffer(uint32_t slot, RhiBufferHandle buffer, uint64_t offset) {
    if (m_device == nullptr || !m_device->m_data) {
        logRhiError("setVertexBuffer requires an initialized device");
        return;
    }

    const GlPipelineRecord* pipeline =
        recordForHandle(m_device->m_data->pipelines, m_device->m_data->pipelineRecords, m_graphicsPipeline);
    const GlBufferRecord* bufferRecord =
        recordForHandle(m_device->m_data->buffers, m_device->m_data->bufferRecords, buffer);
    if (pipeline == nullptr || pipeline->compute || bufferRecord == nullptr ||
        (bufferRecord->desc.usage & rhiFlag(RhiBufferUsage::Vertex)) == 0u ||
        offset >= bufferRecord->desc.size) {
        logRhiError("setVertexBuffer requires a graphics pipeline and a valid vertex buffer range");
        return;
    }

    uint32_t stride = 0u;
    for (const RhiVertexBinding& binding : pipeline->graphicsDesc.vertexInput.bindings) {
        if (binding.binding == slot) {
            stride = binding.stride;
            break;
        }
    }
    if (stride == 0u) {
        logRhiError("setVertexBuffer slot is not declared by the graphics pipeline");
        return;
    }

    glVertexArrayVertexBuffer(pipeline->vertexArray,
                              slot,
                              bufferRecord->buffer,
                              static_cast<GLintptr>(offset),
                              static_cast<GLsizei>(stride));
}

void GlRhiCommandList::setIndexBuffer(RhiBufferHandle buffer, RhiIndexFormat format, uint64_t offset) {
    if (m_device == nullptr || !m_device->m_data) {
        logRhiError("setIndexBuffer requires an initialized device");
        return;
    }

    const GlPipelineRecord* pipeline =
        recordForHandle(m_device->m_data->pipelines, m_device->m_data->pipelineRecords, m_graphicsPipeline);
    const GlBufferRecord* bufferRecord =
        recordForHandle(m_device->m_data->buffers, m_device->m_data->bufferRecords, buffer);
    const uint64_t elementSize = indexElementSize(format);
    if (pipeline == nullptr || pipeline->compute || bufferRecord == nullptr ||
        (bufferRecord->desc.usage & rhiFlag(RhiBufferUsage::Index)) == 0u ||
        offset >= bufferRecord->desc.size || (offset % elementSize) != 0u) {
        logRhiError("setIndexBuffer requires a graphics pipeline and a valid index buffer range");
        return;
    }

    glVertexArrayElementBuffer(pipeline->vertexArray, bufferRecord->buffer);
    m_device->m_data->indexFormat = format;
    m_device->m_data->indexOffset = offset;
}

void GlRhiCommandList::pushConstants(const void* data, size_t size, RhiShaderStageFlags stages) {
    if (m_device == nullptr || !m_device->m_data) {
        logRhiError("pushConstants requires an initialized device");
        return;
    }
    if (size == 0u) {
        return;
    }
    if (data == nullptr) {
        logRhiError("pushConstants received null data");
        return;
    }

    auto& deviceData = *m_device->m_data;
    const GlPipelineRecord* pipeline = nullptr;
    if (m_graphicsPipeline.isValid()) {
        pipeline = recordForHandle(deviceData.pipelines, deviceData.pipelineRecords, m_graphicsPipeline);
    } else if (m_computePipeline.isValid()) {
        pipeline = recordForHandle(deviceData.pipelines, deviceData.pipelineRecords, m_computePipeline);
    }
    if (pipeline == nullptr) {
        logRhiError("pushConstants requires a bound pipeline");
        return;
    }

    const RhiPipelineLayoutHandle layoutHandle =
        pipeline->compute ? pipeline->computeDesc.layout : pipeline->graphicsDesc.layout;
    const GlPipelineLayoutRecord* layout =
        recordForHandle(deviceData.pipelineLayouts, deviceData.pipelineLayoutRecords, layoutHandle);
    if (layout == nullptr ||
        layout->desc.pushConstantBytes == 0u ||
        size > layout->desc.pushConstantBytes ||
        (stages & ~layout->desc.pushConstantStages) != 0u) {
        logRhiError("pushConstants exceeds the bound pipeline layout contract");
        return;
    }

    const auto byteSize = static_cast<GLsizeiptr>(size);
    if (deviceData.pushConstantBuffer == 0u) {
        glCreateBuffers(1, &deviceData.pushConstantBuffer);
    }
    if (deviceData.pushConstantCapacity < size) {
        glNamedBufferData(deviceData.pushConstantBuffer, byteSize, nullptr, GL_DYNAMIC_DRAW);
        deviceData.pushConstantCapacity = static_cast<uint32_t>(size);
    }
    glNamedBufferSubData(deviceData.pushConstantBuffer, 0, byteSize, data);
    glBindBufferRange(GL_UNIFORM_BUFFER,
                      kRhiPushConstantBinding,
                      deviceData.pushConstantBuffer,
                      0,
                      byteSize);
}

void GlRhiCommandList::draw(uint32_t vertexCount, uint32_t instanceCount,
                            uint32_t firstVertex, uint32_t firstInstance) {
    if (!m_rendering || m_device == nullptr || !m_device->m_data) {
        logRhiError("draw requires an active rendering scope");
        return;
    }

    const GlPipelineRecord* pipeline =
        recordForHandle(m_device->m_data->pipelines, m_device->m_data->pipelineRecords, m_graphicsPipeline);
    if (pipeline == nullptr || pipeline->compute) {
        logRhiError("draw requires a bound graphics pipeline");
        return;
    }

    glDrawArraysInstancedBaseInstance(toGlTopology(pipeline->graphicsDesc.topology),
                                      static_cast<GLint>(firstVertex),
                                      static_cast<GLsizei>(vertexCount),
                                      static_cast<GLsizei>(instanceCount),
                                      firstInstance);
}

void GlRhiCommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                   uint32_t firstIndex, int32_t vertexOffset,
                                   uint32_t firstInstance) {
    if (!m_rendering || m_device == nullptr || !m_device->m_data) {
        logRhiError("drawIndexed requires an active rendering scope");
        return;
    }

    const GlPipelineRecord* pipeline =
        recordForHandle(m_device->m_data->pipelines, m_device->m_data->pipelineRecords, m_graphicsPipeline);
    if (pipeline == nullptr || pipeline->compute) {
        logRhiError("drawIndexed requires a bound graphics pipeline");
        return;
    }

    const uint64_t byteOffset = m_device->m_data->indexOffset + firstIndex * indexElementSize(m_device->m_data->indexFormat);
    glDrawElementsInstancedBaseVertexBaseInstance(toGlTopology(pipeline->graphicsDesc.topology),
                                                 static_cast<GLsizei>(indexCount),
                                                 m_device->m_data->indexFormat == RhiIndexFormat::Uint16
                                                     ? GL_UNSIGNED_SHORT
                                                     : GL_UNSIGNED_INT,
                                                 reinterpret_cast<const void*>(byteOffset),
                                                 static_cast<GLsizei>(instanceCount),
                                                 vertexOffset,
                                                 firstInstance);
}

void GlRhiCommandList::drawIndirect(RhiBufferHandle indirectBuffer, uint64_t offset,
                                    uint32_t drawCount, uint32_t stride) {
    if (!m_rendering || m_device == nullptr || !m_device->m_data) {
        logRhiError("drawIndirect requires an active rendering scope");
        return;
    }

    const GlPipelineRecord* pipeline =
        recordForHandle(m_device->m_data->pipelines, m_device->m_data->pipelineRecords, m_graphicsPipeline);
    const GlBufferRecord* buffer =
        recordForHandle(m_device->m_data->buffers, m_device->m_data->bufferRecords, indirectBuffer);
    constexpr uint64_t kDrawCommandSize = sizeof(uint32_t) * 4u;
    const uint64_t commandStride = stride == 0u ? kDrawCommandSize : stride;
    const uint64_t requiredBytes = drawCount == 0u
        ? 0u
        : kDrawCommandSize + static_cast<uint64_t>(drawCount - 1u) * commandStride;
    if (pipeline == nullptr || pipeline->compute || buffer == nullptr ||
        (buffer->desc.usage & rhiFlag(RhiBufferUsage::Indirect)) == 0u ||
        drawCount == 0u || commandStride < kDrawCommandSize || (commandStride & 3u) != 0u ||
        (offset & 3u) != 0u || offset > buffer->desc.size ||
        requiredBytes > buffer->desc.size - offset) {
        logRhiError("drawIndirect requires a graphics pipeline and a valid indirect buffer range");
        return;
    }

    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, buffer->buffer);
    glMultiDrawArraysIndirect(toGlTopology(pipeline->graphicsDesc.topology),
                              reinterpret_cast<const void*>(offset),
                              static_cast<GLsizei>(drawCount),
                              static_cast<GLsizei>(stride));
}

void GlRhiCommandList::dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    if (m_device == nullptr || !m_device->m_data) {
        logRhiError("dispatch requires an initialized device");
        return;
    }

    const GlPipelineRecord* pipeline =
        recordForHandle(m_device->m_data->pipelines, m_device->m_data->pipelineRecords, m_computePipeline);
    if (pipeline == nullptr || !pipeline->compute) {
        logRhiError("dispatch requires a bound compute pipeline");
        return;
    }

    glDispatchCompute(groupCountX, groupCountY, groupCountZ);
}

void GlRhiCommandList::updateBuffer(const RhiBufferHandle buffer,
                                    const uint64_t offset,
                                    const void* data,
                                    const size_t size) {
    if (m_device == nullptr || !m_device->m_data) {
        logRhiError("updateBuffer requires an initialized device");
        return;
    }
    if (m_rendering) {
        logRhiError("updateBuffer cannot be recorded inside a rendering scope");
        return;
    }

    const GlBufferRecord* record =
        recordForHandle(m_device->m_data->buffers, m_device->m_data->bufferRecords, buffer);
    if (record == nullptr || data == nullptr || size == 0u ||
        (offset & 3u) != 0u || (size & 3u) != 0u || offset > record->desc.size ||
        size > record->desc.size - offset ||
        (record->desc.usage & rhiFlag(RhiBufferUsage::TransferDst)) == 0u) {
        logRhiError("updateBuffer received an invalid buffer, range, or transfer contract");
        return;
    }

    glNamedBufferSubData(record->buffer,
                         static_cast<GLintptr>(offset),
                         static_cast<GLsizeiptr>(size),
                         data);
}

void GlRhiCommandList::copyBuffer(const RhiBufferCopy& copy) {
    if (m_device == nullptr || !m_device->m_data) {
        logRhiError("copyBuffer requires an initialized device");
        return;
    }

    const GlBufferRecord* src = recordForHandle(m_device->m_data->buffers, m_device->m_data->bufferRecords, copy.src);
    const GlBufferRecord* dst = recordForHandle(m_device->m_data->buffers, m_device->m_data->bufferRecords, copy.dst);
    if (src == nullptr || dst == nullptr || copy.size == 0u ||
        (src->desc.usage & rhiFlag(RhiBufferUsage::TransferSrc)) == 0u ||
        (dst->desc.usage & rhiFlag(RhiBufferUsage::TransferDst)) == 0u ||
        copy.srcOffset > src->desc.size || copy.size > src->desc.size - copy.srcOffset ||
        copy.dstOffset > dst->desc.size || copy.size > dst->desc.size - copy.dstOffset) {
        logRhiError("copyBuffer received invalid buffers or ranges");
        return;
    }

    glCopyNamedBufferSubData(src->buffer,
                             dst->buffer,
                             static_cast<GLintptr>(copy.srcOffset),
                             static_cast<GLintptr>(copy.dstOffset),
                             static_cast<GLsizeiptr>(copy.size));
}

void GlRhiCommandList::copyBufferToTexture(const RhiBufferTextureCopy& copy) {
    if (m_device == nullptr || !m_device->m_data) {
        logRhiError("copyBufferToTexture requires an initialized device");
        return;
    }

    const GlBufferRecord* src =
        recordForHandle(m_device->m_data->buffers, m_device->m_data->bufferRecords, copy.srcBuffer);
    GlTextureRecord* dst =
        recordForHandle(m_device->m_data->textures, m_device->m_data->textureRecords, copy.dstTexture);
    if (m_rendering || src == nullptr || dst == nullptr ||
        (src->desc.usage & rhiFlag(RhiBufferUsage::TransferSrc)) == 0u ||
        (dst->desc.usage & rhiFlag(RhiTextureUsage::TransferDst)) == 0u ||
        copy.mipLevel >= dst->desc.mipLevels || copy.width == 0u || copy.height == 0u || copy.depth == 0u) {
        logRhiError("copyBufferToTexture received an invalid resource or transfer contract");
        return;
    }

    const uint32_t mipWidth = std::max(1u, dst->desc.width >> copy.mipLevel);
    const uint32_t mipHeight = std::max(1u, dst->desc.height >> copy.mipLevel);
    const uint32_t mipDepth = dst->desc.dimension == RhiTextureDimension::Texture3D
        ? std::max(1u, dst->desc.depthOrLayers >> copy.mipLevel)
        : dst->desc.depthOrLayers;
    const uint32_t targetZ = dst->desc.dimension == RhiTextureDimension::Texture2D
        ? copy.dstZ
        : copy.arrayLayer + copy.dstZ;
    const bool invalid2D = dst->desc.dimension == RhiTextureDimension::Texture2D &&
                           (copy.arrayLayer != 0u || targetZ != 0u || copy.depth != 1u);
    const size_t copySize = static_cast<size_t>(copy.width) * copy.height * copy.depth *
                            textureFormatSizeBytes(dst->desc.format);
    if (invalid2D || copy.dstX > mipWidth || copy.width > mipWidth - copy.dstX ||
        copy.dstY > mipHeight || copy.height > mipHeight - copy.dstY ||
        targetZ > mipDepth || copy.depth > mipDepth - targetZ ||
        copy.bufferOffset > src->desc.size || copySize > src->desc.size - copy.bufferOffset) {
        logRhiError("copyBufferToTexture received an out-of-range copy region");
        return;
    }

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, src->buffer);
    if (dst->desc.dimension == RhiTextureDimension::Texture2D) {
        glTextureSubImage2D(dst->texture,
                            static_cast<GLint>(copy.mipLevel),
                            static_cast<GLint>(copy.dstX),
                            static_cast<GLint>(copy.dstY),
                            static_cast<GLsizei>(copy.width),
                            static_cast<GLsizei>(copy.height),
                            dst->format.externalFormat,
                            dst->format.type,
                            reinterpret_cast<const void*>(copy.bufferOffset));
    } else {
        glTextureSubImage3D(dst->texture,
                            static_cast<GLint>(copy.mipLevel),
                            static_cast<GLint>(copy.dstX),
                            static_cast<GLint>(copy.dstY),
                            static_cast<GLint>(targetZ),
                            static_cast<GLsizei>(copy.width),
                            static_cast<GLsizei>(copy.height),
                            static_cast<GLsizei>(copy.depth),
                            dst->format.externalFormat,
                            dst->format.type,
                            reinterpret_cast<const void*>(copy.bufferOffset));
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0u);
}

void GlRhiCommandList::copyTexture(const RhiTextureCopy& copy) {
    if (m_device == nullptr || !m_device->m_data) {
        logRhiError("copyTexture requires an initialized device");
        return;
    }

    GlResolvedTextureRecord src;
    GlResolvedTextureRecord dst;
    if (!resolveTextureRecord(*m_device->m_data, copy.src, src) ||
        !resolveTextureRecord(*m_device->m_data, copy.dst, dst)) {
        logRhiError("copyTexture received invalid texture handles");
        return;
    }

    const auto validateSubresource = [](const GlResolvedTextureRecord& texture,
                                        const RhiTextureSubresourceLayers& subresource,
                                        const RhiOffset3D& offset,
                                        const RhiExtent3D& extent) {
        if (subresource.mipLevel >= texture.desc.mipLevels ||
            subresource.layerCount == 0u || extent.width == 0u ||
            extent.height == 0u || extent.depth == 0u) {
            return false;
        }
        const uint32_t width = mipExtent(texture.desc.width, subresource.mipLevel);
        const uint32_t height = mipExtent(texture.desc.height, subresource.mipLevel);
        if (offset.x > width || extent.width > width - offset.x ||
            offset.y > height || extent.height > height - offset.y) {
            return false;
        }
        if (texture.desc.dimension == RhiTextureDimension::Texture3D) {
            const uint32_t depth = mipExtent(texture.desc.depthOrLayers, subresource.mipLevel);
            return subresource.baseArrayLayer == 0u && subresource.layerCount == 1u &&
                   offset.z <= depth && extent.depth <= depth - offset.z;
        }
        return offset.z == 0u && extent.depth == 1u &&
               subresource.baseArrayLayer < texture.desc.depthOrLayers &&
               subresource.layerCount <= texture.desc.depthOrLayers - subresource.baseArrayLayer;
    };
    if (src.desc.format != dst.desc.format ||
        copy.srcSubresource.layerCount != copy.dstSubresource.layerCount ||
        !validateSubresource(src, copy.srcSubresource, copy.srcOffset, copy.extent) ||
        !validateSubresource(dst, copy.dstSubresource, copy.dstOffset, copy.extent)) {
        logRhiError("copyTexture received an invalid copy region");
        return;
    }

    const auto copyZ = [](const GlResolvedTextureRecord& texture,
                          const RhiTextureSubresourceLayers& subresource,
                          const RhiOffset3D& offset) {
        return texture.desc.dimension == RhiTextureDimension::Texture3D
            ? offset.z
            : subresource.baseArrayLayer;
    };
    const uint32_t copyDepth = src.desc.dimension == RhiTextureDimension::Texture3D
        ? copy.extent.depth
        : copy.srcSubresource.layerCount;

    glCopyImageSubData(src.texture,
                       src.target,
                       static_cast<GLint>(copy.srcSubresource.mipLevel),
                       static_cast<GLint>(copy.srcOffset.x),
                       static_cast<GLint>(copy.srcOffset.y),
                       static_cast<GLint>(copyZ(src, copy.srcSubresource, copy.srcOffset)),
                       dst.texture,
                       dst.target,
                       static_cast<GLint>(copy.dstSubresource.mipLevel),
                       static_cast<GLint>(copy.dstOffset.x),
                       static_cast<GLint>(copy.dstOffset.y),
                       static_cast<GLint>(copyZ(dst, copy.dstSubresource, copy.dstOffset)),
                       static_cast<GLsizei>(copy.extent.width),
                       static_cast<GLsizei>(copy.extent.height),
                       static_cast<GLsizei>(copyDepth));
}

void GlRhiCommandList::blitTexture(const RhiTextureBlit& blit) {
    if (m_device == nullptr || !m_device->m_data) {
        logRhiError("blitTexture requires an initialized device");
        return;
    }

    GlBlitEndpoint src;
    GlBlitEndpoint dst;
    if (!resolveBlitEndpoint(*m_device->m_data, blit.src, blit.srcView, blit.srcMipLevel, src) ||
        !resolveBlitEndpoint(*m_device->m_data, blit.dst, blit.dstView, blit.dstMipLevel, dst) ||
        !src.valid || !dst.valid ||
        src.desc.dimension != RhiTextureDimension::Texture2D ||
        dst.desc.dimension != RhiTextureDimension::Texture2D ||
        src.format.depth != dst.format.depth ||
        src.format.stencil != dst.format.stencil) {
        logRhiError("blitTexture requires valid 2D source and destination endpoints");
        return;
    }

    GLuint readFramebuffer = 0u;
    GLuint drawFramebuffer = 0u;
    if (!src.swapchain) {
        glCreateFramebuffers(1, &readFramebuffer);
    }
    if (!dst.swapchain) {
        glCreateFramebuffers(1, &drawFramebuffer);
    }

    auto destroyFramebuffers = [&]() {
        if (readFramebuffer != 0u) {
            glDeleteFramebuffers(1, &readFramebuffer);
            readFramebuffer = 0u;
        }
        if (drawFramebuffer != 0u) {
            glDeleteFramebuffers(1, &drawFramebuffer);
            drawFramebuffer = 0u;
        }
    };

    GLbitfield mask = GL_COLOR_BUFFER_BIT;
    GLenum filter = GL_LINEAR;
    if (src.format.depth) {
        const GLenum attachment = src.format.stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
        if (!src.swapchain) {
            glNamedFramebufferTexture(readFramebuffer, attachment, src.texture, static_cast<GLint>(src.attachmentMip));
        }
        if (!dst.swapchain) {
            glNamedFramebufferTexture(drawFramebuffer, attachment, dst.texture, static_cast<GLint>(dst.attachmentMip));
        }
        mask = src.format.stencil ? (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT) : GL_DEPTH_BUFFER_BIT;
        filter = GL_NEAREST;
    } else {
        if (!src.swapchain) {
            glNamedFramebufferTexture(readFramebuffer,
                                      GL_COLOR_ATTACHMENT0,
                                      src.texture,
                                      static_cast<GLint>(src.attachmentMip));
            glNamedFramebufferReadBuffer(readFramebuffer, GL_COLOR_ATTACHMENT0);
        } else {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0u);
            glReadBuffer(GL_BACK);
        }
        if (!dst.swapchain) {
            glNamedFramebufferTexture(drawFramebuffer,
                                      GL_COLOR_ATTACHMENT0,
                                      dst.texture,
                                      static_cast<GLint>(dst.attachmentMip));
            glNamedFramebufferDrawBuffer(drawFramebuffer, GL_COLOR_ATTACHMENT0);
        } else {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0u);
            glDrawBuffer(GL_BACK);
        }
    }

    const GLenum readStatus = src.swapchain
        ? GL_FRAMEBUFFER_COMPLETE
        : glCheckNamedFramebufferStatus(readFramebuffer, GL_READ_FRAMEBUFFER);
    const GLenum drawStatus = dst.swapchain
        ? GL_FRAMEBUFFER_COMPLETE
        : glCheckNamedFramebufferStatus(drawFramebuffer, GL_DRAW_FRAMEBUFFER);
    if (readStatus != GL_FRAMEBUFFER_COMPLETE || drawStatus != GL_FRAMEBUFFER_COMPLETE) {
        destroyFramebuffers();
        if (readStatus != GL_FRAMEBUFFER_COMPLETE) {
            logFramebufferStatus("blitTexture read", readStatus);
        }
        if (drawStatus != GL_FRAMEBUFFER_COMPLETE) {
            logFramebufferStatus("blitTexture draw", drawStatus);
        }
        return;
    }

    glBlitNamedFramebuffer(readFramebuffer,
                           drawFramebuffer,
                           0,
                           0,
                           static_cast<GLint>(src.desc.width),
                           static_cast<GLint>(src.desc.height),
                           0,
                           0,
                           static_cast<GLint>(dst.desc.width),
                           static_cast<GLint>(dst.desc.height),
                           mask,
                           filter);
    destroyFramebuffers();
}

void GlRhiCommandList::generateMipmaps(const RhiTextureHandle texture) {
    if (m_device == nullptr) {
        logRhiError("generateMipmaps requires an attached device");
        return;
    }
    const GlTextureRecord* record = recordForHandle(
        m_device->m_data->textures, m_device->m_data->textureRecords, texture);
    if (record == nullptr || record->desc.mipLevels <= 1u || record->format.depth) {
        logRhiError("generateMipmaps requires a valid color texture with multiple mip levels");
        return;
    }
    glGenerateTextureMipmap(record->texture);
}

void GlRhiCommandList::writeTimestamp(RhiQueryPoolHandle pool, uint32_t queryIndex) {
    if (m_device == nullptr) {
        logRhiError("writeTimestamp requires an attached device");
        return;
    }
    const GlQueryPoolRecord* record = recordForHandle(
        m_device->m_data->queryPools, m_device->m_data->queryPoolRecords, pool);
    if (record == nullptr || queryIndex >= record->queries.size()) {
        logRhiError("writeTimestamp received an invalid query pool range");
        return;
    }
    glQueryCounter(record->queries[queryIndex], GL_TIMESTAMP);
}

GlRhiDevice::GlRhiDevice()
    : m_data(std::make_unique<GlRhiDeviceData>()) {
    m_commandList.attachDevice(this);
}

GlRhiDevice::~GlRhiDevice() {
    if (m_initialized) {
        shutdown();
    }
}

bool GlRhiDevice::init(const RhiDeviceDesc& desc) {
    if (!GLAD_GL_VERSION_4_5) {
        logRhiError("OpenGL 4.5 is required");
        return false;
    }
    if (desc.width <= 0 || desc.height <= 0) {
        logRhiError("init received invalid swapchain dimensions");
        return false;
    }
    GLint maxUniformBufferBindings = 0;
    glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &maxUniformBufferBindings);
    if (maxUniformBufferBindings <= static_cast<GLint>(kRhiPushConstantBinding)) {
        logRhiError("init requires enough uniform buffer bindings for RHI push constants");
        return false;
    }

    m_initialized = true;
    m_capabilities.multiDrawIndirect = true;
    m_capabilities.timestampQuery = true;
    m_capabilities.textureView = true;
    m_capabilities.samplerAnisotropy = true;
    m_capabilities.storageImage = true;
    m_capabilities.maxColorAttachments = 8;
    m_capabilities.maxSampledTexturesPerStage = 32;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &m_capabilities.maxSamplerAnisotropy);

    m_data->swapchainWidth = static_cast<uint32_t>(desc.width);
    m_data->swapchainHeight = static_cast<uint32_t>(desc.height);
    m_data->swapchainFormat = RhiTextureFormat::Rgba8Unorm;
    m_data->swapchainDepthStencilFormat = RhiTextureFormat::Depth24;

    RhiTextureViewDesc swapchainViewDesc;
    swapchainViewDesc.viewType = RhiTextureViewType::Texture2D;
    swapchainViewDesc.format = m_data->swapchainFormat;
    RhiTextureViewDesc swapchainDepthViewDesc;
    swapchainDepthViewDesc.viewType = RhiTextureViewType::Texture2D;
    swapchainDepthViewDesc.format = m_data->swapchainDepthStencilFormat;
    GlFormatInfo swapchainFormat;
    if (!toGlFormatInfo(m_data->swapchainFormat, swapchainFormat)) {
        logRhiError("init received an unsupported swapchain format");
        m_initialized = false;
        return false;
    }
    GlFormatInfo swapchainDepthFormat;
    if (!toGlFormatInfo(m_data->swapchainDepthStencilFormat, swapchainDepthFormat)) {
        logRhiError("init received an unsupported swapchain depth-stencil format");
        m_initialized = false;
        return false;
    }

    m_data->swapchainColorView = m_data->textureViews.allocate();
    const uint32_t colorSlot = m_data->swapchainColorView.index - 1u;
    if (colorSlot >= m_data->textureViewRecords.size()) {
        m_data->textureViewRecords.resize(colorSlot + 1u);
    }
    m_data->textureViewRecords[colorSlot] = {
        0u,
        GL_TEXTURE_2D,
        swapchainViewDesc,
        m_data->swapchainFormat,
        swapchainFormat,
        true,
        false,
        false,
        true
    };

    m_data->swapchainDepthStencilView = m_data->textureViews.allocate();
    const uint32_t depthSlot = m_data->swapchainDepthStencilView.index - 1u;
    if (depthSlot >= m_data->textureViewRecords.size()) {
        m_data->textureViewRecords.resize(depthSlot + 1u);
    }
    m_data->textureViewRecords[depthSlot] = {
        0u,
        GL_TEXTURE_2D,
        swapchainDepthViewDesc,
        m_data->swapchainDepthStencilFormat,
        swapchainDepthFormat,
        false,
        true,
        false,
        true
    };
    return true;
}

void GlRhiDevice::shutdown() {
    if (!m_data) {
        m_initialized = false;
        return;
    }

    for (GlFramebufferRecord& record : m_data->framebufferCache) {
        if (record.active && record.framebuffer != 0u) {
            glDeleteFramebuffers(1, &record.framebuffer);
        }
    }
    for (GlBindGroupRecord& record : m_data->bindGroupRecords) {
        record = {};
    }
    for (GlPipelineRecord& record : m_data->pipelineRecords) {
        if (record.active) {
            if (record.vertexArray != 0u) {
                glDeleteVertexArrays(1, &record.vertexArray);
            }
            if (record.program != 0u) {
                glDeleteProgram(record.program);
            }
        }
        record = {};
    }
    for (GlShaderRecord& record : m_data->shaderRecords) {
        if (record.active && record.shader != 0u) {
            glDeleteShader(record.shader);
        }
        record = {};
    }
    for (GlSamplerRecord& record : m_data->samplerRecords) {
        if (record.active && record.sampler != 0u) {
            glDeleteSamplers(1, &record.sampler);
        }
        record = {};
    }
    for (GlTextureViewRecord& record : m_data->textureViewRecords) {
        if (record.active && record.ownsTexture && record.texture != 0u) {
            glDeleteTextures(1, &record.texture);
        }
        record = {};
    }
    for (GlTextureRecord& record : m_data->textureRecords) {
        if (record.active && record.texture != 0u) {
            glDeleteTextures(1, &record.texture);
        }
        record = {};
    }
    for (GlBufferRecord& record : m_data->bufferRecords) {
        if (record.active && record.buffer != 0u) {
            glDeleteBuffers(1, &record.buffer);
        }
        record = {};
    }
    if (m_data->pushConstantBuffer != 0u) {
        glDeleteBuffers(1, &m_data->pushConstantBuffer);
        m_data->pushConstantBuffer = 0u;
        m_data->pushConstantCapacity = 0u;
    }

    m_data->framebufferCache.clear();
    m_data->swapchainColorView = {};
    m_data->swapchainDepthStencilView = {};
    m_data->swapchainWidth = 1u;
    m_data->swapchainHeight = 1u;
    m_data->bindGroups.clear();
    for (GlQueryPoolRecord& record : m_data->queryPoolRecords) {
        if (record.active && !record.queries.empty()) {
            glDeleteQueries(static_cast<GLsizei>(record.queries.size()), record.queries.data());
        }
        record = {};
    }
    m_data->queryPools.clear();
    m_data->pipelines.clear();
    m_data->pipelineLayouts.clear();
    m_data->bindGroupLayouts.clear();
    m_data->shaders.clear();
    m_data->samplers.clear();
    m_data->textureViews.clear();
    m_data->textures.clear();
    m_data->buffers.clear();
    m_data->bufferRecords.clear();
    m_data->textureRecords.clear();
    m_data->textureViewRecords.clear();
    m_data->samplerRecords.clear();
    m_data->shaderRecords.clear();
    m_data->bindGroupLayoutRecords.clear();
    m_data->pipelineLayoutRecords.clear();
    m_data->pipelineRecords.clear();
    m_data->bindGroupRecords.clear();
    m_data->queryPoolRecords.clear();
    m_commandList.resetFrameState();
    m_initialized = false;
}

RhiBackend GlRhiDevice::backend() const {
    return RhiBackend::OpenGL;
}

const RhiCapabilities& GlRhiDevice::capabilities() const {
    return m_capabilities;
}

RhiBufferHandle GlRhiDevice::createBuffer(const RhiBufferDesc& desc,
                                          const void* initialData,
                                          size_t initialDataSize) {
    if (!m_initialized || desc.size == 0u || desc.usage == 0u ||
        (initialData == nullptr && initialDataSize != 0u) ||
        initialDataSize > desc.size) {
        logRhiError("createBuffer received an invalid descriptor");
        return {};
    }

    GLuint buffer = 0u;
    glCreateBuffers(1, &buffer);
    const GLenum usage = desc.memoryUsage == RhiMemoryUsage::GpuOnly ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW;
    glNamedBufferData(buffer, static_cast<GLsizeiptr>(desc.size), nullptr, usage);
    if (initialData != nullptr && initialDataSize != 0u) {
        glNamedBufferSubData(buffer, 0, static_cast<GLsizeiptr>(initialDataSize), initialData);
    }
    labelGlObject(GL_BUFFER, buffer, desc.debugName);

    const RhiBufferHandle handle = m_data->buffers.allocate();
    const uint32_t slot = handle.index - 1u;
    if (slot >= m_data->bufferRecords.size()) {
        m_data->bufferRecords.resize(slot + 1u);
    }
    m_data->bufferRecords[slot] = {buffer, desc, true};
    return handle;
}

RhiTextureHandle GlRhiDevice::createTexture(const RhiTextureDesc& desc,
                                            const RhiTextureInitialData* initialData) {
    GlFormatInfo format;
    const GLenum target = toGlTextureTarget(desc.dimension);
    if (!m_initialized || target == 0u || !toGlFormatInfo(desc.format, format) ||
        desc.width == 0u || desc.height == 0u || desc.depthOrLayers == 0u ||
        desc.mipLevels == 0u || desc.sampleCount != 1u || desc.usage == 0u) {
        logRhiError("createTexture received an invalid descriptor");
        return {};
    }

    GLuint texture = 0u;
    glCreateTextures(target, 1, &texture);
    if (desc.dimension == RhiTextureDimension::Texture2D || desc.dimension == RhiTextureDimension::Cube) {
        glTextureStorage2D(texture,
                           static_cast<GLsizei>(desc.mipLevels),
                           format.internalFormat,
                           static_cast<GLsizei>(desc.width),
                           static_cast<GLsizei>(desc.height));
    } else {
        glTextureStorage3D(texture,
                           static_cast<GLsizei>(desc.mipLevels),
                           format.internalFormat,
                           static_cast<GLsizei>(desc.width),
                           static_cast<GLsizei>(desc.height),
                           static_cast<GLsizei>(desc.depthOrLayers));
    }
    glTextureParameteri(texture, GL_TEXTURE_BASE_LEVEL, 0);
    glTextureParameteri(texture,
                        GL_TEXTURE_MAX_LEVEL,
                        static_cast<GLint>(desc.mipLevels - 1u));
    glTextureParameteri(texture,
                        GL_TEXTURE_MIN_FILTER,
                        desc.mipLevels > 1u ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
    glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(texture, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    labelGlObject(GL_TEXTURE, texture, desc.debugName);

    if (initialData != nullptr) {
        if (initialData->pixels == nullptr || initialData->mipLevel >= desc.mipLevels ||
            initialData->layerCount == 0u) {
            glDeleteTextures(1, &texture);
            logRhiError("createTexture received invalid initial texture data");
            return {};
        }
        const uint32_t mipWidth = std::max(1u, desc.width >> initialData->mipLevel);
        const uint32_t mipHeight = std::max(1u, desc.height >> initialData->mipLevel);
        const uint32_t availableLayers = desc.dimension == RhiTextureDimension::Texture3D
            ? std::max(1u, desc.depthOrLayers >> initialData->mipLevel)
            : desc.depthOrLayers;
        if (initialData->arrayLayer >= availableLayers ||
            initialData->layerCount > availableLayers - initialData->arrayLayer ||
            (desc.dimension == RhiTextureDimension::Texture2D &&
             (initialData->arrayLayer != 0u || initialData->layerCount != 1u))) {
            glDeleteTextures(1, &texture);
            logRhiError("createTexture received an invalid initial texture layer range");
            return {};
        }
        const size_t expectedSize = static_cast<size_t>(mipWidth) * mipHeight *
                                    initialData->layerCount *
                                    textureFormatSizeBytes(desc.format);
        if (initialData->sizeBytes != expectedSize) {
            glDeleteTextures(1, &texture);
            logRhiError("createTexture received invalid initial texture data");
            return {};
        }
        if (desc.dimension == RhiTextureDimension::Texture2D) {
            glTextureSubImage2D(texture,
                                static_cast<GLint>(initialData->mipLevel),
                                0,
                                0,
                                static_cast<GLsizei>(mipWidth),
                                static_cast<GLsizei>(mipHeight),
                                format.externalFormat,
                                format.type,
                                initialData->pixels);
        } else {
            glTextureSubImage3D(texture,
                                static_cast<GLint>(initialData->mipLevel),
                                0,
                                0,
                                static_cast<GLint>(initialData->arrayLayer),
                                static_cast<GLsizei>(mipWidth),
                                static_cast<GLsizei>(mipHeight),
                                static_cast<GLsizei>(initialData->layerCount),
                                format.externalFormat,
                                format.type,
                                initialData->pixels);
        }
    }

    const RhiTextureHandle handle = m_data->textures.allocate();
    const uint32_t slot = handle.index - 1u;
    if (slot >= m_data->textureRecords.size()) {
        m_data->textureRecords.resize(slot + 1u);
    }
    m_data->textureRecords[slot] = {texture, target, desc, format, true};
    return handle;
}

RhiTextureViewHandle GlRhiDevice::createTextureView(const RhiTextureViewDesc& desc) {
    GlResolvedTextureRecord textureRecord;
    const bool textureResolved = resolveTextureRecord(*m_data, desc.texture, textureRecord);
    RhiTextureViewDesc resolvedDesc = desc;
    if (textureResolved && desc.baseMip < textureRecord.desc.mipLevels &&
        desc.mipCount == kRhiRemainingMipLevels) {
        resolvedDesc.mipCount = textureRecord.desc.mipLevels - desc.baseMip;
    }
    if (textureResolved && desc.baseLayer < textureRecord.desc.depthOrLayers &&
        desc.layerCount == kRhiRemainingArrayLayers) {
        resolvedDesc.layerCount = textureRecord.desc.depthOrLayers - desc.baseLayer;
    }
    const RhiTextureFormat resolvedFormat =
        desc.format == RhiTextureFormat::Undefined && textureResolved ? textureRecord.desc.format : desc.format;
    GlFormatInfo format;
    const GLenum viewTarget = toGlTextureViewTarget(desc.viewType);
    if (!m_initialized || !textureResolved || viewTarget == 0u ||
        !toGlFormatInfo(resolvedFormat, format) || resolvedDesc.mipCount == 0u ||
        resolvedDesc.layerCount == 0u || resolvedDesc.baseMip >= textureRecord.desc.mipLevels ||
        resolvedDesc.mipCount > textureRecord.desc.mipLevels - resolvedDesc.baseMip ||
        resolvedDesc.baseLayer >= textureRecord.desc.depthOrLayers ||
        resolvedDesc.layerCount > textureRecord.desc.depthOrLayers - resolvedDesc.baseLayer) {
        std::cerr << "GlRhiDevice: invalid texture view"
                  << " handle=" << desc.texture.index << ':' << desc.texture.generation
                  << " resolved=" << textureResolved
                  << " viewType=" << static_cast<uint32_t>(desc.viewType)
                  << " format=" << static_cast<uint32_t>(resolvedFormat)
                  << " baseMip=" << resolvedDesc.baseMip
                  << " mipCount=" << resolvedDesc.mipCount
                  << " baseLayer=" << resolvedDesc.baseLayer
                  << " layerCount=" << resolvedDesc.layerCount;
        if (textureResolved) {
            std::cerr << " textureDimension=" << static_cast<uint32_t>(textureRecord.desc.dimension)
                      << " textureFormat=" << static_cast<uint32_t>(textureRecord.desc.format)
                      << " textureMips=" << textureRecord.desc.mipLevels
                      << " textureLayers=" << textureRecord.desc.depthOrLayers;
        }
        std::cerr << '\n';
        return {};
    }

    const bool aliasesWholeTexture =
        resolvedFormat == textureRecord.desc.format &&
        resolvedDesc.baseMip == 0u && resolvedDesc.mipCount == textureRecord.desc.mipLevels &&
        resolvedDesc.baseLayer == 0u && resolvedDesc.layerCount == textureRecord.desc.depthOrLayers &&
        viewTarget == textureRecord.target && !resolvedDesc.depthCompare;

    GLuint textureView = textureRecord.texture;
    bool ownsTexture = false;
    if (!aliasesWholeTexture) {
        glGenTextures(1, &textureView);
        glTextureView(textureView,
                      viewTarget,
                      textureRecord.texture,
                      format.internalFormat,
                      resolvedDesc.baseMip,
                      resolvedDesc.mipCount,
                      resolvedDesc.baseLayer,
                      resolvedDesc.layerCount);
        glTextureParameteri(textureView, GL_TEXTURE_BASE_LEVEL, 0);
        glTextureParameteri(textureView,
                            GL_TEXTURE_MAX_LEVEL,
                            static_cast<GLint>(resolvedDesc.mipCount - 1u));
        glTextureParameteri(textureView,
                            GL_TEXTURE_MIN_FILTER,
                            resolvedDesc.mipCount > 1u ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
        glTextureParameteri(textureView, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(textureView, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(textureView, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameteri(textureView, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        if (resolvedDesc.depthCompare) {
            glTextureParameteri(textureView, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glTextureParameteri(textureView, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        }
        labelGlObject(GL_TEXTURE, textureView, rhiDebugName(textureRecord.desc.debugName));
        ownsTexture = true;
    }

    const RhiTextureViewHandle handle = m_data->textureViews.allocate();
    const uint32_t slot = handle.index - 1u;
    if (slot >= m_data->textureViewRecords.size()) {
        m_data->textureViewRecords.resize(slot + 1u);
    }
    m_data->textureViewRecords[slot] = {
        textureView,
        viewTarget,
        resolvedDesc,
        resolvedFormat,
        format,
        false,
        false,
        ownsTexture,
        true
    };
    return handle;
}

RhiSamplerHandle GlRhiDevice::createSampler(const RhiSamplerDesc& desc) {
    if (!m_initialized || desc.maxAnisotropy < 1.0f) {
        logRhiError("createSampler received an invalid descriptor");
        return {};
    }

    GLuint sampler = 0u;
    glCreateSamplers(1, &sampler);
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, toGlMinFilter(desc.minFilter, desc.mipmapMode));
    glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, toGlMagFilter(desc.magFilter));
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, toGlAddressMode(desc.addressU));
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, toGlAddressMode(desc.addressV));
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_R, toGlAddressMode(desc.addressW));
    const std::array<GLfloat, 4> borderColor = toGlBorderColor(desc.borderColor);
    glSamplerParameterfv(sampler, GL_TEXTURE_BORDER_COLOR, borderColor.data());
    if (desc.compareEnabled) {
        glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_FUNC, toGlCompareOp(desc.compareOp));
    }
    if (m_capabilities.samplerAnisotropy && desc.maxAnisotropy > 1.0f) {
        glSamplerParameterf(sampler, GL_TEXTURE_MAX_ANISOTROPY, desc.maxAnisotropy);
    }

    const RhiSamplerHandle handle = m_data->samplers.allocate();
    const uint32_t slot = handle.index - 1u;
    if (slot >= m_data->samplerRecords.size()) {
        m_data->samplerRecords.resize(slot + 1u);
    }
    m_data->samplerRecords[slot] = {sampler, desc, true};
    return handle;
}

RhiShaderHandle GlRhiDevice::createShader(const RhiShaderDesc& desc) {
    if (!m_initialized || desc.bytecode != nullptr || desc.bytecodeSize != 0u) {
        logRhiError("createShader requires GLSL source for the OpenGL backend");
        return {};
    }

    const GLuint shader = compileShaderObject(desc);
    if (shader == 0u) {
        return {};
    }

    const RhiShaderHandle handle = m_data->shaders.allocate();
    const uint32_t slot = handle.index - 1u;
    if (slot >= m_data->shaderRecords.size()) {
        m_data->shaderRecords.resize(slot + 1u);
    }
    m_data->shaderRecords[slot] = {shader, desc.stage, true};
    return handle;
}

RhiBindGroupLayoutHandle GlRhiDevice::createBindGroupLayout(const RhiBindGroupLayoutDesc& desc) {
    if (!m_initialized) {
        logRhiError("createBindGroupLayout requires an initialized device");
        return {};
    }

    const RhiBindGroupLayoutHandle handle = m_data->bindGroupLayouts.allocate();
    const uint32_t slot = handle.index - 1u;
    if (slot >= m_data->bindGroupLayoutRecords.size()) {
        m_data->bindGroupLayoutRecords.resize(slot + 1u);
    }
    m_data->bindGroupLayoutRecords[slot] = {desc, true};
    return handle;
}

RhiPipelineLayoutHandle GlRhiDevice::createPipelineLayout(const RhiPipelineLayoutDesc& desc) {
    if (!m_initialized) {
        logRhiError("createPipelineLayout requires an initialized device");
        return {};
    }
    for (const RhiBindGroupLayoutHandle layout : desc.bindGroupLayouts) {
        if (recordForHandle(m_data->bindGroupLayouts, m_data->bindGroupLayoutRecords, layout) == nullptr) {
            logRhiError("createPipelineLayout received an invalid bind group layout");
            return {};
        }
    }

    const RhiPipelineLayoutHandle handle = m_data->pipelineLayouts.allocate();
    const uint32_t slot = handle.index - 1u;
    if (slot >= m_data->pipelineLayoutRecords.size()) {
        m_data->pipelineLayoutRecords.resize(slot + 1u);
    }
    m_data->pipelineLayoutRecords[slot] = {desc, true};
    return handle;
}

RhiPipelineHandle GlRhiDevice::createGraphicsPipeline(const RhiGraphicsPipelineDesc& desc) {
    const GlShaderRecord* vertexShader =
        recordForHandle(m_data->shaders, m_data->shaderRecords, desc.vertexShader);
    const GlShaderRecord* fragmentShader =
        recordForHandle(m_data->shaders, m_data->shaderRecords, desc.fragmentShader);
    if (!m_initialized || vertexShader == nullptr || fragmentShader == nullptr ||
        vertexShader->stage != RhiShaderStage::Vertex ||
        fragmentShader->stage != RhiShaderStage::Fragment ||
        recordForHandle(m_data->pipelineLayouts, m_data->pipelineLayoutRecords, desc.layout) == nullptr) {
        logRhiError("createGraphicsPipeline received an invalid descriptor");
        return {};
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader->shader);
    glAttachShader(program, fragmentShader->shader);
    glLinkProgram(program);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        std::array<char, 2048> infoLog{};
        glGetProgramInfoLog(program, static_cast<GLsizei>(infoLog.size()), nullptr, infoLog.data());
        std::cerr << "GlRhiDevice: graphics pipeline link failed [" << rhiDebugName(desc.debugName)
                  << "]\n" << infoLog.data() << '\n';
        glDeleteProgram(program);
        return {};
    }

    GLuint vertexArray = 0u;
    glCreateVertexArrays(1, &vertexArray);
    for (const RhiVertexBinding& binding : desc.vertexInput.bindings) {
        glVertexArrayBindingDivisor(vertexArray,
                                    binding.binding,
                                    binding.inputRate == RhiVertexInputRate::Instance ? 1u : 0u);
    }
    for (const RhiVertexAttribute& attribute : desc.vertexInput.attributes) {
        const GlVertexFormatInfo format = toGlVertexFormat(attribute.format);
        glEnableVertexArrayAttrib(vertexArray, attribute.location);
        glVertexArrayAttribBinding(vertexArray, attribute.location, attribute.binding);
        if (format.integer) {
            glVertexArrayAttribIFormat(vertexArray,
                                       attribute.location,
                                       format.componentCount,
                                       format.type,
                                       attribute.offset);
        } else {
            glVertexArrayAttribFormat(vertexArray,
                                      attribute.location,
                                      format.componentCount,
                                      format.type,
                                      format.normalized ? GL_TRUE : GL_FALSE,
                                      attribute.offset);
        }
    }
    labelGlObject(GL_PROGRAM, program, desc.debugName);
    labelGlObject(GL_VERTEX_ARRAY, vertexArray, desc.debugName);

    const RhiPipelineHandle handle = m_data->pipelines.allocate();
    const uint32_t slot = handle.index - 1u;
    if (slot >= m_data->pipelineRecords.size()) {
        m_data->pipelineRecords.resize(slot + 1u);
    }
    GlPipelineRecord record;
    record.program = program;
    record.vertexArray = vertexArray;
    record.compute = false;
    record.graphicsDesc = desc;
    record.active = true;
    m_data->pipelineRecords[slot] = std::move(record);
    return handle;
}

RhiPipelineHandle GlRhiDevice::createComputePipeline(const RhiComputePipelineDesc& desc) {
    const GlShaderRecord* computeShader =
        recordForHandle(m_data->shaders, m_data->shaderRecords, desc.computeShader);
    if (!m_initialized || computeShader == nullptr || computeShader->stage != RhiShaderStage::Compute ||
        recordForHandle(m_data->pipelineLayouts, m_data->pipelineLayoutRecords, desc.layout) == nullptr) {
        logRhiError("createComputePipeline received an invalid descriptor");
        return {};
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, computeShader->shader);
    glLinkProgram(program);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        std::array<char, 2048> infoLog{};
        glGetProgramInfoLog(program, static_cast<GLsizei>(infoLog.size()), nullptr, infoLog.data());
        std::cerr << "GlRhiDevice: compute pipeline link failed [" << rhiDebugName(desc.debugName)
                  << "]\n" << infoLog.data() << '\n';
        glDeleteProgram(program);
        return {};
    }
    labelGlObject(GL_PROGRAM, program, desc.debugName);

    const RhiPipelineHandle handle = m_data->pipelines.allocate();
    const uint32_t slot = handle.index - 1u;
    if (slot >= m_data->pipelineRecords.size()) {
        m_data->pipelineRecords.resize(slot + 1u);
    }
    GlPipelineRecord record;
    record.program = program;
    record.compute = true;
    record.computeDesc = desc;
    record.active = true;
    m_data->pipelineRecords[slot] = std::move(record);
    return handle;
}

RhiBindGroupHandle GlRhiDevice::createBindGroup(const RhiBindGroupDesc& desc) {
    const GlBindGroupLayoutRecord* layout =
        recordForHandle(m_data->bindGroupLayouts, m_data->bindGroupLayoutRecords, desc.layout);
    if (!m_initialized || layout == nullptr) {
        logRhiError("createBindGroup received an invalid layout");
        return {};
    }

    for (const RhiBindGroupEntry& entry : desc.entries) {
        const auto layoutIt = std::find_if(layout->desc.entries.begin(),
                                           layout->desc.entries.end(),
                                           [&](const RhiBindGroupLayoutEntry& layoutEntry) {
                                               return layoutEntry.binding == entry.binding;
                                           });
        if (layoutIt == layout->desc.entries.end()) {
            logRhiError("createBindGroup entry is not declared by its layout");
            return {};
        }
    }

    const RhiBindGroupHandle handle = m_data->bindGroups.allocate();
    const uint32_t slot = handle.index - 1u;
    if (slot >= m_data->bindGroupRecords.size()) {
        m_data->bindGroupRecords.resize(slot + 1u);
    }
    m_data->bindGroupRecords[slot] = {desc, true};
    return handle;
}

RhiQueryPoolHandle GlRhiDevice::createQueryPool(const RhiQueryPoolDesc& desc) {
    if (!m_initialized || desc.queryCount == 0u) {
        logRhiError("createQueryPool received an invalid descriptor");
        return {};
    }
    std::vector<GLuint> queries(desc.queryCount, 0u);
    glGenQueries(static_cast<GLsizei>(queries.size()), queries.data());
    const RhiQueryPoolHandle handle = m_data->queryPools.allocate();
    const uint32_t slot = handle.index - 1u;
    if (slot >= m_data->queryPoolRecords.size()) m_data->queryPoolRecords.resize(slot + 1u);
    m_data->queryPoolRecords[slot] = {std::move(queries), true};
    return handle;
}

bool GlRhiDevice::areQueryResultsAvailable(RhiQueryPoolHandle pool,
                                           uint32_t firstQuery,
                                           uint32_t queryCount) const {
    const GlQueryPoolRecord* record = recordForHandle(
        m_data->queryPools, m_data->queryPoolRecords, pool);
    if (record == nullptr || queryCount == 0u ||
        firstQuery > record->queries.size() || queryCount > record->queries.size() - firstQuery) return false;
    for (uint32_t i = 0; i < queryCount; ++i) {
        GLint available = GL_FALSE;
        glGetQueryObjectiv(record->queries[firstQuery + i], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == GL_FALSE) return false;
    }
    return true;
}

bool GlRhiDevice::getQueryResults(RhiQueryPoolHandle pool, uint32_t firstQuery,
                                  uint32_t queryCount, uint64_t* results) const {
    if (results == nullptr || !areQueryResultsAvailable(pool, firstQuery, queryCount)) return false;
    const GlQueryPoolRecord* record = recordForHandle(
        m_data->queryPools, m_data->queryPoolRecords, pool);
    for (uint32_t i = 0; i < queryCount; ++i) {
        GLuint64 value = 0u;
        glGetQueryObjectui64v(record->queries[firstQuery + i], GL_QUERY_RESULT, &value);
        results[i] = static_cast<uint64_t>(value);
    }
    return true;
}

RhiTextureViewHandle GlRhiDevice::currentSwapchainColorView() const {
    return m_initialized && m_data ? m_data->swapchainColorView : RhiTextureViewHandle{};
}

RhiTextureViewHandle GlRhiDevice::currentSwapchainDepthStencilView() const {
    return m_initialized && m_data ? m_data->swapchainDepthStencilView : RhiTextureViewHandle{};
}

RhiTextureFormat GlRhiDevice::swapchainColorFormat() const {
    return m_data ? m_data->swapchainFormat : RhiTextureFormat::Undefined;
}

RhiTextureFormat GlRhiDevice::swapchainDepthStencilFormat() const {
    return m_data ? m_data->swapchainDepthStencilFormat : RhiTextureFormat::Undefined;
}

bool GlRhiDevice::resizeSwapchain(const uint32_t width, const uint32_t height) {
    if (!m_initialized || !m_data || width == 0u || height == 0u) {
        logRhiError("resizeSwapchain received invalid dimensions");
        return false;
    }

    GlTextureViewRecord* swapchainView =
        recordForHandle(m_data->textureViews, m_data->textureViewRecords, m_data->swapchainColorView);
    if (swapchainView == nullptr || !swapchainView->swapchainBackbuffer) {
        logRhiError("resizeSwapchain requires a live swapchain color view");
        return false;
    }
    GlTextureViewRecord* swapchainDepthView =
        recordForHandle(m_data->textureViews, m_data->textureViewRecords, m_data->swapchainDepthStencilView);
    if (swapchainDepthView == nullptr || !swapchainDepthView->swapchainDepthStencil) {
        logRhiError("resizeSwapchain requires a live swapchain depth-stencil view");
        return false;
    }

    m_data->swapchainWidth = width;
    m_data->swapchainHeight = height;
    return true;
}

void GlRhiDevice::destroyBuffer(RhiBufferHandle handle) {
    GlBufferRecord* record = recordForHandle(m_data->buffers, m_data->bufferRecords, handle);
    if (record != nullptr) {
        glDeleteBuffers(1, &record->buffer);
        *record = {};
    }
    (void) m_data->buffers.release(handle);
}

void GlRhiDevice::destroyTexture(RhiTextureHandle handle) {
    GlTextureRecord* record = recordForHandle(m_data->textures, m_data->textureRecords, handle);
    if (record != nullptr) {
        glDeleteTextures(1, &record->texture);
        *record = {};
    }
    (void) m_data->textures.release(handle);
}

void GlRhiDevice::destroyTextureView(RhiTextureViewHandle handle) {
    GlTextureViewRecord* record = recordForHandle(m_data->textureViews, m_data->textureViewRecords, handle);
    if (record != nullptr) {
        if (record->ownsTexture) {
            glDeleteTextures(1, &record->texture);
        }
        *record = {};
    }
    for (GlFramebufferRecord& framebuffer : m_data->framebufferCache) {
        if (!framebuffer.active) {
            continue;
        }
        const bool usesDepth = sameHandle(framebuffer.depthView, handle);
        const bool usesColor = std::any_of(framebuffer.colorViews.begin(),
                                           framebuffer.colorViews.end(),
                                           [&](const RhiTextureViewHandle view) {
                                               return sameHandle(view, handle);
                                           });
        if (usesDepth || usesColor) {
            glDeleteFramebuffers(1, &framebuffer.framebuffer);
            framebuffer = {};
        }
    }
    (void) m_data->textureViews.release(handle);
}

void GlRhiDevice::destroySampler(RhiSamplerHandle handle) {
    GlSamplerRecord* record = recordForHandle(m_data->samplers, m_data->samplerRecords, handle);
    if (record != nullptr) {
        glDeleteSamplers(1, &record->sampler);
        *record = {};
    }
    (void) m_data->samplers.release(handle);
}

void GlRhiDevice::destroyShader(RhiShaderHandle handle) {
    GlShaderRecord* record = recordForHandle(m_data->shaders, m_data->shaderRecords, handle);
    if (record != nullptr) {
        glDeleteShader(record->shader);
        *record = {};
    }
    (void) m_data->shaders.release(handle);
}

void GlRhiDevice::destroyBindGroupLayout(RhiBindGroupLayoutHandle handle) {
    GlBindGroupLayoutRecord* record =
        recordForHandle(m_data->bindGroupLayouts, m_data->bindGroupLayoutRecords, handle);
    if (record != nullptr) {
        *record = {};
    }
    (void) m_data->bindGroupLayouts.release(handle);
}

void GlRhiDevice::destroyPipelineLayout(RhiPipelineLayoutHandle handle) {
    GlPipelineLayoutRecord* record =
        recordForHandle(m_data->pipelineLayouts, m_data->pipelineLayoutRecords, handle);
    if (record != nullptr) {
        *record = {};
    }
    (void) m_data->pipelineLayouts.release(handle);
}

void GlRhiDevice::destroyPipeline(RhiPipelineHandle handle) {
    GlPipelineRecord* record = recordForHandle(m_data->pipelines, m_data->pipelineRecords, handle);
    if (record != nullptr) {
        if (record->vertexArray != 0u) {
            glDeleteVertexArrays(1, &record->vertexArray);
        }
        if (record->program != 0u) {
            glDeleteProgram(record->program);
        }
        *record = {};
    }
    (void) m_data->pipelines.release(handle);
}

void GlRhiDevice::destroyBindGroup(RhiBindGroupHandle handle) {
    GlBindGroupRecord* record = recordForHandle(m_data->bindGroups, m_data->bindGroupRecords, handle);
    if (record != nullptr) {
        *record = {};
    }
    (void) m_data->bindGroups.release(handle);
}

void GlRhiDevice::destroyQueryPool(RhiQueryPoolHandle handle) {
    GlQueryPoolRecord* record = recordForHandle(
        m_data->queryPools, m_data->queryPoolRecords, handle);
    if (record != nullptr) {
        if (!record->queries.empty()) {
            glDeleteQueries(static_cast<GLsizei>(record->queries.size()), record->queries.data());
        }
        *record = {};
    }
    (void) m_data->queryPools.release(handle);
}

RhiCommandList& GlRhiDevice::beginFrame() {
    m_commandList.resetFrameState();
    return m_commandList;
}

void GlRhiDevice::submitFrame(RhiCommandList& commandList) {
    (void) commandList;
}

void GlRhiDevice::present() {}
