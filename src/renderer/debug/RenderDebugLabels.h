#ifndef MECRAFT_RENDER_DEBUG_LABELS_H
#define MECRAFT_RENDER_DEBUG_LABELS_H

#include <cstdint>

namespace renderer::debug {

// Returns true if KHR_debug labels are active at runtime.
// On non-debug builds, always returns false (all calls compile to no-ops).
// On debug builds, checks the MEC_RENDER_LABELS env var (default: enabled).
bool labelsEnabled();

// Push/pop a named debug group (shows as a collapsible section in RenderDoc).
void pushGroup(const char* name);
void popGroup();

// Insert a one-shot event marker into the debug timeline.
void insertEvent(const char* name);

// Label native graphics objects with human-readable names (visible in RenderDoc Resource Inspector).
void labelTexture(uint32_t id, const char* name);

// RAII scoped debug group — automatically pops on destruction.
class ScopedDebugGroup {
public:
    explicit ScopedDebugGroup(const char* name);
    ~ScopedDebugGroup();

    ScopedDebugGroup(const ScopedDebugGroup&) = delete;
    ScopedDebugGroup& operator=(const ScopedDebugGroup&) = delete;
};

} // namespace renderer::debug

#endif // MECRAFT_RENDER_DEBUG_LABELS_H
