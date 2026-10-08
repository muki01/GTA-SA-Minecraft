#pragma once
// GTA San Andreas as the host of the Minecraft core (src/core/Host.h): GtaHost.cpp holds the answers about its
// map, water, weather and player, and does what only GTA can do (its own explosions).

namespace mc {

// GTA's own explosions (grenades, rockets, cars) break blocks too. Every frame.
void PollExplosions(float dt);

} // namespace mc
