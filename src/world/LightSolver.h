#ifndef MECRAFT_LIGHTSOLVER_H
#define MECRAFT_LIGHTSOLVER_H

#include "LightTypes.h"

class LightSolver {
public:
    static LightResult solve(const LightJob& job);
};

#endif // MECRAFT_LIGHTSOLVER_H

