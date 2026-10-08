#pragma once

namespace mc {

// Raw keyboard state (only while the GTA window has focus). PollKeys() once per frame.
void PollKeys();
bool KeyPressed(int vk);
bool KeyDown(int vk);
bool MouseLeft();
bool MouseLeftPressed();
bool MouseRight();
bool MouseRightPressed();
bool MouseRightReleased();

} // namespace mc
