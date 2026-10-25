#include "qol/errands.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#include "engine/random.hpp"
#include "engine/sound.h"
#include "engine/sound_defs.hpp"
#include "levels/gendung.h"
#include "player.h"
#include "plrmsg.h"
#include "towners.h"
#include "utils/format.hpp"
#include "utils/language.h"

namespace devilution {

namespace {

constexpr int ArtifactsPerErrand = 5;

const std::array<std::array<const char *, ArtifactsPerErrand>, ErrandCount> ArtifactNames = { {
	{ N_("Ember Ingot"), N_("Runed Whetstone"), N_("Forgemaster's Seal"), N_("Tongs of the Old Smithy"), N_("Hammer of Kael") },
	{ N_("Silver Mortar"), N_("Vial of Dawn"), N_("Healer's Codex"), N_("Sunleaf Bundle"), N_("Pestle of Mercy") },
	{ N_("Moonstone Orb"), N_("Raven Skull"), N_("Lost Grimoire"), N_("Starfire Crystal"), N_("Black Candle") },
	{ N_("Golden Ledger"), N_("Gilded Tankard"), N_("Merchant's Seal"), N_("Strongbox Key"), N_("Tavern Deed") },
} };

const std::array<const char *, ErrandCount> GiverNames = { N_("Griswold"), N_("Pepin"), N_("Adria"), N_("Ogden") };

} // namespace

Errand ErrandForTowner(int townerId)
{
	switch (townerId) {
	case TOWN_SMITH: return ErrandSmith;
	case TOWN_HEALER: return ErrandHealer;
	case TOWN_WITCH: return ErrandWitch;
	case TOWN_TAVERN: return ErrandTavern;
	default: return ErrandCount;
	}
}

int ErrandMaxTier(Errand errand)
{
	return errand == ErrandTavern ? 1 : 3;
}

bool ErrandAvailable(const Player &player, Errand errand)
{
	if (errand >= ErrandCount)
		return false;
	const ErrandState &state = player.errands[errand];
	return state.level != 0 || state.tier < ErrandMaxTier(errand);
}

std::string ErrandArtifactName(Errand errand, int artifact)
{
	return std::string(_(ArtifactNames[errand][std::clamp(artifact, 0, ArtifactsPerErrand - 1)]));
}

std::string ErrandGiverName(Errand errand)
{
	return std::string(_(GiverNames[errand]));
}

ErrandState ErrandOffer(const Player &player, Errand errand)
{
	const ErrandState &current = player.errands[errand];
	ErrandState offer = current;
	offer.found = false;
	offer.artifact = static_cast<uint8_t>(GenerateRnd(ArtifactsPerErrand));
	// Deeper with every artifact brought back: levels 3-6, 7-10, 11-14. Ogden's is in the caves or below.
	const int base = errand == ErrandTavern ? 9 : 3 + 4 * current.tier;
	offer.level = static_cast<uint8_t>(std::min(base + GenerateRnd(4), 15));
	return offer;
}

void ErrandUniqueKilled()
{
	Player &player = *MyPlayer;
	if (setlevel || currlevel == 0)
		return;
	for (int i = 0; i < ErrandCount; i++) {
		ErrandState &state = player.errands[i];
		if (state.level == 0 || state.found || currlevel < state.level)
			continue;
		state.found = true;
		const auto errand = static_cast<Errand>(i);
		EventPlrMsg(FormatRuntime(_("You found the {:s}! Bring it to {:s}."), ErrandArtifactName(errand, state.artifact), ErrandGiverName(errand)));
		PlaySFX(SfxID::ItemGold);
		return; // one artifact per kill
	}
}

int ErrandStoreBonus(const Player &player, Errand errand)
{
	return 2 * player.errands[errand].tier;
}

bool HasBigGoldPiles(const Player &player)
{
	return player.errands[ErrandTavern].tier >= 1;
}

} // namespace devilution
