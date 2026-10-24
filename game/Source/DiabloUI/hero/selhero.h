#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace devilution {

/** @brief DETH: the player chose "Load Autosave" (single player). */
extern bool SelheroLoadAutosave;
/** DETH: Continue or a recent save picked: the save file to play from. */
extern std::optional<std::string> SelheroLoadPath;


extern bool selhero_isMultiPlayer;
extern bool selhero_endMenu;

void selhero_Init();
void selhero_List_Init();

} // namespace devilution
