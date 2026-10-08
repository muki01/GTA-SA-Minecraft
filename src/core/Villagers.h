#pragma once
// Minecraft's villagers, pillagers and vindicators: how they act around the player (walking animation, hurt flash,
// their sounds, the head that follows him), what a villager of each profession trades, and what they leave when they
// die. The host decides who of its own people is which and draws them.

#include "Core.h"

namespace mc {

enum NpcKind { NPC_VILLAGER = 0, NPC_PILLAGER, NPC_VINDICATOR };

struct NpcLook {
    int kind = NPC_VILLAGER;
    int variant = 0; // villager profession (0 none, 1 farmer, 2 librarian, 3 butcher, 4 cleric, 5 armorer)
    float limbSwing = 0.0f, limbAmount = 0.0f;
    float headYaw = 0.0f;
    float hurt = 0.0f;
    float lastHealth = -1.0f;
    float death = -1.0f;
    float say = 0.0f;
    int offer = 0; // trade the villager shows next
};

// what the host knows about one of its people this frame
struct NpcFacts {
    Vec3 pos, forward;
    float speed = 0.0f; // m/s over the ground (0 when seated)
    float health = 0.0f;
    bool dead = false;
    bool seated = false; // in a vehicle
};

NpcLook NpcStart(int kind, int profession, float health, bool dead);
void NpcTick(NpcLook& l, float dt, const NpcFacts& f, const Vec3& playerPos);
// The player right-clicks a villager of `profession` with what he holds: he sells, buys or sees the next offer
// (`offer` moves on). `head`: where the sounds and the happy particles come from.
void VillagerTrade(int profession, int& offer, const Vec3& head);
// what a dead villager or pillager leaves behind: experience and a few items
void DropVillagerLoot(const Vec3& at, bool illager);

} // namespace mc
