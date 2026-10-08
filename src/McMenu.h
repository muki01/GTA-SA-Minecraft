#pragma once
// GTA's front end in Minecraft's clothes: title screen, world list (the GTA save games), pause menu.
// GTA's own menu logic keeps doing the work (loading, saving, options); only these screens are drawn and
// clicked the Minecraft way.

namespace mc {

// hooks into CMenuManager's drawing and input and into the save / delete of a save slot
void InstallMenuHooks();

} // namespace mc
