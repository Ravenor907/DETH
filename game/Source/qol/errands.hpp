/**
 * @file qol/errands.hpp
 *
 * DETH: errands for the town folk. Griswold, Pepin and Adria each send the hero after an artifact
 * that a unique monster carries; every artifact brought back makes their wares finer. Ogden's
 * errand makes gold piles hold 50,000 instead of 5,000. Progress belongs to the hero (saved with it).
 */
#pragma once

#include <cstdint>
#include <string>

namespace devilution {

struct Player;

enum Errand : uint8_t {
	ErrandSmith,
	ErrandHealer,
	ErrandWitch,
	ErrandTavern,
	ErrandCount,
};

struct ErrandState {
	/** Artifacts brought back so far. */
	uint8_t tier = 0;
	/** Dungeon level the artifact is found from (that level or deeper); 0 = no hunt running. */
	uint8_t level = 0;
	/** The artifact has been found and can be handed over. */
	bool found = false;
	/** Which artifact (index into the errand's artifact names). */
	uint8_t artifact = 0;
};

/** Gold pile size with Ogden's errand done. Gold piles are 16 bit in saves and network messages. */
constexpr int BigGoldPileLimit = 50000;

/** The errand of a towner (TOWN_SMITH, ...), or ErrandCount when they have none. */
Errand ErrandForTowner(int townerId);

int ErrandMaxTier(Errand errand);
bool ErrandAvailable(const Player &player, Errand errand);
std::string ErrandArtifactName(Errand errand, int artifact);
std::string ErrandGiverName(Errand errand);

/** Picks a new artifact and level for an offer (not yet accepted). */
ErrandState ErrandOffer(const Player &player, Errand errand);

/** A unique monster died on the level the local hero is on. */
void ErrandUniqueKilled();

/** Item level bonus for a store from the artifacts brought to its owner. */
int ErrandStoreBonus(const Player &player, Errand errand);

/** True once Ogden's errand is done: gold piles hold BigGoldPileLimit. */
bool HasBigGoldPiles(const Player &player);

} // namespace devilution
