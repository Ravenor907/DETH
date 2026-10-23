#pragma once

#include <cstdint>

namespace devilution {

/** @brief DETH: the player chose "Load Autosave" (single player). */
extern bool SelheroLoadAutosave;


extern bool selhero_isMultiPlayer;
extern bool selhero_endMenu;

void selhero_Init();
void selhero_List_Init();

} // namespace devilution
