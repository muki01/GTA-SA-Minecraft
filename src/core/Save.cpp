#include "Save.h"

#include "Beds.h"
#include "GameState.h"
#include "Inventory.h"
#include "Survival.h"
#include "World.h"

namespace mc {

namespace {
const uint32_t kSaveMagic = 0x4153434D; // "MCSA"
const uint32_t kSaveVersion = 4; // 3: item ids moved to 1024, xp, the host's sections; 4: the bed to respawn at
} // namespace

void WriteWorldFile(FILE* f, int hostValue) {
    fwrite(&kSaveMagic, 4, 1, f);
    fwrite(&kSaveVersion, 4, 1, f);
    int32_t vals[5] = { gGame.gameMode, gInv.selected, hostValue, gGame.cameraMode, FIRST_ITEM };
    fwrite(vals, sizeof(vals), 1, f);
    fwrite(&gSurvival.food, 4, 1, f);
    fwrite(&gSurvival.saturation, 4, 1, f);
    for (auto& s : gInv.slots)
        WriteStack(f, s);
    for (auto& s : gInv.armor)
        WriteStack(f, s);
    WriteStack(f, gInv.offhand);
    gWorld.Write(f);
    int32_t xp[2] = { gSurvival.xpLevel, gSurvival.xpTotal };
    fwrite(xp, sizeof(xp), 1, f);
    fwrite(&gSurvival.xpProgress, 4, 1, f);
    int32_t bed[4] = { gSleep.spawnSet ? 1 : 0, gSleep.spawn.x, gSleep.spawn.y, gSleep.spawn.z };
    fwrite(bed, sizeof(bed), 1, f);
}

bool ReadWorldFile(FILE* f, WorldFileInfo& info) {
    uint32_t magic = 0, version = 0;
    fread(&magic, 4, 1, f);
    fread(&version, 4, 1, f);
    info.version = version;
    if (magic != kSaveMagic || version < 2 || version > kSaveVersion)
        return false;
    gSleep = SleepState();
    int32_t vals[5] = {};
    fread(vals, sizeof(vals), 1, f);
    gGame.gameMode = vals[0] == MODE_CREATIVE ? MODE_CREATIVE : MODE_SURVIVAL;
    gInv.selected = std::clamp(vals[1], 0, 8);
    info.hostValue = vals[2];
    gGame.cameraMode = std::clamp(vals[3], 0, 2);
    SetItemIdMigration(version == 2 ? 385 : vals[4]); // items used to start at 385
    fread(&gSurvival.food, 4, 1, f);
    fread(&gSurvival.saturation, 4, 1, f);
    for (auto& s : gInv.slots)
        s = ReadStack(f);
    for (auto& s : gInv.armor)
        s = ReadStack(f);
    gInv.offhand = ReadStack(f);
    info.blocksOk = gWorld.Read(f);
    if (info.blocksOk && version >= 3) {
        int32_t xp[2] = {};
        if (fread(xp, sizeof(xp), 1, f) == 1 && fread(&gSurvival.xpProgress, 4, 1, f) == 1) {
            gSurvival.xpLevel = std::max(0, xp[0]);
            gSurvival.xpTotal = std::max(0, xp[1]);
            gSurvival.xpProgress = std::clamp(gSurvival.xpProgress, 0.0f, 0.999f);
        }
        int32_t bed[4] = {};
        if (version >= 4 && fread(bed, sizeof(bed), 1, f) == 1 && bed[0]) {
            gSleep.spawnSet = true;
            gSleep.spawn = Int3{ bed[1], bed[2], bed[3] };
        }
        info.hostPart = true;
    }
    SetItemIdMigration(0);
    return true;
}

} // namespace mc
