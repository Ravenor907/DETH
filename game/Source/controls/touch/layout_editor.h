#pragma once

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include "engine/surface.hpp"

namespace devilution {

/** @brief DETH: the touch layout editor is offered (phones; on other systems only for testing). */
bool TouchLayoutEditorAvailable();

/** @brief Moves and resizes the on-screen controls as saved in the layout editor (after the default layout). */
void ApplyTouchLayout();

/** @brief Opens the editor over the running game; it closes (and saves) with Done or Back. */
void RunTouchLayoutEditor();
/** @brief While the editor is open, takes every input event (true when handled). */
bool HandleTouchLayoutEditorEvent(SDL_Event &event);

/** @brief Asks for the editor once the in-game Settings screen has closed. */
void RequestTouchLayoutEditor();
/** @brief Runs the editor if it was requested. */
void RunRequestedTouchLayoutEditor();

[[nodiscard]] bool TouchLayoutEditorActive();

/** @brief Draws the editor's own panel and the selection (called while drawing the game). */
void DrawTouchLayoutEditor(const Surface &out);

} // namespace devilution
