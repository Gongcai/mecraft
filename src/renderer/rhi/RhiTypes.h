#ifndef MECRAFT_RHI_TYPES_H
#define MECRAFT_RHI_TYPES_H

#include <cstdint>

enum class RhiBackend {
    OpenGL,
    Vulkan
};

struct RhiDeviceDesc {
    const char* debugName = nullptr;
    void* nativeWindow = nullptr;
    int width = 1;
    int height = 1;
    bool enableDebugMarkers = false;
};

struct RhiCapabilities {
    bool multiDrawIndirect = false;
    bool timestampQuery = false;
    bool textureView = false;
    bool samplerAnisotropy = false;
    bool storageImage = false;
    bool descriptorIndexing = false;
    uint32_t maxColorAttachments = 0;
    uint32_t maxSampledTexturesPerStage = 0;
};

enum class RhiShaderStage : uint32_t {
    Vertex = 1u << 0u,
    Fragment = 1u << 1u,
    Compute = 1u << 2u
};

using RhiShaderStageFlags = uint32_t;

enum class RhiTextureDimension {
    Texture2D,
    Texture2DArray,
    Texture3D,
    Cube
};

enum class RhiTextureViewType {
    Texture2D,
    Texture2DArray,
    Texture3D,
    Cube
};

enum class RhiTextureFormat {
    Undefined,
    R8Unorm,
    Rg8Unorm,
    Rgba8Unorm,
    Rgba8Srgb,
    Rg16Float,
    Rgba16Float,
    Rgba32Float,
    R16Float,
    R32Float,
    Depth16,
    Depth24,
    Depth24Stencil8,
    Depth32Float
};

enum class RhiTextureUsage : uint32_t {
    Sampled = 1u << 0u,
    Storage = 1u << 1u,
    ColorAttachment = 1u << 2u,
    DepthStencilAttachment = 1u << 3u,
    TransferSrc = 1u << 4u,
    TransferDst = 1u << 5u,
    Present = 1u << 6u
};

using RhiTextureUsageFlags = uint32_t;

enum class RhiBufferUsage : uint32_t {
    Vertex = 1u << 0u,
    Index = 1u << 1u,
    Uniform = 1u << 2u,
    Storage = 1u << 3u,
    Indirect = 1u << 4u,
    TransferSrc = 1u << 5u,
    TransferDst = 1u << 6u,
    MapRead = 1u << 7u,
    MapWrite = 1u << 8u
};

using RhiBufferUsageFlags = uint32_t;

enum class RhiMemoryUsage {
    GpuOnly,
    CpuToGpu,
    GpuToCpu
};

enum class RhiFilter {
    Nearest,
    Linear
};

enum class RhiMipmapMode {
    Nearest,
    Linear
};

enum class RhiAddressMode {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder
};

enum class RhiCompareOp {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always
};

enum class RhiLoadOp {
    Load,
    Clear,
    DontCare
};

enum class RhiStoreOp {
    Store,
    DontCare
};

enum class RhiResourceState {
    Undefined,
    Present,
    RenderTarget,
    DepthWrite,
    DepthRead,
    ShaderRead,
    ShaderWrite,
    TransferSrc,
    TransferDst,
    VertexBuffer,
    IndexBuffer,
    IndirectArgument,
    UniformBuffer,
    StorageBuffer
};

enum class RhiIndexFormat {
    Uint16,
    Uint32
};

enum class RhiPrimitiveTopology {
    TriangleList,
    TriangleStrip,
    LineList,
    LineStrip
};

enum class RhiCullMode {
    None,
    Front,
    Back
};

enum class RhiFrontFace {
    CounterClockwise,
    Clockwise
};

enum class RhiBlendFactor {
    Zero,
    One,
    SrcAlpha,
    OneMinusSrcAlpha,
    SrcColor,
    OneMinusSrcColor,
    DstAlpha,
    OneMinusDstAlpha,
    DstColor,
    OneMinusDstColor
};

enum class RhiBlendOp {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max
};

enum class RhiVertexFormat {
    Float,
    Float2,
    Float3,
    Float4,
    Uint,
    Uint2,
    Uint3,
    Uint4
};

enum class RhiVertexInputRate {
    Vertex,
    Instance
};

struct RhiViewport {
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct RhiRect2D {
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 1;
    uint32_t height = 1;
};

[[nodiscard]] constexpr RhiTextureUsageFlags rhiFlag(RhiTextureUsage usage) {
    return static_cast<RhiTextureUsageFlags>(usage);
}

[[nodiscard]] constexpr RhiBufferUsageFlags rhiFlag(RhiBufferUsage usage) {
    return static_cast<RhiBufferUsageFlags>(usage);
}

[[nodiscard]] constexpr RhiShaderStageFlags rhiFlag(RhiShaderStage stage) {
    return static_cast<RhiShaderStageFlags>(stage);
}

#endif // MECRAFT_RHI_TYPES_H
