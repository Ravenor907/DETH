/**
 * @file pfile.h
 *
 * Interface of the save game encoding functionality.
 */
#pragma once

#include <cstdint>
#include <vector>
#include <ctime>
#include <optional>
#include <expected>
#include <string>

#include "DiabloUI/diabloui.h"
#include "player.h"

#ifdef UNPACKED_SAVES
#include "utils/file_util.h"
#else
#include "mpq/mpq_reader.hpp"
#include "mpq/mpq_writer.hpp"
#endif

namespace devilution {

#define MAX_CHARACTERS 99

extern bool gbValidSaveFile;

#ifdef UNPACKED_SAVES
struct SaveReader {
	explicit SaveReader(std::string &&dir)
	    : dir_(std::move(dir))
	{
	}

	const std::string &dir() const
	{
		return dir_;
	}

	std::unique_ptr<std::byte[]> ReadFile(const char *filename, std::size_t &fileSize, int32_t &error);

	bool HasFile(const char *path)
	{
		return ::devilution::FileExists((dir_ + path).c_str());
	}

private:
	std::string dir_;
};

struct SaveWriter {
	explicit SaveWriter(std::string &&dir)
	    : dir_(std::move(dir))
	{
	}

	bool WriteFile(const char *filename, const std::byte *data, size_t size);

	bool HasFile(const char *path)
	{
		return ::devilution::FileExists((dir_ + path).c_str());
	}

	void RenameFile(const char *from, const char *to)
	{
		::devilution::RenameFile((dir_ + from).c_str(), (dir_ + to).c_str());
	}

	void RemoveHashEntry(const char *path)
	{
		RemoveFile((dir_ + path).c_str());
	}

	void RemoveHashEntries(bool (*fnGetName)(uint8_t, char *));

private:
	std::string dir_;
};

#else
using SaveReader = MpqArchive;
using SaveWriter = MpqWriter;
#endif

/**
 * @brief Comparison result of pfile_compare_hero_demo
 */
struct HeroCompareResult {
	enum Status : uint8_t {
		ReferenceNotFound,
		Same,
		Difference,
	};
	Status status;
	std::string message;
};

std::optional<SaveReader> OpenSaveArchive(uint32_t saveNum);
std::optional<SaveReader> OpenStashArchive();
const char *pfile_get_password();
std::unique_ptr<std::byte[]> ReadArchive(SaveReader &archive, const char *pszName, size_t *pdwLen = nullptr);
void pfile_write_hero(bool writeGameData = false);

#ifndef DISABLE_DEMOMODE
/**
 * @brief Save a reference game-state (save game) for the demo recording
 * @param demo that is recorded
 */
void pfile_write_hero_demo(int demo);
/**
 * @brief Compares the actual game-state (savegame) with a reference game-state (save game from demo recording)
 * @param demo for the comparison
 * @param logDetails in case of a difference log details
 * @return The comparison result.
 */
HeroCompareResult pfile_compare_hero_demo(int demo, bool logDetails);
#endif

void sfile_write_stash();
bool pfile_ui_set_hero_infos(bool (*uiAddHeroInfo)(_uiheroinfo *));
void pfile_ui_set_class_stats(HeroClass playerClass, _uidefaultstats *classStats);
uint32_t pfile_ui_get_first_unused_save_num();
bool pfile_ui_save_create(_uiheroinfo *heroinfo);
bool pfile_delete_save(_uiheroinfo *heroInfo);
void pfile_read_player_from_save(uint32_t saveNum, Player &player);
void pfile_save_level();
std::expected<void, std::string> pfile_convert_levels();
void pfile_remove_temp_files();
std::unique_ptr<std::byte[]> pfile_read(const char *pszName, size_t *pdwLen);
void pfile_update(bool forceSave);

/** @brief DETH: when the hero's autosave (multiplayer: backup) was written, if there is one. */
std::optional<std::time_t> pfile_autosave_time(uint32_t saveNum);
/**
 * @brief DETH: single player plays from a working copy of the chosen save (manual or autosave),
 * so autosaves never overwrite the manual save. Call when the hero is chosen.
 */
void pfile_begin_session(bool fromAutosave);
/** @brief DETH: a single player save on disk: a hero's manual save, autosave or an older kept copy. */
struct SaveHistoryEntry {
	uint32_t saveNum = 0;
	std::time_t time = 0;
	bool autosave = false;
	std::string path;
	std::string heroName;
	int level = 0;
	HeroClass heroClass = HeroClass::Warrior;
};
/** @brief DETH: the newest saves of all heroes (single player or multiplayer, as now played), newest first (each hero keeps its last 5). */
std::vector<SaveHistoryEntry> pfile_recent_saves(size_t count);
/** @brief DETH: like pfile_begin_session, from any save file (Continue and the recent saves list). */
void pfile_begin_session_from(const std::string &path);
/** @brief DETH: ends the working copy (back in the menu). */
void pfile_end_session();
/** @brief DETH: after a manual Save Game: the working copy becomes the manual save. */
void pfile_manual_saved();
/** @brief DETH: writes the autosave (single player) or the backup copy (multiplayer). */
void pfile_autosave();
/** @brief DETH: multiplayer: the hero was just saved; keep a copy in its history (exit, or Save Character). */
void pfile_multiplayer_saved(bool autosave);
/** @brief DETH: multiplayer: the hero's save becomes the given kept copy (an older save picked in the list). */
void pfile_restore_hero_from(uint32_t saveNum, const std::string &path);
/** @brief DETH: multiplayer: replaces the hero's save with its backup copy. */
bool pfile_restore_backup(uint32_t saveNum);

} // namespace devilution
