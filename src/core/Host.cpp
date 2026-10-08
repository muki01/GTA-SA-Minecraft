#include "Host.h"

#include "World.h"

namespace mc {

namespace {
Host* gHost = nullptr;
} // namespace

void SetHost(Host* host) { gHost = host; }

Host& TheHost() {
    static Host none;
    return gHost ? *gHost : none;
}

bool GroundBelow(const Vec3& from, float maxDrop, float* zOut) {
    float best = -1e9f;
    bool found = TheHost().GroundBelow(from, maxDrop, &best);
    if (!found)
        best = -1e9f;
    const int bx = FloorI(from.x), by = FloorI(from.y);
    const int z0 = FloorI(from.z), z1 = FloorI(from.z - std::min(maxDrop, 48.0f));
    for (int z = z0; z >= z1; --z) {
        float capTop;
        if (TheHost().CapAt({ bx, by, z }, &capTop) && capTop <= from.z + 0.01f) {
            if (capTop > best) {
                best = capTop;
                found = true;
            }
            break;
        }
        if (gWorld.IsSolid(bx, by, z)) {
            if (z + 1.0f > best) {
                best = z + 1.0f;
                found = true;
            }
            break;
        }
    }
    if (found && zOut)
        *zOut = best;
    return found;
}

} // namespace mc
