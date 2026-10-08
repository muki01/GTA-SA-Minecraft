#include "Game.h"

#include "CCamera.h"
#include "CColPoint.h"
#include "CCutsceneMgr.h"
#include "CDraw.h"
#include "CFireManager.h"
#include "CHud.h"
#include "CMenuManager.h"
#include "CPad.h"
#include "CGame.h"
#include "CPlayerPed.h"
#include "CPools.h"
#include "CScene.h"
#include "CTheScripts.h"
#include "CVehicle.h"
#include "CWeather.h"
#include "CWorld.h"
#include "RenderWare.h"
#include "common.h"
#include "extensions/ScriptCommands.h"

#include "Blocks.h"
#include "Buildings.h"
#include "Carve.h"
#include "Collision.h"
#include "Combat.h"
#include "GtaCombat.h"
#include "Config.h"
#include "Effects.h"
#include "Fishing.h"
#include "GtaHost.h"
#include "GtaMining.h"
#include "GtaWorld.h"
#include "Gui.h"
#include "Input.h"
#include "Interact.h"
#include "Inventory.h"
#include "Items.h"
#include "GtaMobs.h"
#include "Mobs.h"
#include "Movement.h"
#include "PedSkins.h"
#include "Player3D.h"
#include "Pose.h"
#include "Render3D.h"
#include "Save.h"
#include "Sound.h"
#include "Terrain.h"

namespace mc {

GtaState gGta;

// ================================================================ helpers
namespace {
bool gWasDead = false;
bool gHudFlagsTouched = false;
bool gMeleeHeld = false; // the attack button was used up by a hit: no mining until it is let go
CVector gLastPlayerPos;
bool gWasInAir = false;
bool gInitDone = false;
// field of view override
float gFovWritten = 0.0f;  // what we stored into CDraw::ms_fFOV last frame (0 = nothing)
float gFovInEffect = 0.0f; // the value the last frame was rendered with
float gGameFov = 70.0f;    // the game's own value
float gFovK = 0.75f;       // measured: tan(vertical fov / 2) = k * tan(CDraw fov / 2)
bool gFovKMeasured = false;
constexpr float kFirstPersonNear = 0.1f;
float gSavedNearClip = 0.0f;
bool gNearClipOverridden = false;

CVector Normalized(CVector v) {
    float m = v.Magnitude();
    return m > 1e-5f ? v * (1.0f / m) : CVector(0, 1, 0);
}

// ---------------------------------------------------------------- pad control
void BlockAllPadInput() {
    CPad* pad = CPad::GetPad(0);
    memset(&pad->NewState, 0, sizeof(pad->NewState));
    CPad::NewMouseControllerState.x = 0.0f;
    CPad::NewMouseControllerState.y = 0.0f;
    CPad::NewMouseControllerState.wheelUp = 0;
    CPad::NewMouseControllerState.wheelDown = 0;
}

void BlockActionInput() {
    CPad* pad = CPad::GetPad(0);
    pad->NewState.ButtonCircle = 0; // fire / punch
    pad->OldState.ButtonCircle = 0;
    pad->NewState.RightShoulder1 = 0; // aim
    pad->OldState.RightShoulder1 = 0;
    pad->NewState.LeftShoulder2 = 0;  // weapon cycling
    pad->NewState.RightShoulder2 = 0;
    pad->OldState.LeftShoulder2 = 0;
    pad->OldState.RightShoulder2 = 0;
    CPad::NewMouseControllerState.wheelUp = 0;
    CPad::NewMouseControllerState.wheelDown = 0;
}

// vehicles whose fire button works a weapon of their own (tank, gunships, fighter planes, water cannons)
bool VehicleHasGuns(CVehicle* v) {
    switch (v->m_nModelIndex) {
    case 407: // Fire Truck
    case 425: // Hunter
    case 430: // Predator
    case 432: // Rhino
    case 447: // Seasparrow
    case 464: // RC Baron
    case 476: // Rustler
    case 520: // Hydra
    case 601: // S.W.A.T.
        return true;
    default:
        return false;
    }
}

// At the wheel the mouse buttons are the Minecraft hands: GTA neither fires nor pulls the handbrake on them. Its
// own keys for that (fire: Ctrl, handbrake: Space) work as ever, and so do the guns of vehicles that have some.
void BlockVehicleInput(bool gunVehicle) {
    CPad* pad = CPad::GetPad(0);
    if (ActionDown(ACT_ATTACK) && !ActionDown(ACT_SPRINT) && !gunVehicle) {
        pad->NewState.ButtonCircle = 0;
        pad->OldState.ButtonCircle = 0;
    }
    if (ActionDown(ACT_USE) && !ActionDown(ACT_JUMP)) {
        pad->NewState.RightShoulder1 = 0;
        pad->OldState.RightShoulder1 = 0;
    }
}

void SetGtaHud(bool minecraft) {
    if (minecraft) {
        CTheScripts::bDisplayHud = false;
        CHud::bScriptDontDisplayRadar = !gConfig.showRadar;
        gHudFlagsTouched = true;
    } else if (gHudFlagsTouched) {
        CTheScripts::bDisplayHud = true;
        CHud::bScriptDontDisplayRadar = false;
        gHudFlagsTouched = false;
    }
}

// ---------------------------------------------------------------- hunger, death, creative mode
void UpdateSurvival(float dt, CPlayerPed* ped) {
    float maxH = ped->m_fMaxHealth > 1.0f ? ped->m_fMaxHealth : 100.0f;
    if (gGame.gameMode == MODE_CREATIVE) {
        CreativeTick(ped->m_fHealth, maxH);
        return;
    }
    // running and jumping tire the player: where GTA moves him it is counted here (the Minecraft movement counts
    // its own)
    CVector p = ped->GetPosition();
    if (!ped->bInVehicle && !gGame.gliding && !MovementControllerActive()) {
        CVector delta = p - gLastPlayerPos;
        delta.z = 0;
        float dist = delta.Magnitude();
        if (dt > 0 && dist / dt > 4.5f && dist < 5.0f)
            gSurvival.exhaustion += 0.1f * dist;
        if (ped->bIsInTheAir && !gWasInAir)
            gSurvival.exhaustion += 0.05f;
        gWasInAir = ped->bIsInTheAir;
    }
    gLastPlayerPos = p;
    SurvivalEvents ev;
    HungerTick(dt, ped->m_fHealth, maxH, ev);
}

void HandleDeath(CPlayerPed* ped) {
    bool dead = ped->m_fHealth <= 0.0f;
    if (dead && !gWasDead) {
        StopFlying(ped);
        StopRiding(ped);
        PlayerDied(ped->GetPosition());
    }
    if (!dead && gWasDead)
        gSurvival.Respawn();
    gWasDead = dead;
}

// creative mode: no damage at all, like in Minecraft
bool gProofsSet = false;
void SetCreativeProofs(CPlayerPed* ped, bool on) {
    if (on == gProofsSet)
        return;
    ped->bBulletProof = ped->bFireProof = ped->bCollisionProof = ped->bMeleeProof = ped->bExplosionProof = on;
    gProofsSet = on;
}


bool NearVehicle(CPlayerPed* ped, float r) {
    auto* pool = CPools::ms_pVehiclePool;
    if (!pool)
        return false;
    for (int i = 0; i < pool->m_nSize; ++i)
        if (CVehicle* v = pool->GetAt(i))
            if ((v->GetPosition() - ped->GetPosition()).Magnitude() < r)
                return true;
    return false;
}

// ---------------------------------------------------------------- eating / drinking
// what a glass of milk does in San Andreas, on top of what it does in Minecraft
void MilkDrunk(CPlayerPed* ped, bool hadFireResistance) {
    // milk clears every effect... and the police's memory of you
    plugin::Command<plugin::Commands::CLEAR_WANTED_LEVEL>(0);
    gFireManager.ExtinguishPoint(ped->GetPosition(), 2.5f);
    FireResistanceCleared(ped, hadFireResistance);
    ShowMessage("Süt içtin: etkiler ve aranma seviyen sıfırlandı");
}

// returns true while the player eats or drinks (the eating itself is the core's EatingTick)
bool UpdateEating(float dt, CPlayerPed* ped, bool rmbDown) {
    const bool fireResistance = HasEffect(EFFECT_FIRE_RESISTANCE);
    const EatResult r = EatingTick(dt, rmbDown, gGame.gameMode == MODE_SURVIVAL);
    if (r.finished && Item(r.finished).special == SP_MILK)
        MilkDrunk(ped, fireResistance);
    return r.eating;
}

// ---------------------------------------------------------------- health: armour, totem, hurt flash
void UpdateHealthEffects(float dt, CPlayerPed* ped) {
    const float maxH = ped->m_fMaxHealth > 1.0f ? ped->m_fMaxHealth : 100.0f;
    const bool fireResistance = HasEffect(EFFECT_FIRE_RESISTANCE);
    const float direct = gGta.directDamage;
    gGta.directDamage = 0.0f;
    if (HealthTick(dt, ped->m_fHealth, maxH, direct)) {
        // a totem saved him: GTA's own fire around him goes out
        FireResistanceCleared(ped, fireResistance);
        gFireManager.ExtinguishPoint(ped->GetPosition(), 2.5f);
    }
    if (ped->m_fHealth <= 0.0f && CHud::m_BigMessage)
        for (int i = 0; i < 7; ++i)
            CHud::m_BigMessage[i][0] = 0; // "WASTED": the Minecraft death screen says it
}
} // namespace

// ================================================================ save / load
static std::string WorldPath(int id);
static int gWorldId = -1; // the world being played: -1 none yet, 0 new (not in a save slot), 1..8 save slot

// the core's world file (src/core/Save.h), then what GTA adds: dug ground, broken buildings
static void SaveAll() {
    if (gWorldId < 1)
        return; // only a world that belongs to a GTA save slot is written
    std::string path = WorldPath(gWorldId), tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) {
        Log("ERROR: cannot write %s", tmp.c_str());
        return;
    }
    WriteWorldFile(f, gGta.steve ? 1 : 0);
    TerrainWrite(f);
    BuildingsWrite(f);
    CarveWrite(f);
    fclose(f);
    MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    gWorld.dirty = false;
    Log("Saved: %u chunks", (unsigned)gWorld.chunks.size());
}

static void LoadAll(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        Log("No save found, starting a new world");
        return;
    }
    WorldFileInfo info;
    if (!ReadWorldFile(f, info)) {
        Log("Save has an old format (v%u), starting a new world", info.version);
        fclose(f);
        MoveFileExA(path.c_str(), (path + ".old").c_str(), MOVEFILE_REPLACE_EXISTING);
        return;
    }
    gGta.steve = info.hostValue != 0;
    if (info.hostPart) {
        if (!TerrainRead(f))
            Log("Dug terrain could not be read");
        else if (BuildingsRead(f)) // (saves from before 0.7 end here)
            CarveRead(f);          // (saves from 0.7 end here)
    }
    fclose(f);
    Log("Loaded save: %s, %u chunks", info.blocksOk ? "ok" : "PARTIAL", (unsigned)gWorld.chunks.size());
}

// ================================================================ worlds
// Every GTA save game has its own world file. 0 = a world that was started with "new game" and has not been
// saved into a slot yet.
static int gNextWorld = -2;      // -2: the next session keeps the world it has
static bool gNextFresh = false;

static std::string WorldPath(int id) {
    if (id <= 0)
        return ModPath("world_new.dat");
    char name[32];
    snprintf(name, sizeof(name), "world_slot%d.dat", id);
    return ModPath(name);
}

bool WorldFileExists(int id) { return GetFileAttributesA(WorldPath(id).c_str()) != INVALID_FILE_ATTRIBUTES; }

void SetNextWorld(int id, bool fresh) {
    gNextWorld = std::clamp(id, 0, 8);
    gNextFresh = fresh;
}

static void ResetWorldState() {
    gWorld.Clear();
    TerrainClear();
    CarveClear();
    BuildingsClear();
    for (auto& s : gInv.slots)
        s.Clear();
    for (auto& s : gInv.armor)
        s.Clear();
    gInv.offhand.Clear();
    gInv.cursor.Clear();
    gInv.selected = 0;
    gSurvival.NewGame();
    gGame.gameMode = gConfig.startGameMode == 1 ? MODE_CREATIVE : MODE_SURVIVAL;
}

static int gProcessTicks = 0;    // GameProcess calls so far
static int gActivatedTick = -1;  // gProcessTicks when a world was last activated

// Called when a game session starts (first start, new game, loading a save). The world is always read again from
// what was saved with that GTA save game: whatever was changed after the last save is gone, as in GTA itself.
static void ActivateWorld() {
    if (gWorldId >= 0 && gActivatedTick == gProcessTicks)
        return; // GTA reports one restart twice (before and after it loads the save)
    int want = gNextWorld;
    gNextWorld = -2;
    gNextFresh = false;
    if (want == -2) {
        // started from GTA's own menu: a save game that is being loaded, or a new game
        const uint8_t* menu = reinterpret_cast<const uint8_t*>(&FrontEndMenuManager);
        want = menu[0x60] ? std::clamp((int)(int8_t)menu[0x15F], 0, 7) + 1 : 0; // m_bLoadingData, m_SelectedSlot
    }
    gActivatedTick = gProcessTicks;
    ResetWorldState();
    gWorldId = want;
    if (want == 0) {
        Log("World: a new world (kept only when the game is saved)");
    } else if (WorldFileExists(want)) {
        LoadAll(WorldPath(want));
        Log("World: world %d as it was saved", want);
    } else {
        // the one world of the versions before 0.8 goes to the first save game that is played
        const std::string legacy = ModPath("world.dat");
        if (GetFileAttributesA(legacy.c_str()) != INVALID_FILE_ATTRIBUTES) {
            LoadAll(legacy);
            MoveFileExA(legacy.c_str(), ModPath("world_eski_tek_dunya.dat").c_str(), MOVEFILE_REPLACE_EXISTING);
            Log("World: the old single world now belongs to world %d", want);
            SaveAll();
        } else {
            Log("World: world %d starts empty", want);
        }
    }
    gWorld.dirty = false;
    GiveStarterKit();
}

void WorldSavedToSlot(int slot) {
    if (slot < 1 || slot > 8 || gWorldId < 0)
        return;
    gWorldId = slot;
    SaveAll();
    Log("World: saved with GTA save slot %d", slot);
}

void WorldSlotDeleted(int slot) {
    if (slot < 1 || slot > 8)
        return;
    DeleteFileA(WorldPath(slot).c_str());
    if (gWorldId == slot)
        gWorldId = 0; // the world being played lives on as an unsaved one
}

// ================================================================ main hooks
void GameInit() {
    if (gInitDone)
        return;
    gInitDone = true;
    gGta.enabled = gConfig.startEnabled;
    gGame.gameMode = gConfig.startGameMode == 1 ? MODE_CREATIVE : MODE_SURVIVAL;
    gGta.steve = gConfig.startAsSteve;
    gRules.explosionsBreakBlocks = gConfig.explosionsBreakBlocks;
    gRules.animals = gConfig.animals;
    gRules.maxAnimals = gConfig.maxAnimals;
    gRules.keepInventory = gConfig.keepInventory;
    LoadGtaModelNames();
    InstallCombatHooks();
    InstallMovementHooks();
    ActivateWorld();
}

void GameOnNewSession() {
    CollisionForgetAll();
    gDrops.clear();
    gPrimedTnt.clear();
    gParticles.clear();
    CombatClear();
    GtaCombatClear();
    MobsClear();
    PedSkinsForget();
    FishingClear();
    BlocksClear();
    gXpOrbs.clear();
    ClearEffects();
    GtaMiningClear();
    gSurvival.air = kMaxAir;
    gGta.directDamage = 0.0f;
    gGame.screen = SCREEN_NONE;
    gGame.flying = gGame.gliding = gGame.jumping = false;
    gGame.ridingMob = 0;
    gGame.crossbowSlot = -1;
    gGta.hidPlayer = false;
    gSurvival.healthSeen = -1.0f;
    gHudFlagsTouched = false;
    gProofsSet = false; // a fresh player ped has default flags
    SetLoopSfx(SND_ELYTRA_FLYING, false);
    Log("New GTA session: collision objects will be recreated");
    if (gInitDone)
        ActivateWorld(); // the save game that is being loaded brings its own world
}

void GameShutdown() {
    // nothing is written here: the world was saved with the GTA save game, or it was not saved at all
    ShutdownSfx();
}

void GameProcess() {
    float dt = FrameDelta();
    PollKeys();
    gGame.viewW = (float)RsGlobal.maximumWidth;
    gGame.viewH = (float)RsGlobal.maximumHeight;
    GameTimersTick(dt);
    InteractTick(dt);

    CPlayerPed* ped = FindPlayerPed();
    if (ped)
        SetCreativeProofs(ped, gGta.enabled && gGame.gameMode == MODE_CREATIVE);

    // the world keeps running even when the Minecraft HUD is switched off
    TickFurnaces(dt);
    // items on the ground and experience orbs go to a living player; from a vehicle he reaches further for items
    const bool collects = ped && ped->m_fHealth > 0.0f;
    const Vec3 collector = collects ? Vec3(ped->GetPosition()) : Vec3();
    DropsTick(dt, collects ? &collector : nullptr, ped && ped->bInVehicle ? 2.8f : 1.6f);
    ParticlesTick(dt);
    TntTick(dt);
    ProjectilesTick(dt);
    UpdatePedLoot();
    PollExplosions(dt);
    CollisionUpdate();
    BlocksUpdate(dt);
    BuildingsUpdate(dt);
    CarveUpdate();
    UpdateBrokenVehicles();
    XpTick(dt, collects && gGta.enabled ? &collector : nullptr);
    MobsUpdate(dt, ped);
    if (gGta.enabled && ped && ped->m_fHealth > 0.0f)
        FishingTick(dt);
    else
        FishingClear();
    UpdateLaunchedPeds(dt);
    PedSkinsUpdate(dt, ped);

    ++gProcessTicks;

    if (gGame.screen == SCREEN_NONE) {
        if (KeyPressed(gConfig.keyToggleMode)) {
            gGta.enabled = !gGta.enabled;
            ShowMessage(gGta.enabled ? "Minecraft modu: A\xC3\x87IK" : "Minecraft modu: KAPALI");
            if (!gGta.enabled)
                SetGtaHud(false);
        }
        if (gGta.enabled && ActionPressed(ACT_GAME_MODE)) {
            gGame.gameMode = gGame.gameMode == MODE_SURVIVAL ? MODE_CREATIVE : MODE_SURVIVAL;
            ShowMessage(gGame.gameMode == MODE_CREATIVE ? "Oyun modu: Yarat\xC4\xB1" "c\xC4\xB1"
                                                        : "Oyun modu: Hayatta Kalma");
            gWorld.dirty = true;
        }
        if (KeyPressed(gConfig.keyRadar)) {
            gConfig.showRadar = !gConfig.showRadar;
            SaveConfigValue("Settings", "ShowRadar", gConfig.showRadar ? "1" : "0");
            ShowMessage(gConfig.showRadar ? "Harita (radar): A\xC3\x87IK" : "Harita (radar): KAPALI");
        }
        if (gGta.enabled && KeyPressed(gConfig.keySteve)) {
            gGta.steve = !gGta.steve;
            ShowMessage(gGta.steve ? "Karakter: Steve" : "Karakter: CJ");
            gWorld.dirty = true;
        }
        if (gGta.enabled && ActionPressed(ACT_PERSPECTIVE)) {
            // Minecraft order: first person -> third person back -> third person front
            gGame.cameraMode = gGame.cameraMode == CAM_FIRST ? CAM_THIRD_BACK
                             : gGame.cameraMode == CAM_THIRD_BACK ? CAM_THIRD_FRONT
                                                                  : CAM_FIRST;
        }
    }

    bool blocked = FrontEndMenuManager.m_bMenuActive || CCutsceneMgr::ms_running || TheCamera.m_bWideScreenOn;
    gGta.inWorld = ped && !blocked;
    gGta.hudVisible = gGta.enabled && gGta.inWorld;
    gTargetVisual.show = false;
    if (!gGta.enabled || !ped) {
        if (gGame.screen != SCREEN_NONE)
            CloseScreen();
        if (ped && (gGame.flying || gGame.gliding || gGame.jumping))
            StopFlying(ped);
        gGame.sprinting = false;
        return;
    }
    SetGtaHud(!blocked);
    HandleDeath(ped);
    EffectsUpdate(dt, ped);
    UpdateHealthEffects(dt, ped);
    UpdateSurvival(dt, ped);
    UpdatePlayerAnimation(dt);
    if (blocked) {
        // a cutscene took over: never leave the player hanging in the air without collision
        if (gGame.flying || gGame.gliding || gGame.jumping)
            StopFlying(ped);
        return;
    }

    const bool alive = ped->m_fHealth > 0.0f;
    const bool inVehicle = alive && ped->bInVehicle && ped->m_pVehicle;
    UpdateMovement(dt, ped);
    LightningTick(dt);
    UpdateCarBoost(dt, ped);

    // ------------------------------------------------ open screen
    if (gGame.screen != SCREEN_NONE) {
        float sx = CPad::NewMouseControllerState.x * 1.5f * gConfig.mouseSensitivity;
        float sy = CPad::NewMouseControllerState.y * 1.5f * gConfig.mouseSensitivity * (CMenuManager::bInvertMouseY ? -1.0f : 1.0f);
        gGame.cursorX = Clamp(gGame.cursorX + sx, 0.0f, (float)RsGlobal.maximumWidth - 1);
        gGame.cursorY = Clamp(gGame.cursorY + sy, 0.0f, (float)RsGlobal.maximumHeight - 1);
        GuiProcessInput();
        if (ActionPressed(ACT_INVENTORY) || ActionPressed(ACT_BACK) || !alive)
            CloseScreen();
        BlockAllPadInput();
        return;
    }

    // ------------------------------------------------ hotbar (also while driving)
    if (alive) {
        HotbarTick();
        // the wheel belongs to the hotbar, not to the radio / weapons
        CPad::NewMouseControllerState.wheelUp = 0;
        CPad::NewMouseControllerState.wheelDown = 0;
    }

    if (!alive) {
        StopMining();
        gGame.bowDraw = gGame.crossbowCharge = gGame.tridentCharge = -1.0f;
        gSurvival.eatTimer = 0.0f;
        gGame.spyglass = false;
        return;
    }
    // the hands work at the wheel as they do on foot: breaking, placing, using, fighting
    const bool gunVehicle = inVehicle && VehicleHasGuns(ped->m_pVehicle);
    if (inVehicle) {
        BlockVehicleInput(gunVehicle);
    } else {
        BlockActionInput();
        LateMovementInput(ped);
    }

    if (ActionPressed(ACT_INVENTORY)) {
        OpenScreen(gGame.gameMode == MODE_CREATIVE ? SCREEN_CREATIVE : SCREEN_INVENTORY);
        BlockAllPadInput();
        return;
    }
    if (ActionPressed(ACT_SWAP_HANDS) && !NearVehicle(ped, 5.0f)) // (next to a car GTA uses the key to get in)
        SwapHands();
    if (ActionPressed(ACT_DROP) && !inVehicle) // (at the wheel the key looks to the side)
        DropHeldItem(ActionDown(ACT_DROP_STACK));

    // ------------------------------------------------ look at / fight / mine / place
    UpdateTarget();
    if (gTarget.valid && gTarget.outline && (gTarget.voxel || gTarget.virtualBlock != ID_AIR)) {
        gTargetVisual.show = true;
        gTargetVisual.pos = gTarget.pos;
    }
    if (ActionPressed(ACT_PICK_BLOCK))
        PickBlock();

    const bool targetIsContainer = TargetIsContainer();

    // the off hand gets the right click when the main hand has no use for it
    const bool useOff = !HasRightClickUse(gInv.Held()) && !gInv.offhand.Empty();
    if (useOff) {
        std::swap(gInv.slots[gInv.selected], gInv.offhand);
        gGame.offhandActive = true;
    }
    struct SwapBack {
        bool on;
        ~SwapBack() {
            if (on) {
                std::swap(gInv.slots[gInv.selected], gInv.offhand);
                gGame.offhandActive = false;
            }
        }
    } swapBack{ useOff };
    ItemStack& useItem = gInv.Held();

    // bow, crossbow, trident, spyglass; then food
    const bool rmb = ActionDown(ACT_USE) && !targetIsContainer;
    const bool charging = ChargedItemsTick(dt, rmb, ActionPressed(ACT_USE) && !targetIsContainer);
    const bool eating = !charging && UpdateEating(dt, ped, rmb);

    // use
    if (ActionPressed(ACT_USE) && !charging) {
        bool used = false;
        if (targetIsContainer && !ped->bIsDucking) {
            OpenTargetContainer();
            used = true;
        }
        const float targetDist = gTarget.valid ? (gTarget.point - gGame.rayOrigin).Length() : 1e9f;
        const float reach = (gGame.eyePos - gGame.rayOrigin).Length() + 4.0f;
        if (!used && !eating && !inVehicle) {
            // a villager under the crosshair: trade
            PedHit ph = RaycastPeds(gGame.rayOrigin, gGame.lookDir, reach, ped, false);
            if (ph.ped && (ph.point - gGame.eyePos).Magnitude() <= 4.0f && ph.dist <= targetDist)
                used = VillagerInteract(ph.ped);
        }
        if (!used && !inVehicle) {
            // an animal under the crosshair: shear, milk, feed, saddle or ride it
            MobHit mh = MobsRaycast(gGame.rayOrigin, gGame.lookDir, reach);
            if (mh.index >= 0 && (mh.point - gGame.eyePos).Length() <= 4.0f && mh.dist <= targetDist)
                used = MobInteract(mh.index, true);
        }
        if (!used && !eating)
            used = UseWorldItem();
        if (!used && !eating)
            used = UseHeldItem();
        if (gGame.screen != SCREEN_NONE) {
            BlockAllPadInput();
            return;
        }
        if (!used && !eating)
            PlaceHeldBlock();
    } else if (ActionDown(ACT_USE) && PlaceReady() && !eating && !charging && !targetIsContainer &&
               IsBlockItem(useItem.id)) {
        PlaceHeldBlock();
    }
    gGame.usingOffhand = useOff && (eating || charging);
    if (useOff) {
        // back to the main hand for fighting and mining
        swapBack.on = false;
        std::swap(gInv.slots[gInv.selected], gInv.offhand);
        gGame.offhandActive = false;
    }

    // attack, otherwise mine (in a tank or a gunship the button fires its guns instead)
    const bool attackHeld = ActionDown(ACT_ATTACK) && !gunVehicle;
    if (ActionPressed(ACT_ATTACK) && !gunVehicle) {
        gMeleeHeld = MeleeAttack();
        if (!gMeleeHeld) {
            StartSwing();
            if (!gTarget.valid)
                gGame.attackTimer = 0.0f; // swinging at the air also resets the cooldown
        }
    }
    if (!attackHeld)
        gMeleeHeld = false;
    MineTick(dt, attackHeld && !gMeleeHeld);
    gTargetVisual.progress = MiningProgress();
}

// ---------------------------------------------------------------- camera
namespace {
// GTA's field of view is horizontal and narrow; Minecraft's is vertical (70 by default).
// The relation between CDraw's value and what ends up on screen depends on the aspect ratio and on
// widescreen patches, so it is measured from the camera the last frame was drawn with.
void ApplyFov(bool active) {
    const float cur = CDraw::ms_fFOV;
    if (Scene.m_pCamera && gFovInEffect > 1.0f && gFovInEffect < 179.0f) {
        float k = Scene.m_pCamera->viewWindow.y / std::tan(Rad(gFovInEffect) * 0.5f);
        if (k > 0.2f && k < 2.5f) {
            gFovK = gFovKMeasured ? gFovK + (k - gFovK) * 0.5f : k;
            gFovKMeasured = true;
        }
    }
    if (gFovWritten == 0.0f || cur != gFovWritten)
        gGameFov = cur; // the game computed a new value this frame
    if (active && gConfig.fov > 0.0f && gGameFov > 1.0f && gGameFov < 170.0f) {
        float want = std::tan(Rad(gConfig.fov * gGame.fovMod * (gGame.spyglass ? 0.1f : 1.0f)) * 0.5f);
        float scale = want / (gFovK * std::tan(Rad(70.0f) * 0.5f));
        float fov = 2.0f * std::atan(std::tan(Rad(gGameFov) * 0.5f) * scale) * (180.0f / kPi);
        fov = Clamp(fov, 20.0f, 160.0f);
        CDraw::ms_fFOV = fov;
        gFovWritten = fov;
        TheCamera.CalculateDerivedValues(false, false); // frustum planes for the wider view
    }
    gFovInEffect = CDraw::ms_fFOV;
}

void SetCamera(const CVector& pos, const CVector& forward) {
    CVector f = Normalized(forward);
    CVector right = CVector::Cross(f, CVector(0, 0, 1));
    float m = right.Magnitude();
    right = m > 1e-3f ? right * (1.0f / m) : CVector(1, 0, 0);
    CVector up = CVector::Cross(right, f);
    CMatrix mat = TheCamera.m_mCameraMatrix;
    // keep the handedness the game uses for its camera matrix (its "right" is the left vector)
    CVector c = CVector::Cross(mat.up, mat.at);
    float sign = (c.x * mat.right.x + c.y * mat.right.y + c.z * mat.right.z) < 0.0f ? -1.0f : 1.0f;
    mat.right = right * sign;
    mat.up = f;
    mat.at = up;
    mat.pos = pos;
    TheCamera.m_mCameraMatrix = mat;
    TheCamera.SetMatrix(mat);
    TheCamera.CopyCameraMatrixToRWCam(true);
    TheCamera.CalculateDerivedValues(false, false);
}

// how far a camera can go from `from` along `dir`
float CameraRoom(const CVector& from, const CVector& dir, float want, bool vehicles) {
    float dist = want;
    CColPoint cp;
    CEntity* e = nullptr;
    if (CWorld::ProcessLineOfSight(from, from + dir * want, cp, e, true, vehicles, false, true, false, false, true, false))
        dist = std::max(0.3f, (cp.m_vecPoint - from).Magnitude() - 0.2f);
    VoxelHit vh = RaycastVoxels(from, dir, dist);
    if (vh.hit)
        dist = std::max(0.3f, vh.dist - 0.2f);
    return dist;
}
} // namespace

// After CGame::Process: field of view, camera modes and player visibility.
void GameAfterProcess() {
    CPlayerPed* ped = FindPlayerPed();
    bool active = gGta.enabled && gGta.inWorld && ped && ped->m_fHealth > 0.0f;
    ApplyFov(active);
    if (LightningFlashActive())
        CWeather::LightningFlash = true;
    if (!ped)
        return;
    PedSkinsAfterProcess(ped);
    MovementAfterProcess(ped);

    CCam& cam = TheCamera.m_aCams[TheCamera.m_nActiveCam];
    CVector front = Normalized(cam.m_vecFront);
    CVector pp = ped->GetPosition();
    const bool inVehicle = ped->bInVehicle && ped->m_pVehicle;
    CVector eye = pp + CVector(0, 0, ped->bIsDucking ? 0.27f : 0.62f);
    if (inVehicle)
        eye = pp + ped->m_pVehicle->m_matrix->at * 0.6f;
    else if (gGta.swimming)
        eye = pp + CVector(front.x, front.y, 0.0f) * 0.45f + CVector(0, 0, 0.25f);
    gGame.eyePos = eye;
    gGame.lookDir = front;
    gGame.rayOrigin = cam.m_vecSource;

    bool hide = false;
    bool nearChanged = false;
    if (active) {
        bool override = false;
        CVector pos, look = front;
        if (gGame.cameraMode == CAM_FIRST) {
            pos = eye;
            override = true;
            hide = true;
            if (inVehicle) {
                // GTA's chase camera looks down at the car; level it out for the driver's eyes
                look = Normalized(front + CVector(0, 0, 0.16f));
                // aiming, throwing and breaking go where the crosshair of this view is
                gGame.lookDir = look;
                gGame.rayOrigin = eye;
            } else {
                gGame.rayOrigin = eye;
                if (gConfig.viewBobbing && gGame.bob > 0.001f) {
                    // GameRenderer.bobView
                    CVector right = CVector::Cross(front, CVector(0, 0, 1));
                    float rm = right.Magnitude();
                    if (rm > 1e-3f) {
                        right = right * (1.0f / rm);
                        CVector up = CVector::Cross(right, front);
                        float f1 = -gGame.walkDist * kPi, f2 = gGame.bob;
                        pos = pos + up * std::fabs(std::cos(f1) * f2) - right * (std::sin(f1) * f2 * 0.5f);
                        look = Normalized(front - up * std::tan(Rad(std::fabs(std::cos(f1 - 0.2f) * f2) * 5.0f)));
                    }
                }
            }
        } else if (gGame.cameraMode == CAM_THIRD_FRONT) {
            CVector out;
            float want;
            if (inVehicle) {
                // mirror GTA's own chase camera to the other side of the vehicle
                CVector off = cam.m_vecSource - eye;
                out = Normalized(CVector(-off.x, -off.y, std::max(off.z, 0.5f)));
                want = Clamp(off.Magnitude(), 4.0f, 45.0f);
            } else {
                out = front;
                want = 4.0f;
                gGame.rayOrigin = eye;
            }
            pos = eye + out * CameraRoom(eye, out, want, !inVehicle);
            look = out * -1.0f;
            override = true;
        }
        if (override) {
            SetCamera(pos, look);
            if (gGame.cameraMode == CAM_FIRST && Scene.m_pCamera) {
                float cur = RwCameraGetNearClipPlane(Scene.m_pCamera);
                if (cur > kFirstPersonNear + 0.01f)
                    gSavedNearClip = cur; // the game's own value
                RwCameraSetNearClipPlane(Scene.m_pCamera, kFirstPersonNear);
                nearChanged = true;
            }
        }
        if (gGta.steve)
            hide = true;
    }
    if (!nearChanged && gNearClipOverridden && Scene.m_pCamera) {
        // left first person: give the game its near plane back if it did not reset it itself
        if (RwCameraGetNearClipPlane(Scene.m_pCamera) <= kFirstPersonNear + 0.01f && gSavedNearClip > 0.0f)
            RwCameraSetNearClipPlane(Scene.m_pCamera, gSavedNearClip);
    }
    gNearClipOverridden = nearChanged;
    if (hide) {
        SetPedDrawn(ped, false); // every frame: the model is rebuilt when clothes change
        gGta.hidPlayer = true;
    } else if (gGta.hidPlayer) {
        SetPedDrawn(ped, true);
        gGta.hidPlayer = false;
    }
}

} // namespace mc
