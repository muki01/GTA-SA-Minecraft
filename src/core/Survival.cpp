#include "Survival.h"

#include <cstdlib>

#include "Audio.h"
#include "GameState.h"
#include "Host.h"
#include "Inventory.h"
#include "Items.h"
#include "Particles.h"
#include "World.h"

namespace mc {

Survival gSurvival;

void Survival::Respawn() {
    food = 20.0f;
    saturation = 5.0f;
    burn = 0.0f;
}

void Survival::NewGame() {
    burn = 0.0f;
    food = 20.0f;
    saturation = 5.0f;
    exhaustion = 0.0f;
    xpLevel = 0;
    xpTotal = 0;
    xpProgress = 0.0f;
}

// ---------------------------------------------------------------- status effects
void AddEffect(int effect, float seconds, int amplifier) {
    if (effect < 0 || effect >= EFFECT_COUNT || seconds <= 0.0f)
        return;
    EffectState& e = gSurvival.effects[effect];
    if (e.time > 0.0f && (e.amp > amplifier || (e.amp == amplifier && e.time >= seconds)))
        return;
    e.time = seconds;
    e.amp = amplifier;
    e.tick = 0.0f;
    if (effect == EFFECT_ABSORPTION)
        gSurvival.absorption = std::max(gSurvival.absorption, 4.0f * (amplifier + 1)); // two yellow hearts per level
}

void RemoveEffect(int effect) {
    if (effect < 0 || effect >= EFFECT_COUNT)
        return;
    gSurvival.effects[effect] = EffectState();
    if (effect == EFFECT_ABSORPTION)
        gSurvival.absorption = 0.0f;
}

void ClearEffects() {
    for (int i = 0; i < EFFECT_COUNT; ++i)
        RemoveEffect(i);
}

float AbsorbDamage(float loss, float maxHealth) {
    if (loss <= 0.0f)
        return 0.0f;
    if (HasEffect(EFFECT_RESISTANCE))
        loss *= std::max(0.0f, 1.0f - 0.2f * (gSurvival.effects[EFFECT_RESISTANCE].amp + 1));
    if (gSurvival.absorption > 0.0f) {
        const float unit = maxHealth / 20.0f;
        const float absorbed = std::min(loss, gSurvival.absorption * unit);
        gSurvival.absorption -= absorbed / unit;
        loss -= absorbed;
        if (gSurvival.absorption < 0.05f) {
            gSurvival.absorption = 0.0f;
            gSurvival.effects[EFFECT_ABSORPTION] = EffectState(); // the yellow hearts are gone, so is the effect
        }
    }
    return loss;
}

void EffectsTick(float dt, float& health, float maxHealth, SurvivalEvents& ev) {
    const float unit = maxHealth / 20.0f; // one Minecraft health point (half a heart)
    for (int i = 0; i < EFFECT_COUNT; ++i) {
        EffectState& e = gSurvival.effects[i];
        if (e.time <= 0.0f)
            continue;
        e.time -= dt;
        e.tick += dt;
        if (e.time <= 0.0f) {
            RemoveEffect(i);
            ev.expired |= 1u << i;
            continue;
        }
        switch (i) {
        case EFFECT_REGENERATION: {
            const float period = 2.5f / (float)(1 << std::min(e.amp, 5)); // 50 ticks >> amplifier
            while (e.tick >= period) {
                e.tick -= period;
                if (health < maxHealth)
                    health = std::min(maxHealth, health + unit);
            }
            break;
        }
        case EFFECT_POISON: {
            const float period = 1.25f / (float)(1 << std::min(e.amp, 4)); // 25 ticks >> amplifier
            while (e.tick >= period) {
                e.tick -= period;
                if (health > unit * 1.5f) { // poison never kills
                    health -= unit;
                    ev.directDamage += unit;
                }
            }
            break;
        }
        case EFFECT_HUNGER:
            gSurvival.exhaustion += 0.1f * (e.amp + 1) * dt; // 0.005 per tick and level
            break;
        default:
            break;
        }
    }
}

// ---------------------------------------------------------------- air
void BreathTick(float dt, bool underWater, bool mortal, float& health, float maxHealth, SurvivalEvents& ev) {
    Survival& s = gSurvival;
    if (underWater && mortal) {
        s.air -= 20.0f * dt;
        if (s.air <= -20.0f) {
            const float unit = maxHealth / 20.0f;
            s.air = 0.0f;
            health = std::max(0.0f, health - 2.0f * unit); // drowning ignores armour
            ev.directDamage += 2.0f * unit;
            ev.drowned = true;
        }
    } else {
        s.air = underWater ? kMaxAir : std::min(kMaxAir, s.air + 80.0f * dt);
    }
}

// ---------------------------------------------------------------- hunger
bool HungerTick(float dt, float& health, float maxHealth, SurvivalEvents& ev) {
    Survival& s = gSurvival;
    while (s.exhaustion >= 4.0f) {
        s.exhaustion -= 4.0f;
        if (s.saturation > 0)
            s.saturation = std::max(0.0f, s.saturation - 1.0f);
        else
            s.food = std::max(0.0f, s.food - 1.0f);
    }
    s.foodTimer += dt;
    if (s.foodTimer < 4.0f)
        return false;
    s.foodTimer = 0;
    if (s.food >= 18.0f && health > 0 && health < maxHealth) {
        health = std::min(maxHealth, health + maxHealth / 20.0f);
        s.exhaustion += 6.0f;
    } else if (s.food <= 0.0f && health > maxHealth * 0.1f) {
        health = std::max(maxHealth * 0.1f, health - maxHealth / 20.0f);
        ev.starved = true;
        PlaySfx(SND_HURT, nullptr, 0.6f);
        gGame.hurtTimer = 0.5f;
    }
    s.healthSeen = health; // (what the rules themselves did is no damage)
    return true;
}

void CreativeTick(float& health, float maxHealth) {
    gSurvival.food = 20.0f;
    if (health > 0.0f && health < maxHealth)
        health = maxHealth;
}

// ---------------------------------------------------------------- eating / drinking
bool CanEat(int item, bool mortal) {
    const ItemDef& d = Item(item);
    const bool milk = d.special == SP_MILK;
    const bool golden = item == ID_GOLDEN_APPLE || item == ID_ENCHANTED_GOLDEN_APPLE;
    return milk || golden || (d.food > 0 && mortal && gSurvival.food < 20.0f);
}

EatResult EatingTick(float dt, bool useHeld, bool mortal) {
    Survival& s = gSurvival;
    EatResult r;
    ItemStack& held = gInv.Held();
    if (held.Empty()) {
        s.eatTimer = 0.0f;
        return r;
    }
    const bool milk = Item(held.id).special == SP_MILK;
    if (!CanEat(held.id, mortal) || !useHeld) {
        s.eatTimer = 0.0f;
        return r;
    }
    r.eating = true;
    s.eatTimer += dt;
    s.eatSoundTimer -= dt;
    if (s.eatSoundTimer <= 0.0f) {
        s.eatSoundTimer = milk ? 0.25f : 0.2f;
        PlaySfx(milk ? SND_DRINK : SND_EAT, nullptr, 0.5f, 0.8f + Rand01() * 0.4f);
    }
    if (s.eatTimer >= kEatSeconds) {
        s.eatTimer = 0.0f;
        r.finished = held.id;
        FinishEating(mortal);
    }
    return r;
}

void FinishEating(bool mortal) {
    ItemStack& held = gInv.Held();
    const ItemDef& d = Item(held.id);
    if (d.special == SP_MILK) {
        ClearEffects(); // milk clears every effect
        if (mortal) {
            held.id = ID_BUCKET;
            held.count = 1;
            held.damage = 0;
        }
        PlaySfx(SND_BURP, nullptr, 0.5f);
        gWorld.dirty = true;
        return;
    }
    gSurvival.food = std::min(20.0f, gSurvival.food + d.food);
    gSurvival.saturation = std::min(gSurvival.food, gSurvival.saturation + d.saturation);
    switch (held.id) {
    case ID_GOLDEN_APPLE: // Regeneration II 5 s, Absorption I 2 min (two yellow hearts)
        AddEffect(EFFECT_REGENERATION, 5.0f, 1);
        AddEffect(EFFECT_ABSORPTION, 120.0f, 0);
        break;
    case ID_ENCHANTED_GOLDEN_APPLE: // Regeneration II 20 s, Absorption IV 2 min, Resistance and Fire Resistance 5 min
        AddEffect(EFFECT_REGENERATION, 20.0f, 1);
        AddEffect(EFFECT_ABSORPTION, 120.0f, 3);
        AddEffect(EFFECT_RESISTANCE, 300.0f, 0);
        AddEffect(EFFECT_FIRE_RESISTANCE, 300.0f, 0);
        break;
    case ID_ROTTEN_FLESH:
        if (Rand01() < 0.8f)
            AddEffect(EFFECT_HUNGER, 30.0f, 0);
        break;
    case ID_CHICKEN:
        if (Rand01() < 0.3f)
            AddEffect(EFFECT_HUNGER, 30.0f, 0);
        break;
    case ID_SPIDER_EYE:
        AddEffect(EFFECT_POISON, 5.0f, 0);
        break;
    case ID_POISONOUS_POTATO:
        if (Rand01() < 0.6f)
            AddEffect(EFFECT_POISON, 5.0f, 0);
        break;
    case ID_PUFFERFISH:
        AddEffect(EFFECT_HUNGER, 15.0f, 2);
        AddEffect(EFFECT_POISON, 60.0f, 1);
        break;
    case ID_HONEY_BOTTLE:
        RemoveEffect(EFFECT_POISON);
        break;
    default:
        break;
    }
    if (mortal && --held.count == 0)
        held.Clear();
    PlaySfx(SND_BURP, nullptr, 0.5f);
    gWorld.dirty = true;
}

// ---------------------------------------------------------------- armour and the totem
int ArmorPoints() {
    int points = 0;
    for (auto& a : gInv.armor)
        if (!a.Empty())
            points += Item(a.id).armorPoints;
    return points;
}

float ArmorBlock() { return Clamp(ArmorPoints() * 0.04f, 0.0f, 0.8f); }

int WearArmor(int amount) {
    int broke = 0;
    for (auto& a : gInv.armor) {
        if (a.Empty() || a.id == ID_ELYTRA)
            continue;
        const ItemDef& d = Item(a.id);
        a.damage += (uint16_t)amount;
        if (d.durability && a.damage >= d.durability) {
            ++broke;
            PlaySfx(SND_TOOL_BREAK);
            a.Clear();
        }
    }
    return broke;
}

bool UseTotem(float& health, float maxHealth) {
    if (!(health < maxHealth * 0.15f))
        return false;
    ItemStack* totem = nullptr;
    if (!gInv.Held().Empty() && Item(gInv.Held().id).special == SP_TOTEM)
        totem = &gInv.Held();
    else if (!gInv.offhand.Empty() && Item(gInv.offhand.id).special == SP_TOTEM)
        totem = &gInv.offhand;
    if (!totem)
        return false;
    if (--totem->count == 0)
        totem->Clear();
    health = maxHealth * 0.5f;
    ClearEffects();
    AddEffect(EFFECT_REGENERATION, 45.0f, 1);
    AddEffect(EFFECT_ABSORPTION, 5.0f, 1);
    AddEffect(EFFECT_FIRE_RESISTANCE, 40.0f, 0);
    PlaySfx(SND_TOTEM);
    return true;
}

// ---------------------------------------------------------------- experience
int XpToNextLevel(int level) {
    if (level >= 30)
        return 112 + (level - 30) * 9;
    if (level >= 15)
        return 37 + (level - 15) * 5;
    return 7 + level * 2;
}

bool AddXp(int points) {
    if (points <= 0)
        return false;
    Survival& s = gSurvival;
    const int before = s.xpLevel;
    s.xpTotal += points;
    float p = s.xpProgress * XpToNextLevel(s.xpLevel) + points;
    while (p >= XpToNextLevel(s.xpLevel)) {
        p -= XpToNextLevel(s.xpLevel);
        s.xpLevel++;
    }
    s.xpProgress = p / XpToNextLevel(s.xpLevel);
    const bool fanfare = s.xpLevel > before && s.xpLevel % 5 == 0;
    if (fanfare)
        PlaySfx(SND_LEVELUP, nullptr, 0.75f);
    gWorld.dirty = true;
    return fanfare;
}

int XpForBlock(int block) {
    auto range = [](int lo, int hi) { return lo + rand() % (hi - lo + 1); };
    switch (block) {
    case ID_COAL_ORE: case ID_DEEPSLATE_COAL_ORE: return range(0, 2);
    case ID_DIAMOND_ORE: case ID_DEEPSLATE_DIAMOND_ORE: return range(3, 7);
    case ID_EMERALD_ORE: case ID_DEEPSLATE_EMERALD_ORE: return range(3, 7);
    case ID_LAPIS_ORE: case ID_DEEPSLATE_LAPIS_ORE: return range(2, 5);
    case ID_REDSTONE_ORE: case ID_DEEPSLATE_REDSTONE_ORE: return range(1, 5);
    case ID_NETHER_QUARTZ_ORE: return range(2, 5);
    default: return 0;
    }
}

int XpOrbSize(int amount) {
    static const int kSizes[] = { 2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3, 1 };
    for (int s : kSizes)
        if (amount >= s)
            return s;
    return 1;
}

// ---------------------------------------------------------------- the host's health
bool HealthTick(float dt, float& health, float maxHealth, float direct, float* hostPool) {
    Survival& s = gSurvival;
    float h = health;
    gGame.hurtTimer = std::max(0.0f, gGame.hurtTimer - dt);
    if (h <= 0.0f) {
        if (gGame.deathTime < 0.0f && gGame.age - gGame.deathCauseTime > 3.0f)
            gGame.deathCause = STR_DEATH_GENERIC;
        gGame.deathTime = gGame.deathTime < 0.0f ? 0.0f : gGame.deathTime + dt;
        s.healthSeen = h;
        return false;
    }
    gGame.deathTime = -1.0f;
    if (s.healthSeen < 0.0f || s.healthSeen > maxHealth * 2.0f)
        s.healthSeen = h;
    const bool survival = gGame.gameMode == MODE_SURVIVAL;
    direct = std::min(direct, std::max(0.0f, s.healthSeen - h));
    if (h < s.healthSeen - 0.5f) {
        float loss = s.healthSeen - h - direct;
        if (survival && loss > 0.0f) {
            // Minecraft armour: every point blocks 4% of the damage (up to 80%) and wears the pieces
            const float block = ArmorBlock();
            if (block > 0.0f) {
                h = std::min(maxHealth, h + loss * block);
                health = h;
                WearArmor(std::max(1, (int)(loss / 20.0f)));
            }
            // then resistance and the yellow absorption hearts
            const float left = std::max(0.0f, s.healthSeen - direct - h);
            const float after = AbsorbDamage(left, maxHealth);
            if (after < left) {
                h = std::min(maxHealth, h + (left - after));
                health = h;
            }
            // then the yellow hearts the host keeps itself
            if (hostPool && *hostPool > 0.0f) {
                const float soak = std::min(std::max(0.0f, s.healthSeen - direct - h), *hostPool);
                if (soak > 0.0f) {
                    *hostPool -= soak;
                    h = std::min(maxHealth, h + soak);
                    health = h;
                }
            }
        }
        if (survival && loss + direct > 2.0f)
            PlaySfx(SND_HURT, nullptr, 0.7f);
        gGame.hurtTimer = 0.5f;
    }
    // totem of undying: saves the player at the last moment (held in either hand)
    bool totem = false;
    if (survival) {
        if (UseTotem(health, maxHealth)) {
            totem = true;
            Vec3 at;
            TheHost().PlayerPos(&at);
            for (int i = 0; i < 60; ++i) {
                Particle p;
                Vec3 d(Rand01() * 2 - 1, Rand01() * 2 - 1, Rand01() * 2);
                p.pos = at;
                p.vel = d * (2.0f + Rand01() * 3.0f);
                p.maxLife = p.life = 1.0f + Rand01();
                p.tile = TILE_P_SPARK_0;
                p.anim = 1;
                p.size = 0.12f;
                p.gravity = 1.5f;
                p.color = rand() % 2 ? 0xFFF0E040 : 0xFF60E040;
                p.glow = true;
                SpawnParticle(p);
            }
            ShowMessage(Tr("The Totem of Undying saved you!", "Ölümsüzlük Totemi seni kurtardı!"));
            gWorld.dirty = true;
        }
    }
    s.healthSeen = health;
    return totem;
}

// ---------------------------------------------------------------- bubbles
namespace {
float gBubbleTimer = 0.0f;

void Bubbles(const Vec3& at, int n) {
    for (int i = 0; i < n; ++i) {
        Particle p;
        p.pos = at + Vec3((Rand01() - 0.5f) * 0.4f, (Rand01() - 0.5f) * 0.4f, (Rand01() - 0.5f) * 0.3f);
        p.vel = Vec3((Rand01() - 0.5f) * 0.4f, (Rand01() - 0.5f) * 0.4f, 0.8f + Rand01() * 0.8f);
        p.maxLife = p.life = 0.6f + Rand01() * 0.6f;
        p.tile = TILE_P_BUBBLE;
        p.size = 0.04f + Rand01() * 0.03f;
        p.gravity = -1.0f;
        SpawnParticle(p);
    }
}
} // namespace

bool BurnTick(float dt, int hot, bool wet, bool mortal, float& health, float maxHealth) {
    Survival& s = gSurvival;
    if (!mortal || health <= 0.0f) {
        s.burn = 0.0f;
        return false;
    }
    const float unit = maxHealth / 20.0f; // one Minecraft health point
    const bool resist = HasEffect(EFFECT_FIRE_RESISTANCE);
    auto hurt = [&](float points, int cause) {
        if (resist)
            return;
        health = std::max(0.0f, health - points * unit);
        NoteDamage(cause);
    };
    // in fire or lava: a hit every half second (Minecraft's time of invulnerability), and he catches fire
    s.hotTimer = std::max(0.0f, s.hotTimer - dt);
    if (hot == ID_FIRE || hot == ID_LAVA) {
        s.burn = std::max(s.burn, hot == ID_LAVA ? 15.0f : 8.0f);
        if (s.hotTimer <= 0.0f) {
            s.hotTimer = 0.5f;
            hurt(hot == ID_LAVA ? 4.0f : 1.0f, hot == ID_LAVA ? STR_DEATH_LAVA : STR_DEATH_FIRE);
        }
    }
    if (wet)
        s.burn = 0.0f; // water and rain put him out
    if (s.burn <= 0.0f) {
        s.burnTimer = 0.0f;
        return false;
    }
    // on fire: a point a second until it goes out (while the flames under him hit, those count)
    s.burn = std::max(0.0f, s.burn - dt);
    s.burnTimer -= dt;
    if (s.burnTimer <= 0.0f) {
        s.burnTimer = 1.0f;
        if (hot == ID_AIR)
            hurt(1.0f, STR_DEATH_FIRE);
    }
    // flames all over him
    Vec3 at;
    if (Rand01() < 12.0f * dt && TheHost().PlayerPos(&at)) {
        Particle f;
        f.pos = at + Vec3((Rand01() - 0.5f) * 0.6f, (Rand01() - 0.5f) * 0.6f, Rand01() * 1.8f - 0.9f);
        f.vel = Vec3(0, 0, 0.6f + Rand01() * 0.6f);
        f.maxLife = f.life = 0.4f + Rand01() * 0.3f;
        f.tile = TILE_P_FLAME;
        f.size = 0.08f + Rand01() * 0.06f;
        f.gravity = 0.0f;
        f.glow = true;
        SpawnParticle(f);
    }
    return true;
}

void BreathEffects(float dt, bool bubbling, bool drowned) {
    if (drowned) {
        NoteDamage(STR_DEATH_DROWN);
        Bubbles(gGame.eyePos, 8);
    }
    if (bubbling) {
        gBubbleTimer -= dt;
        if (gBubbleTimer <= 0.0f) {
            gBubbleTimer = 0.9f + Rand01() * 0.8f;
            Bubbles(gGame.eyePos + gGame.lookDir * 0.25f - Vec3(0, 0, 0.1f), 2);
        }
    }
}

} // namespace mc
