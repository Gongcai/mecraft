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

    void reference(const RhiBufferHandle handle) { addUnique(buffers, handle); }
    void reference(const RhiTextureHandle handle) { addUnique(textures, handle); }
    void reference(const RhiTextureViewHandle handle) { addUnique(textureViews, handle); }
    void reference(const RhiSamplerHandle handle) { addUnique(samplers, handle); }
    void reference(const RhiBindGroupLayoutHandle handle) {
        addUnique(bindGroupLayouts, handle);
    }
    void reference(const RhiPipelineLayoutHandle handle) {
        addUnique(pipelineLayouts, handle);
    }
    void reference(const RhiPipelineHandle handle) { addUnique(pipelines, handle); }
    void reference(const RhiBindGroupHandle handle) { addUnique(bindGroups, handle); }
    void reference(const RhiQueryPoolHandle handle) { addUnique(queryPools, handle); }

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

    [[nodiscard]] bool empty() const {
        return buffers.empty() && textures.empty() && textureViews.empty() &&
               samplers.empty() && bindGroupLayouts.empty() &&
               pipelineLayouts.empty() && pipelines.empty() &&
               bindGroups.empty() && queryPools.empty();
    }

private:
    template <typename Handle>
    static void addUnique(std::vector<Handle>& handles, const Handle handle) {
        if (!handle.isValid()) {
            return;
        }
        const auto existing = std::find_if(
            handles.begin(), handles.end(), [handle](const Handle candidate) {
                return candidate.index == handle.index &&
                       candidate.generation == handle.generation;
            });
        if (existing == handles.end()) {
            handles.push_back(handle);
        }
    }
};

struct VkRhiDeviceData {
    struct Buffer { VkBuffer buffer = VK_NULL_HANDLE; VmaAllocation allocation = VK_NULL_HANDLE; RhiBufferDesc desc{}; void* mapped = nullptr; VkRhiResourceLifetime lifetime{}; };
    struct Texture { VkImage image = VK_NULL_HANDLE; VmaAllocation allocation = VK_NULL_HANDLE; RhiTextureDesc desc{}; bool swapchainOwned = false; VkRhiResourceLifetime lifetime{}; };
    struct TextureView { VkImageView view = VK_NULL_HANDLE; RhiTextureViewDesc desc{}; VkRhiResourceLifetime lifetime{}; };
    struct Sampler { VkSampler sampler = VK_NULL_HANDLE; VkRhiResourceLifetime lifetime{}; };
    struct Shader { VkShaderModule module = VK_NULL_HANDLE; RhiShaderStage stage = RhiShaderStage::Vertex; std::string entryPoint; renderer::rhi::RhiShaderReflection reflection; VkRhiResourceLifetime lifetime{}; };
    struct BindGroupLayout { VkDescriptorSetLayout layout = VK_NULL_HANDLE; RhiBindGroupLayoutDesc desc{}; VkRhiResourceLifetime lifetime{}; };
    struct PipelineLayout { VkPipelineLayout layout = VK_NULL_HANDLE; RhiPipelineLayoutDesc desc{}; VkRhiResourceLifetime lifetime{}; };
    struct Pipeline { VkPipeline pipeline = VK_NULL_HANDLE; VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; RhiPipelineLayoutHandle layoutHandle{}; VkRhiResourceLifetime lifetime{}; };
    struct BindGroup { VkDescriptorSet set = VK_NULL_HANDLE; RhiBindGroupLayoutHandle layoutHandle{}; RhiBindGroupDesc desc{}; VkRhiResourceLifetime lifetime{}; };
    struct QueryPool { VkQueryPool pool = VK_NULL_HANDLE; uint32_t count = 0u; VkRhiResourceLifetime lifetime{}; };
    struct FrameContext { VkSemaphore imageAvailable = VK_NULL_HANDLE; VkFence fence = VK_NULL_HANDLE; bool fencePending = false; };
    struct DeferredBuffer { uint64_t sequence; VkBuffer buffer; VmaAllocation allocation; };
    struct DeferredImage {
        uint64_t sequence;
        uint64_t textureKey;
        VkImage image;
        VmaAllocation allocation;
    };
    struct DeferredObject { uint64_t sequence = 0u; VkObjectType type = VK_OBJECT_TYPE_UNKNOWN; uint64_t object = 0u; };
    struct PendingList { uint64_t sequence; VkRhiCommandList* list; };

    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    QueueFamilies queueFamilies{};
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    VkQueue transferQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    VkSemaphore timeline = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    PFN_vkSetDebugUtilsObjectNameEXT setObjectName = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT beginLabel = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT endLabel = nullptr;
    PFN_vkCmdInsertDebugUtilsLabelEXT insertLabel = nullptr;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent{};
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
    std::deque<DeferredBuffer> deferredBuffers;
    std::deque<DeferredImage> deferredImages;
    std::deque<DeferredObject> deferredObjects;
    std::deque<PendingList> pendingLists;
    std::mutex commandRegistryMutex;
    std::set<RhiCommandList*> commandLists;
    std::set<VkRhiCommandListPool*> commandListPools;
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
    bool valid = true;
    std::vector<std::pair<VkBuffer, VmaAllocation>> transientBuffers;
    std::vector<uint32_t> initializedSwapchainImages;
    VkRhiCommandResourceReferences resourceReferences;
};

namespace {
template <typename Map, typename Handle>
[[nodiscard]] auto* findRecord(Map& map, const Handle handle) {
    const auto it = map.find(handleKey(handle));
    return it == map.end() ? nullptr : &it->second;
}
template <typename Map, typename Handle>
[[nodiscard]] const auto* findRecord(const Map& map, const Handle handle) {
    const auto it = map.find(handleKey(handle));
    return it == map.end() ? nullptr : &it->second;
}
void nameObject(VkRhiDeviceData& data, VkObjectType type, uint64_t object, const char* name) {
    if (data.setObjectName == nullptr || object == 0u || name == nullptr || name[0] == '\0') return;
    const VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                                             nullptr, type, object, name};
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
} // namespace

#endif // MECRAFT_VK_RHI_INTERNAL_H
