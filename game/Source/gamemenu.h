/**
 * @file gamemenu.h
 *
 * Interface of the in-game menu functions.
 */
#pragma once

#include "utils/attributes.h"

namespace devilution {

/** DETH: true while the full settings screen is open from the game menu (no touch overlay then). */
extern bool InGameSettingsOpen;

void gamemenu_on();
void gamemenu_off();
void gamemenu_handle_previous();
void gamemenu_exit_game(bool bActivate);
void gamemenu_quit_game(bool bActivate);
void gamemenu_load_game(bool bActivate);
void gamemenu_save_game(bool bActivate);

extern DVL_API_FOR_TEST bool isGameMenuOpen;

} // namespace devilution
