#ifndef MECRAFT_IAPPSTATE_H
#define MECRAFT_IAPPSTATE_H

struct GpuFrameStats;

class IAppState {
public:
    virtual ~IAppState() = default;

    virtual void onEnter() {}
    virtual void onExit() {}

    virtual void update(double frameTime, double& accumulator) = 0;
    virtual void render(double frameTime) = 0;

    /// Returns the latest completed gameplay GPU timing frame.
    /// @return Non-owning statistics pointer, or null for states without a renderer.
    [[nodiscard]] virtual const GpuFrameStats* gpuFrameStats() const = 0;

#ifdef MECRAFT_DEBUG
    virtual void recordPollEvents(double ms,
                                  unsigned keyEvents,
                                  unsigned mouseButtonEvents,
                                  unsigned cursorPosEvents,
                                  unsigned scrollEvents,
                                  unsigned charEvents,
                                  double inputCallbackMs,
                                  double cursorPosCallbackMs,
                                  double imguiCallbackMs,
                                  double imguiCursorPosCallbackMs,
                                  double imguiCursorPosBackendMs,
                                  double imguiWndProcMs,
                                  double imguiWndProcSlowestMs,
                                  unsigned imguiWndProcSlowestMsg,
                                  unsigned imguiWndProcCount) {
        (void)ms;
        (void)keyEvents;
        (void)mouseButtonEvents;
        (void)cursorPosEvents;
        (void)scrollEvents;
        (void)charEvents;
        (void)inputCallbackMs;
        (void)cursorPosCallbackMs;
        (void)imguiCallbackMs;
        (void)imguiCursorPosCallbackMs;
        (void)imguiCursorPosBackendMs;
        (void)imguiWndProcMs;
        (void)imguiWndProcSlowestMs;
        (void)imguiWndProcSlowestMsg;
        (void)imguiWndProcCount;
    }
    virtual void recordAppUpdateDispatch(double ms) { (void)ms; }
    virtual void recordAppRenderDispatch(double ms) { (void)ms; }
#endif
};

#endif //MECRAFT_IAPPSTATE_H
