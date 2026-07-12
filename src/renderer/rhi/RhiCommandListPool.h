#ifndef MECRAFT_RHI_COMMAND_LIST_POOL_H
#define MECRAFT_RHI_COMMAND_LIST_POOL_H

#include "renderer/rhi/RhiTypes.h"

class RhiCommandList;

class RhiCommandListPool {
public:
    /// Command pools must be destroyed on the thread that created them.
    virtual ~RhiCommandListPool() = default;

    /// Acquires an Initial command list owned by the calling thread.
    /// @param type Queue capability contract for commands recorded into the list.
    /// @return A stable command-list pointer, or nullptr when acquisition is invalid.
    [[nodiscard]] virtual RhiCommandList* acquire(RhiCommandListType type) = 0;

    /// Discards Initial and Executable lists while rejecting Recording and Pending lists.
    /// @return True when every list in the pool was reset for reuse.
    virtual bool reset() = 0;
};

#endif // MECRAFT_RHI_COMMAND_LIST_POOL_H
