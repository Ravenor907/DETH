#pragma once

#include "engine/point.hpp"

#ifdef USE_SDL3
#include <SDL3/SDL_render.h>
#else
#include <SDL.h>
#endif

namespace devilution {

/** DETH: the six menu buttons in the top-right corner with the compact panel (desktop; phones use the touch strip). */
bool CompactMenuStripVisible();
/** Bottom edge of the strip in game pixels (0 when hidden), so top-right overlays can sit under it. */
int CompactMenuStripBottom();
/** The button under `position`: 0 character, 1 quests, 2 inventory, 3 spellbook, 4 game menu, 5 map; -1 for none. */
int CompactMenuButtonAt(Point position);
void RenderCompactMenuStrip(SDL_Renderer *renderer);

} // namespace devilution
