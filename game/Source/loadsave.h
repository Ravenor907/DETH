/**
 * @file loadsave.h
 *
 * Interface of save game functionality.
 */
#pragma once

#include <cstdint>
#include <expected>
#include <vector>

#include "pfile.h"
#include "player.h"
#include "utils/attributes.h"

namespace devilution {

extern DVL_API_FOR_TEST bool gbIsHellfireSaveGame;
extern DVL_API_FOR_TEST uint8_t giNumberOfLevels;

void RemoveInvalidItem(Item &pItem);
_item_indexes RemapItemIdxFromDiablo(_item_indexes i);
_item_indexes RemapItemIdxToDiablo(_item_indexes i);
_item_indexes RemapItemIdxFromSpawn(_item_indexes i);
_item_indexes RemapItemIdxToSpawn(_item_indexes i);
bool IsHeaderValid(uint32_t magicNumber);
void LoadHotkeys();
void LoadHeroItems(Player &player);
/** DETH: what the hero bought from Cain (the stash from anywhere), in its own save section. */
void SaveHeroAbilities(SaveWriter &saveWriter, const Player &player);
void LoadHeroAbilities(Player &player, uint32_t saveNum);
/** DETH: rows 5 and 6 of the inventory (and how many were bought from Cain), kept apart so older versions still read the save. */
void SaveInventoryExtraRows(SaveWriter &saveWriter, const Player &player, const char *section);
void LoadInventoryExtraRows(Player &player, uint32_t saveNum, const char *section, const char *otherSection, bool rescueItems);
/** DETH: items that found no room when loading; they go to the stash once it is loaded. */
extern std::vector<Item> StashDelivery;
extern int StashDeliveryGold;
void DeliverToStash();
/**
 * @brief Remove invalid inventory items from the inventory grid
 * @param player The player to remove invalid items from
 */
void RemoveEmptyInventory(Player &player);

/**
 * @brief Load game state
 * @param firstflag Can be set to false if we are simply reloading the current game
 */
std::expected<void, std::string> LoadGame(bool firstflag);
void SaveHotkeys(SaveWriter &saveWriter, const Player &player);
void SaveHeroItems(SaveWriter &saveWriter, Player &player);
void SaveGameData(SaveWriter &saveWriter);
void SaveGame();
void SaveLevel(SaveWriter &saveWriter);
std::expected<void, std::string> LoadLevel();
std::expected<void, std::string> ConvertLevels(SaveWriter &saveWriter);
void LoadStash();
void SaveStash(SaveWriter &stashWriter);

} // namespace devilution
