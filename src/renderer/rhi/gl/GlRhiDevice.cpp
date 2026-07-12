#include "renderer/rhi/gl/GlRhiDevice.h"

#include "renderer/rhi/RhiHandleAllocator.h"
#include "renderer/rhi/gl/GlRhiShaderCompiler.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
std::atomic<uint64_t> g_nextRhiDeviceId{1u};

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
    if (filter == RhiFilter::Nearest && mipmapMode == RhiMipmapMode::Nearest) {
        return GL_NEAREST_MIPMAP_NEAREST;
    }
    if (filter == RhiFilter::Nearest && mipmapMode == RhiMipmapMode::Linear) {
        return GL_NEAREST_MIPMAP_LINEAR;
    }
    if (filter == RhiFilter::Linear && mipmapMode == RhiMipmapMode::Nearest) {
        return GL_LINEAR_MIPMAP_NEAREST;
    }
    if (filter == RhiFilter::Linear && mipmapMode == RhiMipmapMode::Linear) {
        return GL_LINEAR_MIPMAP_LINEAR;
    }
    return 0u;
}

[[nodiscard]] GLenum toGlMagFilter(const RhiFilter filter) {
    switch (filter) {
        case RhiFilter::Nearest: return GL_NEAREST;
        case RhiFilter::Linear: return GL_LINEAR;
    }
    return 0u;
}

[[nodiscard]] GLenum toGlAddressMode(const RhiAddressMode mode) {
    switch (mode) {
        case RhiAddressMode::Repeat: return GL_REPEAT;
        case RhiAddressMode::MirroredRepeat: return GL_MIRRORED_REPEAT;
        case RhiAddressMode::ClampToEdge: return GL_CLAMP_TO_EDGE;
        case RhiAddressMode::ClampToBorder: return GL_CLAMP_TO_BORDER;
    }
    return 0u;
}

[[nodiscard]] bool toGlBorderColor(const RhiBorderColor color, std::array<GLfloat, 4>& out) {
    switch (color) {
        case RhiBorderColor::TransparentBlack: out = {0.0f, 0.0f, 0.0f, 0.0f}; return true;
        case RhiBorderColor::OpaqueBlack: out = {0.0f, 0.0f, 0.0f, 1.0f}; return true;
        case RhiBorderColor::OpaqueWhite: out = {1.0f, 1.0f, 1.0f, 1.0f}; return true;
    }
    return false;
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
    return 0u;
}

[[nodiscard]] GLenum toGlTopology(const RhiPrimitiveTopology topology) {
    switch (topology) {
        case RhiPrimitiveTopology::TriangleList: return GL_TRIANGLES;
        case RhiPrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
        case RhiPrimitiveTopology::LineList: return GL_LINES;
        case RhiPrimitiveTopology::LineStrip: return GL_LINE_STRIP;
    }
    return 0u;
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
    return 0u;
}

[[nodiscard]] bool isValidBlendFactor(const RhiBlendFactor factor) {
    switch (factor) {
        case RhiBlendFactor::Zero:
        case RhiBlendFactor::One:
        case RhiBlendFactor::SrcAlpha:
        case RhiBlendFactor::OneMinusSrcAlpha:
        case RhiBlendFactor::SrcColor:
        case RhiBlendFactor::OneMinusSrcColor:
        case RhiBlendFactor::DstAlpha:
        case RhiBlendFactor::OneMinusDstAlpha:
        case RhiBlendFactor::DstColor:
        case RhiBlendFactor::OneMinusDstColor:
            return true;
    }
    return false;
}

[[nodiscard]] GLenum toGlBlendOp(const RhiBlendOp op) {
    switch (op) {
        case RhiBlendOp::Add: return GL_FUNC_ADD;
        case RhiBlendOp::Subtract: return GL_FUNC_SUBTRACT;
        case RhiBlendOp::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
        case RhiBlendOp::Min: return GL_MIN;
        case RhiBlendOp::Max: return GL_MAX;
    }
    return 0u;
}

struct GlVertexFormatInfo {
    GLint componentCount = 0;
    GLenum type = 0u;
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
    return {};
}

[[nodiscard]] uint64_t indexElementSize(const RhiIndexFormat format) {
    switch (format) {
        case RhiIndexFormat::Uint16: return 2u;
        case RhiIndexFormat::Uint32: return 4u;
    }
    return 0u;
}

[[nodiscard]] bool isValidCullMode(const RhiCullMode mode) {
    return mode == RhiCullMode::None || mode == RhiCullMode::Front || mode == RhiCullMode::Back;
}

[[nodiscard]] bool isValidFrontFace(const RhiFrontFace face) {
    return face == RhiFrontFace::CounterClockwise || face == RhiFrontFace::Clockwise;
}

[[nodiscard]] bool isValidVertexInputRate(const RhiVertexInputRate rate) {
    return rate == RhiVertexInputRate::Vertex || rate == RhiVertexInputRate::Instance;
}

[[nodiscard]] const char* resourceStateName(const RhiResourceState state) {
    switch (state) {
        case RhiResourceState::Undefined: return "Undefined";
        case RhiResourceState::Present: return "Present";
        case RhiResourceState::RenderTarget: return "RenderTarget";
        case RhiResourceState::DepthWrite: return "DepthWrite";
        case RhiResourceState::DepthRead: return "DepthRead";
        case RhiResourceState::ShaderRead: return "ShaderRead";
        case RhiResourceState::ShaderWrite: return "ShaderWrite";
        case RhiResourceState::TransferSrc: return "TransferSrc";
        case RhiResourceState::TransferDst: return "TransferDst";
        case RhiResourceState::VertexBuffer: return "VertexBuffer";
        case RhiResourceState::IndexBuffer: return "IndexBuffer";
        case RhiResourceState::IndirectArgument: return "IndirectArgument";
        case RhiResourceState::UniformBuffer: return "UniformBuffer";
        case RhiResourceState::StorageBuffer: return "StorageBuffer";
        case RhiResourceState::HostRead: return "HostRead";
        case RhiResourceState::HostWrite: return "HostWrite";
    }
    return "Invalid";
}

[[nodiscard]] bool textureUsageSupportsState(const RhiTextureUsageFlags usage,
                                             const RhiResourceState state) {
    switch (state) {
        case RhiResourceState::ShaderRead:
            return (usage & rhiFlag(RhiTextureUsage::Sampled)) != 0u;
        case RhiResourceState::ShaderWrite:
            return (usage & rhiFlag(RhiTextureUsage::Storage)) != 0u;
        case RhiResourceState::TransferSrc:
            return (usage & rhiFlag(RhiTextureUsage::TransferSrc)) != 0u;
        case RhiResourceState::TransferDst:
            return (usage & rhiFlag(RhiTextureUsage::TransferDst)) != 0u;
        case RhiResourceState::RenderTarget:
            return (usage & rhiFlag(RhiTextureUsage::ColorAttachment)) != 0u;
        case RhiResourceState::DepthWrite:
        case RhiResourceState::DepthRead:
            return (usage & rhiFlag(RhiTextureUsage::DepthStencilAttachment)) != 0u;
        case RhiResourceState::Present:
            return (usage & rhiFlag(RhiTextureUsage::Present)) != 0u;
        case RhiResourceState::Undefined:
        case RhiResourceState::VertexBuffer:
        case RhiResourceState::IndexBuffer:
        case RhiResourceState::IndirectArgument:
        case RhiResourceState::UniformBuffer:
        case RhiResourceState::StorageBuffer:
        case RhiResourceState::HostRead:
        case RhiResourceState::HostWrite:
            return false;
    }
    return false;
}

[[nodiscard]] bool bufferUsageSupportsState(const RhiBufferUsageFlags usage,
                                            const RhiResourceState state) {
    switch (state) {
        case RhiResourceState::TransferSrc:
            return (usage & rhiFlag(RhiBufferUsage::TransferSrc)) != 0u;
        case RhiResourceState::TransferDst:
            return (usage & rhiFlag(RhiBufferUsage::TransferDst)) != 0u;
        case RhiResourceState::VertexBuffer:
            return (usage & rhiFlag(RhiBufferUsage::Vertex)) != 0u;
        case RhiResourceState::IndexBuffer:
            return (usage & rhiFlag(RhiBufferUsage::Index)) != 0u;
        case RhiResourceState::IndirectArgument:
            return (usage & rhiFlag(RhiBufferUsage::Indirect)) != 0u;
        case RhiResourceState::UniformBuffer:
            return (usage & rhiFlag(RhiBufferUsage::Uniform)) != 0u;
        case RhiResourceState::StorageBuffer:
            return (usage & rhiFlag(RhiBufferUsage::Storage)) != 0u;
        case RhiResourceState::HostRead:
            return (usage & rhiFlag(RhiBufferUsage::MapRead)) != 0u;
        case RhiResourceState::HostWrite:
            return (usage & rhiFlag(RhiBufferUsage::MapWrite)) != 0u;
        case RhiResourceState::Undefined:
            return true;
        case RhiResourceState::Present:
        case RhiResourceState::RenderTarget:
        case RhiResourceState::DepthWrite:
        case RhiResourceState::DepthRead:
        case RhiResourceState::ShaderRead:
        case RhiResourceState::ShaderWrite:
            return false;
    }
    return false;
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
        case RhiResourceState::HostRead: return GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT;
        case RhiResourceState::HostWrite: return GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT;
        case RhiResourceState::Undefined:
        case RhiResourceState::Present:
            return 0u;
    }
    return 0u;
}

[[nodiscard]] GLuint compileShaderObject(const RhiShaderStage shaderStage,
                                         const std::string& sourceText,
                                         const char* debugName) {
    const GLenum stage = toGlShaderStage(shaderStage);
    if (stage == 0u || sourceText.empty()) {
        logRhiError("shader creation requires a valid GLSL source stage");
        return 0u;
    }

    const GLuint shader = glCreateShader(stage);
    const auto sourceSize = static_cast<GLint>(sourceText.size());
    const char* source = sourceText.c_str();
    glShaderSource(shader, 1, &source, &sourceSize);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) {
        labelGlObject(GL_SHADER, shader, debugName);
        return shader;
    }

    std::array<char, 2048> infoLog{};
    glGetShaderInfoLog(shader, static_cast<GLsizei>(infoLog.size()), nullptr, infoLog.data());
    std::cerr << "GlRhiDevice: shader compilation failed [" << rhiDebugName(debugName)
              << "]\n" << infoLog.data() << '\n';
    glDeleteShader(shader);
    return 0u;
}

struct GlBufferRecord {
    GLuint buffer = 0u;
    RhiBufferDesc desc;
    RhiResourceState state = RhiResourceState::Undefined;
    bool mapped = false;
    bool active = false;
};

struct GlTextureRecord {
    GLuint texture = 0u;
    GLenum target = 0u;
    RhiTextureDesc desc;
    std::string debugName;
    GlFormatInfo format;
    std::vector<RhiResourceState> subresourceStates;
    bool swapchainBackbuffer = false;
    bool active = false;
};

struct GlResolvedTextureRecord {
    GLuint texture = 0u;
    GLenum target = 0u;
    RhiTextureDesc desc;
    GlFormatInfo format;
    bool swapchainBackbuffer = false;
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
    renderer::rhi::RhiCompiledShader shader;
    std::string debugName;
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
    struct BindingMapping {
        uint32_t set = 0u;
        uint32_t binding = 0u;
        RhiBindingType type = RhiBindingType::UniformBuffer;
        uint32_t physicalBinding = 0u;
    };

    GLuint program = 0u;
    GLuint vertexArray = 0u;
    bool compute = false;
    RhiGraphicsPipelineDesc graphicsDesc;
    RhiComputePipelineDesc computeDesc;
    std::vector<BindingMapping> bindingMappings;
    std::optional<uint32_t> pushConstantBinding;
    uint32_t pushConstantSize = 0u;
    bool active = false;
};

struct GlBindGroupRecord {
    RhiBindGroupDesc desc;
    bool active = false;
};

struct GlQueryPoolRecord {
    std::vector<GLuint> queries;
    std::vector<bool> issued;
    RhiQueryType type = RhiQueryType::Timestamp;
    bool active = false;
};

struct GlFramebufferRecord {
    GLuint framebuffer = 0u;
    std::vector<RhiTextureViewHandle> colorViews;
    RhiTextureViewHandle depthView;
    bool active = false;
};

struct GlRetiredResources {
    std::vector<GLuint> buffers;
    std::vector<GLuint> textures;
    std::vector<GLuint> samplers;
    std::vector<GLuint> programs;
    std::vector<GLuint> vertexArrays;
    std::vector<GLuint> queries;
    std::vector<GLuint> framebuffers;

    [[nodiscard]] bool empty() const {
        return buffers.empty() && textures.empty() && samplers.empty() &&
               programs.empty() && vertexArrays.empty() && queries.empty() &&
               framebuffers.empty();
    }
};

struct GlRetirementBatch {
    GLsync fence = nullptr;
    uint64_t submissionSequence = 0u;
    GlRetiredResources resources;
    std::vector<std::shared_ptr<GlRhiCommandList>> commandLists;
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

[[nodiscard]] const RhiBindGroupLayoutEntry* findLayoutEntry(
    const GlBindGroupLayoutRecord& layout,
    const uint32_t binding) {
    const auto it = std::find_if(layout.desc.entries.begin(), layout.desc.entries.end(),
                                 [&](const RhiBindGroupLayoutEntry& entry) {
                                     return entry.binding == binding;
                                 });
    return it == layout.desc.entries.end() ? nullptr : &*it;
}

[[nodiscard]] uint32_t bindingNamespace(const RhiBindingType type) {
    switch (type) {
        case RhiBindingType::UniformBuffer: return 0u;
        case RhiBindingType::StorageBuffer: return 1u;
        case RhiBindingType::SampledTexture:
        case RhiBindingType::Sampler:
        case RhiBindingType::CombinedTextureSampler: return 2u;
        case RhiBindingType::StorageTexture: return 3u;
    }
    return 4u;
}

[[nodiscard]] bool buildPipelineBindingMappings(
    const RhiHandleAllocator<RhiBindGroupLayoutHandle>& bindGroupLayouts,
    const std::vector<GlBindGroupLayoutRecord>& bindGroupLayoutRecords,
    const std::array<uint32_t, 4>& bindingLimits,
    const GlPipelineLayoutRecord& pipelineLayout,
    const std::vector<const GlShaderRecord*>& shaders,
    const char* const pipelineDebugName,
    std::vector<GlPipelineRecord::BindingMapping>& mappings,
    std::optional<uint32_t>& pushConstantBinding,
    uint32_t& pushConstantSize) {
    std::vector<renderer::rhi::RhiShaderBindingInfo> reflectedBindings;
    uint32_t reflectedPushConstantSize = 0u;
    RhiShaderStageFlags reflectedPushConstantStages = 0u;

    for (const GlShaderRecord* shaderRecord : shaders) {
        if (shaderRecord == nullptr) {
            logRhiError("pipeline binding reflection received an invalid shader");
            return false;
        }
        for (const renderer::rhi::RhiShaderBindingInfo& binding :
             shaderRecord->shader.reflection.bindings) {
            if (binding.set >= pipelineLayout.desc.bindGroupLayouts.size()) {
                logRhiError("shader descriptor set is not declared by the pipeline layout");
                return false;
            }
            const GlBindGroupLayoutRecord* setLayout = recordForHandle(
                bindGroupLayouts,
                bindGroupLayoutRecords,
                pipelineLayout.desc.bindGroupLayouts[binding.set]);
            const RhiBindGroupLayoutEntry* layoutEntry =
                setLayout != nullptr ? findLayoutEntry(*setLayout, binding.binding) : nullptr;
            if (layoutEntry == nullptr || layoutEntry->type != binding.type ||
                layoutEntry->arrayCount != binding.arrayCount ||
                (layoutEntry->stages & binding.stages) != binding.stages) {
                std::cerr << "GlRhiDevice: shader descriptor reflection does not match pipeline layout"
                          << " (set=" << binding.set
                          << ", binding=" << binding.binding
                          << ", type=" << static_cast<uint32_t>(binding.type)
                          << ", stages=" << binding.stages
                          << ", name=" << binding.name << ")\n";
                return false;
            }

            const auto existing = std::find_if(
                reflectedBindings.begin(), reflectedBindings.end(),
                [&](const renderer::rhi::RhiShaderBindingInfo& candidate) {
                    return candidate.set == binding.set && candidate.binding == binding.binding;
                });
            if (existing == reflectedBindings.end()) {
                reflectedBindings.push_back(binding);
            } else if (existing->type != binding.type || existing->arrayCount != binding.arrayCount) {
                logRhiError("shader stages declare incompatible descriptor types at the same set and binding");
                return false;
            } else {
                existing->stages |= binding.stages;
            }
        }

        if (shaderRecord->shader.reflection.pushConstant.has_value()) {
            const renderer::rhi::RhiPushConstantInfo& pushConstant =
                *shaderRecord->shader.reflection.pushConstant;
            if (reflectedPushConstantSize != 0u && reflectedPushConstantSize != pushConstant.size) {
                logRhiError("shader stages declare incompatible push-constant blocks");
                return false;
            }
            reflectedPushConstantSize = pushConstant.size;
            reflectedPushConstantStages |= pushConstant.stages;
        }
    }

    if (reflectedPushConstantSize != 0u &&
        (pipelineLayout.desc.pushConstantBytes < reflectedPushConstantSize ||
         (pipelineLayout.desc.pushConstantStages & reflectedPushConstantStages) !=
             reflectedPushConstantStages)) {
        std::cerr << "GlRhiDevice: shader push-constant reflection does not match the pipeline layout"
                  << " pipeline=[" << rhiDebugName(pipelineDebugName) << ']'
                  << " reflectedBytes=" << reflectedPushConstantSize
                  << " layoutBytes=" << pipelineLayout.desc.pushConstantBytes
                  << " reflectedStages=" << reflectedPushConstantStages
                  << " layoutStages=" << pipelineLayout.desc.pushConstantStages << '\n';
        return false;
    }

    std::sort(reflectedBindings.begin(), reflectedBindings.end(),
              [](const renderer::rhi::RhiShaderBindingInfo& lhs,
                 const renderer::rhi::RhiShaderBindingInfo& rhs) {
                  if (bindingNamespace(lhs.type) != bindingNamespace(rhs.type)) {
                      return bindingNamespace(lhs.type) < bindingNamespace(rhs.type);
                  }
                  if (lhs.set != rhs.set) return lhs.set < rhs.set;
                  if (lhs.binding != rhs.binding) return lhs.binding < rhs.binding;
                  return static_cast<uint32_t>(lhs.type) < static_cast<uint32_t>(rhs.type);
              });

    std::array<uint32_t, 4> nextPhysicalBinding{};
    mappings.clear();
    for (const renderer::rhi::RhiShaderBindingInfo& binding : reflectedBindings) {
        if (binding.type == RhiBindingType::SampledTexture || binding.type == RhiBindingType::Sampler) {
            logRhiError("separate texture and sampler descriptors require combined-sampler reflection support");
            return false;
        }
        const uint32_t nameSpace = bindingNamespace(binding.type);
        GlPipelineRecord::BindingMapping mapping;
        mapping.set = binding.set;
        mapping.binding = binding.binding;
        mapping.type = binding.type;
        mapping.physicalBinding = nextPhysicalBinding[nameSpace];
        nextPhysicalBinding[nameSpace] += binding.arrayCount;
        mappings.push_back(mapping);
    }

    if (reflectedPushConstantSize != 0u) {
        pushConstantBinding = nextPhysicalBinding[0u]++;
    } else {
        pushConstantBinding.reset();
    }
    pushConstantSize = reflectedPushConstantSize;
    if (nextPhysicalBinding[0u] > bindingLimits[0u] ||
        nextPhysicalBinding[1u] > bindingLimits[1u] ||
        nextPhysicalBinding[2u] > bindingLimits[2u] ||
        nextPhysicalBinding[3u] > bindingLimits[3u]) {
        logRhiError("pipeline descriptor bindings exceed OpenGL hardware limits");
        return false;
    }
    return true;
}

[[nodiscard]] std::vector<renderer::rhi::gl::GlRhiShaderBindingRemap> makeShaderRemaps(
    const std::vector<GlPipelineRecord::BindingMapping>& mappings) {
    std::vector<renderer::rhi::gl::GlRhiShaderBindingRemap> remaps;
    remaps.reserve(mappings.size());
    for (const GlPipelineRecord::BindingMapping& mapping : mappings) {
        remaps.push_back({mapping.set, mapping.binding, mapping.type, mapping.physicalBinding});
    }
    return remaps;
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
    GlRetiredResources pendingRetirements;
    std::deque<GlRetirementBatch> retirementBatches;
    std::vector<std::shared_ptr<GlRhiCommandList>> completedCommandLists;
    uint64_t completedSubmissionSequence = 0u;
    std::mutex commandListRegistryMutex;
    std::unordered_map<GlRhiCommandList*, std::weak_ptr<GlRhiCommandList>> commandLists;

    RhiTextureViewHandle swapchainColorView;
    RhiTextureViewHandle swapchainDepthStencilView;
    RhiTextureHandle swapchainColorTexture;
    RhiTextureFormat swapchainFormat = RhiTextureFormat::Rgba8Unorm;
    RhiTextureFormat swapchainDepthStencilFormat = RhiTextureFormat::Depth24;
    RhiResourceState swapchainDepthStencilState = RhiResourceState::DepthWrite;
    uint32_t swapchainWidth = 1u;
    uint32_t swapchainHeight = 1u;
    GLFWwindow* nativeWindow = nullptr;
    GLuint pushConstantBuffer = 0u;
    uint32_t pushConstantCapacity = 0u;
    uint32_t maxUniformBufferBindings = 0u;
    uint32_t maxStorageBufferBindings = 0u;
    uint32_t maxTextureUnits = 0u;
    uint32_t maxImageUnits = 0u;
    uint32_t uniformBufferOffsetAlignment = 1u;
    uint32_t storageBufferOffsetAlignment = 1u;
    GLuint currentFramebuffer = 0u;
    bool depthWriteMaskEnabled = true;
    std::vector<GLenum> currentStoreDiscardAttachments;
};

struct GlRhiCommandPoolRegistry {
    std::mutex mutex;
    GlRhiDevice* device = nullptr;
    std::unordered_set<GlRhiCommandListPool*> pools;
};

struct GlRhiCommandResourceReferences {
    std::vector<RhiBufferHandle> buffers;
    std::vector<RhiTextureHandle> textures;
    std::vector<RhiTextureViewHandle> textureViews;
    std::vector<RhiSamplerHandle> samplers;
    std::vector<RhiBindGroupLayoutHandle> bindGroupLayouts;
    std::vector<RhiPipelineLayoutHandle> pipelineLayouts;
    std::vector<RhiPipelineHandle> pipelines;
    std::vector<RhiBindGroupHandle> bindGroups;
    std::vector<RhiQueryPoolHandle> queryPools;

    template <typename Handle>
    void add(std::vector<Handle>& handles, const Handle handle) {
        handles.push_back(handle);
    }

    void reference(GlRhiDeviceData& data, const RhiBufferHandle handle) {
        (void) data;
        add(buffers, handle);
    }

    void reference(GlRhiDeviceData& data, const RhiTextureHandle handle) {
        (void) data;
        add(textures, handle);
    }

    void reference(GlRhiDeviceData& data, const RhiTextureViewHandle handle) {
        (void) data;
        add(textureViews, handle);
    }

    void reference(GlRhiDeviceData& data, const RhiSamplerHandle handle) {
        (void) data;
        add(samplers, handle);
    }

    void reference(GlRhiDeviceData& data, const RhiBindGroupLayoutHandle handle) {
        (void) data;
        add(bindGroupLayouts, handle);
    }

    void reference(GlRhiDeviceData& data, const RhiPipelineLayoutHandle handle) {
        (void) data;
        add(pipelineLayouts, handle);
    }

    void reference(GlRhiDeviceData& data, const RhiPipelineHandle handle) {
        (void) data;
        add(pipelines, handle);
    }

    void reference(GlRhiDeviceData& data, const RhiBindGroupHandle handle) {
        (void) data;
        add(bindGroups, handle);
    }

    void reference(GlRhiDeviceData& data, const RhiQueryPoolHandle handle) {
        (void) data;
        add(queryPools, handle);
    }

    void clear() {
        buffers.clear();
        textures.clear();
        textureViews.clear();
        samplers.clear();
        bindGroupLayouts.clear();
        pipelineLayouts.clear();
        pipelines.clear();
        bindGroups.clear();
        queryPools.clear();
    }

    [[nodiscard]] bool validate(const GlRhiDeviceData& data) const {
        const auto allAlive = [](const auto& handles, const auto& allocator,
                                 const auto& records) {
            return std::all_of(handles.begin(), handles.end(),
                               [&](const auto handle) {
                                   return recordForHandle(allocator, records, handle) != nullptr;
                               });
        };
        const auto textureViewAlive = [&data](const RhiTextureViewHandle handle) {
            const GlTextureViewRecord* view = recordForHandle(
                data.textureViews, data.textureViewRecords, handle);
            return view != nullptr &&
                   (!view->desc.texture.isValid() ||
                    recordForHandle(data.textures, data.textureRecords,
                                    view->desc.texture) != nullptr);
        };
        const auto pipelineLayoutAlive = [&data](const RhiPipelineLayoutHandle handle) {
            const GlPipelineLayoutRecord* layout = recordForHandle(
                data.pipelineLayouts, data.pipelineLayoutRecords, handle);
            return layout != nullptr &&
                   std::all_of(layout->desc.bindGroupLayouts.begin(),
                               layout->desc.bindGroupLayouts.end(),
                               [&data](const RhiBindGroupLayoutHandle setLayout) {
                                   return recordForHandle(data.bindGroupLayouts,
                                                          data.bindGroupLayoutRecords,
                                                          setLayout) != nullptr;
                               });
        };
        const auto pipelineAlive = [&data, &pipelineLayoutAlive](const RhiPipelineHandle handle) {
            const GlPipelineRecord* pipeline = recordForHandle(
                data.pipelines, data.pipelineRecords, handle);
            return pipeline != nullptr && pipelineLayoutAlive(
                pipeline->compute ? pipeline->computeDesc.layout
                                  : pipeline->graphicsDesc.layout);
        };
        const auto bindGroupAlive = [&data, &textureViewAlive](const RhiBindGroupHandle handle) {
            const GlBindGroupRecord* group = recordForHandle(
                data.bindGroups, data.bindGroupRecords, handle);
            if (group == nullptr) {
                return false;
            }
            const GlBindGroupLayoutRecord* layout = recordForHandle(
                data.bindGroupLayouts, data.bindGroupLayoutRecords, group->desc.layout);
            if (layout == nullptr) {
                return false;
            }
            for (const RhiBindGroupEntry& entry : group->desc.entries) {
                const RhiBindGroupLayoutEntry* layoutEntry =
                    findLayoutEntry(*layout, entry.binding);
                if (layoutEntry == nullptr) {
                    return false;
                }
                switch (layoutEntry->type) {
                    case RhiBindingType::UniformBuffer:
                    case RhiBindingType::StorageBuffer:
                        if (recordForHandle(data.buffers, data.bufferRecords,
                                            entry.resource.buffer.buffer) == nullptr) {
                            return false;
                        }
                        break;
                    case RhiBindingType::SampledTexture:
                    case RhiBindingType::StorageTexture:
                        if (!textureViewAlive(entry.resource.textureView)) {
                            return false;
                        }
                        break;
                    case RhiBindingType::Sampler:
                        if (recordForHandle(data.samplers, data.samplerRecords,
                                            entry.resource.sampler) == nullptr) {
                            return false;
                        }
                        break;
                    case RhiBindingType::CombinedTextureSampler:
                        if (!textureViewAlive(entry.resource.combinedTextureSampler.textureView) ||
                            recordForHandle(data.samplers, data.samplerRecords,
                                            entry.resource.combinedTextureSampler.sampler) == nullptr) {
                            return false;
                        }
                        break;
                }
            }
            return true;
        };
        const auto validateDirect = [&allAlive](const char* type,
                                                const auto& handles,
                                                const auto& allocator,
                                                const auto& records) {
            const bool alive = allAlive(handles, allocator, records);
            if (!alive) {
                for (const auto handle : handles) {
                    if (recordForHandle(allocator, records, handle) == nullptr) {
                        std::cerr << "GlRhiDevice: recorded " << type
                                  << " is no longer alive (index=" << handle.index
                                  << ", generation=" << handle.generation << ")\n";
                    }
                }
            }
            return alive;
        };
        const auto validateResolved = [](const char* type,
                                         const auto& handles,
                                         const auto& predicate) {
            bool alive = true;
            for (const auto handle : handles) {
                if (!predicate(handle)) {
                    std::cerr << "GlRhiDevice: recorded " << type
                              << " or one of its dependencies is no longer alive (index="
                              << handle.index << ", generation=" << handle.generation << ")\n";
                    alive = false;
                }
            }
            return alive;
        };
        const bool buffersAlive = validateDirect(
            "buffer", buffers, data.buffers, data.bufferRecords);
        const bool texturesAlive = validateDirect(
            "texture", textures, data.textures, data.textureRecords);
        const bool textureViewsAlive = validateResolved(
            "texture view", textureViews, textureViewAlive);
        const bool samplersAlive = validateDirect(
            "sampler", samplers, data.samplers, data.samplerRecords);
        const bool bindGroupLayoutsAlive = validateDirect(
            "bind-group layout", bindGroupLayouts,
            data.bindGroupLayouts, data.bindGroupLayoutRecords);
        const bool pipelineLayoutsAlive = validateResolved(
            "pipeline layout", pipelineLayouts, pipelineLayoutAlive);
        const bool pipelinesAlive = validateResolved(
            "pipeline", pipelines, pipelineAlive);
        const bool bindGroupsAlive = validateResolved(
            "bind group", bindGroups, bindGroupAlive);
        const bool queryPoolsAlive = validateDirect(
            "query pool", queryPools, data.queryPools, data.queryPoolRecords);
        return buffersAlive && texturesAlive && textureViewsAlive && samplersAlive &&
               bindGroupLayoutsAlive && pipelineLayoutsAlive && pipelinesAlive &&
               bindGroupsAlive && queryPoolsAlive;
    }
};

namespace {

void deleteRetiredResources(GlRetiredResources& resources) {
    if (!resources.framebuffers.empty()) {
        glDeleteFramebuffers(static_cast<GLsizei>(resources.framebuffers.size()),
                             resources.framebuffers.data());
    }
    if (!resources.vertexArrays.empty()) {
        glDeleteVertexArrays(static_cast<GLsizei>(resources.vertexArrays.size()),
                             resources.vertexArrays.data());
    }
    for (const GLuint program : resources.programs) {
        glDeleteProgram(program);
    }
    if (!resources.samplers.empty()) {
        glDeleteSamplers(static_cast<GLsizei>(resources.samplers.size()),
                         resources.samplers.data());
    }
    if (!resources.textures.empty()) {
        glDeleteTextures(static_cast<GLsizei>(resources.textures.size()),
                         resources.textures.data());
    }
    if (!resources.buffers.empty()) {
        glDeleteBuffers(static_cast<GLsizei>(resources.buffers.size()),
                        resources.buffers.data());
    }
    if (!resources.queries.empty()) {
        glDeleteQueries(static_cast<GLsizei>(resources.queries.size()),
                        resources.queries.data());
    }
    resources = {};
}

void reclaimCompletedRetirementBatches(GlRhiDeviceData& data) {
    while (!data.retirementBatches.empty()) {
        GlRetirementBatch& batch = data.retirementBatches.front();
        const GLenum result = glClientWaitSync(batch.fence, 0u, 0u);
        if (result == GL_TIMEOUT_EXPIRED) {
            return;
        }
        if (result == GL_WAIT_FAILED) {
            logRhiError("deferred resource retirement fence wait failed");
            return;
        }
        deleteRetiredResources(batch.resources);
        data.completedCommandLists.insert(data.completedCommandLists.end(),
                                          batch.commandLists.begin(),
                                          batch.commandLists.end());
        data.completedSubmissionSequence = batch.submissionSequence;
        glDeleteSync(batch.fence);
        data.retirementBatches.pop_front();
    }
}

void reclaimAllRetiredResources(GlRhiDeviceData& data) {
    for (GlRetirementBatch& batch : data.retirementBatches) {
        deleteRetiredResources(batch.resources);
        data.completedCommandLists.insert(data.completedCommandLists.end(),
                                          batch.commandLists.begin(),
                                          batch.commandLists.end());
        data.completedSubmissionSequence = batch.submissionSequence;
        if (batch.fence != nullptr) {
            glDeleteSync(batch.fence);
        }
    }
    data.retirementBatches.clear();
    deleteRetiredResources(data.pendingRetirements);
}

struct GlBlitEndpoint {
    GLuint texture = 0u;
    GLenum target = 0u;
    RhiTextureDesc desc;
    GlFormatInfo format;
    RhiTextureHandle stateTexture;
    uint32_t stateMip = 0u;
    uint32_t stateBaseLayer = 0u;
    uint32_t stateLayerCount = 0u;
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

struct GlTextureSubresourceRange {
    uint32_t baseMip = 0u;
    uint32_t mipCount = 0u;
    uint32_t baseLayer = 0u;
    uint32_t layerCount = 0u;
};

[[nodiscard]] bool resolveTextureSubresourceRange(
    const RhiTextureDesc& desc,
    const uint32_t baseMip,
    const uint32_t mipCount,
    const uint32_t baseLayer,
    const uint32_t layerCount,
    GlTextureSubresourceRange& resolved) {
    if (baseMip >= desc.mipLevels || baseLayer >= desc.depthOrLayers) {
        return false;
    }
    resolved.baseMip = baseMip;
    resolved.mipCount = mipCount == kRhiRemainingMipLevels
                            ? desc.mipLevels - baseMip
                            : mipCount;
    resolved.baseLayer = baseLayer;
    resolved.layerCount = layerCount == kRhiRemainingArrayLayers
                              ? desc.depthOrLayers - baseLayer
                              : layerCount;
    return resolved.mipCount != 0u && resolved.layerCount != 0u &&
           resolved.mipCount <= desc.mipLevels - baseMip &&
           resolved.layerCount <= desc.depthOrLayers - baseLayer;
}

[[nodiscard]] size_t textureSubresourceIndex(const RhiTextureDesc& desc,
                                             const uint32_t mip,
                                             const uint32_t layer) {
    return static_cast<size_t>(mip) * desc.depthOrLayers + layer;
}

[[nodiscard]] bool textureRangeHasState(const GlTextureRecord& record,
                                        const GlTextureSubresourceRange& range,
                                        const RhiResourceState state) {
    for (uint32_t mip = range.baseMip; mip < range.baseMip + range.mipCount; ++mip) {
        for (uint32_t layer = range.baseLayer; layer < range.baseLayer + range.layerCount; ++layer) {
            if (record.subresourceStates[textureSubresourceIndex(record.desc, mip, layer)] != state) {
                return false;
            }
        }
    }
    return true;
}

void setTextureRangeState(GlTextureRecord& record,
                          const GlTextureSubresourceRange& range,
                          const RhiResourceState state) {
    for (uint32_t mip = range.baseMip; mip < range.baseMip + range.mipCount; ++mip) {
        for (uint32_t layer = range.baseLayer; layer < range.baseLayer + range.layerCount; ++layer) {
            record.subresourceStates[textureSubresourceIndex(record.desc, mip, layer)] = state;
        }
    }
}

[[nodiscard]] bool textureHandleRangeHasState(
    const GlRhiDeviceData& data,
    const RhiTextureHandle texture,
    const GlTextureSubresourceRange& range,
    const RhiResourceState state) {
    const GlTextureRecord* record = recordForHandle(
        data.textures, data.textureRecords, texture);
    return record != nullptr && textureRangeHasState(*record, range, state);
}

[[nodiscard]] bool textureViewHasState(
    const GlRhiDeviceData& data,
    const RhiTextureViewHandle viewHandle,
    const RhiResourceState state) {
    const GlTextureViewRecord* view = recordForHandle(
        data.textureViews, data.textureViewRecords, viewHandle);
    if (view == nullptr || !view->desc.texture.isValid()) {
        return false;
    }
    const GlTextureRecord* texture = recordForHandle(
        data.textures, data.textureRecords, view->desc.texture);
    if (texture == nullptr) {
        return false;
    }
    GlTextureSubresourceRange range;
    return resolveTextureSubresourceRange(texture->desc,
                                          view->desc.baseMip,
                                          view->desc.mipCount,
                                          view->desc.baseLayer,
                                          view->desc.layerCount,
                                          range) &&
           textureRangeHasState(*texture, range, state);
}

[[nodiscard]] bool resolveTextureRecord(GlRhiDeviceData& data,
                                        const RhiTextureHandle handle,
                                        GlResolvedTextureRecord& resolved) {
    resolved = {};
    if (!handle.isValid()) {
        return false;
    }

    const GlTextureRecord* deviceRecord = recordForHandle(data.textures, data.textureRecords, handle);
    if (deviceRecord == nullptr) {
        return false;
    }

    resolved.texture = deviceRecord->texture;
    resolved.target = deviceRecord->target;
    resolved.desc = deviceRecord->desc;
    resolved.format = deviceRecord->format;
    resolved.swapchainBackbuffer = deviceRecord->swapchainBackbuffer;
    resolved.valid = true;
    return true;
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
    endpoint.stateTexture = handle;
    endpoint.stateMip = mipLevel;
    endpoint.stateBaseLayer = 0u;
    endpoint.stateLayerCount = 1u;
    endpoint.attachmentMip = mipLevel;
    endpoint.swapchain = resolved.swapchainBackbuffer;
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
            ? rhiFlag(RhiTextureUsage::Present) | rhiFlag(RhiTextureUsage::ColorAttachment) |
                  rhiFlag(RhiTextureUsage::TransferSrc) | rhiFlag(RhiTextureUsage::TransferDst)
            : rhiFlag(RhiTextureUsage::DepthStencilAttachment);
        endpoint.format = viewRecord->format;
        endpoint.stateTexture = viewRecord->desc.texture;
        endpoint.stateMip = 0u;
        endpoint.stateBaseLayer = 0u;
        endpoint.stateLayerCount = 1u;
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
    endpoint.stateTexture = viewRecord->desc.texture;
    endpoint.stateMip = viewRecord->desc.baseMip;
    endpoint.stateBaseLayer = viewRecord->desc.baseLayer;
    endpoint.stateLayerCount = viewRecord->desc.layerCount;
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

[[nodiscard]] bool validatePipelineTextureStates(
    const GlRhiDeviceData& data,
    const GlPipelineRecord& pipeline,
    const std::vector<RhiBindGroupHandle>& boundGroups,
    const char* commandName) {
    for (const GlPipelineRecord::BindingMapping& mapping : pipeline.bindingMappings) {
        if (mapping.type != RhiBindingType::SampledTexture &&
            mapping.type != RhiBindingType::StorageTexture &&
            mapping.type != RhiBindingType::CombinedTextureSampler) {
            continue;
        }
        if (mapping.set >= boundGroups.size()) {
            return false;
        }
        const GlBindGroupRecord* group = recordForHandle(
            data.bindGroups, data.bindGroupRecords, boundGroups[mapping.set]);
        if (group == nullptr) {
            return false;
        }
        const auto entryIt = std::find_if(
            group->desc.entries.begin(), group->desc.entries.end(),
            [&](const RhiBindGroupEntry& entry) { return entry.binding == mapping.binding; });
        if (entryIt == group->desc.entries.end()) {
            return false;
        }
        const RhiTextureViewHandle view = mapping.type == RhiBindingType::CombinedTextureSampler
            ? entryIt->resource.combinedTextureSampler.textureView
            : entryIt->resource.textureView;
        const GlTextureViewRecord* viewRecord = recordForHandle(
            data.textureViews, data.textureViewRecords, view);
        const RhiResourceState requiredState = mapping.type == RhiBindingType::StorageTexture
            ? RhiResourceState::ShaderWrite
            : (viewRecord != nullptr && viewRecord->format.depth
                ? RhiResourceState::DepthRead
                : RhiResourceState::ShaderRead);
        if (!textureViewHasState(data, view, requiredState)) {
            const GlTextureRecord* textureRecord = viewRecord != nullptr
                ? recordForHandle(data.textures, data.textureRecords, viewRecord->desc.texture)
                : nullptr;
            std::cerr << "GlRhiDevice: " << commandName
                      << " texture descriptor state does not match its binding type"
                      << " pipeline=["
                      << rhiDebugName(pipeline.compute
                              ? pipeline.computeDesc.debugName
                              : pipeline.graphicsDesc.debugName)
                      << "]"
                      << " set=" << mapping.set
                      << " binding=" << mapping.binding
                      << " bindGroupHandle=" << boundGroups[mapping.set].index << ':'
                      << boundGroups[mapping.set].generation
                      << " viewHandle=" << view.index << ':' << view.generation;
            if (viewRecord != nullptr) {
                std::cerr << " textureHandle=" << viewRecord->desc.texture.index << ':'
                          << viewRecord->desc.texture.generation
                          << " mipRange=" << viewRecord->desc.baseMip << '+'
                          << viewRecord->desc.mipCount
                          << " layerRange=" << viewRecord->desc.baseLayer << '+'
                          << viewRecord->desc.layerCount;
            }
            if (textureRecord != nullptr) {
                std::cerr << " texture=[" << rhiDebugName(textureRecord->debugName.c_str()) << ']';
                GlTextureSubresourceRange range;
                if (resolveTextureSubresourceRange(textureRecord->desc,
                                                   viewRecord->desc.baseMip,
                                                   viewRecord->desc.mipCount,
                                                   viewRecord->desc.baseLayer,
                                                   viewRecord->desc.layerCount,
                                                   range)) {
                    bool foundMismatch = false;
                    for (uint32_t mip = range.baseMip;
                         mip < range.baseMip + range.mipCount && !foundMismatch;
                         ++mip) {
                        for (uint32_t layer = range.baseLayer;
                             layer < range.baseLayer + range.layerCount;
                             ++layer) {
                            const RhiResourceState tracked = textureRecord->subresourceStates[
                                textureSubresourceIndex(textureRecord->desc, mip, layer)];
                            if (tracked != requiredState) {
                                std::cerr << " mismatchMip=" << mip
                                          << " mismatchLayer=" << layer
                                          << " required=" << resourceStateName(requiredState)
                                          << " tracked=" << resourceStateName(tracked);
                                foundMismatch = true;
                                break;
                            }
                        }
                    }
                }
            }
            std::cerr << '\n';
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool validatePipelineBufferStates(
    const GlRhiDeviceData& data,
    const GlPipelineRecord& pipeline,
    const std::vector<RhiBindGroupHandle>& boundGroups,
    const char* commandName) {
    for (const GlPipelineRecord::BindingMapping& mapping : pipeline.bindingMappings) {
        if (mapping.type != RhiBindingType::UniformBuffer &&
            mapping.type != RhiBindingType::StorageBuffer) {
            continue;
        }
        if (mapping.set >= boundGroups.size()) {
            return false;
        }
        const GlBindGroupRecord* group = recordForHandle(
            data.bindGroups, data.bindGroupRecords, boundGroups[mapping.set]);
        if (group == nullptr) {
            return false;
        }
        const auto entryIt = std::find_if(
            group->desc.entries.begin(), group->desc.entries.end(),
            [&](const RhiBindGroupEntry& entry) { return entry.binding == mapping.binding; });
        if (entryIt == group->desc.entries.end()) {
            return false;
        }
        const GlBufferRecord* buffer = recordForHandle(
            data.buffers, data.bufferRecords, entryIt->resource.buffer.buffer);
        const RhiResourceState requiredState = mapping.type == RhiBindingType::UniformBuffer
            ? RhiResourceState::UniformBuffer
            : RhiResourceState::StorageBuffer;
        if (buffer == nullptr || buffer->state != requiredState) {
            std::cerr << "GlRhiDevice: " << commandName
                      << " buffer descriptor state does not match its binding type"
                      << " pipeline=["
                      << rhiDebugName(pipeline.compute
                              ? pipeline.computeDesc.debugName
                              : pipeline.graphicsDesc.debugName)
                      << "] set=" << mapping.set
                      << " binding=" << mapping.binding
                      << " required=" << resourceStateName(requiredState)
                      << " tracked=" << resourceStateName(
                             buffer != nullptr ? buffer->state : RhiResourceState::Undefined)
                      << '\n';
            return false;
        }
    }
    return true;
}

} // namespace

GlRhiCommandList::GlRhiCommandList()
    : m_resourceReferences(std::make_unique<GlRhiCommandResourceReferences>()) {}

GlRhiCommandList::~GlRhiCommandList() = default;

bool GlRhiCommandList::begin(const RhiCommandListDesc& desc) {
    if (!m_acquired || !isRecordingThread()) {
        logRhiError("command list begin requires acquisition on the pool owner thread");
        return false;
    }
    if (m_state != RhiCommandListState::Initial) {
        logRhiError("command list begin requires Initial state");
        return false;
    }
    if (desc.type != m_acquiredType) {
        logRhiError("command list begin type must match the acquired list type");
        return false;
    }
    resetFrameState();
    m_commandStream.clear();
    m_resourceReferences->clear();
    m_recordingRendering = false;
    m_recordingHasDepthAttachment = false;
    m_recordingValid = true;
    m_recordingDebugLabelDepth = 0u;
    m_state = RhiCommandListState::Recording;
    return true;
}

bool GlRhiCommandList::end() {
    if (!isRecordingThread() || m_state != RhiCommandListState::Recording) {
        logRhiError("command list end requires Recording state on the pool owner thread");
        return false;
    }
    if (!m_recordingValid) {
        logRhiError("command list end rejects a recording that contains an illegal command");
        resetFrameState();
        m_commandStream.clear();
        m_resourceReferences->clear();
        m_recordingRendering = false;
        m_recordingHasDepthAttachment = false;
        m_recordingDebugLabelDepth = 0u;
        m_state = RhiCommandListState::Initial;
        m_acquired = false;
        return false;
    }
    if (m_recordingRendering || m_recordingDebugLabelDepth != 0u) {
        logRhiError("command list end requires closed rendering and debug-label scopes");
        return false;
    }
    m_state = RhiCommandListState::Executable;
    return true;
}

RhiCommandListState GlRhiCommandList::state() const {
    return m_state;
}

void GlRhiCommandList::attachDevice(GlRhiDevice* device) {
    m_device = device;
}

void GlRhiCommandList::attachPool(GlRhiCommandListPool* pool) {
    m_pool = pool;
}

bool GlRhiCommandList::isRecordingThread() const {
    return m_pool != nullptr && m_pool->isOwnerThread();
}

void GlRhiCommandList::resetForPoolReuse() {
    resetFrameState();
    m_commandStream.clear();
    m_resourceReferences->clear();
    m_recordingRendering = false;
    m_recordingHasDepthAttachment = false;
    m_recordingValid = true;
    m_recordingDebugLabelDepth = 0u;
    m_replaying = false;
    m_validationOnly = false;
    m_replayValid = true;
    m_state = RhiCommandListState::Initial;
    m_acquired = false;
}

void GlRhiCommandList::resetFrameState() {
    m_graphicsPipeline = {};
    m_computePipeline = {};
    m_boundPipelineLayout = {};
    m_bindGroups.clear();
    m_vertexBuffers.clear();
    m_indexBuffer = {};
    m_pushConstantLayout = {};
    m_pushConstantSize = 0u;
    m_pushConstantStages = 0u;
    m_renderingColorFormats.clear();
    m_renderingDepthFormat = RhiTextureFormat::Undefined;
    m_rendering = false;
}

bool GlRhiCommandList::beginRecordedCommand(const CommandType type) {
    if (!isRecordingThread() || m_state != RhiCommandListState::Recording) {
        logRhiError("command recording requires Recording state");
        return false;
    }
    if (!m_recordingValid) {
        return false;
    }
    if (!commandTypeSupports(type)) {
        return rejectRecordedCommand("command is not supported by this command-list type");
    }
    if (!renderingScopeSupports(type)) {
        std::cerr << "GlRhiDevice: command is not valid in the current rendering scope"
                  << " commandType=" << static_cast<uint32_t>(type)
                  << " rendering=" << (m_recordingRendering ? 1 : 0) << '\n';
        m_recordingValid = false;
        return false;
    }
    appendValue(type);
    return true;
}

bool GlRhiCommandList::rejectRecordedCommand(const char* reason) {
    logRhiError(reason);
    m_recordingValid = false;
    return false;
}

bool GlRhiCommandList::rejectReplayCommand(const char* reason) {
    logRhiError(reason);
    m_replayValid = false;
    return false;
}

bool GlRhiCommandList::commandTypeSupports(const CommandType type) const {
    const auto isSharedCommand = [](const CommandType command) {
        switch (command) {
        case CommandType::BeginDebugLabel:
        case CommandType::EndDebugLabel:
        case CommandType::InsertDebugMarker:
        case CommandType::TextureBarrier:
        case CommandType::BufferBarrier:
        case CommandType::UpdateBuffer:
        case CommandType::CopyBuffer:
        case CommandType::CopyBufferToTexture:
        case CommandType::CopyTextureToBuffer:
        case CommandType::CopyTexture:
        case CommandType::BlitTexture:
        case CommandType::GenerateMipmaps:
        case CommandType::ResetQueryPool:
        case CommandType::WriteTimestamp:
            return true;
        default:
            return false;
        }
    };
    if (isSharedCommand(type)) {
        return true;
    }

    const auto isComputeCommand = [](const CommandType command) {
        switch (command) {
        case CommandType::SetComputePipeline:
        case CommandType::SetBindGroup:
        case CommandType::PushConstants:
        case CommandType::Dispatch:
            return true;
        default:
            return false;
        }
    };
    if (m_acquiredType == RhiCommandListType::Transfer) {
        return false;
    }
    if (isComputeCommand(type)) {
        return true;
    }
    if (m_acquiredType != RhiCommandListType::Graphics) {
        return false;
    }

    switch (type) {
    case CommandType::BeginRendering:
    case CommandType::EndRendering:
    case CommandType::ClearDepthAttachment:
    case CommandType::SetViewport:
    case CommandType::SetScissor:
    case CommandType::SetGraphicsPipeline:
    case CommandType::SetVertexBuffer:
    case CommandType::SetIndexBuffer:
    case CommandType::Draw:
    case CommandType::DrawIndexed:
    case CommandType::DrawIndirect:
        return true;
    default:
        return false;
    }
}

bool GlRhiCommandList::renderingScopeSupports(const CommandType type) const {
    if (!m_recordingRendering) {
        return type != CommandType::EndRendering &&
               type != CommandType::ClearDepthAttachment &&
               type != CommandType::Draw &&
               type != CommandType::DrawIndexed &&
               type != CommandType::DrawIndirect;
    }

    switch (type) {
    case CommandType::BeginDebugLabel:
    case CommandType::EndDebugLabel:
    case CommandType::InsertDebugMarker:
    case CommandType::EndRendering:
    case CommandType::ClearDepthAttachment:
    case CommandType::SetViewport:
    case CommandType::SetScissor:
    case CommandType::SetGraphicsPipeline:
    case CommandType::SetBindGroup:
    case CommandType::SetVertexBuffer:
    case CommandType::SetIndexBuffer:
    case CommandType::PushConstants:
    case CommandType::Draw:
    case CommandType::DrawIndexed:
    case CommandType::DrawIndirect:
    case CommandType::WriteTimestamp:
        return true;
    default:
        return false;
    }
}

void GlRhiCommandList::appendBytes(const void* data, const size_t size) {
    if (size == 0u) {
        return;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    m_commandStream.insert(m_commandStream.end(), bytes, bytes + size);
}

bool GlRhiCommandList::readBytes(size_t& offset, void* destination, const size_t size) const {
    if (offset > m_commandStream.size() || size > m_commandStream.size() - offset) {
        logRhiError("command stream payload is truncated");
        return false;
    }
    if (size != 0u) {
        std::memcpy(destination, m_commandStream.data() + offset, size);
    }
    offset += size;
    return true;
}

void GlRhiCommandList::recordString(const char* value) {
    const size_t length = value != nullptr ? std::strlen(value) + 1u : 0u;
    const uint64_t encodedLength = static_cast<uint64_t>(length);
    appendValue(encodedLength);
    appendBytes(value, length);
}

bool GlRhiCommandList::readString(size_t& offset, const char*& value) const {
    uint64_t length = 0u;
    if (!readValue(offset, length) ||
        length > static_cast<uint64_t>(m_commandStream.size() - offset)) {
        logRhiError("command stream string is truncated");
        return false;
    }
    value = length == 0u
        ? nullptr
        : reinterpret_cast<const char*>(m_commandStream.data() + offset);
    offset += static_cast<size_t>(length);
    return true;
}

void GlRhiCommandList::referenceResource(const RhiBufferHandle handle) {
    if (m_device != nullptr && m_device->m_data) {
        m_resourceReferences->reference(*m_device->m_data, handle);
    }
}

void GlRhiCommandList::referenceResource(const RhiTextureHandle handle) {
    if (m_device != nullptr && m_device->m_data) {
        m_resourceReferences->reference(*m_device->m_data, handle);
    }
}

void GlRhiCommandList::referenceResource(const RhiTextureViewHandle handle) {
    if (m_device != nullptr && m_device->m_data) {
        m_resourceReferences->reference(*m_device->m_data, handle);
    }
}

void GlRhiCommandList::referenceResource(const RhiSamplerHandle handle) {
    if (m_device != nullptr && m_device->m_data) {
        m_resourceReferences->reference(*m_device->m_data, handle);
    }
}

void GlRhiCommandList::referenceResource(const RhiBindGroupLayoutHandle handle) {
    if (m_device != nullptr && m_device->m_data) {
        m_resourceReferences->reference(*m_device->m_data, handle);
    }
}

void GlRhiCommandList::referenceResource(const RhiPipelineLayoutHandle handle) {
    if (m_device != nullptr && m_device->m_data) {
        m_resourceReferences->reference(*m_device->m_data, handle);
    }
}

void GlRhiCommandList::referenceResource(const RhiPipelineHandle handle) {
    if (m_device != nullptr && m_device->m_data) {
        m_resourceReferences->reference(*m_device->m_data, handle);
    }
}

void GlRhiCommandList::referenceResource(const RhiBindGroupHandle handle) {
    if (m_device != nullptr && m_device->m_data) {
        m_resourceReferences->reference(*m_device->m_data, handle);
    }
}

void GlRhiCommandList::referenceResource(const RhiQueryPoolHandle handle) {
    if (m_device != nullptr && m_device->m_data) {
        m_resourceReferences->reference(*m_device->m_data, handle);
    }
}

bool GlRhiCommandList::validateForSubmit() const {
    return m_state == RhiCommandListState::Executable &&
           m_device != nullptr && m_device->m_data &&
           m_resourceReferences->validate(*m_device->m_data);
}

bool GlRhiCommandList::replay(const bool validationOnly) {
    if (m_state != RhiCommandListState::Executable) {
        logRhiError("command list replay requires Executable state");
        return false;
    }
    if (m_device == nullptr || !m_device->m_data ||
        !m_resourceReferences->validate(*m_device->m_data)) {
        logRhiError("command list references a resource destroyed before submission");
        return false;
    }

    resetFrameState();
    if (!validationOnly) {
        m_device->m_data->currentFramebuffer = 0u;
        m_device->m_data->currentStoreDiscardAttachments.clear();
        m_state = RhiCommandListState::Pending;
    }
    m_replaying = true;
    m_validationOnly = validationOnly;
    m_replayValid = true;
    size_t offset = 0u;
    bool valid = true;
    while (valid && offset < m_commandStream.size()) {
        CommandType type{};
        valid = readValue(offset, type);
        if (!valid) {
            break;
        }
        switch (type) {
            case CommandType::BeginDebugLabel: {
                glm::vec4 color{};
                const char* name = nullptr;
                valid = readValue(offset, color) && readString(offset, name);
                if (valid) beginDebugLabel(name, color);
                break;
            }
            case CommandType::EndDebugLabel:
                endDebugLabel();
                break;
            case CommandType::InsertDebugMarker: {
                glm::vec4 color{};
                const char* name = nullptr;
                valid = readValue(offset, color) && readString(offset, name);
                if (valid) insertDebugMarker(name, color);
                break;
            }
            case CommandType::TextureBarrier: {
                RhiTextureBarrier barrier{};
                valid = readValue(offset, barrier);
                if (valid) textureBarrier(barrier);
                break;
            }
            case CommandType::BufferBarrier: {
                RhiBufferBarrier barrier{};
                valid = readValue(offset, barrier);
                if (valid) bufferBarrier(barrier);
                break;
            }
            case CommandType::BeginRendering: {
                RhiRenderingInfo info{};
                bool hasDepth = false;
                uint64_t colorCount = 0u;
                valid = readValue(offset, info.renderArea) &&
                        readValue(offset, colorCount) &&
                        readValue(offset, hasDepth);
                if (!valid || colorCount > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) ||
                    colorCount > static_cast<uint64_t>(m_commandStream.size() / sizeof(RhiColorAttachment))) {
                    valid = false;
                    break;
                }
                std::vector<RhiColorAttachment> colors(static_cast<size_t>(colorCount));
                valid = readBytes(offset, colors.data(), colors.size() * sizeof(RhiColorAttachment));
                RhiDepthStencilAttachment depth{};
                if (valid && hasDepth) {
                    valid = readValue(offset, depth);
                }
                const char* debugName = nullptr;
                if (valid) {
                    valid = readString(offset, debugName);
                }
                if (valid) {
                    info.debugName = debugName;
                    info.colorAttachments = colors.empty() ? nullptr : colors.data();
                    info.colorAttachmentCount = static_cast<uint32_t>(colors.size());
                    info.depthStencilAttachment = hasDepth ? &depth : nullptr;
                    beginRendering(info);
                }
                break;
            }
            case CommandType::EndRendering:
                endRendering();
                break;
            case CommandType::ClearDepthAttachment: {
                float depth = 1.0f;
                RhiRect2D rect{};
                valid = readValue(offset, depth) && readValue(offset, rect);
                if (valid) clearDepthAttachment(depth, rect);
                break;
            }
            case CommandType::SetViewport: {
                RhiViewport viewport{};
                valid = readValue(offset, viewport);
                if (valid) setViewport(viewport);
                break;
            }
            case CommandType::SetScissor: {
                RhiRect2D rect{};
                valid = readValue(offset, rect);
                if (valid) setScissor(rect);
                break;
            }
            case CommandType::SetGraphicsPipeline: {
                RhiPipelineHandle pipeline{};
                valid = readValue(offset, pipeline);
                if (valid) setGraphicsPipeline(pipeline);
                break;
            }
            case CommandType::SetComputePipeline: {
                RhiPipelineHandle pipeline{};
                valid = readValue(offset, pipeline);
                if (valid) setComputePipeline(pipeline);
                break;
            }
            case CommandType::SetBindGroup: {
                uint32_t setIndex = 0u;
                RhiBindGroupHandle bindGroup{};
                valid = readValue(offset, setIndex) && readValue(offset, bindGroup);
                if (valid) setBindGroup(setIndex, bindGroup);
                break;
            }
            case CommandType::SetVertexBuffer: {
                uint32_t slot = 0u;
                RhiBufferHandle buffer{};
                uint64_t bufferOffset = 0u;
                valid = readValue(offset, slot) && readValue(offset, buffer) &&
                        readValue(offset, bufferOffset);
                if (valid) setVertexBuffer(slot, buffer, bufferOffset);
                break;
            }
            case CommandType::SetIndexBuffer: {
                RhiBufferHandle buffer{};
                RhiIndexFormat format = RhiIndexFormat::Uint32;
                uint64_t bufferOffset = 0u;
                valid = readValue(offset, buffer) && readValue(offset, format) &&
                        readValue(offset, bufferOffset);
                if (valid) setIndexBuffer(buffer, format, bufferOffset);
                break;
            }
            case CommandType::PushConstants: {
                uint64_t size = 0u;
                RhiShaderStageFlags stages = 0u;
                valid = readValue(offset, size) && readValue(offset, stages) &&
                        size <= static_cast<uint64_t>(m_commandStream.size() - offset);
                if (valid) {
                    pushConstants(m_commandStream.data() + offset, static_cast<size_t>(size), stages);
                    offset += static_cast<size_t>(size);
                }
                break;
            }
            case CommandType::Draw: {
                uint32_t vertexCount = 0u, instanceCount = 0u, firstVertex = 0u, firstInstance = 0u;
                valid = readValue(offset, vertexCount) && readValue(offset, instanceCount) &&
                        readValue(offset, firstVertex) && readValue(offset, firstInstance);
                if (valid) draw(vertexCount, instanceCount, firstVertex, firstInstance);
                break;
            }
            case CommandType::DrawIndexed: {
                uint32_t indexCount = 0u, instanceCount = 0u, firstIndex = 0u, firstInstance = 0u;
                int32_t vertexOffset = 0;
                valid = readValue(offset, indexCount) && readValue(offset, instanceCount) &&
                        readValue(offset, firstIndex) && readValue(offset, vertexOffset) &&
                        readValue(offset, firstInstance);
                if (valid) drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
                break;
            }
            case CommandType::DrawIndirect: {
                RhiBufferHandle buffer{};
                uint64_t bufferOffset = 0u;
                uint32_t drawCount = 0u, stride = 0u;
                valid = readValue(offset, buffer) && readValue(offset, bufferOffset) &&
                        readValue(offset, drawCount) && readValue(offset, stride);
                if (valid) drawIndirect(buffer, bufferOffset, drawCount, stride);
                break;
            }
            case CommandType::Dispatch: {
                uint32_t x = 0u, y = 0u, z = 0u;
                valid = readValue(offset, x) && readValue(offset, y) && readValue(offset, z);
                if (valid) dispatch(x, y, z);
                break;
            }
            case CommandType::UpdateBuffer: {
                RhiBufferHandle buffer{};
                uint64_t bufferOffset = 0u, size = 0u;
                valid = readValue(offset, buffer) && readValue(offset, bufferOffset) &&
                        readValue(offset, size) &&
                        size <= static_cast<uint64_t>(m_commandStream.size() - offset);
                if (valid) {
                    updateBuffer(buffer, bufferOffset, m_commandStream.data() + offset,
                                 static_cast<size_t>(size));
                    offset += static_cast<size_t>(size);
                }
                break;
            }
            case CommandType::CopyBuffer: {
                RhiBufferCopy copy{};
                valid = readValue(offset, copy);
                if (valid) copyBuffer(copy);
                break;
            }
            case CommandType::CopyBufferToTexture: {
                RhiBufferTextureCopy copy{};
                valid = readValue(offset, copy);
                if (valid) copyBufferToTexture(copy);
                break;
            }
            case CommandType::CopyTextureToBuffer: {
                RhiTextureBufferCopy copy{};
                valid = readValue(offset, copy);
                if (valid) copyTextureToBuffer(copy);
                break;
            }
            case CommandType::CopyTexture: {
                RhiTextureCopy copy{};
                valid = readValue(offset, copy);
                if (valid) copyTexture(copy);
                break;
            }
            case CommandType::BlitTexture: {
                RhiTextureBlit blit{};
                valid = readValue(offset, blit);
                if (valid) blitTexture(blit);
                break;
            }
            case CommandType::GenerateMipmaps: {
                RhiTextureHandle texture{};
                valid = readValue(offset, texture);
                if (valid) generateMipmaps(texture);
                break;
            }
            case CommandType::ResetQueryPool: {
                RhiQueryPoolHandle pool{};
                uint32_t firstQuery = 0u, queryCount = 0u;
                valid = readValue(offset, pool) && readValue(offset, firstQuery) &&
                        readValue(offset, queryCount);
                if (valid) resetQueryPool(pool, firstQuery, queryCount);
                break;
            }
            case CommandType::WriteTimestamp: {
                RhiQueryPoolHandle pool{};
                uint32_t queryIndex = 0u;
                valid = readValue(offset, pool) && readValue(offset, queryIndex);
                if (valid) writeTimestamp(pool, queryIndex);
                break;
            }
            default:
                logRhiError("command stream contains an invalid command type");
                valid = false;
                break;
        }
        valid = valid && m_replayValid;
    }

    if (m_rendering) {
        logRhiError("command replay ended inside a rendering scope");
        valid = false;
    }
    m_replaying = false;
    m_validationOnly = false;
    if (validationOnly) {
        resetFrameState();
        return valid;
    }
    m_state = valid ? RhiCommandListState::Pending : RhiCommandListState::Initial;
    m_commandStream.clear();
    m_resourceReferences->clear();
    if (!valid) m_acquired = false;
    return valid;
}

void GlRhiCommandList::beginDebugLabel(const char* name, const glm::vec4& color) {
    if (!m_replaying) {
        if (name == nullptr || name[0] == '\0') {
            (void) rejectRecordedCommand("beginDebugLabel requires a non-empty name");
            return;
        }
        if (!beginRecordedCommand(CommandType::BeginDebugLabel)) return;
        appendValue(color);
        recordString(name);
        ++m_recordingDebugLabelDepth;
        return;
    }
    (void) color;
    if (!m_validationOnly && name != nullptr && name[0] != '\0' && GLAD_GL_VERSION_4_3) {
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0u, -1, name);
    }
}

void GlRhiCommandList::endDebugLabel() {
    if (!m_replaying) {
        if (m_recordingDebugLabelDepth == 0u) {
            (void) rejectRecordedCommand("endDebugLabel requires an active debug label");
            return;
        }
        if (!beginRecordedCommand(CommandType::EndDebugLabel)) return;
        --m_recordingDebugLabelDepth;
        return;
    }
    if (!m_validationOnly && GLAD_GL_VERSION_4_3) {
        glPopDebugGroup();
    }
}

void GlRhiCommandList::insertDebugMarker(const char* name, const glm::vec4& color) {
    if (!m_replaying) {
        if (name == nullptr || name[0] == '\0') {
            (void) rejectRecordedCommand("insertDebugMarker requires a non-empty name");
            return;
        }
        if (!beginRecordedCommand(CommandType::InsertDebugMarker)) return;
        appendValue(color);
        recordString(name);
        return;
    }
    (void) color;
    if (!m_validationOnly && name != nullptr && name[0] != '\0' && GLAD_GL_VERSION_4_3) {
        glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION,
                             GL_DEBUG_TYPE_MARKER,
                             0u,
                             GL_DEBUG_SEVERITY_NOTIFICATION,
                             -1,
                             name);
    }
}

void GlRhiCommandList::textureBarrier(const RhiTextureBarrier& barrier) {
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::TextureBarrier)) return;
        appendValue(barrier);
        referenceResource(barrier.texture);
        return;
    }
    if (m_device == nullptr || !m_device->m_data) {
        (void) rejectReplayCommand("textureBarrier requires an initialized device");
        return;
    }
    GlTextureRecord* record = recordForHandle(
        m_device->m_data->textures, m_device->m_data->textureRecords, barrier.texture);
    if (record == nullptr) {
        (void) rejectReplayCommand("textureBarrier received an invalid texture handle");
        return;
    }
    GlTextureSubresourceRange range;
    if (!resolveTextureSubresourceRange(record->desc,
                                        barrier.baseMip,
                                        barrier.mipCount,
                                        barrier.baseLayer,
                                        barrier.layerCount,
                                        range)) {
        (void) rejectReplayCommand("textureBarrier received an invalid subresource range");
        return;
    }
    if (!textureRangeHasState(*record, range, barrier.oldState)) {
        for (uint32_t mip = range.baseMip; mip < range.baseMip + range.mipCount; ++mip) {
            for (uint32_t layer = range.baseLayer; layer < range.baseLayer + range.layerCount; ++layer) {
                const RhiResourceState trackedState = record->subresourceStates[
                    textureSubresourceIndex(record->desc, mip, layer)];
                if (trackedState != barrier.oldState) {
                    std::cerr << "GlRhiDevice: textureBarrier oldState mismatch texture=["
                              << rhiDebugName(record->debugName.c_str()) << "] handle="
                              << barrier.texture.index << ':' << barrier.texture.generation
                              << " mip=" << mip << " layer=" << layer
                              << " expected=" << resourceStateName(barrier.oldState)
                              << " tracked=" << resourceStateName(trackedState)
                              << " new=" << resourceStateName(barrier.newState) << '\n';
                    m_replayValid = false;
                    return;
                }
            }
        }
        return;
    }

    const GLbitfield bits = barrierBitsForState(barrier.newState);
    if (!m_validationOnly && bits != 0u) {
        glMemoryBarrier(bits);
    }
    setTextureRangeState(*record, range, barrier.newState);
}

void GlRhiCommandList::bufferBarrier(const RhiBufferBarrier& barrier) {
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::BufferBarrier)) return;
        appendValue(barrier);
        referenceResource(barrier.buffer);
        return;
    }
    if (m_device == nullptr || !m_device->m_data) {
        (void) rejectReplayCommand("bufferBarrier requires an initialized device");
        return;
    }
    GlBufferRecord* record = recordForHandle(
        m_device->m_data->buffers, m_device->m_data->bufferRecords, barrier.buffer);
    if (record == nullptr) {
        (void) rejectReplayCommand("bufferBarrier received an invalid buffer handle");
        return;
    }
    if (record->mapped || record->state != barrier.oldState) {
        std::cerr << "GlRhiDevice: bufferBarrier oldState does not match the tracked buffer state"
                  << " buffer=[" << rhiDebugName(record->desc.debugName) << ']'
                  << " handle=" << barrier.buffer.index << ':' << barrier.buffer.generation
                  << " expected=" << resourceStateName(barrier.oldState)
                  << " tracked=" << resourceStateName(record->state)
                  << " new=" << resourceStateName(barrier.newState) << '\n';
        m_replayValid = false;
        return;
    }
    if (barrier.newState == RhiResourceState::Undefined ||
        !bufferUsageSupportsState(record->desc.usage, barrier.newState)) {
        (void) rejectReplayCommand("bufferBarrier newState is incompatible with the buffer usage");
        return;
    }

    const GLbitfield bits = barrierBitsForState(barrier.newState);
    if (!m_validationOnly && bits != 0u) {
        glMemoryBarrier(bits);
    }
    record->state = barrier.newState;
}

void GlRhiCommandList::beginRendering(const RhiRenderingInfo& info) {
    if (!m_replaying) {
        if (m_recordingRendering) {
            (void) rejectRecordedCommand("beginRendering cannot nest rendering scopes");
            return;
        }
        if (info.colorAttachmentCount != 0u && info.colorAttachments == nullptr) {
            (void) rejectRecordedCommand("beginRendering requires color attachment storage");
            return;
        }
        if (info.depthStencilAttachment != nullptr &&
            !info.depthStencilAttachment->view.isValid()) {
            (void) rejectRecordedCommand("beginRendering requires a valid depth attachment view");
            return;
        }
        if (!beginRecordedCommand(CommandType::BeginRendering)) return;
        appendValue(info.renderArea);
        appendValue(static_cast<uint64_t>(info.colorAttachmentCount));
        appendValue(info.depthStencilAttachment != nullptr);
        appendBytes(info.colorAttachments,
                    static_cast<size_t>(info.colorAttachmentCount) * sizeof(RhiColorAttachment));
        if (info.depthStencilAttachment != nullptr) {
            appendValue(*info.depthStencilAttachment);
        }
        recordString(info.debugName);
        for (uint32_t i = 0u; i < info.colorAttachmentCount; ++i) {
            referenceResource(info.colorAttachments[i].view);
        }
        if (info.depthStencilAttachment != nullptr &&
            info.depthStencilAttachment->view.isValid()) {
            referenceResource(info.depthStencilAttachment->view);
        }
        m_recordingRendering = true;
        m_recordingHasDepthAttachment = info.depthStencilAttachment != nullptr;
        return;
    }
    if (m_device == nullptr || !m_device->m_data) {
        (void) rejectReplayCommand("beginRendering requires an initialized device");
        return;
    }

    auto& data = *m_device->m_data;
    const auto viewHasState = [&data](const GlTextureViewRecord& viewRecord,
                                      const RhiResourceState requiredState) {
        if (viewRecord.swapchainDepthStencil) {
            return data.swapchainDepthStencilState == requiredState;
        }
        GlTextureRecord* textureRecord = recordForHandle(
            data.textures, data.textureRecords, viewRecord.desc.texture);
        if (textureRecord == nullptr) {
            return false;
        }
        GlTextureSubresourceRange range;
        return resolveTextureSubresourceRange(textureRecord->desc,
                                              viewRecord.desc.baseMip,
                                              viewRecord.desc.mipCount,
                                              viewRecord.desc.baseLayer,
                                              viewRecord.desc.layerCount,
                                              range) &&
               textureRangeHasState(*textureRecord, range, requiredState);
    };
    std::vector<RhiTextureViewHandle> colorViews;
    colorViews.reserve(info.colorAttachmentCount);
    for (uint32_t i = 0u; i < info.colorAttachmentCount; ++i) {
        const RhiTextureViewHandle view = info.colorAttachments[i].view;
        const GlTextureViewRecord* viewRecord =
            recordForHandle(data.textureViews, data.textureViewRecords, view);
        if (viewRecord == nullptr) {
            (void) rejectReplayCommand("beginRendering received an invalid color attachment view");
            return;
        }
        if (!viewHasState(*viewRecord, RhiResourceState::RenderTarget)) {
            const GlTextureRecord* textureRecord = recordForHandle(
                data.textures, data.textureRecords, viewRecord->desc.texture);
            std::cerr << "GlRhiDevice: beginRendering color attachment state mismatch"
                      << " scope=[" << rhiDebugName(info.debugName) << ']'
                      << " attachment=" << i
                      << " viewHandle=" << view.index << ':' << view.generation
                      << " textureHandle=" << viewRecord->desc.texture.index << ':'
                      << viewRecord->desc.texture.generation
                      << " texture=["
                      << (textureRecord != nullptr
                              ? rhiDebugName(textureRecord->debugName.c_str())
                              : "<invalid>")
                      << "] tracked="
                      << resourceStateName(
                             textureRecord != nullptr && !textureRecord->subresourceStates.empty()
                                 ? textureRecord->subresourceStates.front()
                                 : RhiResourceState::Undefined)
                      << " required=RenderTarget\n";
            (void) rejectReplayCommand("beginRendering requires color attachments in RenderTarget state");
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
            (void) rejectReplayCommand("beginRendering received an invalid depth attachment view");
            return;
        }
        if (!viewHasState(*depthViewRecord, RhiResourceState::DepthWrite)) {
            (void) rejectReplayCommand("beginRendering requires the depth attachment in DepthWrite state");
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
        (void) rejectReplayCommand("beginRendering requires exactly one swapchain color attachment");
        return;
    }
    if (renderToSwapchain && depthView.isValid() &&
        (depthViewRecord == nullptr || !depthViewRecord->swapchainDepthStencil)) {
        (void) rejectReplayCommand("beginRendering requires the swapchain depth-stencil view for swapchain depth output");
        return;
    }
    if (!renderToSwapchain && depthViewRecord != nullptr && depthViewRecord->swapchainDepthStencil) {
        (void) rejectReplayCommand("beginRendering requires swapchain depth-stencil with swapchain color output");
        return;
    }

    m_renderingColorFormats.clear();
    m_renderingColorFormats.reserve(colorViews.size());
    for (const RhiTextureViewHandle view : colorViews) {
        const GlTextureViewRecord* viewRecord =
            recordForHandle(data.textureViews, data.textureViewRecords, view);
        m_renderingColorFormats.push_back(viewRecord->resolvedFormat);
    }
    m_renderingDepthFormat = depthViewRecord != nullptr
        ? depthViewRecord->resolvedFormat
        : RhiTextureFormat::Undefined;
    if (m_validationOnly) {
        m_rendering = true;
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
                m_replayValid = false;
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
            const bool restoreDepthWriteMask = !data.depthWriteMaskEnabled;
            if (restoreDepthWriteMask) {
                glDepthMask(GL_TRUE);
                data.depthWriteMaskEnabled = true;
            }
            if (viewRecord->format.stencil) {
                clearFramebufferDepthStencil(framebuffer, attachment->clearDepth, attachment->clearStencil);
            } else {
                clearFramebufferDepth(framebuffer, attachment->clearDepth);
            }
            if (restoreDepthWriteMask) {
                glDepthMask(GL_FALSE);
                data.depthWriteMaskEnabled = false;
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
    if (!m_replaying) {
        if (!m_recordingRendering) {
            (void) rejectRecordedCommand("endRendering requires an active rendering scope");
            return;
        }
        if (!beginRecordedCommand(CommandType::EndRendering)) return;
        m_recordingRendering = false;
        m_recordingHasDepthAttachment = false;
        return;
    }
    if (m_device == nullptr || !m_device->m_data) {
        (void) rejectReplayCommand("endRendering requires an initialized device");
        return;
    }

    auto& data = *m_device->m_data;
    if (!m_validationOnly) {
        invalidateFramebufferData(data.currentFramebuffer, data.currentStoreDiscardAttachments);
        data.currentFramebuffer = 0u;
        data.currentStoreDiscardAttachments.clear();
    }
    m_renderingColorFormats.clear();
    m_renderingDepthFormat = RhiTextureFormat::Undefined;
    m_rendering = false;
}

void GlRhiCommandList::clearDepthAttachment(const float depth, const RhiRect2D& rect) {
    if (!m_replaying) {
        if (!m_recordingRendering) {
            (void) rejectRecordedCommand("clearDepthAttachment requires an active rendering scope");
            return;
        }
        if (!m_recordingHasDepthAttachment) {
            (void) rejectRecordedCommand("clearDepthAttachment requires a depth attachment");
            return;
        }
        if (!beginRecordedCommand(CommandType::ClearDepthAttachment)) return;
        appendValue(depth);
        appendValue(rect);
        return;
    }
    if (!m_rendering) {
        (void) rejectReplayCommand("clearDepthAttachment requires an active rendering scope");
        return;
    }
    if (m_renderingDepthFormat == RhiTextureFormat::Undefined) {
        (void) rejectReplayCommand("clearDepthAttachment requires a depth attachment");
        return;
    }
    if (m_validationOnly) return;
    glEnable(GL_SCISSOR_TEST);
    glScissor(rect.x, rect.y,
              static_cast<GLsizei>(rect.width),
              static_cast<GLsizei>(rect.height));
    auto& data = *m_device->m_data;
    const bool restoreDepthWriteMask = !data.depthWriteMaskEnabled;
    if (restoreDepthWriteMask) {
        glDepthMask(GL_TRUE);
        data.depthWriteMaskEnabled = true;
    }
    glClearBufferfv(GL_DEPTH, 0, &depth);
    if (restoreDepthWriteMask) {
        glDepthMask(GL_FALSE);
        data.depthWriteMaskEnabled = false;
    }
}

void GlRhiCommandList::setViewport(const RhiViewport& viewport) {
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::SetViewport)) return;
        appendValue(viewport);
        return;
    }
    if (m_validationOnly) return;
    glViewport(static_cast<GLint>(viewport.x),
               static_cast<GLint>(viewport.y),
               static_cast<GLsizei>(viewport.width),
               static_cast<GLsizei>(viewport.height));
    glDepthRange(viewport.minDepth, viewport.maxDepth);
}

void GlRhiCommandList::setScissor(const RhiRect2D& rect) {
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::SetScissor)) return;
        appendValue(rect);
        return;
    }
    if (m_validationOnly) return;
    glEnable(GL_SCISSOR_TEST);
    glScissor(rect.x, rect.y, static_cast<GLsizei>(rect.width), static_cast<GLsizei>(rect.height));
}

void GlRhiCommandList::setGraphicsPipeline(RhiPipelineHandle pipeline) {
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::SetGraphicsPipeline)) return;
        appendValue(pipeline);
        referenceResource(pipeline);
        return;
    }
    if (m_device == nullptr || !m_device->m_data) {
        (void) rejectReplayCommand("setGraphicsPipeline requires an initialized device");
        return;
    }

    const GlPipelineRecord* record =
        recordForHandle(m_device->m_data->pipelines, m_device->m_data->pipelineRecords, pipeline);
    if (record == nullptr || record->compute) {
        (void) rejectReplayCommand("setGraphicsPipeline received an invalid graphics pipeline");
        return;
    }

    auto& data = *m_device->m_data;
    const RhiPipelineLayoutHandle newLayout = record->graphicsDesc.layout;
    const bool layoutCompatible = sameHandle(m_graphicsPipeline, pipeline) &&
        sameHandle(m_boundPipelineLayout, newLayout);
    const GlPipelineRecord* previous = recordForHandle(
        data.pipelines, data.pipelineRecords, m_graphicsPipeline);
    bool vertexBindingsCompatible = previous != nullptr && !previous->compute &&
        previous->graphicsDesc.vertexInput.bindings.size() ==
            record->graphicsDesc.vertexInput.bindings.size();
    if (vertexBindingsCompatible) {
        for (size_t i = 0u; i < record->graphicsDesc.vertexInput.bindings.size(); ++i) {
            const RhiVertexBinding& lhs = previous->graphicsDesc.vertexInput.bindings[i];
            const RhiVertexBinding& rhs = record->graphicsDesc.vertexInput.bindings[i];
            if (lhs.binding != rhs.binding || lhs.stride != rhs.stride ||
                lhs.inputRate != rhs.inputRate) {
                vertexBindingsCompatible = false;
                break;
            }
        }
    }
    if (!layoutCompatible) {
        m_bindGroups.clear();
        m_pushConstantLayout = {};
        m_pushConstantSize = 0u;
        m_pushConstantStages = 0u;
    }
    if (!vertexBindingsCompatible) {
        m_vertexBuffers.clear();
    }
    if (previous == nullptr || previous->compute) {
        m_indexBuffer = {};
    }

    if (!m_validationOnly) {
        const RhiGraphicsPipelineDesc& desc = record->graphicsDesc;
        glUseProgram(record->program);
        glBindVertexArray(record->vertexArray);

        for (uint32_t slot = 0u; slot < m_vertexBuffers.size(); ++slot) {
            const VertexBufferBindingState& binding = m_vertexBuffers[slot];
            if (!binding.valid) {
                continue;
            }
            const GlBufferRecord* buffer = recordForHandle(
                data.buffers, data.bufferRecords, binding.buffer);
            const auto vertexBinding = std::find_if(
                record->graphicsDesc.vertexInput.bindings.begin(),
                record->graphicsDesc.vertexInput.bindings.end(),
                [slot](const RhiVertexBinding& candidate) { return candidate.binding == slot; });
            if (buffer != nullptr && vertexBinding != record->graphicsDesc.vertexInput.bindings.end()) {
                glVertexArrayVertexBuffer(record->vertexArray, slot, buffer->buffer,
                                          static_cast<GLintptr>(binding.offset),
                                          static_cast<GLsizei>(vertexBinding->stride));
            }
        }
        if (m_indexBuffer.valid) {
            const GlBufferRecord* indexBuffer = recordForHandle(
                data.buffers, data.bufferRecords, m_indexBuffer.buffer);
            if (indexBuffer != nullptr) {
                glVertexArrayElementBuffer(record->vertexArray, indexBuffer->buffer);
            }
        }

        if (desc.depthStencil.depthTestEnabled) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(toGlCompareOp(desc.depthStencil.depthCompare));
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        data.depthWriteMaskEnabled = desc.depthStencil.depthWriteEnabled;
        glDepthMask(data.depthWriteMaskEnabled ? GL_TRUE : GL_FALSE);

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
    }

    m_graphicsPipeline = pipeline;
    m_computePipeline = {};
    m_boundPipelineLayout = newLayout;
}

void GlRhiCommandList::setComputePipeline(RhiPipelineHandle pipeline) {
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::SetComputePipeline)) return;
        appendValue(pipeline);
        referenceResource(pipeline);
        return;
    }
    if (m_device == nullptr || !m_device->m_data) {
        (void) rejectReplayCommand("setComputePipeline requires an initialized device");
        return;
    }

    const GlPipelineRecord* record =
        recordForHandle(m_device->m_data->pipelines, m_device->m_data->pipelineRecords, pipeline);
    if (record == nullptr || !record->compute) {
        (void) rejectReplayCommand("setComputePipeline received an invalid compute pipeline");
        return;
    }

    const RhiPipelineLayoutHandle newLayout = record->computeDesc.layout;
    if (!sameHandle(m_computePipeline, pipeline) ||
        !sameHandle(m_boundPipelineLayout, newLayout)) {
        m_bindGroups.clear();
        m_pushConstantLayout = {};
        m_pushConstantSize = 0u;
        m_pushConstantStages = 0u;
    }
    if (m_graphicsPipeline.isValid()) {
        m_vertexBuffers.clear();
        m_indexBuffer = {};
    }

    if (!m_validationOnly) glUseProgram(record->program);
    m_computePipeline = pipeline;
    m_graphicsPipeline = {};
    m_boundPipelineLayout = newLayout;
}

void GlRhiCommandList::setBindGroup(uint32_t setIndex, RhiBindGroupHandle bindGroup) {
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::SetBindGroup)) return;
        appendValue(setIndex);
        appendValue(bindGroup);
        referenceResource(bindGroup);
        return;
    }
    if (m_device == nullptr || !m_device->m_data) {
        (void) rejectReplayCommand("setBindGroup requires an initialized device");
        return;
    }

    auto& data = *m_device->m_data;
    const GlBindGroupRecord* record = recordForHandle(data.bindGroups, data.bindGroupRecords, bindGroup);
    if (record == nullptr) {
        (void) rejectReplayCommand("setBindGroup received an invalid bind group");
        return;
    }

    const GlBindGroupLayoutRecord* layoutRecord =
        recordForHandle(data.bindGroupLayouts, data.bindGroupLayoutRecords, record->desc.layout);
    if (layoutRecord == nullptr) {
        (void) rejectReplayCommand("setBindGroup references an invalid layout");
        return;
    }

    const GlPipelineRecord* pipeline = nullptr;
    if (m_graphicsPipeline.isValid()) {
        pipeline = recordForHandle(data.pipelines, data.pipelineRecords, m_graphicsPipeline);
    } else if (m_computePipeline.isValid()) {
        pipeline = recordForHandle(data.pipelines, data.pipelineRecords, m_computePipeline);
    }
    if (pipeline == nullptr) {
        (void) rejectReplayCommand("setBindGroup requires a bound pipeline");
        return;
    }
    const RhiPipelineLayoutHandle pipelineLayoutHandle =
        pipeline->compute ? pipeline->computeDesc.layout : pipeline->graphicsDesc.layout;
    const GlPipelineLayoutRecord* pipelineLayout =
        recordForHandle(data.pipelineLayouts, data.pipelineLayoutRecords, pipelineLayoutHandle);
    if (pipelineLayout == nullptr || setIndex >= pipelineLayout->desc.bindGroupLayouts.size() ||
        !sameHandle(pipelineLayout->desc.bindGroupLayouts[setIndex], record->desc.layout)) {
        (void) rejectReplayCommand("setBindGroup is incompatible with the bound pipeline layout set");
        return;
    }

    for (const RhiBindGroupEntry& entry : record->desc.entries) {
        const auto layoutIt = std::find_if(layoutRecord->desc.entries.begin(),
                                           layoutRecord->desc.entries.end(),
                                           [&](const RhiBindGroupLayoutEntry& layoutEntry) {
                                               return layoutEntry.binding == entry.binding;
                                           });
        if (layoutIt == layoutRecord->desc.entries.end()) {
            (void) rejectReplayCommand("setBindGroup entry is not declared by its layout");
            return;
        }
        const auto mappingIt = std::find_if(
            pipeline->bindingMappings.begin(), pipeline->bindingMappings.end(),
            [&](const GlPipelineRecord::BindingMapping& mapping) {
                return mapping.set == setIndex && mapping.binding == entry.binding &&
                       mapping.type == layoutIt->type;
            });
        if (mappingIt == pipeline->bindingMappings.end()) {
            continue;
        }
        const GLuint physicalBinding = static_cast<GLuint>(mappingIt->physicalBinding);

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
                    entry.resource.buffer.offset % data.uniformBufferOffsetAlignment != 0u ||
                    range == 0u || entry.resource.buffer.offset > buffer->desc.size ||
                    range > buffer->desc.size - entry.resource.buffer.offset) {
                    (void) rejectReplayCommand("setBindGroup received an invalid uniform buffer binding");
                    return;
                }
                if (!m_validationOnly) {
                    glBindBufferRange(GL_UNIFORM_BUFFER,
                                      physicalBinding,
                                      buffer->buffer,
                                      static_cast<GLintptr>(entry.resource.buffer.offset),
                                      static_cast<GLsizeiptr>(range));
                }
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
                    entry.resource.buffer.offset % data.storageBufferOffsetAlignment != 0u ||
                    range == 0u || entry.resource.buffer.offset > buffer->desc.size ||
                    range > buffer->desc.size - entry.resource.buffer.offset) {
                    (void) rejectReplayCommand("setBindGroup received an invalid storage buffer binding");
                    return;
                }
                if (!m_validationOnly) {
                    glBindBufferRange(GL_SHADER_STORAGE_BUFFER,
                                      physicalBinding,
                                      buffer->buffer,
                                      static_cast<GLintptr>(entry.resource.buffer.offset),
                                      static_cast<GLsizeiptr>(range));
                }
                break;
            }
            case RhiBindingType::SampledTexture: {
                const GlTextureViewRecord* view =
                    recordForHandle(data.textureViews, data.textureViewRecords, entry.resource.textureView);
                GlResolvedTextureRecord texture;
                if (view == nullptr || !resolveTextureRecord(data, view->desc.texture, texture) ||
                    (texture.desc.usage & rhiFlag(RhiTextureUsage::Sampled)) == 0u) {
                    (void) rejectReplayCommand("setBindGroup received an invalid sampled texture view");
                    return;
                }
                if (!m_validationOnly) glBindTextureUnit(physicalBinding, view->texture);
                break;
            }
            case RhiBindingType::StorageTexture: {
                const GlTextureViewRecord* view =
                    recordForHandle(data.textureViews, data.textureViewRecords, entry.resource.textureView);
                GlResolvedTextureRecord texture;
                if (view == nullptr || !resolveTextureRecord(data, view->desc.texture, texture) ||
                    (texture.desc.usage & rhiFlag(RhiTextureUsage::Storage)) == 0u) {
                    (void) rejectReplayCommand("setBindGroup received an invalid storage texture view");
                    return;
                }
                if (!m_validationOnly) {
                    glBindImageTexture(physicalBinding, view->texture, 0, GL_FALSE, 0,
                                       GL_READ_WRITE, view->format.internalFormat);
                }
                break;
            }
            case RhiBindingType::Sampler: {
                const GlSamplerRecord* sampler =
                    recordForHandle(data.samplers, data.samplerRecords, entry.resource.sampler);
                if (sampler == nullptr) {
                    (void) rejectReplayCommand("setBindGroup received an invalid sampler");
                    return;
                }
                if (!m_validationOnly) glBindSampler(physicalBinding, sampler->sampler);
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
                GlResolvedTextureRecord texture;
                if (view == nullptr || !resolveTextureRecord(data, view->desc.texture, texture) ||
                    sampler == nullptr ||
                    (texture.desc.usage & rhiFlag(RhiTextureUsage::Sampled)) == 0u) {
                    (void) rejectReplayCommand("setBindGroup received an invalid combined texture sampler");
                    return;
                }
                if (!m_validationOnly) {
                    glBindTextureUnit(physicalBinding, view->texture);
                    glBindSampler(physicalBinding, sampler->sampler);
                }
                break;
            }
        }
    }

    if (setIndex >= m_bindGroups.size()) {
        m_bindGroups.resize(static_cast<size_t>(setIndex) + 1u);
    }
    m_bindGroups[setIndex] = bindGroup;
}

void GlRhiCommandList::setVertexBuffer(uint32_t slot, RhiBufferHandle buffer, uint64_t offset) {
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::SetVertexBuffer)) return;
        appendValue(slot);
        appendValue(buffer);
        appendValue(offset);
        referenceResource(buffer);
        return;
    }
    if (m_device == nullptr || !m_device->m_data) {
        (void) rejectReplayCommand("setVertexBuffer requires an initialized device");
        return;
    }

    const GlPipelineRecord* pipeline =
        recordForHandle(m_device->m_data->pipelines, m_device->m_data->pipelineRecords, m_graphicsPipeline);
    const GlBufferRecord* bufferRecord =
        recordForHandle(m_device->m_data->buffers, m_device->m_data->bufferRecords, buffer);
    if (pipeline == nullptr || pipeline->compute || bufferRecord == nullptr ||
        (bufferRecord->desc.usage & rhiFlag(RhiBufferUsage::Vertex)) == 0u ||
        offset >= bufferRecord->desc.size) {
        std::cerr << "GlRhiDevice: setVertexBuffer requires a graphics pipeline and a valid vertex buffer range"
                  << " pipeline=[" << (pipeline != nullptr ? rhiDebugName(pipeline->graphicsDesc.debugName) : "<invalid>") << ']'
                  << " pipelineHandle=" << m_graphicsPipeline.index << ':' << m_graphicsPipeline.generation
                  << " buffer=[" << (bufferRecord != nullptr ? rhiDebugName(bufferRecord->desc.debugName) : "<invalid>") << ']'
                  << " bufferHandle=" << buffer.index << ':' << buffer.generation
                  << " slot=" << slot
                  << " offset=" << offset
                  << " size=" << (bufferRecord != nullptr ? bufferRecord->desc.size : 0u)
                  << " usage=" << (bufferRecord != nullptr ? bufferRecord->desc.usage : 0u) << '\n';
        m_replayValid = false;
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
        (void) rejectReplayCommand("setVertexBuffer slot is not declared by the graphics pipeline");
        return;
    }

    if (!m_validationOnly) {
        glVertexArrayVertexBuffer(pipeline->vertexArray,
                                  slot,
                                  bufferRecord->buffer,
                                  static_cast<GLintptr>(offset),
                                  static_cast<GLsizei>(stride));
    }
    if (slot >= m_vertexBuffers.size()) {
        m_vertexBuffers.resize(static_cast<size_t>(slot) + 1u);
    }
    m_vertexBuffers[slot] = {buffer, offset, true};
}

void GlRhiCommandList::setIndexBuffer(RhiBufferHandle buffer, RhiIndexFormat format, uint64_t offset) {
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::SetIndexBuffer)) return;
        appendValue(buffer);
        appendValue(format);
        appendValue(offset);
        referenceResource(buffer);
        return;
    }
    if (m_device == nullptr || !m_device->m_data) {
        (void) rejectReplayCommand("setIndexBuffer requires an initialized device");
        return;
    }

    const GlPipelineRecord* pipeline =
        recordForHandle(m_device->m_data->pipelines, m_device->m_data->pipelineRecords, m_graphicsPipeline);
    const GlBufferRecord* bufferRecord =
        recordForHandle(m_device->m_data->buffers, m_device->m_data->bufferRecords, buffer);
    const uint64_t elementSize = indexElementSize(format);
    if (pipeline == nullptr || pipeline->compute || bufferRecord == nullptr ||
        (bufferRecord->desc.usage & rhiFlag(RhiBufferUsage::Index)) == 0u ||
        elementSize == 0u || offset >= bufferRecord->desc.size || (offset % elementSize) != 0u) {
        (void) rejectReplayCommand("setIndexBuffer requires a graphics pipeline and a valid index buffer range");
        return;
    }

    if (!m_validationOnly) glVertexArrayElementBuffer(pipeline->vertexArray, bufferRecord->buffer);
    m_indexBuffer = {buffer, format, offset, true};
}

void GlRhiCommandList::pushConstants(const void* data, size_t size, RhiShaderStageFlags stages) {
    if (!m_replaying) {
        if (size != 0u && data == nullptr) {
            logRhiError("pushConstants requires non-null payload storage");
            return;
        }
        if (!beginRecordedCommand(CommandType::PushConstants)) return;
        appendValue(static_cast<uint64_t>(size));
        appendValue(stages);
        appendBytes(data, size);
        return;
    }
    if (m_device == nullptr || !m_device->m_data) {
        (void) rejectReplayCommand("pushConstants requires an initialized device");
        return;
    }
    if (size == 0u) {
        return;
    }
    if (data == nullptr) {
        (void) rejectReplayCommand("pushConstants received null data");
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
        (void) rejectReplayCommand("pushConstants requires a bound pipeline");
        return;
    }

    const RhiPipelineLayoutHandle layoutHandle =
        pipeline->compute ? pipeline->computeDesc.layout : pipeline->graphicsDesc.layout;
    const GlPipelineLayoutRecord* layout =
        recordForHandle(deviceData.pipelineLayouts, deviceData.pipelineLayoutRecords, layoutHandle);
    if (layout == nullptr ||
        layout->desc.pushConstantBytes == 0u ||
        size != pipeline->pushConstantSize ||
        (stages & ~layout->desc.pushConstantStages) != 0u ||
        !pipeline->pushConstantBinding.has_value()) {
        (void) rejectReplayCommand("pushConstants exceeds the bound pipeline layout contract");
        return;
    }

    if (!m_validationOnly) {
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
                          *pipeline->pushConstantBinding,
                          deviceData.pushConstantBuffer,
                          0,
                          byteSize);
    }
    m_pushConstantLayout = layoutHandle;
    m_pushConstantSize = static_cast<uint32_t>(size);
    m_pushConstantStages = stages;
}

bool GlRhiCommandList::validateGraphicsDrawState(const bool indexed) const {
    if (!m_rendering || m_device == nullptr || !m_device->m_data) {
        logRhiError("graphics draw requires an active rendering scope");
        return false;
    }

    const auto& data = *m_device->m_data;
    const GlPipelineRecord* pipeline =
        recordForHandle(data.pipelines, data.pipelineRecords, m_graphicsPipeline);
    if (pipeline == nullptr || pipeline->compute) {
        logRhiError("graphics draw requires a bound graphics pipeline");
        return false;
    }
    const bool pipelineUsesDepth =
        pipeline->graphicsDesc.depthStencil.depthTestEnabled ||
        pipeline->graphicsDesc.depthStencil.depthWriteEnabled;
    const bool depthFormatMatches =
        pipeline->graphicsDesc.depthFormat == m_renderingDepthFormat ||
        (!pipelineUsesDepth &&
         pipeline->graphicsDesc.depthFormat == RhiTextureFormat::Undefined);
    if (pipeline->graphicsDesc.colorFormats != m_renderingColorFormats ||
        !depthFormatMatches) {
        std::cerr << "GlRhiDevice: graphics draw pipeline attachment formats do not match the rendering scope"
                  << " pipeline=[" << rhiDebugName(pipeline->graphicsDesc.debugName) << ']'
                  << " pipelineColors=";
        for (const RhiTextureFormat format : pipeline->graphicsDesc.colorFormats) {
            std::cerr << static_cast<uint32_t>(format) << ',';
        }
        std::cerr << " scopeColors=";
        for (const RhiTextureFormat format : m_renderingColorFormats) {
            std::cerr << static_cast<uint32_t>(format) << ',';
        }
        std::cerr << " pipelineDepth=" << static_cast<uint32_t>(pipeline->graphicsDesc.depthFormat)
                  << " scopeDepth=" << static_cast<uint32_t>(m_renderingDepthFormat) << '\n';
        return false;
    }

    const GlPipelineLayoutRecord* layout = recordForHandle(
        data.pipelineLayouts, data.pipelineLayoutRecords, pipeline->graphicsDesc.layout);
    if (layout == nullptr || !sameHandle(m_boundPipelineLayout, pipeline->graphicsDesc.layout)) {
        logRhiError("graphics draw requires the graphics pipeline layout to be active");
        return false;
    }
    for (const GlPipelineRecord::BindingMapping& mapping : pipeline->bindingMappings) {
        if (mapping.set >= m_bindGroups.size()) {
            logRhiError("graphics draw requires every reflected descriptor set to be bound");
            return false;
        }
        const GlBindGroupRecord* group = recordForHandle(
            data.bindGroups, data.bindGroupRecords, m_bindGroups[mapping.set]);
        if (group == nullptr || mapping.set >= layout->desc.bindGroupLayouts.size() ||
            !sameHandle(group->desc.layout, layout->desc.bindGroupLayouts[mapping.set])) {
            logRhiError("graphics draw has a missing or incompatible bind group");
            return false;
        }
    }
    if (!validatePipelineTextureStates(data, *pipeline, m_bindGroups, "graphics draw")) {
        return false;
    }
    if (!validatePipelineBufferStates(data, *pipeline, m_bindGroups, "graphics draw")) {
        return false;
    }
    for (const RhiVertexBinding& required : pipeline->graphicsDesc.vertexInput.bindings) {
        if (required.binding >= m_vertexBuffers.size() ||
            !m_vertexBuffers[required.binding].valid) {
            std::cerr << "GlRhiDevice: graphics draw requires every declared vertex buffer binding"
                      << " pipeline=[" << rhiDebugName(pipeline->graphicsDesc.debugName) << ']'
                      << " pipelineHandle=" << m_graphicsPipeline.index << ':' << m_graphicsPipeline.generation
                      << " missingBinding=" << required.binding
                      << " stride=" << required.stride << '\n';
            return false;
        }
        const VertexBufferBindingState& binding = m_vertexBuffers[required.binding];
        const GlBufferRecord* buffer = recordForHandle(
            data.buffers, data.bufferRecords, binding.buffer);
        if (buffer == nullptr || buffer->state != RhiResourceState::VertexBuffer ||
            (buffer->desc.usage & rhiFlag(RhiBufferUsage::Vertex)) == 0u ||
            binding.offset >= buffer->desc.size) {
            logRhiError("graphics draw requires vertex buffers in VertexBuffer state");
            return false;
        }
    }
    if (indexed) {
        const GlBufferRecord* buffer = m_indexBuffer.valid
            ? recordForHandle(data.buffers, data.bufferRecords, m_indexBuffer.buffer)
            : nullptr;
        if (buffer == nullptr || buffer->state != RhiResourceState::IndexBuffer ||
            (buffer->desc.usage & rhiFlag(RhiBufferUsage::Index)) == 0u ||
            m_indexBuffer.offset >= buffer->desc.size) {
            logRhiError("indexed draw requires an index buffer in IndexBuffer state");
            return false;
        }
    }
    if (pipeline->pushConstantSize != 0u &&
        (!sameHandle(m_pushConstantLayout, pipeline->graphicsDesc.layout) ||
         m_pushConstantSize != pipeline->pushConstantSize || m_pushConstantStages == 0u)) {
        logRhiError("graphics draw requires push constants for the active pipeline");
        return false;
    }
    return true;
}

bool GlRhiCommandList::validateComputeDispatchState() const {
    if (m_device == nullptr || !m_device->m_data || m_rendering) {
        logRhiError("dispatch requires an initialized device outside a rendering scope");
        return false;
    }

    const auto& data = *m_device->m_data;
    const GlPipelineRecord* pipeline =
        recordForHandle(data.pipelines, data.pipelineRecords, m_computePipeline);
    if (pipeline == nullptr || !pipeline->compute) {
        logRhiError("dispatch requires a bound compute pipeline");
        return false;
    }
    const GlPipelineLayoutRecord* layout = recordForHandle(
        data.pipelineLayouts, data.pipelineLayoutRecords, pipeline->computeDesc.layout);
    if (layout == nullptr || !sameHandle(m_boundPipelineLayout, pipeline->computeDesc.layout)) {
        logRhiError("dispatch requires the compute pipeline layout to be active");
        return false;
    }
    for (const GlPipelineRecord::BindingMapping& mapping : pipeline->bindingMappings) {
        if (mapping.set >= m_bindGroups.size()) {
            logRhiError("dispatch requires every reflected descriptor set to be bound");
            return false;
        }
        const GlBindGroupRecord* group = recordForHandle(
            data.bindGroups, data.bindGroupRecords, m_bindGroups[mapping.set]);
        if (group == nullptr || mapping.set >= layout->desc.bindGroupLayouts.size() ||
            !sameHandle(group->desc.layout, layout->desc.bindGroupLayouts[mapping.set])) {
            logRhiError("dispatch has a missing or incompatible bind group");
            return false;
        }
    }
    if (!validatePipelineTextureStates(data, *pipeline, m_bindGroups, "dispatch")) {
        return false;
    }
    if (!validatePipelineBufferStates(data, *pipeline, m_bindGroups, "dispatch")) {
        return false;
    }
    if (pipeline->pushConstantSize != 0u &&
        (!sameHandle(m_pushConstantLayout, pipeline->computeDesc.layout) ||
         m_pushConstantSize != pipeline->pushConstantSize || m_pushConstantStages == 0u)) {
        logRhiError("dispatch requires push constants for the active pipeline");
        return false;
    }
    return true;
}

void GlRhiCommandList::draw(uint32_t vertexCount, uint32_t instanceCount,
                            uint32_t firstVertex, uint32_t firstInstance) {
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::Draw)) return;
        appendValue(vertexCount);
        appendValue(instanceCount);
        appendValue(firstVertex);
        appendValue(firstInstance);
        return;
    }
    if (!validateGraphicsDrawState(false)) {
        m_replayValid = false;
        return;
    }
    const GlPipelineRecord* pipeline = recordForHandle(
        m_device->m_data->pipelines, m_device->m_data->pipelineRecords, m_graphicsPipeline);

    if (!m_validationOnly) {
        glDrawArraysInstancedBaseInstance(toGlTopology(pipeline->graphicsDesc.topology),
                                          static_cast<GLint>(firstVertex),
                                          static_cast<GLsizei>(vertexCount),
                                          static_cast<GLsizei>(instanceCount),
                                          firstInstance);
    }
}

void GlRhiCommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                   uint32_t firstIndex, int32_t vertexOffset,
                                   uint32_t firstInstance) {
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::DrawIndexed)) return;
        appendValue(indexCount);
        appendValue(instanceCount);
        appendValue(firstIndex);
        appendValue(vertexOffset);
        appendValue(firstInstance);
        return;
    }
    if (!validateGraphicsDrawState(true)) {
        m_replayValid = false;
        return;
    }
    const GlPipelineRecord* pipeline = recordForHandle(
        m_device->m_data->pipelines, m_device->m_data->pipelineRecords, m_graphicsPipeline);
    const GlBufferRecord* indexBuffer = recordForHandle(
        m_device->m_data->buffers, m_device->m_data->bufferRecords, m_indexBuffer.buffer);
    const uint64_t elementSize = indexElementSize(m_indexBuffer.format);
    const uint64_t firstIndexBytes = static_cast<uint64_t>(firstIndex) * elementSize;
    const uint64_t indexBytes = static_cast<uint64_t>(indexCount) * elementSize;
    if (firstIndexBytes > indexBuffer->desc.size - m_indexBuffer.offset ||
        indexBytes > indexBuffer->desc.size - m_indexBuffer.offset - firstIndexBytes) {
        (void) rejectReplayCommand("drawIndexed index range exceeds the bound index buffer");
        return;
    }
    const uint64_t byteOffset = m_indexBuffer.offset + firstIndexBytes;
    if (!m_validationOnly) {
        glDrawElementsInstancedBaseVertexBaseInstance(toGlTopology(pipeline->graphicsDesc.topology),
                                                     static_cast<GLsizei>(indexCount),
                                                     m_indexBuffer.format == RhiIndexFormat::Uint16
                                                         ? GL_UNSIGNED_SHORT
                                                         : GL_UNSIGNED_INT,
                                                     reinterpret_cast<const void*>(byteOffset),
                                                     static_cast<GLsizei>(instanceCount),
                                                     vertexOffset,
                                                     firstInstance);
    }
}

void GlRhiCommandList::drawIndirect(RhiBufferHandle indirectBuffer, uint64_t offset,
                                    uint32_t drawCount, uint32_t stride) {
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::DrawIndirect)) return;
        appendValue(indirectBuffer);
        appendValue(offset);
        appendValue(drawCount);
        appendValue(stride);
        referenceResource(indirectBuffer);
        return;
    }
    if (!validateGraphicsDrawState(false)) {
        m_replayValid = false;
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
        requiredBytes > buffer->desc.size - offset ||
        buffer->state != RhiResourceState::IndirectArgument) {
        (void) rejectReplayCommand("drawIndirect requires a graphics pipeline and a valid indirect buffer range");
        return;
    }

    if (!m_validationOnly) {
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, buffer->buffer);
        glMultiDrawArraysIndirect(toGlTopology(pipeline->graphicsDesc.topology),
                                  reinterpret_cast<const void*>(offset),
                                  static_cast<GLsizei>(drawCount),
                                  static_cast<GLsizei>(stride));
    }
}

void GlRhiCommandList::dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::Dispatch)) return;
        appendValue(groupCountX);
        appendValue(groupCountY);
        appendValue(groupCountZ);
        return;
    }
    if (!validateComputeDispatchState()) {
        m_replayValid = false;
        return;
    }

    if (!m_validationOnly) glDispatchCompute(groupCountX, groupCountY, groupCountZ);
}

void GlRhiCommandList::updateBuffer(const RhiBufferHandle buffer,
                                    const uint64_t offset,
                                    const void* data,
                                    const size_t size) {
    if (!m_replaying) {
        if (size != 0u && data == nullptr) {
            logRhiError("updateBuffer requires non-null payload storage");
            return;
        }
        if (!beginRecordedCommand(CommandType::UpdateBuffer)) return;
        appendValue(buffer);
        appendValue(offset);
        appendValue(static_cast<uint64_t>(size));
        appendBytes(data, size);
        referenceResource(buffer);
        return;
    }
    if (m_device == nullptr || !m_device->m_data) {
        (void) rejectReplayCommand("updateBuffer requires an initialized device");
        return;
    }
    if (m_rendering) {
        (void) rejectReplayCommand("updateBuffer cannot be recorded inside a rendering scope");
        return;
    }

    const GlBufferRecord* record =
        recordForHandle(m_device->m_data->buffers, m_device->m_data->bufferRecords, buffer);
    if (record == nullptr || data == nullptr || size == 0u ||
        (offset & 3u) != 0u || (size & 3u) != 0u || offset > record->desc.size ||
        size > record->desc.size - offset ||
        (record->desc.usage & rhiFlag(RhiBufferUsage::TransferDst)) == 0u ||
        record->state != RhiResourceState::TransferDst) {
        (void) rejectReplayCommand("updateBuffer received an invalid buffer, range, or transfer contract");
        return;
    }

    if (!m_validationOnly) {
        glNamedBufferSubData(record->buffer,
                             static_cast<GLintptr>(offset),
                             static_cast<GLsizeiptr>(size),
                             data);
    }
}

void GlRhiCommandList::copyBuffer(const RhiBufferCopy& copy) {
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::CopyBuffer)) return;
        appendValue(copy);
        referenceResource(copy.src);
        referenceResource(copy.dst);
        return;
    }
    if (m_device == nullptr || !m_device->m_data) {
        (void) rejectReplayCommand("copyBuffer requires an initialized device");
        return;
    }

    const GlBufferRecord* src = recordForHandle(m_device->m_data->buffers, m_device->m_data->bufferRecords, copy.src);
    const GlBufferRecord* dst = recordForHandle(m_device->m_data->buffers, m_device->m_data->bufferRecords, copy.dst);
    if (src == nullptr || dst == nullptr || copy.size == 0u ||
        (src->desc.usage & rhiFlag(RhiBufferUsage::TransferSrc)) == 0u ||
        (dst->desc.usage & rhiFlag(RhiBufferUsage::TransferDst)) == 0u ||
        copy.srcOffset > src->desc.size || copy.size > src->desc.size - copy.srcOffset ||
        copy.dstOffset > dst->desc.size || copy.size > dst->desc.size - copy.dstOffset ||
        src->state != RhiResourceState::TransferSrc ||
        dst->state != RhiResourceState::TransferDst) {
        (void) rejectReplayCommand("copyBuffer received invalid buffers or ranges");
        return;
    }

    if (!m_validationOnly) {
        glCopyNamedBufferSubData(src->buffer,
                                 dst->buffer,
                                 static_cast<GLintptr>(copy.srcOffset),
                                 static_cast<GLintptr>(copy.dstOffset),
                                 static_cast<GLsizeiptr>(copy.size));
    }
}

void GlRhiCommandList::copyBufferToTexture(const RhiBufferTextureCopy& copy) {
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::CopyBufferToTexture)) return;
        appendValue(copy);
        referenceResource(copy.srcBuffer);
        referenceResource(copy.dstTexture);
        return;
    }
    if (m_device == nullptr || !m_device->m_data) {
        (void) rejectReplayCommand("copyBufferToTexture requires an initialized device");
        return;
    }

    const GlBufferRecord* src =
        recordForHandle(m_device->m_data->buffers, m_device->m_data->bufferRecords, copy.srcBuffer);
    GlTextureRecord* dst =
        recordForHandle(m_device->m_data->textures, m_device->m_data->textureRecords, copy.dstTexture);
    if (m_rendering || src == nullptr || dst == nullptr ||
        (src->desc.usage & rhiFlag(RhiBufferUsage::TransferSrc)) == 0u ||
        (dst->desc.usage & rhiFlag(RhiTextureUsage::TransferDst)) == 0u ||
        copy.mipLevel >= dst->desc.mipLevels || copy.width == 0u || copy.height == 0u ||
        copy.depth == 0u || src->state != RhiResourceState::TransferSrc) {
        (void) rejectReplayCommand("copyBufferToTexture received an invalid resource or transfer contract");
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
        (void) rejectReplayCommand("copyBufferToTexture received an out-of-range copy region");
        return;
    }
    const GlTextureSubresourceRange destinationRange{
        copy.mipLevel,
        1u,
        dst->desc.dimension == RhiTextureDimension::Texture2D ? 0u : targetZ,
        dst->desc.dimension == RhiTextureDimension::Texture2D ? 1u : copy.depth
    };
    if (!textureRangeHasState(*dst, destinationRange, RhiResourceState::TransferDst)) {
        (void) rejectReplayCommand("copyBufferToTexture requires destination subresources in TransferDst state");
        return;
    }
    if (m_validationOnly) return;

    GLint previousUnpackAlignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
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
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
}

void GlRhiCommandList::copyTextureToBuffer(const RhiTextureBufferCopy& copy) {
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::CopyTextureToBuffer)) return;
        appendValue(copy);
        referenceResource(copy.srcTexture);
        referenceResource(copy.dstBuffer);
        return;
    }
    if (m_device == nullptr || !m_device->m_data) {
        (void) rejectReplayCommand("copyTextureToBuffer requires an initialized device");
        return;
    }

    GlResolvedTextureRecord src;
    const bool textureResolved = resolveTextureRecord(
        *m_device->m_data, copy.srcTexture, src);
    const GlBufferRecord* dst = recordForHandle(
        m_device->m_data->buffers, m_device->m_data->bufferRecords, copy.dstBuffer);
    if (m_rendering || !textureResolved || dst == nullptr ||
        (src.desc.usage & rhiFlag(RhiTextureUsage::TransferSrc)) == 0u ||
        (dst->desc.usage & rhiFlag(RhiBufferUsage::TransferDst)) == 0u ||
        copy.mipLevel >= src.desc.mipLevels || copy.width == 0u ||
        copy.height == 0u || copy.depth == 0u ||
        dst->state != RhiResourceState::TransferDst) {
        (void) rejectReplayCommand("copyTextureToBuffer received an invalid resource or transfer contract");
        return;
    }

    const uint32_t bytesPerTexel = textureFormatSizeBytes(src.desc.format);
    if (bytesPerTexel == 0u) {
        (void) rejectReplayCommand("copyTextureToBuffer does not support the source texture format");
        return;
    }

    const uint64_t tightRowBytes = static_cast<uint64_t>(copy.width) * bytesPerTexel;
    const uint64_t bytesPerRow = copy.bytesPerRow == 0u ? tightRowBytes : copy.bytesPerRow;
    const uint64_t rowsPerImage = copy.rowsPerImage == 0u ? copy.height : copy.rowsPerImage;
    const uint32_t mipWidth = std::max(1u, src.desc.width >> copy.mipLevel);
    const uint32_t mipHeight = std::max(1u, src.desc.height >> copy.mipLevel);
    const uint32_t mipDepth = src.desc.dimension == RhiTextureDimension::Texture3D
        ? std::max(1u, src.desc.depthOrLayers >> copy.mipLevel)
        : src.desc.depthOrLayers;
    const uint64_t sourceZ = src.desc.dimension == RhiTextureDimension::Texture2D
        ? copy.srcZ
        : static_cast<uint64_t>(copy.arrayLayer) + copy.srcZ;
    const bool invalid2D = src.desc.dimension == RhiTextureDimension::Texture2D &&
                           (copy.arrayLayer != 0u || sourceZ != 0u || copy.depth != 1u);
    if (bytesPerRow < tightRowBytes || bytesPerRow % bytesPerTexel != 0u ||
        bytesPerRow / bytesPerTexel > static_cast<uint64_t>(std::numeric_limits<GLint>::max()) ||
        rowsPerImage > static_cast<uint64_t>(std::numeric_limits<GLint>::max()) ||
        rowsPerImage < copy.height || invalid2D ||
        copy.srcX > mipWidth || copy.width > mipWidth - copy.srcX ||
        copy.srcY > mipHeight || copy.height > mipHeight - copy.srcY ||
        sourceZ > mipDepth || copy.depth > mipDepth - sourceZ ||
        sourceZ > static_cast<uint64_t>(std::numeric_limits<GLint>::max())) {
        (void) rejectReplayCommand("copyTextureToBuffer received an out-of-range copy region");
        return;
    }

    constexpr uint64_t kMaxSize = std::numeric_limits<uint64_t>::max();
    if (rowsPerImage > kMaxSize / bytesPerRow) {
        (void) rejectReplayCommand("copyTextureToBuffer size calculation overflowed");
        return;
    }
    const uint64_t imageStride = bytesPerRow * rowsPerImage;
    if (copy.depth - 1u > kMaxSize / imageStride ||
        copy.height - 1u > kMaxSize / bytesPerRow) {
        (void) rejectReplayCommand("copyTextureToBuffer size calculation overflowed");
        return;
    }
    const uint64_t precedingImages = imageStride * (copy.depth - 1u);
    const uint64_t precedingRows = bytesPerRow * (copy.height - 1u);
    if (precedingRows > kMaxSize - tightRowBytes ||
        precedingImages > kMaxSize - precedingRows - tightRowBytes) {
        (void) rejectReplayCommand("copyTextureToBuffer size calculation overflowed");
        return;
    }
    const uint64_t requiredSize = precedingImages + precedingRows + tightRowBytes;
    if (copy.bufferOffset > dst->desc.size || requiredSize > dst->desc.size - copy.bufferOffset ||
        requiredSize > static_cast<uint64_t>(std::numeric_limits<GLsizei>::max())) {
        (void) rejectReplayCommand("copyTextureToBuffer destination range is too small");
        return;
    }
    const GlTextureSubresourceRange sourceRange{
        copy.mipLevel,
        1u,
        src.desc.dimension == RhiTextureDimension::Texture2D
            ? 0u
            : static_cast<uint32_t>(sourceZ),
        src.desc.dimension == RhiTextureDimension::Texture2D ? 1u : copy.depth
    };
    if (!textureHandleRangeHasState(*m_device->m_data,
                                    copy.srcTexture,
                                    sourceRange,
                                    RhiResourceState::TransferSrc)) {
        (void) rejectReplayCommand("copyTextureToBuffer requires source subresources in TransferSrc state");
        return;
    }
    if (m_validationOnly) return;

    GLint previousPackBuffer = 0;
    GLint previousPackAlignment = 4;
    GLint previousPackRowLength = 0;
    GLint previousPackImageHeight = 0;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previousPackBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &previousPackRowLength);
    glGetIntegerv(GL_PACK_IMAGE_HEIGHT, &previousPackImageHeight);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, static_cast<GLint>(bytesPerRow / bytesPerTexel));
    glPixelStorei(GL_PACK_IMAGE_HEIGHT, static_cast<GLint>(rowsPerImage));
    glBindBuffer(GL_PIXEL_PACK_BUFFER, dst->buffer);

    void* bufferOffset = reinterpret_cast<void*>(copy.bufferOffset);
    if (src.swapchainBackbuffer) {
        glReadPixels(static_cast<GLint>(copy.srcX),
                     static_cast<GLint>(copy.srcY),
                     static_cast<GLsizei>(copy.width),
                     static_cast<GLsizei>(copy.height),
                     src.format.externalFormat,
                     src.format.type,
                     bufferOffset);
    } else {
        glGetTextureSubImage(src.texture,
                             static_cast<GLint>(copy.mipLevel),
                             static_cast<GLint>(copy.srcX),
                             static_cast<GLint>(copy.srcY),
                             static_cast<GLint>(sourceZ),
                             static_cast<GLsizei>(copy.width),
                             static_cast<GLsizei>(copy.height),
                             static_cast<GLsizei>(copy.depth),
                             src.format.externalFormat,
                             src.format.type,
                             static_cast<GLsizei>(requiredSize),
                             bufferOffset);
    }

    glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(previousPackBuffer));
    glPixelStorei(GL_PACK_IMAGE_HEIGHT, previousPackImageHeight);
    glPixelStorei(GL_PACK_ROW_LENGTH, previousPackRowLength);
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
}

void GlRhiCommandList::copyTexture(const RhiTextureCopy& copy) {
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::CopyTexture)) return;
        appendValue(copy);
        referenceResource(copy.src);
        referenceResource(copy.dst);
        return;
    }
    if (m_device == nullptr || !m_device->m_data) {
        (void) rejectReplayCommand("copyTexture requires an initialized device");
        return;
    }

    GlResolvedTextureRecord src;
    GlResolvedTextureRecord dst;
    if (!resolveTextureRecord(*m_device->m_data, copy.src, src) ||
        !resolveTextureRecord(*m_device->m_data, copy.dst, dst)) {
        (void) rejectReplayCommand("copyTexture received invalid texture handles");
        return;
    }
    if (m_rendering ||
        (src.desc.usage & rhiFlag(RhiTextureUsage::TransferSrc)) == 0u ||
        (dst.desc.usage & rhiFlag(RhiTextureUsage::TransferDst)) == 0u) {
        (void) rejectReplayCommand("copyTexture received an invalid transfer contract");
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
        (void) rejectReplayCommand("copyTexture received an invalid copy region");
        return;
    }
    const auto stateRange = [](const GlResolvedTextureRecord& texture,
                               const RhiTextureSubresourceLayers& subresource,
                               const RhiOffset3D& offset,
                               const RhiExtent3D& extent) {
        return GlTextureSubresourceRange{
            subresource.mipLevel,
            1u,
            texture.desc.dimension == RhiTextureDimension::Texture3D
                ? offset.z
                : subresource.baseArrayLayer,
            texture.desc.dimension == RhiTextureDimension::Texture3D
                ? extent.depth
                : subresource.layerCount
        };
    };
    const GlTextureSubresourceRange sourceRange =
        stateRange(src, copy.srcSubresource, copy.srcOffset, copy.extent);
    const GlTextureSubresourceRange destinationRange =
        stateRange(dst, copy.dstSubresource, copy.dstOffset, copy.extent);
    if (!textureHandleRangeHasState(*m_device->m_data,
                                    copy.src,
                                    sourceRange,
                                    RhiResourceState::TransferSrc) ||
        !textureHandleRangeHasState(*m_device->m_data,
                                    copy.dst,
                                    destinationRange,
                                    RhiResourceState::TransferDst)) {
        (void) rejectReplayCommand("copyTexture requires TransferSrc and TransferDst subresource states");
        return;
    }
    if (m_validationOnly) return;

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
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::BlitTexture)) return;
        appendValue(blit);
        if (blit.src.isValid()) referenceResource(blit.src);
        if (blit.dst.isValid()) referenceResource(blit.dst);
        if (blit.srcView.isValid()) referenceResource(blit.srcView);
        if (blit.dstView.isValid()) referenceResource(blit.dstView);
        return;
    }
    if (m_device == nullptr || !m_device->m_data) {
        (void) rejectReplayCommand("blitTexture requires an initialized device");
        return;
    }

    GlRhiDeviceData& data = *m_device->m_data;
    GlBlitEndpoint src;
    GlBlitEndpoint dst;
    if (!resolveBlitEndpoint(data, blit.src, blit.srcView, blit.srcMipLevel, src) ||
        !resolveBlitEndpoint(data, blit.dst, blit.dstView, blit.dstMipLevel, dst) ||
        !src.valid || !dst.valid ||
        src.desc.dimension != RhiTextureDimension::Texture2D ||
        dst.desc.dimension != RhiTextureDimension::Texture2D ||
        src.format.depth != dst.format.depth ||
        src.format.stencil != dst.format.stencil) {
        (void) rejectReplayCommand("blitTexture requires valid 2D source and destination endpoints");
        return;
    }
    if (m_rendering ||
        (src.desc.usage & rhiFlag(RhiTextureUsage::TransferSrc)) == 0u ||
        (dst.desc.usage & rhiFlag(RhiTextureUsage::TransferDst)) == 0u) {
        (void) rejectReplayCommand("blitTexture received an invalid transfer contract");
        return;
    }
    const GlTextureSubresourceRange sourceRange{
        src.stateMip, 1u, src.stateBaseLayer, src.stateLayerCount};
    const GlTextureSubresourceRange destinationRange{
        dst.stateMip, 1u, dst.stateBaseLayer, dst.stateLayerCount};
    if (!textureHandleRangeHasState(data,
                                    src.stateTexture,
                                    sourceRange,
                                    RhiResourceState::TransferSrc) ||
        !textureHandleRangeHasState(data,
                                    dst.stateTexture,
                                    destinationRange,
                                    RhiResourceState::TransferDst)) {
        (void) rejectReplayCommand("blitTexture requires TransferSrc and TransferDst subresource states");
        return;
    }
    if (m_validationOnly) return;

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
            data.pendingRetirements.framebuffers.push_back(readFramebuffer);
            readFramebuffer = 0u;
        }
        if (drawFramebuffer != 0u) {
            data.pendingRetirements.framebuffers.push_back(drawFramebuffer);
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
        m_replayValid = false;
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
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::GenerateMipmaps)) return;
        appendValue(texture);
        referenceResource(texture);
        return;
    }
    if (m_device == nullptr) {
        (void) rejectReplayCommand("generateMipmaps requires an attached device");
        return;
    }
    const GlTextureRecord* record = recordForHandle(
        m_device->m_data->textures, m_device->m_data->textureRecords, texture);
    if (record == nullptr || record->desc.mipLevels <= 1u || record->format.depth ||
        (record->desc.usage & rhiFlag(RhiTextureUsage::TransferDst)) == 0u) {
        (void) rejectReplayCommand("generateMipmaps requires a valid color texture with multiple mip levels");
        return;
    }
    const GlTextureSubresourceRange fullRange{
        0u, record->desc.mipLevels, 0u, record->desc.depthOrLayers};
    if (!textureRangeHasState(*record, fullRange, RhiResourceState::TransferDst)) {
        (void) rejectReplayCommand("generateMipmaps requires every texture subresource in TransferDst state");
        return;
    }
    if (!m_validationOnly) glGenerateTextureMipmap(record->texture);
}

void GlRhiCommandList::resetQueryPool(const RhiQueryPoolHandle pool,
                                      const uint32_t firstQuery,
                                      const uint32_t queryCount) {
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::ResetQueryPool)) return;
        appendValue(pool);
        appendValue(firstQuery);
        appendValue(queryCount);
        referenceResource(pool);
        return;
    }
    if (m_device == nullptr) {
        (void) rejectReplayCommand("resetQueryPool requires an attached device");
        return;
    }
    GlQueryPoolRecord* record = recordForHandle(
        m_device->m_data->queryPools, m_device->m_data->queryPoolRecords, pool);
    if (record == nullptr || queryCount == 0u || firstQuery > record->queries.size() ||
        queryCount > record->queries.size() - firstQuery) {
        (void) rejectReplayCommand("resetQueryPool received an invalid query pool range");
        return;
    }

    if (!m_validationOnly) {
        std::fill(record->issued.begin() + firstQuery,
                  record->issued.begin() + firstQuery + queryCount,
                  false);
    }
}

void GlRhiCommandList::writeTimestamp(RhiQueryPoolHandle pool, uint32_t queryIndex) {
    if (!m_replaying) {
        if (!beginRecordedCommand(CommandType::WriteTimestamp)) return;
        appendValue(pool);
        appendValue(queryIndex);
        referenceResource(pool);
        return;
    }
    if (m_device == nullptr) {
        (void) rejectReplayCommand("writeTimestamp requires an attached device");
        return;
    }
    GlQueryPoolRecord* record = recordForHandle(
        m_device->m_data->queryPools, m_device->m_data->queryPoolRecords, pool);
    if (record == nullptr || record->type != RhiQueryType::Timestamp ||
        queryIndex >= record->queries.size()) {
        (void) rejectReplayCommand("writeTimestamp received an invalid query pool range");
        return;
    }
    if (!m_validationOnly) {
        glQueryCounter(record->queries[queryIndex], GL_TIMESTAMP);
        record->issued[queryIndex] = true;
    }
}

GlRhiCommandListPool::GlRhiCommandListPool(GlRhiDevice& device,
                                           const RhiCommandListPoolDesc& desc)
    : m_device(&device),
      m_registry(device.m_commandPoolRegistry),
      m_ownerThread(std::this_thread::get_id()),
      m_initialArenaCapacity(desc.initialArenaCapacity) {
    m_commandLists.reserve(desc.initialCommandListCapacity);
    std::lock_guard<std::mutex> poolRegistryLock(m_registry->mutex);
    std::lock_guard<std::mutex> registryLock(device.m_data->commandListRegistryMutex);
    m_registry->pools.emplace(this);
    for (uint32_t index = 0u; index < desc.initialCommandListCapacity; ++index) {
        auto commandList = std::make_shared<GlRhiCommandList>();
        commandList->attachDevice(&device);
        commandList->attachPool(this);
        commandList->m_commandStream.reserve(m_initialArenaCapacity);
        device.m_data->commandLists.emplace(commandList.get(), commandList);
        m_commandLists.push_back(std::move(commandList));
    }
}

GlRhiCommandListPool::~GlRhiCommandListPool() {
    if (!isOwnerThread()) {
        logRhiError("command-list pool destruction requires its owner thread");
    }
    std::lock_guard<std::mutex> poolRegistryLock(m_registry->mutex);
    if (m_device != nullptr && m_registry->device == m_device && m_device->m_data) {
        std::lock_guard<std::mutex> registryLock(m_device->m_data->commandListRegistryMutex);
        for (const std::shared_ptr<GlRhiCommandList>& commandList : m_commandLists) {
            m_device->m_data->commandLists.erase(commandList.get());
        }
    }
    m_registry->pools.erase(this);
    for (const std::shared_ptr<GlRhiCommandList>& commandList : m_commandLists) {
        commandList->attachDevice(nullptr);
        commandList->attachPool(nullptr);
    }
    m_device = nullptr;
}

bool GlRhiCommandListPool::isOwnerThread() const {
    return std::this_thread::get_id() == m_ownerThread;
}

RhiCommandList* GlRhiCommandListPool::acquire(const RhiCommandListType type) {
    std::lock_guard<std::mutex> poolRegistryLock(m_registry->mutex);
    if (!isOwnerThread() || m_device == nullptr || !m_device->m_initialized) {
        logRhiError("command-list pool acquire requires its owner thread and an initialized device");
        return nullptr;
    }
    if (type != RhiCommandListType::Graphics &&
        type != RhiCommandListType::Compute &&
        type != RhiCommandListType::Transfer) {
        logRhiError("command-list pool acquire received an invalid command-list type");
        return nullptr;
    }
    if (std::this_thread::get_id() == m_device->m_deviceThread) {
        m_device->reclaimCompletedCommandLists();
    }
    for (const std::shared_ptr<GlRhiCommandList>& commandList : m_commandLists) {
        if (!commandList->m_acquired && commandList->m_state == RhiCommandListState::Initial) {
            commandList->m_acquired = true;
            commandList->m_acquiredType = type;
            return commandList.get();
        }
    }
    auto commandList = std::make_shared<GlRhiCommandList>();
    commandList->attachDevice(m_device);
    commandList->attachPool(this);
    commandList->m_commandStream.reserve(m_initialArenaCapacity);
    commandList->m_acquired = true;
    commandList->m_acquiredType = type;
    GlRhiCommandList* result = commandList.get();
    {
        std::lock_guard<std::mutex> registryLock(m_device->m_data->commandListRegistryMutex);
        m_device->m_data->commandLists.emplace(result, commandList);
    }
    m_commandLists.push_back(std::move(commandList));
    return result;
}

bool GlRhiCommandListPool::reset() {
    if (!isOwnerThread()) {
        logRhiError("command-list pool reset requires its owner thread");
        return false;
    }
    for (const std::shared_ptr<GlRhiCommandList>& commandList : m_commandLists) {
        if (commandList->m_state == RhiCommandListState::Pending ||
            commandList->m_state == RhiCommandListState::Recording) {
            logRhiError("command-list pool reset rejects recording or pending command lists");
            return false;
        }
    }
    for (const std::shared_ptr<GlRhiCommandList>& commandList : m_commandLists) {
        commandList->resetForPoolReuse();
    }
    return true;
}

void GlRhiCommandListPool::detachDevice() {
    for (const std::shared_ptr<GlRhiCommandList>& commandList : m_commandLists) {
        commandList->attachDevice(nullptr);
    }
    m_device = nullptr;
}

GlRhiDevice::GlRhiDevice()
    : m_data(std::make_unique<GlRhiDeviceData>()),
      m_commandPoolRegistry(std::make_shared<GlRhiCommandPoolRegistry>()) {
    m_commandPoolRegistry->device = this;
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
    if (desc.nativeWindow == nullptr) {
        logRhiError("init requires a native window for swapchain presentation");
        return false;
    }
    {
        std::lock_guard<std::mutex> poolRegistryLock(m_commandPoolRegistry->mutex);
        m_commandPoolRegistry->device = this;
    }
    GLint maxUniformBufferBindings = 0;
    GLint maxStorageBufferBindings = 0;
    GLint maxTextureUnits = 0;
    GLint maxImageUnits = 0;
    GLint uniformBufferOffsetAlignment = 0;
    GLint storageBufferOffsetAlignment = 0;
    glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &maxUniformBufferBindings);
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &maxStorageBufferBindings);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxTextureUnits);
    glGetIntegerv(GL_MAX_IMAGE_UNITS, &maxImageUnits);
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uniformBufferOffsetAlignment);
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &storageBufferOffsetAlignment);
    if (maxUniformBufferBindings <= 0 || maxStorageBufferBindings <= 0 ||
        maxTextureUnits <= 0 || maxImageUnits <= 0 ||
        uniformBufferOffsetAlignment <= 0 || storageBufferOffsetAlignment <= 0) {
        logRhiError("init requires valid OpenGL descriptor binding limits");
        return false;
    }
    m_data->maxUniformBufferBindings = static_cast<uint32_t>(maxUniformBufferBindings);
    m_data->maxStorageBufferBindings = static_cast<uint32_t>(maxStorageBufferBindings);
    m_data->maxTextureUnits = static_cast<uint32_t>(maxTextureUnits);
    m_data->maxImageUnits = static_cast<uint32_t>(maxImageUnits);
    m_data->uniformBufferOffsetAlignment = static_cast<uint32_t>(uniformBufferOffsetAlignment);
    m_data->storageBufferOffsetAlignment = static_cast<uint32_t>(storageBufferOffsetAlignment);

    m_initialized = true;
    m_deviceThread = std::this_thread::get_id();
    m_deviceId = g_nextRhiDeviceId.fetch_add(1u, std::memory_order_relaxed);
    m_lastSubmittedSequence = 0u;
    m_data->completedSubmissionSequence = 0u;
    m_capabilities.multiDrawIndirect = true;
    m_capabilities.timestampQuery = true;
    m_capabilities.textureView = true;
    m_capabilities.samplerAnisotropy = true;
    m_capabilities.storageImage = true;
    m_capabilities.textureBufferCopyRowPitchAlignment = 1u;
    m_capabilities.maxColorAttachments = 8;
    m_capabilities.maxSampledTexturesPerStage = static_cast<uint32_t>(maxTextureUnits);
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &m_capabilities.maxSamplerAnisotropy);

    m_data->swapchainWidth = static_cast<uint32_t>(desc.width);
    m_data->swapchainHeight = static_cast<uint32_t>(desc.height);
    m_data->nativeWindow = static_cast<GLFWwindow*>(desc.nativeWindow);
    m_data->swapchainFormat = RhiTextureFormat::Rgba8Unorm;
    m_data->swapchainDepthStencilFormat = RhiTextureFormat::Depth24;

    RhiTextureDesc swapchainTextureDesc;
    swapchainTextureDesc.debugName = "Swapchain.Color";
    swapchainTextureDesc.dimension = RhiTextureDimension::Texture2D;
    swapchainTextureDesc.format = m_data->swapchainFormat;
    swapchainTextureDesc.width = m_data->swapchainWidth;
    swapchainTextureDesc.height = m_data->swapchainHeight;
    swapchainTextureDesc.depthOrLayers = 1u;
    swapchainTextureDesc.mipLevels = 1u;
    swapchainTextureDesc.sampleCount = 1u;
    swapchainTextureDesc.usage = rhiFlag(RhiTextureUsage::Present) |
                                 rhiFlag(RhiTextureUsage::ColorAttachment) |
                                 rhiFlag(RhiTextureUsage::TransferSrc) |
                                 rhiFlag(RhiTextureUsage::TransferDst);

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

    m_data->swapchainColorTexture = m_data->textures.allocate();
    const uint32_t textureSlot = m_data->swapchainColorTexture.index - 1u;
    if (textureSlot >= m_data->textureRecords.size()) {
        m_data->textureRecords.resize(textureSlot + 1u);
    }
    m_data->textureRecords[textureSlot] = {
        0u,
        GL_TEXTURE_2D,
        swapchainTextureDesc,
        rhiDebugName(swapchainTextureDesc.debugName),
        swapchainFormat,
        std::vector<RhiResourceState>(1u, RhiResourceState::Present),
        true,
        true
    };

    m_data->swapchainColorView = m_data->textureViews.allocate();
    swapchainViewDesc.texture = m_data->swapchainColorTexture;
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
    std::unique_lock<std::mutex> poolRegistryLock(m_commandPoolRegistry->mutex);
    if (!m_data) {
        for (GlRhiCommandListPool* pool : m_commandPoolRegistry->pools) {
            pool->detachDevice();
        }
        m_commandPoolRegistry->pools.clear();
        m_commandPoolRegistry->device = nullptr;
        m_initialized = false;
        return;
    }

    if (m_initialized) {
        glFinish();
    }
    reclaimAllRetiredResources(*m_data);
    for (const std::shared_ptr<GlRhiCommandList>& commandList :
         m_data->completedCommandLists) {
        if (commandList != nullptr && commandList->m_state == RhiCommandListState::Pending) {
            commandList->resetForPoolReuse();
        }
    }
    m_data->completedCommandLists.clear();

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
    for (GlShaderRecord& record : m_data->shaderRecords) record = {};
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
            if (record.mapped) {
                glUnmapNamedBuffer(record.buffer);
            }
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
    m_data->swapchainColorTexture = {};
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
    m_data->completedCommandLists.clear();
    m_data->completedSubmissionSequence = 0u;
    for (GlRhiCommandListPool* pool : m_commandPoolRegistry->pools) {
        pool->detachDevice();
    }
    m_commandPoolRegistry->pools.clear();
    {
        std::lock_guard<std::mutex> registryLock(m_data->commandListRegistryMutex);
        m_data->commandLists.clear();
    }
    m_commandPoolRegistry->device = nullptr;
    m_deviceId = 0u;
    m_lastSubmittedSequence = 0u;
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
        initialDataSize > desc.size ||
        !bufferUsageSupportsState(desc.usage, desc.initialState) ||
        (initialData != nullptr &&
         ((desc.usage & rhiFlag(RhiBufferUsage::TransferDst)) == 0u ||
          desc.initialState == RhiResourceState::Undefined || initialDataSize == 0u))) {
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
    m_data->bufferRecords[slot] = {buffer, desc, desc.initialState, false, true};
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
            initialData->layerCount == 0u ||
            (desc.usage & rhiFlag(RhiTextureUsage::TransferDst)) == 0u ||
            !textureUsageSupportsState(desc.usage, initialData->finalState)) {
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
    const size_t subresourceCount = static_cast<size_t>(desc.mipLevels) * desc.depthOrLayers;
    const RhiResourceState initialState = initialData != nullptr
        ? initialData->finalState
        : RhiResourceState::Undefined;
    m_data->textureRecords[slot] = {
        texture,
        target,
        desc,
        rhiDebugName(desc.debugName),
        format,
        std::vector<RhiResourceState>(subresourceCount, initialState),
        false,
        true
    };
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
    const bool cubeRangeValid = desc.viewType != RhiTextureViewType::Cube ||
                                (resolvedDesc.baseLayer % 6u == 0u &&
                                 resolvedDesc.layerCount == 6u);
    if (!m_initialized || !textureResolved || textureRecord.swapchainBackbuffer || viewTarget == 0u ||
        !toGlFormatInfo(resolvedFormat, format) || resolvedDesc.mipCount == 0u ||
        resolvedDesc.layerCount == 0u || resolvedDesc.baseMip >= textureRecord.desc.mipLevels ||
        resolvedDesc.mipCount > textureRecord.desc.mipLevels - resolvedDesc.baseMip ||
        resolvedDesc.baseLayer >= textureRecord.desc.depthOrLayers ||
        resolvedDesc.layerCount > textureRecord.desc.depthOrLayers - resolvedDesc.baseLayer ||
        !cubeRangeValid) {
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
    std::array<GLfloat, 4> borderColor{};
    const GLenum minFilter = toGlMinFilter(desc.minFilter, desc.mipmapMode);
    const GLenum magFilter = toGlMagFilter(desc.magFilter);
    const GLenum addressU = toGlAddressMode(desc.addressU);
    const GLenum addressV = toGlAddressMode(desc.addressV);
    const GLenum addressW = toGlAddressMode(desc.addressW);
    const GLenum compareOp = toGlCompareOp(desc.compareOp);
    if (!m_initialized || desc.maxAnisotropy < 1.0f || minFilter == 0u || magFilter == 0u ||
        addressU == 0u || addressV == 0u || addressW == 0u ||
        !toGlBorderColor(desc.borderColor, borderColor) || compareOp == 0u) {
        logRhiError("createSampler received an invalid descriptor");
        return {};
    }

    GLuint sampler = 0u;
    glCreateSamplers(1, &sampler);
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, minFilter);
    glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, magFilter);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, addressU);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, addressV);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_R, addressW);
    glSamplerParameterfv(sampler, GL_TEXTURE_BORDER_COLOR, borderColor.data());
    if (desc.compareEnabled) {
        glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_FUNC, compareOp);
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
    if (!m_initialized) {
        logRhiError("createShader requires an initialized device");
        return {};
    }

    std::string errorMessage;
    std::optional<renderer::rhi::RhiCompiledShader> shader =
        renderer::rhi::compileShaderToSpirv(desc, errorMessage);
    if (!shader.has_value()) {
        std::cerr << "GlRhiDevice: canonical shader compilation failed ["
                  << rhiDebugName(desc.debugName) << "]\n" << errorMessage << '\n';
        return {};
    }

    const RhiShaderHandle handle = m_data->shaders.allocate();
    const uint32_t slot = handle.index - 1u;
    if (slot >= m_data->shaderRecords.size()) {
        m_data->shaderRecords.resize(slot + 1u);
    }
    GlShaderRecord record;
    record.shader = std::move(*shader);
    record.debugName = rhiDebugName(desc.debugName);
    record.active = true;
    m_data->shaderRecords[slot] = std::move(record);
    return handle;
}

RhiBindGroupLayoutHandle GlRhiDevice::createBindGroupLayout(const RhiBindGroupLayoutDesc& desc) {
    if (!m_initialized) {
        logRhiError("createBindGroupLayout requires an initialized device");
        return {};
    }
    for (size_t i = 0u; i < desc.entries.size(); ++i) {
        const RhiBindGroupLayoutEntry& entry = desc.entries[i];
        if (entry.stages == 0u || entry.arrayCount != 1u) {
            logRhiError("createBindGroupLayout requires non-empty stages and scalar bindings");
            return {};
        }
        const auto duplicate = std::find_if(desc.entries.begin() + static_cast<std::ptrdiff_t>(i + 1u),
                                            desc.entries.end(),
                                            [&](const RhiBindGroupLayoutEntry& candidate) {
                                                return candidate.binding == entry.binding;
                                            });
        if (duplicate != desc.entries.end()) {
            logRhiError("createBindGroupLayout received duplicate binding numbers");
            return {};
        }
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
    if ((desc.pushConstantBytes == 0u) != (desc.pushConstantStages == 0u) ||
        (desc.pushConstantBytes % 4u) != 0u) {
        logRhiError("createPipelineLayout received an invalid push-constant range");
        return {};
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
    const GlPipelineLayoutRecord* pipelineLayout =
        recordForHandle(m_data->pipelineLayouts, m_data->pipelineLayoutRecords, desc.layout);
    if (!m_initialized || vertexShader == nullptr || fragmentShader == nullptr ||
        vertexShader->shader.stage != RhiShaderStage::Vertex ||
        fragmentShader->shader.stage != RhiShaderStage::Fragment || pipelineLayout == nullptr) {
        logRhiError("createGraphicsPipeline received an invalid descriptor");
        return {};
    }
    if (toGlTopology(desc.topology) == 0u || !isValidCullMode(desc.raster.cullMode) ||
        !isValidFrontFace(desc.raster.frontFace) || toGlCompareOp(desc.depthStencil.depthCompare) == 0u) {
        logRhiError("createGraphicsPipeline received an invalid fixed-function enum");
        return {};
    }
    for (const RhiVertexBinding& binding : desc.vertexInput.bindings) {
        if (!isValidVertexInputRate(binding.inputRate)) {
            logRhiError("createGraphicsPipeline received an invalid vertex input rate");
            return {};
        }
    }
    for (const RhiVertexAttribute& attribute : desc.vertexInput.attributes) {
        if (toGlVertexFormat(attribute.format).type == 0u) {
            logRhiError("createGraphicsPipeline received an invalid vertex format");
            return {};
        }
    }
    for (const RhiBlendAttachmentState& blend : desc.blend.attachments) {
        if (!isValidBlendFactor(blend.srcColor) || !isValidBlendFactor(blend.dstColor) ||
            !isValidBlendFactor(blend.srcAlpha) || !isValidBlendFactor(blend.dstAlpha) ||
            toGlBlendOp(blend.colorOp) == 0u || toGlBlendOp(blend.alphaOp) == 0u) {
            logRhiError("createGraphicsPipeline received an invalid blend enum");
            return {};
        }
    }

    std::vector<GlPipelineRecord::BindingMapping> bindingMappings;
    std::optional<uint32_t> pushConstantBinding;
    uint32_t pushConstantSize = 0u;
    const std::array<uint32_t, 4> bindingLimits = {
        m_data->maxUniformBufferBindings,
        m_data->maxStorageBufferBindings,
        m_data->maxTextureUnits,
        m_data->maxImageUnits
    };
    if (!buildPipelineBindingMappings(m_data->bindGroupLayouts,
                                      m_data->bindGroupLayoutRecords,
                                      bindingLimits,
                                      *pipelineLayout,
                                      {vertexShader, fragmentShader},
                                      desc.debugName,
                                      bindingMappings,
                                      pushConstantBinding,
                                      pushConstantSize)) {
        return {};
    }
    const std::vector<renderer::rhi::gl::GlRhiShaderBindingRemap> remaps =
        makeShaderRemaps(bindingMappings);
    std::string errorMessage;
    const std::optional<std::string> vertexSource = renderer::rhi::gl::crossCompileShaderToOpenGl(
        vertexShader->shader, remaps, pushConstantBinding, errorMessage);
    if (!vertexSource.has_value()) {
        std::cerr << "GlRhiDevice: vertex shader OpenGL generation failed ["
                  << rhiDebugName(desc.debugName) << "]\n" << errorMessage << '\n';
        return {};
    }
    const std::optional<std::string> fragmentSource = renderer::rhi::gl::crossCompileShaderToOpenGl(
        fragmentShader->shader, remaps, pushConstantBinding, errorMessage);
    if (!fragmentSource.has_value()) {
        std::cerr << "GlRhiDevice: fragment shader OpenGL generation failed ["
                  << rhiDebugName(desc.debugName) << "]\n" << errorMessage << '\n';
        return {};
    }
    const GLuint vertexObject = compileShaderObject(
        RhiShaderStage::Vertex, *vertexSource, vertexShader->debugName.c_str());
    if (vertexObject == 0u) {
        return {};
    }
    const GLuint fragmentObject = compileShaderObject(
        RhiShaderStage::Fragment, *fragmentSource, fragmentShader->debugName.c_str());
    if (fragmentObject == 0u) {
        glDeleteShader(vertexObject);
        return {};
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexObject);
    glAttachShader(program, fragmentObject);
    glLinkProgram(program);
    glDetachShader(program, vertexObject);
    glDetachShader(program, fragmentObject);
    glDeleteShader(vertexObject);
    glDeleteShader(fragmentObject);
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
    record.bindingMappings = std::move(bindingMappings);
    record.pushConstantBinding = pushConstantBinding;
    record.pushConstantSize = pushConstantSize;
    record.active = true;
    m_data->pipelineRecords[slot] = std::move(record);
    return handle;
}

RhiPipelineHandle GlRhiDevice::createComputePipeline(const RhiComputePipelineDesc& desc) {
    const GlShaderRecord* computeShader =
        recordForHandle(m_data->shaders, m_data->shaderRecords, desc.computeShader);
    const GlPipelineLayoutRecord* pipelineLayout =
        recordForHandle(m_data->pipelineLayouts, m_data->pipelineLayoutRecords, desc.layout);
    if (!m_initialized || computeShader == nullptr ||
        computeShader->shader.stage != RhiShaderStage::Compute || pipelineLayout == nullptr) {
        logRhiError("createComputePipeline received an invalid descriptor");
        return {};
    }

    std::vector<GlPipelineRecord::BindingMapping> bindingMappings;
    std::optional<uint32_t> pushConstantBinding;
    uint32_t pushConstantSize = 0u;
    const std::array<uint32_t, 4> bindingLimits = {
        m_data->maxUniformBufferBindings,
        m_data->maxStorageBufferBindings,
        m_data->maxTextureUnits,
        m_data->maxImageUnits
    };
    if (!buildPipelineBindingMappings(m_data->bindGroupLayouts,
                                      m_data->bindGroupLayoutRecords,
                                      bindingLimits,
                                      *pipelineLayout,
                                      {computeShader},
                                      desc.debugName,
                                      bindingMappings,
                                      pushConstantBinding,
                                      pushConstantSize)) {
        return {};
    }
    const std::vector<renderer::rhi::gl::GlRhiShaderBindingRemap> remaps =
        makeShaderRemaps(bindingMappings);
    std::string errorMessage;
    const std::optional<std::string> computeSource = renderer::rhi::gl::crossCompileShaderToOpenGl(
        computeShader->shader, remaps, pushConstantBinding, errorMessage);
    if (!computeSource.has_value()) {
        std::cerr << "GlRhiDevice: compute shader OpenGL generation failed ["
                  << rhiDebugName(desc.debugName) << "]\n" << errorMessage << '\n';
        return {};
    }
    const GLuint computeObject = compileShaderObject(
        RhiShaderStage::Compute, *computeSource, computeShader->debugName.c_str());
    if (computeObject == 0u) {
        return {};
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, computeObject);
    glLinkProgram(program);
    glDetachShader(program, computeObject);
    glDeleteShader(computeObject);
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
    record.bindingMappings = std::move(bindingMappings);
    record.pushConstantBinding = pushConstantBinding;
    record.pushConstantSize = pushConstantSize;
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
    if (desc.entries.size() != layout->desc.entries.size()) {
        logRhiError("createBindGroup requires exactly one resource for every layout binding");
        return {};
    }

    for (size_t i = 0u; i < desc.entries.size(); ++i) {
        const RhiBindGroupEntry& entry = desc.entries[i];
        const auto duplicate = std::find_if(
            desc.entries.begin() + static_cast<std::ptrdiff_t>(i + 1u), desc.entries.end(),
            [&](const RhiBindGroupEntry& candidate) { return candidate.binding == entry.binding; });
        const RhiBindGroupLayoutEntry* layoutEntry = findLayoutEntry(*layout, entry.binding);
        if (duplicate != desc.entries.end() || layoutEntry == nullptr) {
            logRhiError("createBindGroup received a duplicate or undeclared binding");
            return {};
        }

        switch (layoutEntry->type) {
            case RhiBindingType::UniformBuffer:
            case RhiBindingType::StorageBuffer: {
                const GlBufferRecord* buffer = recordForHandle(
                    m_data->buffers, m_data->bufferRecords, entry.resource.buffer.buffer);
                const RhiBufferUsage requiredUsage = layoutEntry->type == RhiBindingType::UniformBuffer
                    ? RhiBufferUsage::Uniform
                    : RhiBufferUsage::Storage;
                const uint32_t requiredAlignment = layoutEntry->type == RhiBindingType::UniformBuffer
                    ? m_data->uniformBufferOffsetAlignment
                    : m_data->storageBufferOffsetAlignment;
                const uint64_t range = buffer != nullptr && entry.resource.buffer.range != 0u
                    ? entry.resource.buffer.range
                    : (buffer != nullptr && entry.resource.buffer.offset <= buffer->desc.size
                        ? buffer->desc.size - entry.resource.buffer.offset
                        : 0u);
                if (buffer == nullptr || (buffer->desc.usage & rhiFlag(requiredUsage)) == 0u ||
                    entry.resource.buffer.offset % requiredAlignment != 0u ||
                    range == 0u || entry.resource.buffer.offset > buffer->desc.size ||
                    range > buffer->desc.size - entry.resource.buffer.offset) {
                    logRhiError("createBindGroup received an invalid buffer resource");
                    return {};
                }
                break;
            }
            case RhiBindingType::SampledTexture:
            case RhiBindingType::StorageTexture: {
                const GlTextureViewRecord* view = recordForHandle(
                    m_data->textureViews, m_data->textureViewRecords, entry.resource.textureView);
                GlResolvedTextureRecord texture;
                const RhiTextureUsage requiredUsage = layoutEntry->type == RhiBindingType::SampledTexture
                    ? RhiTextureUsage::Sampled
                    : RhiTextureUsage::Storage;
                if (view == nullptr || !resolveTextureRecord(*m_data, view->desc.texture, texture) ||
                    (texture.desc.usage & rhiFlag(requiredUsage)) == 0u) {
                    logRhiError("createBindGroup received an invalid texture-view resource");
                    return {};
                }
                break;
            }
            case RhiBindingType::Sampler:
                if (recordForHandle(m_data->samplers, m_data->samplerRecords,
                                    entry.resource.sampler) == nullptr) {
                    logRhiError("createBindGroup received an invalid sampler resource");
                    return {};
                }
                break;
            case RhiBindingType::CombinedTextureSampler: {
                const GlTextureViewRecord* view = recordForHandle(
                    m_data->textureViews, m_data->textureViewRecords,
                    entry.resource.combinedTextureSampler.textureView);
                GlResolvedTextureRecord texture;
                if (view == nullptr || !resolveTextureRecord(*m_data, view->desc.texture, texture) ||
                    (texture.desc.usage & rhiFlag(RhiTextureUsage::Sampled)) == 0u ||
                    recordForHandle(m_data->samplers, m_data->samplerRecords,
                                    entry.resource.combinedTextureSampler.sampler) == nullptr) {
                    logRhiError("createBindGroup received an invalid combined texture-sampler resource");
                    return {};
                }
                break;
            }
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
    if (!m_initialized || desc.type != RhiQueryType::Timestamp || desc.queryCount == 0u) {
        logRhiError("createQueryPool received an invalid descriptor");
        return {};
    }
    std::vector<GLuint> queries(desc.queryCount, 0u);
    glGenQueries(static_cast<GLsizei>(queries.size()), queries.data());
    std::vector<bool> issued(desc.queryCount, false);
    const RhiQueryPoolHandle handle = m_data->queryPools.allocate();
    const uint32_t slot = handle.index - 1u;
    if (slot >= m_data->queryPoolRecords.size()) m_data->queryPoolRecords.resize(slot + 1u);
    m_data->queryPoolRecords[slot] = {
        std::move(queries), std::move(issued), desc.type, true};
    return handle;
}

void* GlRhiDevice::mapBuffer(const RhiBufferHandle buffer,
                             const uint64_t offset,
                             const uint64_t size) {
    GlBufferRecord* record = recordForHandle(m_data->buffers, m_data->bufferRecords, buffer);
    if (!m_initialized || record == nullptr || record->mapped || size == 0u ||
        offset > record->desc.size || size > record->desc.size - offset) {
        logRhiError("mapBuffer received an invalid buffer or range");
        return nullptr;
    }

    GLbitfield access = 0u;
    if (record->state == RhiResourceState::HostRead &&
        (record->desc.usage & rhiFlag(RhiBufferUsage::MapRead)) != 0u &&
        record->desc.memoryUsage == RhiMemoryUsage::GpuToCpu) {
        access |= GL_MAP_READ_BIT;
    }
    if (record->state == RhiResourceState::HostWrite &&
        (record->desc.usage & rhiFlag(RhiBufferUsage::MapWrite)) != 0u &&
        record->desc.memoryUsage == RhiMemoryUsage::CpuToGpu) {
        access |= GL_MAP_WRITE_BIT;
    }
    if (access == 0u || offset > static_cast<uint64_t>(std::numeric_limits<GLintptr>::max()) ||
        size > static_cast<uint64_t>(std::numeric_limits<GLsizeiptr>::max())) {
        logRhiError("mapBuffer usage does not permit host access");
        return nullptr;
    }

    void* mapped = glMapNamedBufferRange(record->buffer,
                                         static_cast<GLintptr>(offset),
                                         static_cast<GLsizeiptr>(size),
                                         access);
    if (mapped == nullptr) {
        logRhiError("mapBuffer failed to map the native buffer");
        return nullptr;
    }
    record->mapped = true;
    return mapped;
}

void GlRhiDevice::unmapBuffer(const RhiBufferHandle buffer) {
    GlBufferRecord* record = recordForHandle(m_data->buffers, m_data->bufferRecords, buffer);
    if (!m_initialized || record == nullptr || !record->mapped) {
        logRhiError("unmapBuffer received a buffer that is not mapped");
        return;
    }
    glUnmapNamedBuffer(record->buffer);
    record->mapped = false;
}

bool GlRhiDevice::areQueryResultsAvailable(RhiQueryPoolHandle pool,
                                           uint32_t firstQuery,
                                           uint32_t queryCount) const {
    const GlQueryPoolRecord* record = recordForHandle(
        m_data->queryPools, m_data->queryPoolRecords, pool);
    if (record == nullptr || queryCount == 0u ||
        firstQuery > record->queries.size() || queryCount > record->queries.size() - firstQuery) return false;
    for (uint32_t i = 0; i < queryCount; ++i) {
        if (!record->issued[firstQuery + i]) return false;
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
        // GL_TIMESTAMP is specified in nanoseconds, matching the public RHI contract.
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

RhiTextureHandle GlRhiDevice::currentSwapchainColorTexture() const {
    return m_initialized && m_data ? m_data->swapchainColorTexture : RhiTextureHandle{};
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

    GlTextureRecord* swapchainTexture = recordForHandle(
        m_data->textures, m_data->textureRecords, m_data->swapchainColorTexture);
    if (swapchainTexture == nullptr || !swapchainTexture->swapchainBackbuffer) {
        logRhiError("resizeSwapchain requires a live swapchain color texture");
        return false;
    }

    m_data->swapchainWidth = width;
    m_data->swapchainHeight = height;
    swapchainTexture->desc.width = width;
    swapchainTexture->desc.height = height;
    return true;
}

void GlRhiDevice::destroyBuffer(RhiBufferHandle handle) {
    GlBufferRecord* record = recordForHandle(m_data->buffers, m_data->bufferRecords, handle);
    if (record != nullptr) {
        if (record->mapped) {
            glUnmapNamedBuffer(record->buffer);
        }
        if (record->buffer != 0u) {
            m_data->pendingRetirements.buffers.push_back(record->buffer);
        }
        *record = {};
    }
    (void) m_data->buffers.release(handle);
}

void GlRhiDevice::destroyTexture(RhiTextureHandle handle) {
    GlTextureRecord* record = recordForHandle(m_data->textures, m_data->textureRecords, handle);
    if (record != nullptr && record->swapchainBackbuffer) {
        logRhiError("destroyTexture cannot destroy the device-owned swapchain texture");
        return;
    }
    if (record != nullptr) {
        if (record->texture != 0u) {
            m_data->pendingRetirements.textures.push_back(record->texture);
        }
        *record = {};
    }
    (void) m_data->textures.release(handle);
}

void GlRhiDevice::destroyTextureView(RhiTextureViewHandle handle) {
    GlTextureViewRecord* record = recordForHandle(m_data->textureViews, m_data->textureViewRecords, handle);
    if (record != nullptr) {
        if (record->ownsTexture && record->texture != 0u) {
            m_data->pendingRetirements.textures.push_back(record->texture);
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
            if (framebuffer.framebuffer != 0u) {
                m_data->pendingRetirements.framebuffers.push_back(framebuffer.framebuffer);
            }
            framebuffer = {};
        }
    }
    (void) m_data->textureViews.release(handle);
}

void GlRhiDevice::destroySampler(RhiSamplerHandle handle) {
    GlSamplerRecord* record = recordForHandle(m_data->samplers, m_data->samplerRecords, handle);
    if (record != nullptr) {
        if (record->sampler != 0u) {
            m_data->pendingRetirements.samplers.push_back(record->sampler);
        }
        *record = {};
    }
    (void) m_data->samplers.release(handle);
}

void GlRhiDevice::destroyShader(RhiShaderHandle handle) {
    GlShaderRecord* record = recordForHandle(m_data->shaders, m_data->shaderRecords, handle);
    if (record != nullptr) {
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
            m_data->pendingRetirements.vertexArrays.push_back(record->vertexArray);
        }
        if (record->program != 0u) {
            m_data->pendingRetirements.programs.push_back(record->program);
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
            m_data->pendingRetirements.queries.insert(
                m_data->pendingRetirements.queries.end(),
                record->queries.begin(),
                record->queries.end());
        }
        *record = {};
    }
    (void) m_data->queryPools.release(handle);
}

std::unique_ptr<RhiCommandListPool> GlRhiDevice::createCommandListPool(
    const RhiCommandListPoolDesc& desc) {
    if (!m_initialized) {
        logRhiError("createCommandListPool requires an initialized device");
        return nullptr;
    }
    return std::make_unique<GlRhiCommandListPool>(*this, desc);
}

void GlRhiDevice::reclaimCompletedCommandLists() {
    reclaimCompletedRetirementBatches(*m_data);
    for (const std::shared_ptr<GlRhiCommandList>& commandList :
         m_data->completedCommandLists) {
        if (commandList != nullptr && commandList->m_state == RhiCommandListState::Pending) {
            commandList->resetForPoolReuse();
        }
    }
    m_data->completedCommandLists.clear();
}

bool GlRhiDevice::submit(const RhiSubmitInfo& info,
                         RhiSubmissionToken* completionToken) {
    if (completionToken != nullptr) {
        *completionToken = {};
    }
    if (!m_initialized || std::this_thread::get_id() != m_deviceThread ||
        info.commandLists == nullptr || info.commandListCount == 0u) {
        logRhiError("submit requires command lists on the device thread");
        return false;
    }

    reclaimCompletedCommandLists();

    std::vector<std::shared_ptr<GlRhiCommandList>> commandLists;
    commandLists.reserve(info.commandListCount);
    {
        std::lock_guard<std::mutex> registryLock(m_data->commandListRegistryMutex);
        for (uint32_t index = 0u; index < info.commandListCount; ++index) {
            RhiCommandList* baseCommandList = info.commandLists[index];
            auto* commandList = reinterpret_cast<GlRhiCommandList*>(baseCommandList);
            const auto registered = m_data->commandLists.find(commandList);
            const bool duplicate = std::any_of(
                commandLists.begin(), commandLists.end(),
                [commandList](const std::shared_ptr<GlRhiCommandList>& candidate) {
                    return candidate.get() == commandList;
                });
            const std::shared_ptr<GlRhiCommandList> retained =
                registered != m_data->commandLists.end() ? registered->second.lock() : nullptr;
            if (baseCommandList == nullptr || !retained || duplicate) {
                logRhiError("submit received an invalid, foreign, or duplicate command list");
                return false;
            }
            commandLists.push_back(retained);
        }
    }

    for (const std::shared_ptr<GlRhiCommandList>& commandList : commandLists) {
        if (!commandList->m_acquired || commandList->m_device != this ||
            commandList->m_state != RhiCommandListState::Executable ||
            !commandList->validateForSubmit()) {
            logRhiError("submit requires executable command lists with live resource references");
            return false;
        }
    }
    if (m_lastSubmittedSequence == std::numeric_limits<uint64_t>::max()) {
        logRhiError("submit exhausted the submission sequence space");
        return false;
    }

    std::vector<RhiResourceState> bufferStateSnapshot;
    bufferStateSnapshot.reserve(m_data->bufferRecords.size());
    for (const GlBufferRecord& record : m_data->bufferRecords) {
        bufferStateSnapshot.push_back(record.state);
    }
    std::vector<std::vector<RhiResourceState>> textureStateSnapshot;
    textureStateSnapshot.reserve(m_data->textureRecords.size());
    for (const GlTextureRecord& record : m_data->textureRecords) {
        textureStateSnapshot.push_back(record.subresourceStates);
    }
    const auto restoreResourceStates = [&]() {
        for (size_t index = 0u; index < bufferStateSnapshot.size(); ++index) {
            m_data->bufferRecords[index].state = bufferStateSnapshot[index];
        }
        for (size_t index = 0u; index < textureStateSnapshot.size(); ++index) {
            m_data->textureRecords[index].subresourceStates =
                std::move(textureStateSnapshot[index]);
        }
    };
    bool validationSucceeded = true;
    for (const std::shared_ptr<GlRhiCommandList>& commandList : commandLists) {
        if (!commandList->replay(true)) {
            validationSucceeded = false;
            break;
        }
    }
    restoreResourceStates();
    if (!validationSucceeded) {
        logRhiError("submit rejected a command list during semantic validation");
        return false;
    }

    for (const std::shared_ptr<GlRhiCommandList>& commandList : commandLists) {
        if (!commandList->replay(false)) {
            logRhiError("submit failed to replay a validated command list");
            return false;
        }
    }

    GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0u);
    if (fence == nullptr) {
        logRhiError("submit failed to create a completion fence");
        return false;
    }
    GlRetirementBatch batch;
    batch.fence = fence;
    batch.submissionSequence = ++m_lastSubmittedSequence;
    batch.resources = std::move(m_data->pendingRetirements);
    batch.commandLists = std::move(commandLists);
    m_data->pendingRetirements = {};
    m_data->retirementBatches.push_back(std::move(batch));
    if (completionToken != nullptr) {
        *completionToken = {m_deviceId, m_lastSubmittedSequence};
    }
    glFlush();
    return true;
}

bool GlRhiDevice::validateSubmissionToken(const RhiSubmissionToken token) const {
    return m_initialized && token.isValid() && token.deviceId == m_deviceId &&
           token.sequence <= m_lastSubmittedSequence;
}

bool GlRhiDevice::isSubmissionComplete(const RhiSubmissionToken token,
                                       bool& complete) {
    complete = false;
    if (std::this_thread::get_id() != m_deviceThread || !validateSubmissionToken(token)) {
        logRhiError("submission completion query received an invalid or foreign token");
        return false;
    }
    reclaimCompletedCommandLists();
    complete = token.sequence <= m_data->completedSubmissionSequence;
    return true;
}

bool GlRhiDevice::waitForSubmission(const RhiSubmissionToken token) {
    if (std::this_thread::get_id() != m_deviceThread || !validateSubmissionToken(token)) {
        logRhiError("submission wait received an invalid or foreign token");
        return false;
    }
    if (token.sequence <= m_data->completedSubmissionSequence) {
        return true;
    }

    const auto batch = std::find_if(
        m_data->retirementBatches.begin(), m_data->retirementBatches.end(),
        [token](const GlRetirementBatch& candidate) {
            return candidate.submissionSequence == token.sequence;
        });
    if (batch == m_data->retirementBatches.end()) {
        logRhiError("submission wait could not find the pending submission");
        return false;
    }
    const GLenum result = glClientWaitSync(
        batch->fence, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);
    if (result == GL_WAIT_FAILED) {
        logRhiError("submission fence wait failed");
        return false;
    }
    reclaimCompletedCommandLists();
    if (token.sequence > m_data->completedSubmissionSequence) {
        logRhiError("submission remained pending after its fence completed");
        return false;
    }
    return true;
}

void GlRhiDevice::waitIdle() {
    if (m_initialized) {
        glFinish();
        reclaimAllRetiredResources(*m_data);
        for (const std::shared_ptr<GlRhiCommandList>& commandList :
             m_data->completedCommandLists) {
            if (commandList != nullptr && commandList->m_state == RhiCommandListState::Pending) {
                commandList->resetForPoolReuse();
            }
        }
        m_data->completedCommandLists.clear();
    }
}

void GlRhiDevice::present() {
    if (!m_initialized || std::this_thread::get_id() != m_deviceThread) {
        logRhiError("present requires an initialized device on the device thread");
        return;
    }
    const GlTextureRecord* swapchain = recordForHandle(
        m_data->textures, m_data->textureRecords, m_data->swapchainColorTexture);
    const GlTextureSubresourceRange colorRange{0u, 1u, 0u, 1u};
    if (swapchain == nullptr ||
        !textureRangeHasState(*swapchain, colorRange, RhiResourceState::Present)) {
        logRhiError("present requires the swapchain color texture in Present state");
        return;
    }
    reclaimCompletedCommandLists();
    glfwSwapBuffers(m_data->nativeWindow);
}
