#ifndef MECRAFT_RENDER_PASS_H
#define MECRAFT_RENDER_PASS_H

class ResourceMgr;

/// Abstract base class for render passes.
/// Each pass owns its rendering resources and records its own commands.
class RenderPass {
public:
    virtual ~RenderPass() = default;

    /// Load shaders and acquire resource handles.
    /// Called once during Renderer::init().
    /// @param resourceMgr Resource manager for shader/texture loading
    virtual void init(ResourceMgr& resourceMgr) = 0;

    /// Release any pass-specific resources (if needed).
    /// Called during Renderer::shutdown().
    virtual void shutdown() = 0;

    /// Human-readable name for debug/stats display.
    [[nodiscard]] virtual const char* name() const = 0;
};

#endif // MECRAFT_RENDER_PASS_H
