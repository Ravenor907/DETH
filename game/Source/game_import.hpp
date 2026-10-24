/**
 * @file game_import.hpp
 *
 * DETH: imports the Diablo and Hellfire game files on Windows and Linux. Accepts the MPQ files
 * themselves, folders, archives (zip, 7z, rar, tar, gz, xz, also password protected and nested)
 * and GOG offline installers. Archives are opened with the bundled 7-Zip, installers with the
 * bundled innoextract. Android has its own importer (ImportActivity).
 */
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace devilution {

/** Something the player can import. */
struct ImportCandidate {
	std::string path;
	std::string name;
	/** Where it was found, for display ("Downloads", "Downloads/DETH", ...). */
	std::string where;
	std::uintmax_t size = 0;
	bool isFolder = false;
};

struct ImportResult {
	/** Game files now in the data folder because of this import. */
	std::vector<std::string> imported;
	/** Game files that were already there (same size). */
	std::vector<std::string> alreadyThere;
	/** Archives skipped (password cancelled, unreadable), with the reason. */
	std::vector<std::string> problems;
};

/** Asks for the password of `archiveName`; `retry` is true after a wrong one. nullopt = skip it. */
using PasswordPrompt = std::function<std::optional<std::string>(std::string_view archiveName, bool retry)>;
/** Shows what is being worked on. */
using ImportProgress = std::function<void(std::string_view message)>;

/** True when this build can import on its own (desktop with the bundled tools). */
bool GameImportSupported();

/** Game files and archives in the usual places (Downloads, the game data folder). */
std::vector<ImportCandidate> FindImportCandidates();

/** Imports from a file or folder into the game data folder. */
ImportResult ImportGameFiles(const std::string &path, const PasswordPrompt &askPassword, const ImportProgress &progress);

/** Folder the game files are imported into (with a trailing separator). */
std::string ImportDestination();

/** True for the MPQ names DETH uses (diabdat, spawn, hellfire, hfmonk, ...). */
bool IsGameMpqName(std::string_view fileName);

/** True for file names the importer can open as archives or installers. */
bool IsImportableArchiveName(std::string_view fileName);

} // namespace devilution
