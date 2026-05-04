#ifndef MECRAFT_IAPPSTATE_H
#define MECRAFT_IAPPSTATE_H

class IAppState {
public:
    virtual ~IAppState() = default;

    virtual void onEnter() {}
    virtual void onExit() {}

    virtual void update(double frameTime, double& accumulator) = 0;
    virtual void render(double frameTime) = 0;
};

#endif //MECRAFT_IAPPSTATE_H
