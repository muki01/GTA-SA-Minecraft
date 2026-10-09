#include "Villagers.h"

#include <cstdlib>
#include <string>

#include "Audio.h"
#include "Entities.h"
#include "GameState.h"
#include "Inventory.h"
#include "Items.h"
#include "Particles.h"

namespace mc {

namespace {
Vec3 Flat(Vec3 v) {
    v.z = 0.0f;
    float m = v.Length();
    return m > 1e-4f ? v * (1.0f / m) : Vec3(0, 1, 0);
}

SoundEvent SayOf(int kind) { return kind == NPC_VILLAGER ? SND_VILLAGER_SAY : SND_PILLAGER_SAY; }
SoundEvent HurtOf(int kind) { return kind == NPC_VILLAGER ? SND_VILLAGER_HURT : SND_PILLAGER_HURT; }
SoundEvent DeathOf(int kind) { return kind == NPC_VILLAGER ? SND_VILLAGER_DEATH : SND_PILLAGER_DEATH; }

struct Offer {
    uint16_t item;
    int count;
    int price; // emeralds
};
struct Want {
    uint16_t item;
    int count; // for one emerald
};
struct Profession {
    int name; // STR_*
    Offer sells[5];
    int numSells;
    Want buys[4];
    int numBuys;
};
const Profession kProfessions[6] = {
    { STR_VILLAGER, {}, 0, {}, 0 },
    { STR_FARMER,
      { { ID_BREAD, 6, 1 }, { ID_APPLE, 4, 1 }, { ID_PUMPKIN_PIE, 4, 1 }, { ID_GOLDEN_CARROT, 3, 3 } }, 4,
      { { ID_WHEAT, 20 }, { ID_CARROT, 22 }, { ID_POTATO, 26 }, { ID_BEETROOT, 15 } }, 4 },
    { STR_LIBRARIAN,
      { { ID_BOOK, 1, 1 }, { ID_GLASS, 4, 1 }, { ID_BOOKSHELF, 1, 9 }, { ID_EXPERIENCE_BOTTLE, 1, 3 } }, 4,
      { { ID_PAPER, 24 }, { ID_INK_SAC, 5 } }, 2 },
    { STR_BUTCHER,
      { { ID_COOKED_PORKCHOP, 5, 1 }, { ID_COOKED_CHICKEN, 8, 1 }, { ID_COOKED_BEEF, 5, 1 }, { ID_RABBIT_STEW, 1, 1 } }, 4,
      { { ID_CHICKEN, 14 }, { ID_PORKCHOP, 7 }, { ID_BEEF, 10 }, { ID_MUTTON, 7 } }, 4 },
    { STR_CLERIC,
      { { ID_REDSTONE, 2, 1 }, { ID_LAPIS_LAZULI, 1, 1 }, { ID_GLOWSTONE, 1, 4 }, { ID_ENDER_PEARL, 1, 5 },
        { ID_EXPERIENCE_BOTTLE, 1, 3 } }, 5,
      { { ID_ROTTEN_FLESH, 32 }, { ID_GOLD_INGOT, 3 } }, 2 },
    { STR_ARMORER,
      { { ID_IRON_HELMET, 1, 5 }, { ID_IRON_CHESTPLATE, 1, 9 }, { ID_IRON_LEGGINGS, 1, 7 }, { ID_IRON_BOOTS, 1, 4 },
        { ID_DIAMOND_CHESTPLATE, 1, 21 } }, 5,
      { { ID_COAL, 15 }, { ID_IRON_INGOT, 4 }, { ID_DIAMOND, 1 } }, 3 },
};

std::string OfferText(const Profession& pr, int offer) {
    std::string s = std::string(LangStr(pr.name)) + ": ";
    const std::string emerald = ItemName(ID_EMERALD);
    if (pr.numSells > 0) {
        const Offer& o = pr.sells[((offer % pr.numSells) + pr.numSells) % pr.numSells];
        s += std::to_string(o.price) + " " + emerald + " -> " + std::to_string(o.count) + " " + ItemName(o.item);
    }
    if (pr.numBuys > 0) {
        const Want& w = pr.buys[((offer % pr.numBuys) + pr.numBuys) % pr.numBuys];
        s += "  |  " + std::to_string(w.count) + " " + ItemName(w.item) + " -> 1 " + emerald;
    }
    return s;
}

void HappyParticles(const Vec3& at) {
    for (int i = 0; i < 8; ++i) {
        Particle p;
        p.pos = at + Vec3((Rand01() - 0.5f) * 0.8f, (Rand01() - 0.5f) * 0.8f, Rand01() * 0.6f);
        p.vel = Vec3(0, 0, 0.6f);
        p.maxLife = p.life = 0.8f;
        p.tile = TILE_P_ENCHANTED_HIT;
        p.size = 0.09f;
        p.gravity = 0.0f;
        p.color = 0xFF40FF40;
        p.glow = true;
        SpawnParticle(p);
    }
}

void GiveOrDrop(uint16_t id, int count) {
    while (count > 0) {
        ItemStack s;
        s.id = id;
        s.count = (uint8_t)std::min(count, MaxStack(id));
        count -= s.count;
        int left = gInv.Add(s);
        if (left > 0) {
            s.count = (uint8_t)left;
            DropStackAtPlayer(s, false);
        }
    }
}
} // namespace

NpcLook NpcStart(int kind, int profession, float health, bool dead) {
    NpcLook l;
    l.kind = kind;
    l.variant = profession;
    l.lastHealth = health;
    l.say = 4.0f + Rand01() * 40.0f;
    l.death = dead ? 10.0f : -1.0f;
    l.offer = rand() % 5;
    return l;
}

void NpcTick(NpcLook& l, float dt, const NpcFacts& f, const Vec3& playerPos) {
    const Vec3 pos = f.pos;
    const float dist = (pos - playerPos).Length();
    const float speed = f.speed;
    float amount = Clamp(speed / 20.0f * 4.0f, 0.0f, 1.0f);
    l.limbAmount += (amount - l.limbAmount) * Clamp(dt * 8.0f, 0.0f, 1.0f);
    l.limbSwing += l.limbAmount * 20.0f * dt;
    l.hurt = std::max(0.0f, l.hurt - dt);

    const bool dead = f.dead;
    if (dead) {
        if (l.death < 0.0f) {
            l.death = 0.0f;
            if (dist < 40.0f)
                PlaySfx(DeathOf(l.kind), &pos);
        }
        l.death += dt;
    } else {
        l.death = -1.0f;
        if (f.health < l.lastHealth - 0.5f) {
            // (a burning ped loses health every frame: one grunt per flash is enough)
            if (l.hurt <= 0.0f && dist < 40.0f)
                PlaySfx(HurtOf(l.kind), &pos);
            l.hurt = 0.45f;
        }
        l.say -= dt;
        if (l.say <= 0.0f) {
            l.say = 15.0f + Rand01() * 45.0f;
            if (dist < 18.0f && !f.seated)
                PlaySfx(SayOf(l.kind), &pos, 0.8f, 0.9f + Rand01() * 0.2f);
        }
    }
    l.lastHealth = f.health;

    // the head follows the player when he is close
    float wantYaw = 0.0f;
    if (!dead && !f.seated && dist < 6.0f && dist > 0.5f) {
        Vec3 fwd = Flat(f.forward);
        Vec3 right(fwd.y, -fwd.x, 0.0f);
        Vec3 dir = Flat(playerPos - pos);
        wantYaw = Clamp(std::atan2(dir.x * right.x + dir.y * right.y, dir.x * fwd.x + dir.y * fwd.y), -1.0f, 1.0f);
    }
    l.headYaw += (wantYaw - l.headYaw) * Clamp(dt * 5.0f, 0.0f, 1.0f);
}

void VillagerTrade(int profession, int& offer, const Vec3& head) {
    const Profession& pr = kProfessions[((profession % 6) + 6) % 6];
    StartSwing();
    if (pr.numSells == 0 && pr.numBuys == 0) {
        PlaySfx(SND_VILLAGER_NO, &head);
        ShowMessage(std::string(LangStr(pr.name)) + Tr(": this villager has no profession", ": bu köylünün mesleği yok"));
        return;
    }
    ItemStack& held = gInv.Held();
    // selling to the villager
    for (int i = 0; i < pr.numBuys; ++i) {
        const Want& w = pr.buys[i];
        if (held.Empty() || held.id != w.item)
            continue;
        if (gInv.CountOf(w.item) < w.count) {
            PlaySfx(SND_VILLAGER_NO, &head);
            ShowMessage(std::string(LangStr(pr.name)) + ": " + std::to_string(w.count) + " " + ItemName(w.item) + Tr(" needed", " gerekli"));
            return;
        }
        gInv.Remove(w.item, w.count);
        GiveOrDrop(ID_EMERALD, 1);
        PlaySfx(SND_VILLAGER_TRADE, &head);
        HappyParticles(head);
        ShowMessage(std::string(Tr("Sold: ", "Sattın: ")) + std::to_string(w.count) + " " + ItemName(w.item) + " -> 1 " + ItemName(ID_EMERALD));
        gWorld.dirty = true;
        return;
    }
    // buying with emeralds
    if (!held.Empty() && held.id == ID_EMERALD && pr.numSells > 0) {
        const Offer& o = pr.sells[((offer % pr.numSells) + pr.numSells) % pr.numSells];
        if (gGame.gameMode != MODE_CREATIVE && gInv.CountOf(ID_EMERALD) < o.price) {
            PlaySfx(SND_VILLAGER_NO, &head);
            ShowMessage(std::string(LangStr(pr.name)) + ": " + std::to_string(o.price) + " " + ItemName(ID_EMERALD) + Tr(" needed", " gerekli"));
            return;
        }
        if (gGame.gameMode != MODE_CREATIVE)
            gInv.Remove(ID_EMERALD, o.price);
        GiveOrDrop(o.item, o.count);
        PlaySfx(SND_VILLAGER_TRADE, &head);
        HappyParticles(head);
        ShowMessage(std::string(Tr("Bought: ", "Aldın: ")) + std::to_string(o.count) + " " + ItemName(o.item));
        gWorld.dirty = true;
        return;
    }
    // anything else: show the next offer
    offer = (offer + 1) % 60;
    PlaySfx(SND_VILLAGER_YES, &head);
    ShowMessage(OfferText(pr, offer), 4.0f);
}

void DropVillagerLoot(const Vec3& at, bool illager) {
    SpawnXp(at, illager ? 5 + rand() % 3 : 1 + rand() % 5);
    struct Loot { uint16_t id; int maxCount; float chance; };
    if (illager) {
        // pillager loot
        const Loot loot[] = { { ID_ARROW, 3, 0.7f }, { ID_EMERALD, 1, 0.35f }, { ID_CROSSBOW, 1, 0.09f },
                              { ID_IRON_INGOT, 1, 0.15f } };
        for (auto& l : loot)
            if (Rand01() < l.chance)
                SpawnDropItem(at, l.id, 1 + rand() % l.maxCount);
    } else {
        const Loot loot[] = { { ID_ROTTEN_FLESH, 2, 0.45f }, { ID_BONE, 2, 0.3f }, { ID_STRING, 2, 0.35f },
                              { ID_GUNPOWDER, 2, 0.35f }, { ID_ARROW, 3, 0.3f }, { ID_PAPER, 2, 0.15f },
                              { ID_BREAD, 2, 0.25f }, { ID_EMERALD, 1, 0.12f } };
        for (auto& l : loot)
            if (Rand01() < l.chance)
                SpawnDropItem(at, l.id, 1 + rand() % l.maxCount);
    }
}

} // namespace mc
