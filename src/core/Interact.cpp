#include "Interact.h"

#include <cstdlib>

#include "BlockRules.h"
#include "Controls.h"
#include "Beds.h"
#include "Entities.h"
#include "GameState.h"
#include "Host.h"
#include "Inventory.h"
#include "Particles.h"
#include "Shapes.h"
#include "Survival.h"

namespace mc {

Target gTarget;

namespace {
Int3 gMiningPos;
bool gMiningVoxel = false;
float gMiningProgress = 0.0f;
float gBreakCooldown = 0.0f;
float gPlaceCooldown = 0.0f;
float gHitSoundTimer = 0.0f;

void HitParticles(const Vec3& at, const Vec3& normal, int block) {
    uint16_t tile = BlockFaceTile(block, FACE_NORTH, 0);
    for (int i = 0; i < 4; ++i) {
        Particle pt;
        pt.pos = at + normal * 0.05f;
        pt.vel = normal * 1.5f + Vec3(Rand01() - 0.5f, Rand01() - 0.5f, Rand01()) * 1.5f;
        pt.maxLife = pt.life = 0.4f + Rand01() * 0.3f;
        pt.tile = tile;
        pt.u = (rand() % 4) * 0.25f;
        pt.v = (rand() % 4) * 0.25f;
        pt.sub = 0.25f;
        pt.size = 0.05f;
        SpawnParticle(pt);
    }
}

// the cell is free: no block, and nobody of the host's standing in it
bool CanPlaceAt(const Int3& p) {
    if (gWorld.GetBlock(p.x, p.y, p.z) != ID_AIR)
        return false;
    return !TheHost().CellBlocked(p);
}

int PlacementMeta(int block) {
    const BlockDef& d = Block(block);
    if (d.shape == SHAPE_FACING) {
        const Vec3& dir = gGame.lookDir;
        if (std::fabs(dir.x) > std::fabs(dir.y))
            return dir.x > 0 ? 3 : 1;
        return dir.y > 0 ? 2 : 0;
    }
    if (d.shape == SHAPE_BED) {
        // the head goes the way the player looks
        const Vec3& dir = gGame.lookDir;
        return std::fabs(dir.x) > std::fabs(dir.y) ? (dir.x > 0 ? FACE_EAST : FACE_WEST) : (dir.y > 0 ? FACE_NORTH : FACE_SOUTH);
    }
    if (d.shape == SHAPE_STAIRS || d.shape == SHAPE_SLAB) {
        // the upper half: put under a block, or against the upper half of a side
        bool upper = gTarget.normal.z < -0.5f;
        if (std::fabs(gTarget.normal.z) < 0.5f)
            upper = gTarget.point.z - std::floor(gTarget.point.z) > 0.5f;
        if (d.shape == SHAPE_SLAB)
            return upper ? META_SLAB_TOP : 0;
        // stairs rise away from the player
        const Vec3& dir = gGame.lookDir;
        const int side = std::fabs(dir.x) > std::fabs(dir.y) ? (dir.x > 0 ? FACE_EAST : FACE_WEST) : (dir.y > 0 ? FACE_NORTH : FACE_SOUTH);
        return side | (upper ? META_UPSIDE : 0);
    }
    if (d.shape == SHAPE_COLUMN) {
        Vec3 n = gTarget.normal;
        float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
        if (az >= ax && az >= ay)
            return 0;
        return ax > ay ? 1 : 2;
    }
    return 0;
}

void ReplaceHeldWith(uint16_t id) {
    ItemStack& held = gInv.Held();
    if (gGame.gameMode == MODE_CREATIVE)
        return;
    ItemStack s;
    s.id = id;
    s.count = 1;
    if (held.count <= 1) {
        held = s;
    } else {
        --held.count;
        if (gInv.Add(s) > 0)
            DropStackAtPlayer(s, false);
    }
    gWorld.dirty = true;
}
} // namespace

// ---------------------------------------------------------------- what the player looks at
void UpdateTarget() {
    gTarget = Target();
    Vec3 origin = gGame.rayOrigin, dir = gGame.lookDir;
    if (dir.Length() < 0.5f || !std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z))
        return;
    Vec3 head = gGame.eyePos;
    float reach = gGame.gameMode == MODE_CREATIVE ? 5.0f : 4.5f;
    float maxDist = (head - origin).Length() + reach + 0.5f;

    VoxelHit vh = RaycastVoxels(origin, dir, maxDist);

    // something of the host's in the way, or in front of the block?
    const PickRay ray{ origin, dir, head, reach, maxDist };
    if (TheHost().Pick(ray, vh, gTarget))
        return;

    if (vh.hit) {
        Vec3 hp = origin + dir * vh.dist;
        if ((hp - head).Length() > reach + 0.5f)
            return;
        gTarget.valid = true;
        gTarget.voxel = true;
        gTarget.pos = vh.pos;
        gTarget.key = vh.pos;
        gTarget.face = vh.face;
        gTarget.point = hp;
        const Int3& n = FACE_DIR[vh.face];
        gTarget.normal = Vec3((float)n.x, (float)n.y, (float)n.z);
    }
}

// ---------------------------------------------------------------- mining
float BreakSecondsFor(int block, float hardnessOverride, const ItemStack& tool, bool* canHarvest) {
    const BlockDef& d = Block(block);
    const float hardness = hardnessOverride > -1.0f ? hardnessOverride : d.hardness;
    if (hardness < 0) {
        if (canHarvest)
            *canHarvest = false;
        return 1e9f;
    }
    const ItemDef* td = tool.Empty() ? nullptr : &Item(tool.id);
    bool correct = td && d.tool != TOOL_NONE && td->tool == d.tool;
    float speed = correct ? td->speed : 1.0f;
    if (td && td->tool == TOOL_SWORD && (d.sound == SG_GRASS || d.sound == SG_WOOL))
        speed = 1.5f;
    bool harvest = !d.requiresTool || (correct && td->tier >= std::max<int>(d.tier, 1));
    if (canHarvest)
        *canHarvest = harvest;
    if (hardness == 0)
        return 0.0f;
    return hardness * (harvest ? 30.0f : 100.0f) / speed / 20.0f;
}

float BreakSeconds(int block, const ItemStack& tool, bool* canHarvest) { return BreakSecondsFor(block, -2.0f, tool, canHarvest); }

void BreakVoxel(const Int3& p, bool withDrops) {
    int block = gWorld.GetBlock(p.x, p.y, p.z);
    if (block == ID_AIR)
        return;
    if (gGame.screen != SCREEN_NONE && gGame.openPos == p)
        CloseScreen();
    Vec3 c(p.x + 0.5f, p.y + 0.5f, p.z + 0.5f);
    if (IsFireBlock(block)) {
        // punching a fire puts it out
        PlaySfx(SND_FIRE_EXTINGUISH, &c, 0.5f, 2.0f + (Rand01() - Rand01()) * 0.8f);
        gWorld.Set(p.x, p.y, p.z, MakeVox(ID_AIR));
        return;
    }
    SpawnBreakParticles(p, block);
    PlaySfx(DigSound(block), &c);
    if (withDrops) {
        bool harvest = true;
        BreakSeconds(block, gInv.Held(), &harvest);
        if (harvest) {
            SpawnBlockDrops(block, c);
            if (Block(block).shape == SHAPE_SLAB && (VoxMeta(gWorld.Get(p.x, p.y, p.z)) & META_SLAB_DOUBLE))
                SpawnBlockDrops(block, c); // a double slab is two
            SpawnXp(c, XpForBlock(block));
        }
    }
    DropContainerContents(p);
    const int meta = VoxMeta(gWorld.Get(p.x, p.y, p.z));
    gWorld.Set(p.x, p.y, p.z, MakeVox(ID_AIR));
    if (IsBedBlock(block)) {
        // the other half goes with it
        const Int3 o = BedOtherCell(p, block, meta);
        if (gWorld.GetBlock(o.x, o.y, o.z) == BedOtherBlock(block))
            gWorld.Set(o.x, o.y, o.z, MakeVox(ID_AIR));
    }
}

void InteractTick(float dt) {
    gBreakCooldown = std::max(0.0f, gBreakCooldown - dt);
    gPlaceCooldown = std::max(0.0f, gPlaceCooldown - dt);
    gHitSoundTimer = std::max(0.0f, gHitSoundTimer - dt);
}

void StopMining() { gMiningProgress = 0; }

float MiningProgress() { return gGame.gameMode == MODE_SURVIVAL ? gMiningProgress : 0.0f; }

void MineTick(float dt, bool mining) {
    ItemStack& held = gInv.Held();
    const bool wand = !held.Empty() && Item(held.id).special == SP_WAND;
    if (mining && gTarget.valid && !wand) {
        bool voxel = gTarget.voxel;
        int block = voxel ? gWorld.GetBlock(gTarget.pos.x, gTarget.pos.y, gTarget.pos.z) : gTarget.virtualBlock;
        const Int3 key = gTarget.key;
        if (block == ID_AIR) {
            gMiningProgress = 0;
        } else if (gGame.gameMode == MODE_CREATIVE) {
            if (gBreakCooldown <= 0.0f && !(held.id && Item(held.id).tool == TOOL_SWORD)) {
                if (voxel)
                    BreakVoxel(gTarget.pos, false);
                else
                    TheHost().BreakTarget();
                gBreakCooldown = 0.25f;
                StartSwing();
            }
        } else {
            if (gMiningPos != key || gMiningVoxel != voxel) {
                gMiningPos = key;
                gMiningVoxel = voxel;
                gMiningProgress = 0;
            }
            const float hardness = voxel ? -2.0f : gTarget.hardness;
            float secs = BreakSecondsFor(block, hardness, held, nullptr);
            if (gBreakCooldown <= 0.0f) {
                gMiningProgress += secs <= 0.0f ? 1.0f : dt / secs;
                if (gHitSoundTimer <= 0.0f) {
                    gHitSoundTimer = 0.25f;
                    PlaySfx(HitSound(block), &gTarget.point, 0.25f, 0.5f);
                    StartSwing();
                    if (!voxel)
                        HitParticles(gTarget.point, gTarget.normal, block);
                }
                if (gMiningProgress >= 1.0f) {
                    if (voxel)
                        BreakVoxel(gTarget.pos, true);
                    else
                        TheHost().BreakTarget();
                    if (Block(block).hardness > 0.0f)
                        DamageHeldItem(Item(held.id).tool == TOOL_SWORD ? 2 : 1);
                    gSurvival.exhaustion += kExhaustMine;
                    gMiningProgress = 0;
                    gBreakCooldown = 0.25f;
                    gWorld.dirty = true;
                }
            }
        }
    } else {
        gMiningProgress = 0;
    }
}

// ---------------------------------------------------------------- the hands
void HotbarTick() {
    int prevSel = gInv.selected;
    uint16_t prevId = gInv.Held().id;
    for (int i = 0; i < 9; ++i)
        if (ActionPressed((Action)(ACT_HOTBAR_1 + i)))
            gInv.selected = i;
    if (gControls.scrollUp)
        gInv.selected = (gInv.selected + 8) % 9;
    if (gControls.scrollDown)
        gInv.selected = (gInv.selected + 1) % 9;
    if (gInv.selected != prevSel) {
        gGame.selectedNameTimer = 2.0f;
        if (gInv.Held().id != prevId)
            gGame.attackTimer = 0.0f; // Minecraft resets the attack cooldown when the item changes
    }
}

void SwapHands() {
    std::swap(gInv.slots[gInv.selected], gInv.offhand);
    PlaySfx(SND_EQUIP_GENERIC, nullptr, 0.6f);
    gGame.handHeight = 0.0f;
    gGame.offHeight = 0.0f;
}

void DropHeldItem(bool wholeStack) {
    if (gInv.Held().Empty())
        return;
    ItemStack d = gInv.Held();
    if (!wholeStack)
        d.count = 1;
    gInv.Held().count -= d.count;
    if (gInv.Held().count == 0)
        gInv.Held().Clear();
    DropStackAtPlayer(d, true);
    StartSwing();
}

void PickBlock() {
    if (gGame.gameMode != MODE_CREATIVE || !gTarget.valid)
        return;
    int b = gTarget.voxel ? gWorld.GetBlock(gTarget.pos.x, gTarget.pos.y, gTarget.pos.z) : gTarget.virtualBlock;
    if (Block(b).shape == SHAPE_BED_HEAD)
        b = BedOtherBlock(b); // (the bed)
    if (b != ID_AIR) {
        gInv.Held().id = (uint16_t)b;
        gInv.Held().count = 1;
        gInv.Held().damage = 0;
    }
}

// ---------------------------------------------------------------- using
bool IsContainer(int b) {
    return b == ID_CRAFTING_TABLE || b == ID_FURNACE || b == ID_BLAST_FURNACE || b == ID_SMOKER || b == ID_CHEST ||
           b == ID_BARREL;
}

bool TargetHasUse() {
    if (!gTarget.valid || !gTarget.voxel)
        return false;
    const int b = gWorld.GetBlock(gTarget.pos.x, gTarget.pos.y, gTarget.pos.z);
    return IsContainer(b) || IsBedBlock(b);
}

void UseTargetBlock() {
    const Int3& p = gTarget.pos;
    int b = gWorld.GetBlock(p.x, p.y, p.z);
    if (IsBedBlock(b))
        UseBed(p);
    else if (b == ID_CRAFTING_TABLE)
        OpenScreen(SCREEN_CRAFTING, p);
    else if (b == ID_CHEST || b == ID_BARREL)
        OpenScreen(SCREEN_CHEST, p);
    else
        OpenScreen(SCREEN_FURNACE, p);
}

bool HasRightClickUse(const ItemStack& s) {
    if (s.Empty())
        return false;
    if (IsBlockItem(s.id))
        return true;
    const ItemDef& d = Item(s.id);
    if (d.food > 0 || d.armorSlot)
        return true;
    return d.special != SP_NONE && d.special != SP_ARROW && d.special != SP_TOTEM;
}

// use with buckets, bone meal, armour
bool UseWorldItem() {
    ItemStack& held = gInv.Held();
    if (held.Empty())
        return false;
    const ItemDef& d = Item(held.id);
    const bool survival = gGame.gameMode == MODE_SURVIVAL;
    switch (d.special) {
    case SP_WATER_BUCKET:
    case SP_LAVA_BUCKET: {
        if (!gTarget.valid)
            return false;
        Int3 c;
        if (gTarget.voxel) {
            c = gTarget.pos + FACE_DIR[gTarget.face];
        } else {
            Vec3 q = gTarget.point + gTarget.normal * 0.5f;
            c = { FloorI(q.x), FloorI(q.y), FloorI(q.z) };
        }
        const bool water = d.special == SP_WATER_BUCKET;
        if (!PlaceFluid(water ? ID_WATER : ID_LAVA, c))
            return false;
        const Vec3 at(c.x + 0.5f, c.y + 0.5f, c.z + 0.5f);
        if (water)
            TheHost().Douse(at, 3.0f);
        PlaySfx(water ? SND_BUCKET_EMPTY : SND_BUCKET_EMPTY_LAVA, &at);
        ReplaceHeldWith(ID_BUCKET);
        StartSwing();
        return true;
    }
    case SP_BUCKET: {
        VoxelHit fh = RaycastVoxels(gGame.rayOrigin, gGame.lookDir, (gGame.eyePos - gGame.rayOrigin).Length() + 5.0f, true);
        int fluid;
        if (fh.hit && TakeFluid(fh.pos, &fluid)) {
            const Vec3 at(fh.pos.x + 0.5f, fh.pos.y + 0.5f, fh.pos.z + 0.5f);
            PlaySfx(fluid == ID_LAVA ? SND_BUCKET_FILL_LAVA : SND_BUCKET_FILL, &at);
            ReplaceHeldWith(fluid == ID_LAVA ? ID_LAVA_BUCKET : ID_WATER_BUCKET);
            StartSwing();
            return true;
        }
        return false;
    }
    case SP_BONE_MEAL: {
        if (!gTarget.valid)
            return false;
        if (!ApplyBoneMeal(gTarget.voxel, gTarget.pos, gTarget.point, gTarget.normal, gTarget.grass))
            return false;
        PlaySfx(SND_BONE_MEAL, &gTarget.point);
        if (survival && --held.count == 0)
            held.Clear();
        StartSwing();
        return true;
    }
    default:
        break;
    }
    // armour: a use puts it on
    if (d.armorSlot >= 1 && d.armorSlot <= 4 && gInv.armor[d.armorSlot - 1].Empty()) {
        gInv.armor[d.armorSlot - 1] = held;
        held.Clear();
        PlaySfx(d.special == SP_ELYTRA ? SND_EQUIP_ELYTRA : SND_EQUIP_IRON);
        gWorld.dirty = true;
        return true;
    }
    return false;
}

bool PlaceReady() { return gPlaceCooldown <= 0.0f; }

// a slab put on the free half of a slab of its kind makes a double one
bool TryDoubleSlab(const Int3& c, int block, bool upperHalf) {
    const Voxel v = gWorld.Get(c.x, c.y, c.z);
    if (VoxBlock(v) != block || (VoxMeta(v) & META_SLAB_DOUBLE) || ((VoxMeta(v) & META_SLAB_TOP) != 0) == upperHalf)
        return false;
    gWorld.Set(c.x, c.y, c.z, MakeVox(block, META_SLAB_DOUBLE));
    return true;
}

void PlaceHeldBlock() {
    ItemStack& h = gInv.Held();
    if (h.Empty() || !IsBlockItem(h.id) || !gTarget.valid || IsFluidBlock(h.id))
        return;
    Int3 p;
    if (Block(h.id).shape == SHAPE_SLAB && gTarget.voxel) {
        // clicking the top of a lower slab (the bottom of an upper one) fills it up; so does a slab put into the
        // cell of one with its other half free
        bool doubled = (gTarget.face == FACE_TOP || gTarget.face == FACE_BOTTOM) && TryDoubleSlab(gTarget.pos, h.id, gTarget.face == FACE_TOP);
        const Int3 cell = doubled ? gTarget.pos : gTarget.pos + FACE_DIR[gTarget.face];
        doubled = doubled || TryDoubleSlab(cell, h.id, (PlacementMeta(h.id) & META_SLAB_TOP) != 0);
        if (doubled) {
            const Vec3 c(cell.x + 0.5f, cell.y + 0.5f, cell.z + 0.5f);
            PlaySfx(PlaceSound(h.id), &c);
            if (gGame.gameMode == MODE_SURVIVAL && --h.count == 0)
                h.Clear();
            gPlaceCooldown = 0.25f;
            StartSwing();
            return;
        }
    }
    if (IsPlantBlock(h.id)) {
        // plants go on top of soil (blocks or the host's ground)
        if (gTarget.voxel) {
            if (gTarget.face != FACE_TOP)
                return;
            p = gTarget.pos + FACE_DIR[FACE_TOP];
        } else {
            if (gTarget.normal.z < 0.5f)
                return;
            p = { FloorI(gTarget.point.x), FloorI(gTarget.point.y), PlantCellOnGround(gTarget.point.z) };
        }
        if (!CanPlantAt(h.id, p, gGame.gameMode == MODE_CREATIVE))
            return;
    } else {
        if (gTarget.voxel) {
            p = gTarget.pos + FACE_DIR[gTarget.face];
        } else {
            Vec3 q = gTarget.point + gTarget.normal * 0.5f;
            p = { FloorI(q.x), FloorI(q.y), FloorI(q.z) };
        }
        const int there = gWorld.GetBlock(p.x, p.y, p.z);
        if (IsPlantBlock(there) || IsFluidBlock(there) || IsFireBlock(there)) {
            if (IsPlantBlock(there))
                SpawnBlockDrops(there, Vec3(p.x + 0.5f, p.y + 0.5f, p.z + 0.5f));
            gWorld.SetRaw(p.x, p.y, p.z, MakeVox(ID_AIR)); // plants, fluids and fire make room
        }
        if (!CanPlaceAt(p))
            return;
    }
    int block = h.id;
    const int meta = PlacementMeta(block);
    if (Block(block).shape == SHAPE_BED) {
        // its head needs the cell beyond
        const Int3 head = BedOtherCell(p, block, meta);
        if (gWorld.GetBlock(head.x, head.y, head.z) != ID_AIR || !CanPlaceAt(head))
            return;
        gWorld.Set(head.x, head.y, head.z, MakeVox(BedOtherBlock(block), meta));
    }
    gWorld.Set(p.x, p.y, p.z, MakeVox(block, meta));
    if (block == ID_FURNACE || block == ID_BLAST_FURNACE || block == ID_SMOKER)
        gWorld.furnaces[p] = FurnaceState();
    if (block == ID_CHEST || block == ID_BARREL)
        gWorld.chests[p] = ChestState();
    Vec3 c(p.x + 0.5f, p.y + 0.5f, p.z + 0.5f);
    PlaySfx(PlaceSound(block), &c);
    if (gGame.gameMode == MODE_SURVIVAL && --h.count == 0)
        h.Clear();
    gPlaceCooldown = 0.25f;
    StartSwing();
}

} // namespace mc
