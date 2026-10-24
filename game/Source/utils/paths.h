#pragma once

#include <string>

namespace devilution {

namespace paths {

const std::string &BasePath();
const std::string &PrefPath();
const std::string &ConfigPath();
const std::string &AssetsPath();

/**
 * @brief DETH game data drop folder: <Downloads>/DETH/ (Linux only, empty elsewhere).
 *
 * Players put DIABDAT.MPQ / spawn.mpq / Hellfire MPQs here.
 */
const std::string &DropPath();

/** DETH: the user's Downloads folder with a trailing separator (Windows and Linux; empty elsewhere). */
std::string DownloadsDir();

/**
 * @brief DETH: folder for characters and stashes. Defaults to <Downloads>/DETH/saves/ (Windows,
 * Linux, Android with storage access) so uninstalling the game never deletes them; falls back
 * to PrefPath(). Saves already in PrefPath() are copied over once (the originals stay).
 * `--save-dir` (or DETH_SAVE_DIR) pins it to that folder instead.
 */
std::string SavePath();

void SetBasePath(const std::string &path);
void SetPrefPath(const std::string &path);
void SetConfigPath(const std::string &path);
void SetAssetsPath(const std::string &path);

} // namespace paths

} // namespace devilution
