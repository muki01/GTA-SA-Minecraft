#include "World.h"

namespace mc {

World gWorld;
void (*gNaturalRemovedHook)(int x, int y, int z, int block) = nullptr;

Chunk* World::FindChunk(const Int3& cpos) const {
    auto it = chunks.find(cpos);
    return it == chunks.end() ? nullptr : it->second.get();
}

Voxel World::Get(int x, int y, int z) const {
    Chunk* c = FindChunk(ChunkOf(x, y, z));
    if (!c)
        return 0;
    return c->Get(FloorMod(x, CS), FloorMod(y, CS), FloorMod(z, CS));
}

bool World::IsSolid(int x, int y, int z) const {
    return IsSolidBlock(GetBlock(x, y, z));
}

void World::MarkMeshDirty(int x, int y, int z) {
    int lx = FloorMod(x, CS), ly = FloorMod(y, CS), lz = FloorMod(z, CS);
    Int3 cp = ChunkOf(x, y, z);
    // neighbours (including diagonals, for ambient occlusion) when the block sits on a chunk edge
    for (int dz = -1; dz <= 1; ++dz) {
        if ((dz == -1 && lz != 0) || (dz == 1 && lz != CS - 1))
            continue;
        for (int dy = -1; dy <= 1; ++dy) {
            if ((dy == -1 && ly != 0) || (dy == 1 && ly != CS - 1))
                continue;
            for (int dx = -1; dx <= 1; ++dx) {
                if ((dx == -1 && lx != 0) || (dx == 1 && lx != CS - 1))
                    continue;
                if (Chunk* c = FindChunk({ cp.x + dx, cp.y + dy, cp.z + dz }))
                    c->meshDirty = true;
            }
        }
    }
}

void World::Set(int x, int y, int z, Voxel v) { SetImpl(x, y, z, v, true); }
void World::SetRaw(int x, int y, int z, Voxel v) { SetImpl(x, y, z, v, false); }

void World::SetImpl(int x, int y, int z, Voxel v, bool notify) {
    Int3 cp = ChunkOf(x, y, z);
    Chunk* c = FindChunk(cp);
    if (!c) {
        if (VoxBlock(v) == ID_AIR)
            return;
        auto nc = std::make_unique<Chunk>();
        nc->pos = cp;
        c = nc.get();
        chunks[cp] = std::move(nc);
        columnChunks[{ cp.x, cp.y, 0 }].push_back(cp.z);
    }
    int idx = Chunk::Index(FloorMod(x, CS), FloorMod(y, CS), FloorMod(z, CS));
    Voxel old = c->data[idx];
    if (old == v)
        return;
    c->data[idx] = v;
    if (VoxBlock(old) == ID_AIR && VoxBlock(v) != ID_AIR)
        c->nonAir++;
    else if (VoxBlock(old) != ID_AIR && VoxBlock(v) == ID_AIR)
        c->nonAir--;
    c->meshDirty = true;
    MarkMeshDirty(x, y, z);
    columnVersion[{ cp.x, cp.y, 0 }]++;
    dirty = true;
    if (IsRandomTicking(VoxBlock(old)))
        ticking.erase({ x, y, z });
    if (IsRandomTicking(VoxBlock(v)))
        ticking.insert({ x, y, z });
    if (notify && updates.size() < 50000) {
        updates.push_back({ x, y, z });
        for (const Int3& d : FACE_DIR)
            updates.push_back({ x + d.x, y + d.y, z + d.z });
    }

    if (c->nonAir <= 0) {
        auto& list = columnChunks[{ cp.x, cp.y, 0 }];
        list.erase(std::remove(list.begin(), list.end(), cp.z), list.end());
        if (list.empty())
            columnChunks.erase({ cp.x, cp.y, 0 });
        chunks.erase(cp);
    }
    const bool wasNatural = (VoxMeta(old) & META_NATURAL) && IsSolidBlock(VoxBlock(old));
    const bool isNatural = (VoxMeta(v) & META_NATURAL) && IsSolidBlock(VoxBlock(v));
    if (wasNatural && !isNatural && gNaturalRemovedHook)
        gNaturalRemovedHook(x, y, z, VoxBlock(old));
}

void World::Clear() {
    chunks.clear();
    furnaces.clear();
    chests.clear();
    columnChunks.clear();
    updates.clear();
    ticking.clear();
    // keep bumping versions so the collision manager notices everything changed
    for (auto& kv : columnVersion)
        kv.second++;
    dirty = false;
}

VoxelHit RaycastVoxels(const Vec3& o, const Vec3& d, float maxDist, bool fluids) {
    VoxelHit r;
    int x = FloorI(o.x), y = FloorI(o.y), z = FloorI(o.z);
    int sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1, sz = d.z > 0 ? 1 : -1;
    auto inv = [](float v) { return std::fabs(v) < 1e-6f ? 1e30f : 1.0f / std::fabs(v); };
    float tdx = inv(d.x), tdy = inv(d.y), tdz = inv(d.z);
    float tmx = (d.x > 0 ? (x + 1 - o.x) : (o.x - x)) * tdx;
    float tmy = (d.y > 0 ? (y + 1 - o.y) : (o.y - y)) * tdy;
    float tmz = (d.z > 0 ? (z + 1 - o.z) : (o.z - z)) * tdz;
    float t = 0;
    int face = -1;
    for (int i = 0; i < 512 && t <= maxDist; ++i) {
        int b = face >= 0 ? gWorld.GetBlock(x, y, z) : ID_AIR;
        if (b != ID_AIR && (fluids || !IsFluidBlock(b))) {
            r.hit = true;
            r.pos = { x, y, z };
            r.face = face;
            r.dist = t;
            return r;
        }
        if (tmx < tmy && tmx < tmz) {
            x += sx;
            t = tmx;
            tmx += tdx;
            face = sx > 0 ? FACE_WEST : FACE_EAST;
        } else if (tmy < tmz) {
            y += sy;
            t = tmy;
            tmy += tdy;
            face = sy > 0 ? FACE_SOUTH : FACE_NORTH;
        } else {
            z += sz;
            t = tmz;
            tmz += tdz;
            face = sz > 0 ? FACE_BOTTOM : FACE_TOP;
        }
    }
    return r;
}

// ---------------------------------------------------------------- serialisation
template <typename T> static void W(FILE* f, const T& v) { fwrite(&v, sizeof(T), 1, f); }
template <typename T> static bool R(FILE* f, T& v) { return fread(&v, sizeof(T), 1, f) == 1; }

void WriteStack(FILE* f, const ItemStack& s) {
    W(f, s.id);
    W(f, s.count);
    W(f, s.damage);
}

static int gOldFirstItem = 0;

void SetItemIdMigration(int oldFirstItem) { gOldFirstItem = oldFirstItem; }

int MigrateItemId(int id) {
    if (gOldFirstItem > 0 && gOldFirstItem != FIRST_ITEM && id >= gOldFirstItem)
        return id - gOldFirstItem + FIRST_ITEM;
    return id;
}

ItemStack ReadStack(FILE* f) {
    ItemStack s;
    R(f, s.id);
    R(f, s.count);
    R(f, s.damage);
    s.id = (uint16_t)MigrateItemId(s.id);
    if (!IsValidItem(s.id) || s.count == 0)
        s.Clear();
    return s;
}

void World::Write(FILE* f) const {
    W(f, (uint32_t)chunks.size());
    for (auto& kv : chunks) {
        const Chunk& c = *kv.second;
        W(f, c.pos.x);
        W(f, c.pos.y);
        W(f, c.pos.z);
        // run-length encoding: (count, value) pairs
        std::vector<uint16_t> rle;
        int i = 0;
        while (i < CS3) {
            uint16_t v = c.data[i];
            int run = 1;
            while (i + run < CS3 && c.data[i + run] == v && run < 65535)
                run++;
            rle.push_back((uint16_t)run);
            rle.push_back(v);
            i += run;
        }
        W(f, (uint32_t)rle.size());
        fwrite(rle.data(), sizeof(uint16_t), rle.size(), f);
    }
    W(f, (uint32_t)furnaces.size());
    for (auto& kv : furnaces) {
        W(f, kv.first.x);
        W(f, kv.first.y);
        W(f, kv.first.z);
        WriteStack(f, kv.second.input);
        WriteStack(f, kv.second.fuel);
        WriteStack(f, kv.second.output);
        W(f, kv.second.burnTime);
        W(f, kv.second.burnTimeTotal);
        W(f, kv.second.cookTime);
    }
    W(f, (uint32_t)chests.size());
    for (auto& kv : chests) {
        W(f, kv.first.x);
        W(f, kv.first.y);
        W(f, kv.first.z);
        for (auto& s : kv.second.slots)
            WriteStack(f, s);
    }
}

bool World::Read(FILE* f) {
    Clear();
    uint32_t n = 0;
    if (!R(f, n) || n > 1000000)
        return false;
    for (uint32_t k = 0; k < n; ++k) {
        auto c = std::make_unique<Chunk>();
        uint32_t len = 0;
        if (!R(f, c->pos.x) || !R(f, c->pos.y) || !R(f, c->pos.z) || !R(f, len) || len > CS3 * 2 || (len & 1))
            return false;
        std::vector<uint16_t> rle(len);
        if (fread(rle.data(), sizeof(uint16_t), len, f) != len)
            return false;
        int i = 0;
        for (uint32_t p = 0; p + 1 < len; p += 2) {
            for (int r = 0; r < rle[p] && i < CS3; ++r) {
                Voxel v = rle[p + 1];
                if (VoxBlock(v) >= NUM_BLOCKS)
                    v = 0;
                c->data[i++] = v;
                if (VoxBlock(v) != ID_AIR)
                    c->nonAir++;
            }
        }
        if (c->nonAir > 0) {
            columnChunks[{ c->pos.x, c->pos.y, 0 }].push_back(c->pos.z);
            columnVersion[{ c->pos.x, c->pos.y, 0 }]++;
            Int3 p = c->pos;
            chunks[p] = std::move(c);
        }
    }
    if (!R(f, n) || n > 100000)
        return false;
    for (uint32_t k = 0; k < n; ++k) {
        Int3 p;
        FurnaceState s;
        R(f, p.x);
        R(f, p.y);
        R(f, p.z);
        s.input = ReadStack(f);
        s.fuel = ReadStack(f);
        s.output = ReadStack(f);
        R(f, s.burnTime);
        R(f, s.burnTimeTotal);
        R(f, s.cookTime);
        furnaces[p] = s;
    }
    if (!R(f, n) || n > 100000)
        return false;
    for (uint32_t k = 0; k < n; ++k) {
        Int3 p;
        ChestState s;
        R(f, p.x);
        R(f, p.y);
        R(f, p.z);
        for (auto& slot : s.slots)
            slot = ReadStack(f);
        chests[p] = s;
    }
    // saplings keep growing, fluids settle
    for (auto& kv : chunks) {
        const Chunk& c = *kv.second;
        for (int i = 0; i < CS3; ++i) {
            int b = VoxBlock(c.data[i]);
            if (b == ID_AIR)
                continue;
            if (IsRandomTicking(b) || IsFluidBlock(b)) {
                Int3 p{ c.pos.x * CS + i % CS, c.pos.y * CS + (i / CS) % CS, c.pos.z * CS + i / (CS * CS) };
                if (IsRandomTicking(b))
                    ticking.insert(p);
                if (IsFluidBlock(b) && updates.size() < 50000)
                    updates.push_back(p);
            }
        }
    }
    dirty = false;
    return true;
}

} // namespace mc
