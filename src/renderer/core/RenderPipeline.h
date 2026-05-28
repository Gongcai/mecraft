#ifndef MECRAFT_RENDER_PIPELINE_H
#define MECRAFT_RENDER_PIPELINE_H

#include "FrameContext.h"
#include "FrameOutput.h"
#include "RenderSettings.h"

struct SharedRenderResources;

/// Abstract render pipeline interface
/// Forward and Deferred pipelines implement this interface
class RenderPipeline {
public:
    virtual ~RenderPipeline() = default;

    /// Initialize pipeline resources
    /// @param shared Shared render resources (terrain cache, sky renderer, etc.)
    virtual void init(SharedRenderResources& shared) = 0;

    /// Shutdown and release pipeline resources
    virtual void shutdown() = 0;

    /// Render a frame
    /// @param ctx Unified frame context
    /// @param settings Render settings for this frame
    /// @return Frame output with render targets and metadata
    virtual FrameOutput renderFrame(const FrameContext& ctx, const RenderSettings& settings) = 0;

    /// Get pipeline name for UI/debug display
    virtual const char* name() const = 0;

    /// Query: does this pipeline support deferred rendering features?
    virtual bool supportsDeferred() const = 0;

    /// Query: does this pipeline support debug visualization?
    virtual bool supportsDebugView() const = 0;
};

#endif // MECRAFT_RENDER_PIPELINE_H
