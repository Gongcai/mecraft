#ifndef MECRAFT_RHI_TYPES_H
#define MECRAFT_RHI_TYPES_H

#include "renderer/rhi/RhiHandles.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

class RhiCommandList;

enum class RhiBackend { OpenGL, Vulkan };

enum class RhiCommandListType { Graphics, Compute, Transfer };

enum class RhiQueueType { Graphics, Compute, Transfer, Present };

enum class RhiCommandListState { Initial, Recording, Executable, Pending };

struct RhiCommandListDesc {
    const char* debugName = nullptr;
    RhiCommandListType type = RhiCommandListType::Graphics;
};

struct RhiCommandListPoolDesc {
    const char* debugName = nullptr;
    uint32_t initialCommandListCapacity = 1u;
    size_t initialArenaCapacity = 64u * 1024u;
};

struct RhiSubmissionToken {
    /// Identifies the device instance that issued this backend-independent token.
    uint64_t deviceId = 0u;
    /// Identifies one submission in the device-wide monotonic submission order.
    uint64_t sequence = 0u;
    /// Identifies the logical queue that executes this submission.
    RhiQueueType queue = RhiQueueType::Graphics;
    /// Identifies the signal value on the logical queue timeline.
    uint64_t queueValue = 0u;

    /// Reports whether this token identifies a submission on a device instance.
    /// @return True when both the device identity and queue sequence are non-zero.
    [[nodiscard]] constexpr bool isValid() const {
        return deviceId != 0u && sequence != 0u && queue != RhiQueueType::Present;
    }

    /// Returns the signal value on the token's logical queue timeline.
    /// Legacy tokens use the global submission sequence as their timeline value.
    /// @return Non-zero timeline value used for queue waits and completion queries.
    [[nodiscard]] constexpr uint64_t timelineValue() const { return queueValue != 0u ? queueValue : sequence; }
};

struct RhiQueueDependency {
    RhiSubmissionToken token;
    uint64_t value = 0u;
};

struct RhiSubmitInfo {
    const char* debugName = nullptr;
    RhiCommandList* const* commandLists = nullptr;
    uint32_t commandListCount = 0u;
    RhiQueueType queue = RhiQueueType::Graphics;
    const RhiQueueDependency* waits = nullptr;
    uint32_t waitCount = 0u;
};

enum class RhiFrameStatus { Success, Suboptimal, OutOfDate, Minimized, SurfaceLost, DeviceLost, Error };

struct RhiFrameAcquireResult {
    RhiFrameStatus status = RhiFrameStatus::Error;
    uint64_t frameIndex = 0u;
    uint32_t imageIndex = 0u;
    uint32_t width = 0u;
    uint32_t height = 0u;
    RhiTextureHandle colorTexture;
    RhiTextureViewHandle colorView;
    RhiTextureViewHandle depthStencilView;
};

struct RhiPresentInfo {
    uint64_t frameIndex = 0u;
    uint32_t imageIndex = 0u;
    uint32_t trackingFrameIndex = 0u;
};

enum class RhiColorSpace { SrgbNonlinear, DisplayP3Nonlinear, ExtendedSrgbLinear, Hdr10St2084 };

enum class RhiPresentMode { Immediate, Mailbox, Fifo, FifoRelaxed };

struct RhiDeviceDesc {
    const char* debugName = nullptr;
    void* nativeWindow = nullptr;
    int width = 1;
    int height = 1;
    bool enableDebugMarkers = false;
    bool enableDebugOutput = false;
    std::optional<bool> vsyncEnabled;
};

struct RhiCapabilities {
    bool multiDrawIndirect = false;
    bool timestampQuery = false;
    bool graphicsTimestampQuery = false;
    bool computeTimestampQuery = false;
    bool transferTimestampQuery = false;
    bool textureView = false;
    bool samplerAnisotropy = false;
    bool storageImage = false;
    bool descriptorIndexing = false;
    bool descriptorBindingPartiallyBound = false;
    bool descriptorBindingVariableDescriptorCount = false;
    bool descriptorBindingUpdateUnusedWhilePending = false;
    bool descriptorBindingUniformBufferUpdateAfterBind = false;
    bool descriptorBindingSampledImageUpdateAfterBind = false;
    bool descriptorBindingStorageImageUpdateAfterBind = false;
    bool descriptorBindingStorageBufferUpdateAfterBind = false;
    bool descriptorBindingAccelerationStructureUpdateAfterBind = false;
    bool runtimeDescriptorArray = false;
    bool shaderSampledImageArrayNonUniformIndexing = false;
    bool shaderStorageBufferArrayNonUniformIndexing = false;
    uint32_t maxColorAttachments = 0;
    uint32_t maxSampledTexturesPerStage = 0;
    uint32_t textureBufferCopyRowPitchAlignment = 1;
    float maxSamplerAnisotropy = 1.0f;
    uint32_t swapchainImageCount = 0u;
    RhiColorSpace swapchainColorSpace = RhiColorSpace::SrgbNonlinear;
    bool hdr10Swapchain = false;
    bool scRgbSwapchain = false;
    RhiPresentMode swapchainPresentMode = RhiPresentMode::Fifo;
    bool vsyncControl = false;
    uint32_t vulkanApiVersion = 0u;
    bool dynamicRendering = false;
    bool synchronization2 = false;
    bool shaderDemoteToHelperInvocation = false;
    bool timelineSemaphore = false;
    bool bufferDeviceAddress = false;
    uint32_t maxDescriptorSetUpdateAfterBindSampledImages = 0u;
    uint32_t maxDescriptorSetUpdateAfterBindSamplers = 0u;
    uint32_t maxDescriptorSetUpdateAfterBindStorageImages = 0u;
    uint32_t maxDescriptorSetUpdateAfterBindUniformBuffers = 0u;
    uint32_t maxDescriptorSetUpdateAfterBindStorageBuffers = 0u;
    uint32_t maxDescriptorSetUpdateAfterBindAccelerationStructures = 0u;
    uint32_t maxPerStageDescriptorUpdateAfterBindSampledImages = 0u;
    uint32_t maxPerStageDescriptorUpdateAfterBindSamplers = 0u;
    uint32_t maxPerStageDescriptorUpdateAfterBindStorageBuffers = 0u;
    uint32_t maxPerStageDescriptorUpdateAfterBindAccelerationStructures = 0u;
    uint32_t maxPerStageUpdateAfterBindResources = 0u;
    uint32_t graphicsQueueFamilyIndex = std::numeric_limits<uint32_t>::max();
    uint32_t computeQueueFamilyIndex = std::numeric_limits<uint32_t>::max();
    uint32_t transferQueueFamilyIndex = std::numeric_limits<uint32_t>::max();
    uint32_t presentQueueFamilyIndex = std::numeric_limits<uint32_t>::max();
    bool dedicatedComputeQueue = false;
    /// True when r8-style extended storage image formats may be written.
    bool storageImageExtendedFormats = false;
    bool dedicatedTransferQueue = false;
    bool accelerationStructure = false;
    bool rayQuery = false;
    bool rayTracingPipeline = false;
    /// True when VK_EXT_opacity_micromap is enabled and its micromap feature is available.
    bool opacityMicromap = false;
    uint32_t maxOpacityMicromapTwoStateSubdivisionLevel = 0u;
    uint32_t maxOpacityMicromapFourStateSubdivisionLevel = 0u;
    bool accelerationStructureHostCommands = false;
    uint64_t maxAccelerationStructureGeometryCount = 0u;
    uint64_t maxAccelerationStructureInstanceCount = 0u;
    uint64_t maxAccelerationStructurePrimitiveCount = 0u;
    uint32_t minAccelerationStructureScratchOffsetAlignment = 1u;
    uint32_t shaderBindingTableHandleSize = 0u;
    uint32_t shaderBindingTableHandleAlignment = 0u;
    uint32_t shaderBindingTableBaseAlignment = 0u;
    /// True when placed textures may share device memory blocks
    /// (getTextureMemoryRequirements/allocateTextureMemory/createPlacedTexture).
    bool textureAliasing = false;
};

enum class RhiShaderStage : uint32_t { Vertex = 1u << 0u, Fragment = 1u << 1u, Compute = 1u << 2u };

using RhiShaderStageFlags = uint32_t;

enum class RhiTextureDimension { Texture2D, Texture2DArray, Texture3D, Cube, CubeArray };

enum class RhiTextureViewType { Texture2D, Texture2DArray, Texture3D, Cube, CubeArray };

enum class RhiTextureFormat {
    Undefined,
    R8Unorm,
    R8Uint,
    Rg8Unorm,
    Rgba8Unorm,
    Rgba8Srgb,
    Bgra8Unorm,
    Bgra8Srgb,
    Rgb10A2Unorm,
    Rg16Float,
    Rgba16Float,
    Rgba32Float,
    R16Float,
    R16Uint,
    R32Float,
    R32Uint,
    Rg32Uint,
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
    MapWrite = 1u << 8u,
    DeviceAddress = 1u << 9u,
    AccelerationStructureStorage = 1u << 10u,
    AccelerationStructureBuildInput = 1u << 11u,
    MicromapStorage = 1u << 12u,
    MicromapBuildInput = 1u << 13u
};

using RhiBufferUsageFlags = uint32_t;

enum class RhiMemoryUsage { GpuOnly, CpuToGpu, GpuToCpu };

enum class RhiQueryType { Timestamp, AccelerationStructureCompactedSize };

enum class RhiFilter { Nearest, Linear };

enum class RhiMipmapMode { Nearest, Linear };

enum class RhiAddressMode { Repeat, MirroredRepeat, ClampToEdge, ClampToBorder };

enum class RhiBorderColor { TransparentBlack, OpaqueBlack, OpaqueWhite };

enum class RhiCompareOp { Never, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always };

enum class RhiLoadOp { Load, Clear, DontCare };

enum class RhiStoreOp { Store, DontCare };

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
    StorageBuffer,
    AccelerationStructureBuildInput,
    AccelerationStructureBuildScratch,
    AccelerationStructureBuildWrite,
    AccelerationStructureRead,
    MicromapBuildInput,
    MicromapBuildScratch,
    MicromapBuildWrite,
    HostRead,
    HostWrite
};

enum class RhiIndexFormat { Uint16, Uint32 };

enum class RhiPrimitiveTopology { TriangleList, TriangleStrip, LineList, LineStrip };

enum class RhiCullMode { None, Front, Back };

enum class RhiFrontFace { CounterClockwise, Clockwise };

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

enum class RhiBlendOp { Add, Subtract, ReverseSubtract, Min, Max };

enum class RhiVertexFormat { Float, Float2, Float3, Float4, Uint, Uint2, Uint3, Uint4, Sint8, Unorm8, Uint8, Uint16 };

enum class RhiAccelerationStructureType : uint8_t { BottomLevel, TopLevel };

enum class RhiAccelerationStructureGeometryType : uint8_t { Triangles, Aabbs, Instances };

enum class RhiAccelerationStructureIndexFormat : uint8_t { None, Uint16, Uint32 };

enum class RhiAccelerationStructureBuildMode : uint8_t { Build, Update };

enum class RhiAccelerationStructureCopyMode : uint8_t { Clone, Compact };

enum class RhiAccelerationStructureBuildFlag : uint32_t {
    AllowUpdate = 1u << 0u,
    AllowCompaction = 1u << 1u,
    PreferFastTrace = 1u << 2u,
    PreferFastBuild = 1u << 3u
};

using RhiAccelerationStructureBuildFlags = uint32_t;

enum class RhiAccelerationStructureGeometryFlag : uint32_t {
    Opaque = 1u << 0u,
    NoDuplicateAnyHitInvocation = 1u << 1u
};

using RhiAccelerationStructureGeometryFlags = uint32_t;

enum class RhiAccelerationStructureInstanceFlag : uint32_t {
    TriangleFacingCullDisable = 1u << 0u,
    TriangleFrontCounterClockwise = 1u << 1u,
    ForceOpaque = 1u << 2u,
    ForceNoOpaque = 1u << 3u
};

using RhiAccelerationStructureInstanceFlags = uint32_t;

enum class RhiVertexInputRate { Vertex, Instance };

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

[[nodiscard]] constexpr RhiAccelerationStructureBuildFlags rhiFlag(RhiAccelerationStructureBuildFlag flag) {
    return static_cast<RhiAccelerationStructureBuildFlags>(flag);
}

[[nodiscard]] constexpr RhiAccelerationStructureGeometryFlags rhiFlag(RhiAccelerationStructureGeometryFlag flag) {
    return static_cast<RhiAccelerationStructureGeometryFlags>(flag);
}

[[nodiscard]] constexpr RhiAccelerationStructureInstanceFlags rhiFlag(RhiAccelerationStructureInstanceFlag flag) {
    return static_cast<RhiAccelerationStructureInstanceFlags>(flag);
}

#endif // MECRAFT_RHI_TYPES_H
