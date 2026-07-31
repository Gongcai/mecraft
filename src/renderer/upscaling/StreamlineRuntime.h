#ifndef MECRAFT_STREAMLINE_RUNTIME_H
#define MECRAFT_STREAMLINE_RUNTIME_H

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class VulkanRequirementCollector;

/// Aggregated Vulkan requirements returned by all enabled Streamline features.
struct StreamlineVulkanRequirements {
    std::vector<std::string> instanceExtensions;
    std::vector<std::string> deviceExtensions;
    std::vector<std::string> features12;
    std::vector<std::string> features13;
    uint32_t additionalGraphicsQueues = 0u;
    uint32_t additionalComputeQueues = 0u;
    uint32_t opticalFlowQueues = 0u;
    bool hardwareSchedulingRequired = false;
    bool vsyncOffRequired = false;
};

/// Native Vulkan objects and Streamline-owned queue ranges passed after device creation.
struct StreamlineVulkanDeviceInfo {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    uint32_t graphicsQueueIndex = 0u;
    uint32_t computeQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    uint32_t computeQueueIndex = 0u;
    uint32_t opticalFlowQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    uint32_t opticalFlowQueueIndex = 0u;
    bool useNativeOpticalFlow = false;
};

enum class StreamlineDlssMode { Quality, Balanced, Performance, UltraPerformance };

enum class StreamlineReflexMode { Off, LowLatency, LowLatencyWithBoost };

enum class StreamlinePclMarker {
    SimulationStart,
    SimulationEnd,
    RenderSubmitStart,
    RenderSubmitEnd,
    PresentStart,
    PresentEnd,
    TriggerFlash,
    LatencyPing
};

struct StreamlineReflexState {
    bool lowLatencyAvailable = false;
    bool flashIndicatorDriverControlled = false;
    uint32_t statsWindowMessage = 0u;
};

struct StreamlineDlssOptimalSettings {
    uint32_t renderWidth = 0u;
    uint32_t renderHeight = 0u;
    uint32_t renderWidthMin = 0u;
    uint32_t renderHeightMin = 0u;
    uint32_t renderWidthMax = 0u;
    uint32_t renderHeightMax = 0u;
};

struct StreamlineDlssOptions {
    StreamlineDlssMode mode = StreamlineDlssMode::Quality;
    uint32_t outputWidth = 0u;
    uint32_t outputHeight = 0u;
    float preExposure = 1.0f;
    float exposureScale = 1.0f;
};

struct StreamlineDlssResource {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent{};
    VkImageUsageFlags usage = 0u;
    VkImageAspectFlags aspectMask = 0u;
    uint32_t mipLevels = 0u;
    uint32_t arrayLayers = 0u;
    uint32_t baseMip = 0u;
    uint32_t mipCount = 0u;
    uint32_t baseLayer = 0u;
    uint32_t layerCount = 0u;
};

struct StreamlineDlssFrameConstants {
    std::array<float, 16u> cameraViewToClip{};
    std::array<float, 16u> clipToCameraView{};
    std::array<float, 16u> clipToPrevClip{};
    std::array<float, 16u> prevClipToClip{};
    std::array<float, 2u> jitterOffset{};
    std::array<float, 2u> motionVectorScale{};
    std::array<float, 3u> cameraPosition{};
    std::array<float, 3u> cameraUp{};
    std::array<float, 3u> cameraRight{};
    std::array<float, 3u> cameraForward{};
    float cameraNear = 0.0f;
    float cameraFar = 0.0f;
    float verticalFovRadians = 0.0f;
    float cameraAspectRatio = 1.0f;
    bool depthInverted = false;
    bool reset = true;
};

struct StreamlineDlssDispatchInfo {
    uint64_t frameIndex = 0u;
    uint32_t viewport = 0u;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    StreamlineDlssFrameConstants constants;
    StreamlineDlssResource inputColor;
    StreamlineDlssResource outputColor;
    StreamlineDlssResource depth;
    StreamlineDlssResource motionVectors;
    StreamlineDlssResource exposure;
};

struct StreamlineDlssFrameGenerationOptions {
    bool enabled = false;
    uint32_t renderWidth = 0u;
    uint32_t renderHeight = 0u;
    uint32_t outputWidth = 0u;
    uint32_t outputHeight = 0u;
    uint32_t backBufferCount = 0u;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkFormat motionVectorFormat = VK_FORMAT_UNDEFINED;
    VkFormat hudlessFormat = VK_FORMAT_UNDEFINED;
    VkFormat uiFormat = VK_FORMAT_UNDEFINED;
};

struct StreamlineDlssFrameGenerationState {
    uint64_t estimatedVramUsageBytes = 0u;
    uint32_t status = 0u;
    uint32_t minimumWidthOrHeight = 0u;
    uint32_t framesPresented = 0u;
    uint32_t maximumGeneratedFrames = 0u;
    bool vsyncAvailable = false;
    void* inputsProcessingCompletionFence = nullptr;
    uint64_t inputsProcessingCompletionValue = 0u;
};

struct StreamlineDlssFrameGenerationDispatchInfo {
    uint64_t frameIndex = 0u;
    uint32_t viewport = 0u;
    StreamlineDlssFrameConstants constants;
    StreamlineDlssResource depth;
    StreamlineDlssResource motionVectors;
    StreamlineDlssResource hudlessColor;
    StreamlineDlssResource uiColorAndAlpha;
};

/// Owns the process-wide Streamline runtime used by the Windows Vulkan backend.
class StreamlineRuntime final {
public:
    [[nodiscard]] static StreamlineRuntime& instance();

    /// Verifies and loads the signed Streamline runtime, then queries feature requirements.
    /// @param runtimeDirectory Absolute directory containing the production Streamline DLLs.
    /// @return True when required features initialized and optional feature support was recorded.
    bool initialize(const std::filesystem::path& runtimeDirectory = {});

    /// Adds queried Streamline extensions, features, and queue counts to Vulkan planning.
    /// @param collector Collector used by the Vulkan backend before instance creation.
    [[nodiscard]] bool appendVulkanRequirements(VulkanRequirementCollector& collector) const;

    /// Provides the created Vulkan instance, device, and reserved queue ranges to Streamline.
    /// @param info Native Vulkan objects and queue indices reserved for Streamline.
    /// @return True when Streamline accepted the Vulkan device configuration.
    bool setVulkanDevice(const StreamlineVulkanDeviceInfo& info);

    /// Queries the DLSS render extent selected by the loaded NVIDIA model.
    /// @param options Output dimensions and project quality mode.
    /// @param settings Receives the optimal and dynamic-resolution bounds.
    /// @return True when the DLSS plugin returned valid settings.
    bool queryDlssOptimalSettings(const StreamlineDlssOptions& options, StreamlineDlssOptimalSettings& settings);

    /// Configures DLSS Super Resolution for one Streamline viewport.
    /// @param viewport Stable viewport identifier shared by all DLSS calls.
    /// @param options Output dimensions, quality mode, and HDR exposure values.
    /// @return True when the DLSS plugin accepted the options.
    bool configureDlss(uint32_t viewport, const StreamlineDlssOptions& options);

    /// Tags Vulkan resources, provides common constants, and evaluates DLSS.
    /// @param info Complete frame-scoped Vulkan and temporal reconstruction data.
    /// @return True when every Streamline call completed successfully.
    bool evaluateDlss(const StreamlineDlssDispatchInfo& info);

    /// Releases DLSS resources owned by one Streamline viewport.
    /// @param viewport Stable viewport identifier used during configuration.
    /// @return True when the plugin released the viewport resources.
    bool releaseDlssResources(uint32_t viewport);

    /// Sets common camera and temporal constants once for one tracked frame.
    /// @param frameIndex Application frame identifier shared by Reflex and presentation.
    /// @param viewport Stable Streamline viewport identifier.
    /// @param constants Camera, motion-vector, depth, and reset state.
    /// @return True when constants were accepted or already set for this frame.
    bool setFrameConstants(uint64_t frameIndex, uint32_t viewport, const StreamlineDlssFrameConstants& constants);

    /// Loads or unloads the DLSS Frame Generation plugin at a swapchain boundary.
    /// @param loaded True before creating an interpolated swapchain, false before a native one.
    /// @return True when the plugin load state changed successfully.
    bool setDlssFrameGenerationLoaded(bool loaded);

    /// Configures fixed 2x DLSS Frame Generation for one viewport.
    /// @param viewport Stable viewport identifier shared by tags and presentation markers.
    /// @param options Output, render, swapchain, format, and activation state.
    /// @return True when the DLSS-G plugin accepted the options.
    bool configureDlssFrameGeneration(uint32_t viewport, const StreamlineDlssFrameGenerationOptions& options);

    /// Tags depth, motion vectors, HUD-less color, and premultiplied UI for present-time use.
    /// @param info Complete frame resources and common constants.
    /// @return True when every DLSS-G input was tagged for the tracked frame.
    bool tagDlssFrameGenerationResources(const StreamlineDlssFrameGenerationDispatchInfo& info);

    /// Clears present-time DLSS-G input tags for a frame without valid gameplay resources.
    /// @param frameIndex Application frame identifier shared by present markers.
    /// @param viewport Stable viewport identifier.
    /// @return True when the null tags were accepted.
    bool clearDlssFrameGenerationResources(uint64_t frameIndex, uint32_t viewport);

    /// Queries DLSS-G status, displayed-frame count, and input-completion synchronization.
    /// @param viewport Stable viewport identifier.
    /// @param state Receives current plugin status and completion fence data.
    /// @return True when the state query completed successfully.
    bool queryDlssFrameGenerationState(uint32_t viewport, StreamlineDlssFrameGenerationState& state);

    /// Releases retained DLSS-G resources for one viewport.
    /// @param viewport Stable viewport identifier.
    /// @return True when the plugin released its viewport resources.
    bool releaseDlssFrameGenerationResources(uint32_t viewport);

    /// Configures the process-wide Reflex low-latency mode.
    /// @param mode Off, low-latency, or low-latency with boost.
    /// @param frameLimitMicroseconds Optional Reflex frame limiter period; zero disables it.
    /// @return True when the Reflex plugin accepted the complete option set.
    bool configureReflex(StreamlineReflexMode mode, uint32_t frameLimitMicroseconds = 0u);

    /// Queries Reflex availability and the PCL latency-message identifier.
    /// @param state Receives low-latency, flash-indicator, and message state.
    /// @return True when both Reflex and PCL state queries completed successfully.
    bool queryReflexState(StreamlineReflexState& state);

    /// Starts Streamline tracking for one application frame and invokes Reflex sleep.
    /// @param frameIndex Monotonically increasing application frame identifier.
    /// @return True when Streamline returned a frame token and Reflex sleep completed.
    bool beginReflexFrame(uint32_t frameIndex);

    /// Emits one PCL marker for the currently tracked application frame.
    /// @param frameIndex Frame identifier passed to beginReflexFrame.
    /// @param marker Latency phase or input marker to emit.
    /// @return True when the marker was accepted or was already emitted for this frame.
    bool setPclMarker(uint32_t frameIndex, StreamlinePclMarker marker);

    /// Installs the Win32 message hook used for PCL latency pings and flash triggers.
    /// @param nativeWindowHandle HWND for the main application window.
    /// @return True when the window procedure was chained successfully.
    bool attachLatencyWindow(void* nativeWindowHandle);

    /// Restores the original Win32 window procedure.
    void detachLatencyWindow();

    /// Processes one Win32 message received by the installed latency hook.
    /// @param message Native Win32 message identifier.
    void processLatencyWindowMessage(uint32_t message);

    /// Presents one Vulkan frame through the Streamline interposer hooks.
    /// @param queue Vulkan presentation queue.
    /// @param info Native swapchain presentation description.
    /// @return Vulkan result returned after Streamline before/after-present hooks.
    VkResult presentVulkanFrame(VkQueue queue, const VkPresentInfoKHR& info);

    /// Creates the main Win32 Vulkan surface through the Streamline manual hook.
    VkResult createVulkanWin32Surface(VkInstance instance, void* applicationInstance, void* window,
                                      VkSurfaceKHR& surface);

    /// Destroys the main Vulkan surface through the Streamline manual hook.
    void destroyVulkanSurface(VkInstance instance, VkSurfaceKHR surface);

    /// Creates the main Vulkan swapchain through the Streamline manual hook.
    VkResult createVulkanSwapchain(VkDevice device, const VkSwapchainCreateInfoKHR& info, VkSwapchainKHR& swapchain);

    /// Destroys the main Vulkan swapchain through the Streamline manual hook.
    void destroyVulkanSwapchain(VkDevice device, VkSwapchainKHR swapchain);

    /// Queries Vulkan swapchain images through the Streamline manual hook.
    VkResult getVulkanSwapchainImages(VkDevice device, VkSwapchainKHR swapchain, uint32_t& imageCount, VkImage* images);

    /// Acquires the next Vulkan image through the Streamline manual hook.
    VkResult acquireVulkanImage(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore,
                                VkFence fence, uint32_t& imageIndex);

    /// Waits for the Vulkan device through the Streamline manual hook.
    VkResult waitVulkanDeviceIdle(VkDevice device);

    /// Shuts Streamline down while the Vulkan device and instance are still alive.
    /// @return True when shutdown completed successfully or the runtime was not initialized.
    bool shutdown();

    [[nodiscard]] bool initialized() const;
    [[nodiscard]] bool vulkanDeviceSet() const;
    [[nodiscard]] bool reflexLowLatencyAvailable() const;
    [[nodiscard]] bool dlssFrameGenerationSupported() const;
    [[nodiscard]] bool dlssFrameGenerationLoaded() const;
    [[nodiscard]] const StreamlineVulkanRequirements& vulkanRequirements() const;
    [[nodiscard]] const std::string& lastError() const;

private:
    struct Implementation;

    StreamlineRuntime();
    ~StreamlineRuntime();
    StreamlineRuntime(const StreamlineRuntime&) = delete;
    StreamlineRuntime& operator=(const StreamlineRuntime&) = delete;

    std::unique_ptr<Implementation> m_impl;
};

#endif // MECRAFT_STREAMLINE_RUNTIME_H
