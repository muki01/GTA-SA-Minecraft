#pragma once
// Holes in GTA models. A building that has carved cells (Carve.h) gets its own copy of its render mesh with the
// triangles inside those cells cut out, so the broken spot really is open: GTA draws whatever is behind it (the
// blocks inside, the other side of a tunnel, the sky) itself. The copy belongs to that one building in the world;
// the model and its other copies keep their mesh.

namespace mc {

void InstallGeoCutHooks();
// after GTA has decided what to draw this frame (new buildings that streamed in get their holes before they are seen)
void GeoCutUpdate();

} // namespace mc
