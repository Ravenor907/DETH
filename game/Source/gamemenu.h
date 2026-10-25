/**
 * @file gamemenu.h
 *
 * Interface of the in-game menu functions.
 */
#pragma once

#include "engine/surface.hpp"

#include "utils/attributes.h"

namespace devilution {

/** DETH: true while the full settings screen is open from the game menu (no touch overlay then). */
extern bool InGameSettingsOpen;
/** @brief DETH: the player chose Exit or Quit (not the host leaving or a dropped connection). */
extern bool MultiplayerLeftByChoice;

void gamemenu_on();
/** @brief DETH: the two sliders at the sides of the game menu (left: Zoom or another setting, right: UI Size). */
void GamemenuDrawSides(const Surface &out);
/** @brief Mouse or finger on the side sliders; true when they took it. */
bool GamemenuSidesMouse(bool isDown);
bool GamemenuSidesMove();
void gamemenu_off();
void gamemenu_handle_previous();
void gamemenu_exit_game(bool bActivate);
void gamemenu_quit_game(bool bActivate);
void gamemenu_load_game(bool bActivate);
void gamemenu_save_game(bool bActivate);

extern DVL_API_FOR_TEST bool isGameMenuOpen;

} // namespace devilution
