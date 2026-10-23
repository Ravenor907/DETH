#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace devilution {

/** DETH: the three game types a player can choose from the main menu. */
enum class GameType : uint8_t {
	Shareware = 0,
	Diablo = 1,
	Hellfire = 2,
};

/** The game type currently loaded. */
[[nodiscard]] GameType CurrentGameType();
/** Display name ("Shareware", "Diablo", "Hellfire"). */
[[nodiscard]] std::string_view GameTypeName(GameType type);
/** Whether the files for a game type are installed. */
[[nodiscard]] bool IsGameTypeAvailable(GameType type);
/** The files a game type needs, for messages ("DIABDAT.MPQ and hellfire.mpq"). */
[[nodiscard]] std::string_view GameTypeRequiredFiles(GameType type);
/** Only the files that are not installed yet, comma separated (empty when complete). */
[[nodiscard]] std::string MissingGameTypeFiles(GameType type);

/** Shows the game type picker; switches the loaded game if the player picks another available one. */
void UiSelectGameType();

} // namespace devilution
