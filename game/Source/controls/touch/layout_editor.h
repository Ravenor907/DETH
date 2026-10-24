#pragma once

#include "engine/surface.hpp"

namespace devilution {

/** @brief DETH: the touch layout editor is offered (phones; on other systems only for testing). */
bool TouchLayoutEditorAvailable();

/** @brief Moves and resizes the on-screen controls as saved in the layout editor (after the default layout). */
void ApplyTouchLayout();

/** @brief Runs the editor over the game until Done (or Back). Saves when it closes. */
void RunTouchLayoutEditor();

/** @brief Asks for the editor once the in-game Settings screen has closed. */
void RequestTouchLayoutEditor();
/** @brief Runs the editor if it was requested. */
void RunRequestedTouchLayoutEditor();

[[nodiscard]] bool TouchLayoutEditorActive();

/** @brief Draws the editor's own panel and the selection (called while drawing the game). */
void DrawTouchLayoutEditor(const Surface &out);

} // namespace devilution
