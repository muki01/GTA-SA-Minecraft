#pragma once

#include "ModCommon.h"

namespace mc {

void GuiDrawHud();          // drawing event
void GuiProcessInput();     // script phase, while a screen is open
int GuiScale();
bool GuiMouseOverWindow();

} // namespace mc
