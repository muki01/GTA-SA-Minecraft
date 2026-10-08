#pragma once
// The world file: the player (game mode, hotbar slot, view, hunger, inventory, experience) and the blocks. The host
// opens and closes the file and may put sections of its own after the core's part (GTA: dug ground, broken walls).

#include "Core.h"

namespace mc {

// what a world file said besides what went straight into the game
struct WorldFileInfo {
    uint32_t version = 0;
    int hostValue = 0;     // the number the host gave WriteWorldFile
    bool blocksOk = false; // the blocks were read to the end
    bool hostPart = false; // the host's own sections follow in the file
};

// Writes the core's part. hostValue: one number of the host's own that is kept in the header.
void WriteWorldFile(FILE* f, int hostValue);
// Reads it into the game. False: not a world file of ours, or of a version we cannot read - nothing was changed.
bool ReadWorldFile(FILE* f, WorldFileInfo& info);

} // namespace mc
