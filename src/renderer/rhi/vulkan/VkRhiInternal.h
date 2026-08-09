#ifndef MECRAFT_VK_RHI_INTERNAL_H
#define MECRAFT_VK_RHI_INTERNAL_H

// Tracks the newest submission that may access one native resource. Destruction
// can retire the resource as soon as this sequence has completed.
struct VkRhiResourceLifetime {
    uint64_t lastUseSequence = 0u;

    void markUsed(const uint64_t sequence) {
        if (sequence > lastUseSequence) {
            lastUseSequence = sequence;
        }
    }
};

// Stores every logical resource referenced while recording one command list.
// Submission validates these handles and stamps their native records with the
// accepted submission sequence before the command list becomes pending.
struct VkRhiCommandResourceReferences {
    std::vector<RhiBufferHandle> buffers;
    std::vector<RhiTextureHandle> textures;
    std::vector<RhiTextureViewHandle> textureViews;
    std::vector<RhiSamplerHandle> samplers;
    std::vector<RhiBindGroupLayoutHandle> bindGroupLayouts;
    std::vector<RhiPipelineLayoutHandle> pipelineLayouts;
    std::vector<RhiPipelineHandle> pipelines;
    std::vector<RhiBindGroupHandle> bindGroups;
    std::vector<RhiQueryPoolHandle> queryPools;
    std::vector<RhiAccelerationStructureHandle> accelerationStructures;
    std::vector<RhiMicromapHandle> micromaps;

    void reference(const RhiBufferHandle handle) { addUnique(buffers, handle); }
    void reference(const RhiTextureHandle handle) { addUnique(textures, handle); }
    void reference(const RhiTextureViewHandle handle) { addUnique(textureViews, handle); }
    void reference(const RhiSamplerHandle handle) { addUnique(samplers, handle); }
    void reference(const RhiBindGroupLayoutHandle handle) { addUnique(bindGroupLayouts, handle); }
    void reference(const RhiPipelineLayoutHandle handle) { addUnique(pipelineLayouts, handle); }
    void reference(const RhiPipelineHandle handle) { addUnique(pipelines, handle); }
    void reference(const RhiBindGroupHandle handle) { addUnique(bindGroups, handle); }
    void reference(const RhiQueryPoolHandle handle) { addUnique(queryPools, handle); }
    void reference(const RhiAccelerationStructureHandle handle) { addUnique(accelerationStructures, handle); }
    void reference(const RhiMicromapHandle handle) { addUnique(micromaps, handle); }

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
        accelerationStructures.clear();
        micromaps.clear();
    }

    [[nodiscard]] bool empty() const {
        return buffers.empty() && textures.empty() && textureViews.empty() && samplers.empty() &&
               bindGroupLayouts.empty() && pipelineLayouts.empty() && pipelines.empty() && bindGroups.empty() &&
               queryPools.empty() && accelerationStructures.empty() && micromaps.empty();
    }

private:
    template <typename Handle> static void addUnique(std::vector<Handle>& handles, const Handle handle) {
        if (!handle.isValid()) {
            return;
        }
        const auto existing = std::find_if(handles.begin(), handles.end(), [handle](const Handle candidate) {
            return candidate.index == handle.index && candidate.generation == handle.generation;
        });
        if (existing == handles.end()) {
            handles.push_back(handle);
        }
    }
};

struct VkRhiDeviceData {
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        RhiBufferDesc desc{};
        std::string debugName;
        void* mapped = nullptr;
        VkRhiResourceLifetime lifetime{};
        uint64_t allocationBytes = 0u;
    };
    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        RhiTextureDesc desc{};
        std::string debugName;
        bool swapchainOwned = false;
        bool swapchainAttachment = false;
        VkRhiResourceLifetime lifetime{};
        uint64_t allocationBytes = 0u;
    };
    struct TextureView {
        VkImageView view = VK_NULL_HANDLE;
        RhiTextureViewDesc desc{};
        VkRhiResourceLifetime lifetime{};
    };
    struct Sampler {
        VkSampler sampler = VK_NULL_HANDLE;
        RhiSamplerDesc desc{};
        VkRhiResourceLifetime lifetime{};
    };
    struct Shader {
        VkShaderModule module = VK_NULL_HANDLE;
        RhiShaderStage stage = RhiShaderStage::Vertex;
        std::string entryPoint;
        renderer::rhi::RhiShaderReflection reflection;
        VkRhiResourceLifetime lifetime{};
    };
    struct BindGroupLayout {
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        RhiBindGroupLayoutDesc desc{};
        VkRhiResourceLifetime lifetime{};
    };
    struct PipelineLayout {
        VkPipelineLayout layout = VK_NULL_HANDLE;
        RhiPipelineLayoutDesc desc{};
        VkRhiResourceLifetime lifetime{};
    };
    struct Pipeline {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        RhiPipelineLayoutHandle layoutHandle{};
        VkRhiResourceLifetime lifetime{};
    };
    struct BindGroup {
        VkDescriptorSet set = VK_NULL_HANDLE;
        RhiBindGroupLayoutHandle layoutHandle{};
        RhiBindGroupDesc desc{};
        VkRhiResourceLifetime lifetime{};
    };
    struct QueryPool {
        VkQueryPool pool = VK_NULL_HANDLE;
        uint32_t count = 0u;
        RhiQueryType type = RhiQueryType::Timestamp;
        VkRhiResourceLifetime lifetime{};
    };
    struct AccelerationStructure {
        VkAccelerationStructureKHR accelerationStructure = VK_NULL_HANDLE;
        RhiAccelerationStructureDesc desc{};
        std::string debugName;
        VkRhiResourceLifetime lifetime{};
    };
    struct Micromap {
        VkMicromapEXT micromap = VK_NULL_HANDLE;
        RhiMicromapDesc desc{};
        std::string debugName;
        VkRhiResourceLifetime lifetime{};
    };
    struct FrameContext {
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        bool fencePending = false;
    };
    /// Shared memory block hosting placed textures; the allocation stays
    /// alive until explicitly destroyed, independent of member textures.
    struct TextureMemory {
        VmaAllocation allocation = VK_NULL_HANDLE;
        uint64_t sizeBytes = 0u;
        RhiMemoryCategory category = RhiMemoryCategory::Unclassified;
        std::string debugName;
    };
    struct DeferredBuffer {
        uint64_t sequence;
        VkBuffer buffer;
        VmaAllocation allocation;
    };
    struct DeferredMemory {
        uint64_t sequence;
        VmaAllocation allocation;
    };
    struct DeferredImage {
        uint64_t sequence;
        uint64_t textureKey;
        VkImage image;
        VmaAllocation allocation;
    };
    struct DeferredObject {
        uint64_t sequence = 0u;
        VkObjectType type = VK_OBJECT_TYPE_UNKNOWN;
        uint64_t object = 0u;
        RhiBufferHandle accelerationStructureBuffer;
        uint64_t accelerationStructureOffset = 0u;
        uint64_t accelerationStructureSize = 0u;
    };
    struct PendingList {
        uint64_t sequence = 0u;
        RhiQueueType queue = RhiQueueType::Graphics;
        uint64_t queueValue = 0u;
        VkRhiCommandList* list = nullptr;
    };
    struct PendingSubmission {
        uint64_t sequence = 0u;
        RhiQueueType queue = RhiQueueType::Graphics;
        uint64_t queueValue = 0u;
    };

    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    std::atomic<uint64_t> validationErrorCount{0u};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    QueueFamilies queueFamilies{};
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    VkQueue transferQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    uint32_t streamlineGraphicsQueueIndex = 0u;
    uint32_t streamlineComputeQueueIndex = 0u;
    uint32_t streamlineOpticalFlowQueueIndex = 0u;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    std::array<VkSemaphore, 3u> queueTimelines{};
    std::array<uint64_t, 3u> lastQueueTimelineValues{};
    uint64_t completedSubmissionSequence = 0u;
    VkSemaphore externalFrameCompletionSemaphore = VK_NULL_HANDLE;
    uint64_t externalFrameCompletionValue = 0u;
    bool frameGenerationAcquireLayoutPending = false;
    VkPhysicalDeviceProperties properties{};
    PFN_vkSetDebugUtilsObjectNameEXT setObjectName = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT beginLabel = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT endLabel = nullptr;
    PFN_vkCmdInsertDebugUtilsLabelEXT insertLabel = nullptr;
    PFN_vkCreateAccelerationStructureKHR createAccelerationStructure = nullptr;
    PFN_vkDestroyAccelerationStructureKHR destroyAccelerationStructure = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR getAccelerationStructureBuildSizes = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR getAccelerationStructureDeviceAddress = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR cmdBuildAccelerationStructures = nullptr;
    PFN_vkCmdCopyAccelerationStructureKHR cmdCopyAccelerationStructure = nullptr;
    PFN_vkCmdWriteAccelerationStructuresPropertiesKHR cmdWriteAccelerationStructuresProperties = nullptr;
    PFN_vkCreateMicromapEXT createMicromap = nullptr;
    PFN_vkDestroyMicromapEXT destroyMicromap = nullptr;
    PFN_vkGetMicromapBuildSizesEXT getMicromapBuildSizes = nullptr;
    PFN_vkCmdBuildMicromapsEXT cmdBuildMicromaps = nullptr;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent{};
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    bool immediatePresentSupported = false;
    bool vsyncEnabled = true;
    std::vector<RhiTextureHandle> swapchainTextures;
    std::vector<RhiTextureViewHandle> swapchainViews;
    std::vector<RhiTextureHandle> depthTextures;
    std::vector<RhiTextureViewHandle> depthViews;
    std::vector<bool> swapchainImageInitialized;
    std::vector<VkSemaphore> presentReadySemaphores;
    std::array<FrameContext, 2u> frames{};
    uint32_t frameSlot = 0u;
    uint32_t acquiredImage = UINT32_MAX;
    uint64_t acquiredFrameIndex = 0u;
    bool frameAcquired = false;
    bool frameSubmitted = false;
    bool frameImageAvailableWaited = false;
    uint64_t frameLastGraphicsSequence = 0u;
    bool swapchainDirty = false;
    bool surfaceLost = false;
    uint32_t requestedWidth = 1u;
    uint32_t requestedHeight = 1u;
    RhiHandleAllocator<RhiBufferHandle> bufferHandles;
    RhiHandleAllocator<RhiTextureHandle> textureHandles;
    RhiHandleAllocator<RhiTextureViewHandle> textureViewHandles;
    RhiHandleAllocator<RhiSamplerHandle> samplerHandles;
    RhiHandleAllocator<RhiShaderHandle> shaderHandles;
    RhiHandleAllocator<RhiBindGroupLayoutHandle> bindGroupLayoutHandles;
    RhiHandleAllocator<RhiPipelineLayoutHandle> pipelineLayoutHandles;
    RhiHandleAllocator<RhiPipelineHandle> pipelineHandles;
    RhiHandleAllocator<RhiBindGroupHandle> bindGroupHandles;
    RhiHandleAllocator<RhiQueryPoolHandle> queryPoolHandles;
    RhiHandleAllocator<RhiAccelerationStructureHandle> accelerationStructureHandles;
    RhiHandleAllocator<RhiMicromapHandle> micromapHandles;
    RhiHandleAllocator<RhiMemoryHandle> textureMemoryHandles;
    std::unordered_map<uint64_t, Buffer> buffers;
    std::unordered_map<uint64_t, Texture> textures;
    std::unordered_map<uint64_t, TextureView> textureViews;
    std::unordered_map<uint64_t, Sampler> samplers;
    std::unordered_map<uint64_t, Shader> shaders;
    std::unordered_map<uint64_t, BindGroupLayout> bindGroupLayouts;
    std::unordered_map<uint64_t, PipelineLayout> pipelineLayouts;
    std::unordered_map<uint64_t, Pipeline> pipelines;
    std::unordered_map<uint64_t, BindGroup> bindGroups;
    std::unordered_map<uint64_t, QueryPool> queryPools;
    std::unordered_map<uint64_t, AccelerationStructure> accelerationStructures;
    std::unordered_map<uint64_t, Micromap> micromaps;
    std::unordered_map<uint64_t, TextureMemory> textureMemories;
    std::deque<DeferredBuffer> deferredBuffers;
    std::deque<DeferredImage> deferredImages;
    std::deque<DeferredMemory> deferredMemories;
    std::deque<DeferredObject> deferredObjects;
    std::deque<PendingList> pendingLists;
    std::deque<PendingSubmission> pendingSubmissions;
    std::mutex commandRegistryMutex;
    std::set<RhiCommandList*> commandLists;
    std::set<VkRhiCommandListPool*> commandListPools;
    /// Guards every resource registry map, its handle allocator, and the
    /// deferred-destruction queues. Command recording takes shared locks on
    /// its per-command handle lookups while resource creation/destruction
    /// takes exclusive locks, allowing render-graph batches to record on
    /// worker threads concurrently with lazy resource creation.
    /// Lock order: commandRegistryMutex before resourceRegistryMutex.
    mutable std::shared_mutex resourceRegistryMutex;
};

struct VkRhiCommandListData {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    RhiCommandListType type = RhiCommandListType::Graphics;
    RhiCommandListState state = RhiCommandListState::Initial;
    std::thread::id ownerThread;
    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    RhiPipelineLayoutHandle pipelineLayoutHandle{};
    uint64_t pendingSequence = 0u;
    bool rendering = false;
    bool graphicsPipelineBound = false;
    uint32_t renderingTargetWidth = 0u;
    uint32_t renderingTargetHeight = 0u;
    bool valid = true;
    /// Set when GPU completion was observed on a foreign thread. The actual
    /// vkResetCommandBuffer requires external synchronization of the owning
    /// command pool, so the owner thread performs it on its next acquire.
    std::atomic<bool> completedAwaitingReset{false};
    std::vector<std::pair<VkBuffer, VmaAllocation>> transientBuffers;
    std::vector<uint32_t> initializedSwapchainImages;
    VkRhiCommandResourceReferences resourceReferences;
};

namespace {
template <typename Map, typename Handle> [[nodiscard]] auto* findRecord(Map& map, const Handle handle) {
    const auto it = map.find(handleKey(handle));
    return it == map.end() ? nullptr : &it->second;
}
template <typename Map, typename Handle> [[nodiscard]] const auto* findRecord(const Map& map, const Handle handle) {
    const auto it = map.find(handleKey(handle));
    return it == map.end() ? nullptr : &it->second;
}
void nameObject(VkRhiDeviceData& data, VkObjectType type, uint64_t object, const char* name) {
    if (data.setObjectName == nullptr || object == 0u || name == nullptr || name[0] == '\0')
        return;
    const VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, nullptr, type, object,
                                             name};
    data.setObjectName(data.device, &info);
}
[[nodiscard]] VkQueue queueForType(VkRhiDeviceData& data, RhiQueueType type) {
    switch (type) {
    case RhiQueueType::Graphics: return data.graphicsQueue;
    case RhiQueueType::Compute: return data.computeQueue;
    case RhiQueueType::Transfer: return data.transferQueue;
    case RhiQueueType::Present: return data.presentQueue;
    }
    return VK_NULL_HANDLE;
}

[[nodiscard]] size_t queueTimelineIndex(const RhiQueueType type) {
    switch (type) {
    case RhiQueueType::Graphics: return 0u;
    case RhiQueueType::Compute: return 1u;
    case RhiQueueType::Transfer: return 2u;
    case RhiQueueType::Present: return 3u;
    }
    return 3u;
}
} // namespace

#endif // MECRAFT_VK_RHI_INTERNAL_H
