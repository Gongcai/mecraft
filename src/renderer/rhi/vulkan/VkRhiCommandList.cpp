#include "renderer/rhi/vulkan/VkRhiCommandList.h"

// Vulkan command-list implementation is colocated with VkRhiDevice.cpp so the
// command recorder can access native resource records without exposing Vulkan
// objects through the backend-independent RHI headers.
