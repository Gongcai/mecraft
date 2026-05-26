#ifndef MECRAFT_RENDER_PASS_H
#define MECRAFT_RENDER_PASS_H

#include <glad/glad.h>

class Shader;
class ResourceMgr;

/// Abstract base class for render passes.
/// Each pass owns its shaders and manages its own GL state.
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

protected:
    /// Draw a fullscreen triangle using the given shader.
    /// Caller must have already bound uniforms/textures before calling.
    /// @param vao The fullscreen triangle VAO (typically from DeferredRenderTargets)
    /// @param shader The active shader program
    static void renderFullscreen(GLuint vao, Shader& shader);
};

#endif // MECRAFT_RENDER_PASS_H
