#ifndef MECRAFT_IAPPSTATE_H
#define MECRAFT_IAPPSTATE_H

class IAppState {
public:
    virtual ~IAppState() = default;

    virtual void onEnter() {}
    virtual void onExit() {}

    virtual void update(double frameTime, double& accumulator) = 0;
    virtual void render(double frameTime) = 0;

#ifdef MECRAFT_DEBUG
    virtual void recordPollEvents(double ms,
                                  unsigned keyEvents,
                                  unsigned mouseButtonEvents,
                                  unsigned cursorPosEvents,
                                  unsigned scrollEvents,
                                  unsigned charEvents) {
        (void)ms;
        (void)keyEvents;
        (void)mouseButtonEvents;
        (void)cursorPosEvents;
        (void)scrollEvents;
        (void)charEvents;
    }
    virtual void recordAppUpdateDispatch(double ms) { (void)ms; }
    virtual void recordAppRenderDispatch(double ms) { (void)ms; }
#endif
};

#endif //MECRAFT_IAPPSTATE_H
