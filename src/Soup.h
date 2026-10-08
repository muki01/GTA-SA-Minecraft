#pragma once
// GTA's visible geometry around a spot: the triangles of the building models there (what the player sees, not
// their simpler collision), for exact line tests. Breaking into buildings decides with it what is solid and how
// thick a wall, a deck or a road is.

#include <vector>

#include "ModCommon.h"

namespace mc {

struct SoupFace {
    float at;       // where the line crosses it (on the line's axis)
    int8_t facing;  // +1: the face looks along +axis, -1: along -axis
    uint16_t block; // what its texture looks like (0 = unknown)
    CVector n;      // its normal (outwards)
};

// makes sure the models in this box are loaded (whole: a large building's far side counts too)
void SoupPrepare(const CVector& lo, const CVector& hi);
// every face the line along `axis` through `p` crosses, sorted (the model as it was, holes or not)
void SoupLine(int axis, const CVector& p, std::vector<SoupFace>& out);
// inside a GTA model as it was before anything was cut out of it (the six axis directions vote: the first face each
// meets is seen from behind or from the front), but never in a hole
bool SoupSolidAt(const CVector& p);
// forget everything (world changed)
void SoupClear();

} // namespace mc
