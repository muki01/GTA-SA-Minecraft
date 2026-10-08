#include "Beds.h"

#include "GameState.h"
#include "Host.h"
#include "Items.h"
#include "Mobs.h"
#include "Shapes.h"
#include "World.h"

namespace mc {

SleepState gSleep;

namespace {
constexpr float kWakeFade = 0.5f;           // 10 ticks
constexpr float kBedTop = 9.0f / 16.0f;

// the foot of the bed with a part in this cell, and its side (false: no whole bed there)
bool FootOf(const Int3& c, Int3* foot, int* meta) {
    const Voxel v = gWorld.Get(c.x, c.y, c.z);
    const int b = VoxBlock(v);
    if (!IsBedBlock(b))
        return false;
    *meta = VoxMeta(v);
    const Int3 o = BedOtherCell(c, b, *meta);
    if (gWorld.GetBlock(o.x, o.y, o.z) != BedOtherBlock(b))
        return false;
    *foot = Block(b).shape == SHAPE_BED ? c : o;
    return true;
}

Vec3 Side(int meta) {
    const Int3& d = FACE_DIR[meta & 3];
    return Vec3((float)d.x, (float)d.y, 0.0f);
}

Vec3 Top(const Int3& foot, int meta, float along) {
    return Vec3(foot.x + 0.5f, foot.y + 0.5f, foot.z + kBedTop) + Side(meta) * along;
}

bool MonstersNear(const Int3& at) {
    for (const Mob& m : gMobs)
        if (m.death < 0.0f && IsMonster(m.kind) && std::fabs(m.pos.x - (at.x + 0.5f)) <= 8.5f &&
            std::fabs(m.pos.y - (at.y + 0.5f)) <= 8.5f && std::fabs(m.pos.z - at.z) <= 5.0f)
            return true;
    return false;
}
} // namespace

bool CanSleepAt(float hours) { return hours >= 18.54f || hours < 5.46f; } // ticks 12542..23459

bool UseBed(const Int3& cell) {
    Int3 foot;
    int meta = 0;
    if (!FootOf(cell, &foot, &meta))
        return false;
    // the bed is the respawn point before anything else is checked (as in Minecraft)
    if (!gSleep.spawnSet || !(gSleep.spawn == foot)) {
        gSleep.spawnSet = true;
        gSleep.spawn = foot;
        ShowMessage("Doğma noktası ayarlandı");
    }
    Host& h = TheHost();
    if (!CanSleepAt(h.ClockHours()) && !h.Thunderstorm()) {
        ShowMessage("Yalnızca geceleri ve fırtınada uyuyabilirsin");
        return true;
    }
    if (gGame.gameMode == MODE_SURVIVAL && MonstersNear(foot)) {
        ShowMessage("Şimdi dinlenemezsin; yakında canavarlar var");
        return true;
    }
    gSleep.asleep = true;
    gSleep.timer = 0.0f;
    gSleep.wake = 0.0f;
    gSleep.bed = foot;
    gSleep.meta = meta;
    return true;
}

void WakeUp() {
    if (!gSleep.asleep)
        return;
    gSleep.fadeFrom = SleepFade();
    gSleep.asleep = false;
    gSleep.wake = kWakeFade;
}

bool SleepTick(float dt, bool leave) {
    if (!gSleep.asleep) {
        gSleep.wake = std::max(0.0f, gSleep.wake - dt);
        return false;
    }
    Int3 foot;
    int meta = 0;
    if (leave || !FootOf(gSleep.bed, &foot, &meta) || !(foot == gSleep.bed)) {
        WakeUp();
        return false;
    }
    gSleep.timer += dt;
    if (gSleep.timer < kFallAsleep)
        return true;
    // the night is over: the morning, a clear sky
    Host& h = TheHost();
    const float now = h.ClockHours();
    h.SetClock(6.0f, now >= 12.0f);
    if (h.Thunderstorm())
        h.ClearWeather();
    WakeUp();
    return false;
}

bool SleepSpot(Vec3* feet) {
    if (!gSleep.asleep)
        return false;
    *feet = Top(gSleep.bed, gSleep.meta, 0.5f);
    return true;
}

bool SleepPose(Vec3* feet, Vec3* headDir) {
    if (!gSleep.asleep)
        return false;
    *headDir = Side(gSleep.meta);
    *feet = Top(gSleep.bed, gSleep.meta, -0.4f) + Vec3(0, 0, 0.12f);
    return true;
}

float SleepFade() {
    if (gSleep.asleep)
        return std::min(1.0f, gSleep.timer / kFallAsleep);
    return gSleep.fadeFrom * gSleep.wake / kWakeFade;
}

void BedRespawn() {
    gSleep.asleep = false;
    gSleep.wake = 0.0f;
    if (!gSleep.spawnSet)
        return;
    Int3 foot;
    int meta = 0;
    const Int3 above{ gSleep.spawn.x, gSleep.spawn.y, gSleep.spawn.z + 1 };
    if (!FootOf(gSleep.spawn, &foot, &meta) || IsSolidBlock(gWorld.GetBlock(above.x, above.y, above.z))) {
        gSleep.spawnSet = false;
        ShowMessage("Yatağın yok ya da önü kapalı");
        return;
    }
    TheHost().MovePlayer(Top(foot, meta, 0.5f) + Vec3(0, 0, 1.0f));
}

} // namespace mc
