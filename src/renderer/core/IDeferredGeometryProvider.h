#ifndef MECRAFT_I_DEFERRED_GEOMETRY_PROVIDER_H
#define MECRAFT_I_DEFERRED_GEOMETRY_PROVIDER_H

#include <glm/glm.hpp>

class FrameContext;
class RhiCommandList;

/// Supplies non-world geometry to the shared deferred rendering pipeline.
class IDeferredGeometryProvider {
public:
    virtual ~IDeferredGeometryProvider() = default;

    /// Uploads frame resources required before the G-buffer rendering scope begins.
    /// @param commandList Graphics command list recording the G-buffer pass.
    /// @param context Camera, timing, and environment state for the current frame.
    /// @return True when every geometry resource is ready for drawing.
    [[nodiscard]] virtual bool prepareGBuffer(
        RhiCommandList& commandList,
        const FrameContext& context) = 0;

    /// Draws opaque geometry into the active G-buffer rendering scope.
    /// @param commandList Command list with all deferred attachments bound.
    /// @param viewProjection Current raster view-projection matrix.
    /// @param previousViewProjection Previous view-projection used for velocity.
    virtual void renderToGBuffer(
        RhiCommandList& commandList,
        const glm::mat4& viewProjection,
        const glm::mat4& previousViewProjection) = 0;

    /// Draws opaque geometry into the active cascade shadow depth scope.
    /// @param commandList Command list with the selected cascade depth layer bound.
    /// @param shadowViewProjection Light-space view-projection for the cascade.
    virtual void renderToShadowMap(
        RhiCommandList& commandList,
        const glm::mat4& shadowViewProjection) = 0;
};

#endif // MECRAFT_I_DEFERRED_GEOMETRY_PROVIDER_H
