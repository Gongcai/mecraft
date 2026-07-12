#ifndef MECRAFT_RENDER_PASS_H
#define MECRAFT_RENDER_PASS_H

/// Abstract base class for render passes.
/// Each pass owns its rendering resources and records its own commands.
class RenderPass {
public:
    virtual ~RenderPass() = default;

    /// Release any pass-specific resources (if needed).
    /// Called during Renderer::shutdown().
    virtual void shutdown() = 0;

    /// Human-readable name for debug/stats display.
    [[nodiscard]] virtual const char* name() const = 0;
};

#endif // MECRAFT_RENDER_PASS_H
