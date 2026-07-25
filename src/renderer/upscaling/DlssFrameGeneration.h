#ifndef MECRAFT_DLSS_FRAME_GENERATION_H
#define MECRAFT_DLSS_FRAME_GENERATION_H

#include <memory>

class PresentationBackend;
class RhiDevice;

/// Creates the Streamline DLSS Frame Generation presentation backend.
/// @param rhiDevice Initialized Vulkan RHI device that owns the main swapchain.
/// @return A DLSS-G-capable presentation backend, or null for a non-Vulkan device.
[[nodiscard]] std::unique_ptr<PresentationBackend>
createDlssFrameGenerationPresentationBackend(RhiDevice& rhiDevice);

#endif // MECRAFT_DLSS_FRAME_GENERATION_H
