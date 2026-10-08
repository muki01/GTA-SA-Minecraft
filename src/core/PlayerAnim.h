#pragma once
// How the player's body moves in Minecraft: walking and swimming, view bobbing, arm swings, the hand dropping while
// the item changes or the attack recharges, the wider view when sprinting or flying; which sprite an item shows while
// it is used (drawn bow, cast rod, loaded crossbow); the pose of his arms; the red hurt flash; the dead tipping over.
// The host draws the models with what these give it (McModel.h).

#include "Core.h"
#include "McModel.h"

namespace mc {

// what the host knows about the player's body this frame
struct PlayerMotion {
    bool alive = true;
    bool onFoot = true;
    bool standing = true;  // on the ground (view bobbing)
    bool swimming = false; // swimming in the host's water
    Vec3 velocity;         // m/s
};
void PlayerAnimTick(float dt, const PlayerMotion* m); // nullptr: there is no player (only the clock runs)
float SwimPose();   // 0 upright .. 1 swimming flat
float ElytraDive(); // how far the elytra are spread while gliding (1 = open)
int HeldItemTile(uint16_t id); // sprite shown instead of the item's own one, -1 = none
// LivingEntityRenderer: dead for `seconds`, the body tips over onto its side
void DeathTilt(float seconds, Vec3& right, Vec3& up);
// what DrawPlayerFull needs besides the body's place (right / fwd: its axes): animation, items, armour, hurt flash
PlayerDrawInput PlayerLook(const Vec3& right, const Vec3& fwd, bool inVehicle, bool crouching, bool dead, float light);
// the first-person main hand (left: the off hand); the host adds its camera's sway, the arm's look and the light
FirstPersonInput HandInput(bool left);

} // namespace mc
