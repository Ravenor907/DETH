/**
 * @file scroll_books_test.cpp
 *
 * DETH "Scroll Books" mod: Adria sells tomes that hold up to 50 scrolls of one spell.
 */
#include <gtest/gtest.h>

#include "ui_test.hpp"

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "inv.h"
#include "items.h"
#include "options.h"
#include "player.h"
#include "stores.h"
#include "tables/itemdat.h"
#include "data/file.hpp"
#include "pack.h"
#include "levels/gendung.h"
#include "qol/autopickup.h"

namespace devilution {
namespace {

void SetScrollBooksMod(bool enabled)
{
	GetOptions().Mods.AddModEntry("Scroll Books");
	for (OptionEntryBase *entry : GetOptions().Mods.GetEntries()) {
		if (entry->GetName() == "Scroll Books")
			static_cast<OptionEntryBoolean *>(entry)->SetValue(enabled);
	}
}

class ScrollBooksTest : public UITest {
protected:
	static void SetUpTestSuite()
	{
		UITest::SetUpTestSuite();
		if (!HaveMainData())
			return;
		// What the mod's init.lua does when the item data is loaded.
		DataFile dataFile = DataFile::loadOrDie("lua\\mods\\Scroll Books\\itemdat.tsv");
		LoadItemDatFromFile(dataFile, "lua\\mods\\Scroll Books\\itemdat.tsv", 30000);
	}

	void SetUp() override
	{
		UITest::SetUp();
		if (IsSkipped())
			return;
		SetScrollBooksMod(true);
	}

	void TearDown() override
	{
		SetScrollBooksMod(false);
		UITest::TearDown();
	}

	static _item_indexes TomeOf(SpellID spell)
	{
		for (size_t i = 0; i < AllItemsList.size(); ++i) {
			const auto idx = static_cast<_item_indexes>(i);
			if (IsScrollBookItemData(idx) && AllItemsList[i].iSpell == spell)
				return idx;
		}
		return IDI_NONE;
	}

	static Item MakeItem(_item_indexes idx)
	{
		Item item {};
		item._iSeed = AdvanceRndSeed();
		GetItemAttrs(item, idx, 1);
		item._iIdentified = true;
		item._iStatFlag = true;
		return item;
	}

	static void ClearInventory(Player &player)
	{
		for (int i = player._pNumInv - 1; i >= 0; --i)
			player.RemoveInvItem(i, false);
		for (int i = 0; i < MaxBeltItems; ++i)
			player.SpdList[i].clear();
	}
};

TEST_F(ScrollBooksTest, AdriaSellsAllFourTomes)
{
	SetRndSeed(42);
	SpawnWitch(16);
	int tomes = 0;
	for (const Item &item : WitchItems) {
		if (!item.isScrollBook())
			continue;
		++tomes;
		EXPECT_EQ(item._iIvalue, 3000) << item._iName;
		EXPECT_EQ(item._iCharges, ScrollBookCapacity) << item._iName << " comes full";
		EXPECT_EQ(item._iMaxCharges, ScrollBookCapacity) << item._iName;
	}
	EXPECT_EQ(tomes, 4);
	EXPECT_LE(WitchItems.size(), static_cast<size_t>(NumWitchItemsMax));
}

TEST_F(ScrollBooksTest, NoTomesWithoutTheMod)
{
	SetScrollBooksMod(false);
	SetRndSeed(42);
	SpawnWitch(16);
	for (const Item &item : WitchItems)
		EXPECT_FALSE(item.isScrollBook()) << item._iName;
}

TEST_F(ScrollBooksTest, ScrollsGoIntoTheTomeAndAreCastFromIt)
{
	Player &player = *MyPlayer;
	ClearInventory(player);
	leveltype = DTYPE_CATHEDRAL; // Town Portal cannot be read in town
	const _item_indexes tomeIdx = TomeOf(SpellID::TownPortal);
	ASSERT_NE(tomeIdx, IDI_NONE);

	Item tome = MakeItem(tomeIdx);
	EXPECT_EQ(tome._iCharges, ScrollBookCapacity);
	tome._iCharges = 0; // start empty to test filling
	ASSERT_TRUE(AutoPlaceItemInInventory(player, tome, true));
	player.CalcScrolls();
	EXPECT_FALSE(CanUseScroll(player, SpellID::TownPortal)) << "an empty tome has nothing to read";

	// Picking up scrolls fills the tome, up to 50.
	for (int i = 0; i < ScrollBookCapacity; ++i) {
		Item scroll = MakeItem(IDI_PORTAL);
		ASSERT_EQ(AbsorbScrollsIntoBooks(player, scroll), 1) << "scroll " << i;
	}
	Item extra = MakeItem(IDI_PORTAL);
	EXPECT_EQ(AbsorbScrollsIntoBooks(player, extra), 0) << "a full tome takes no more";
	EXPECT_EQ(CountScrollsOf(player, SpellID::TownPortal), ScrollBookCapacity);
	EXPECT_TRUE(CanUseScroll(player, SpellID::TownPortal));

	// Other scrolls are not taken.
	Item identify = MakeItem(IDI_IDENTIFY);
	EXPECT_EQ(AbsorbScrollsIntoBooks(player, identify), 0);

	// Casting takes one scroll out of the tome; the tome stays.
	player.executedSpell.spellId = SpellID::TownPortal;
	player.executedSpell.spellFrom = 0;
	ConsumeScroll(player);
	EXPECT_EQ(CountScrollsOf(player, SpellID::TownPortal), ScrollBookCapacity - 1);
	EXPECT_EQ(player._pNumInv, 1) << "the tome stays in the pack";

	// A loose scroll is used before the tome.
	ASSERT_TRUE(AutoPlaceItemInInventory(player, extra, true));
	ConsumeScroll(player);
	EXPECT_EQ(CountScrollsOf(player, SpellID::TownPortal), ScrollBookCapacity - 1);
	for (int i = 0; i < player._pNumInv; ++i)
		EXPECT_FALSE(player.InvList[i].isScrollOf(SpellID::TownPortal) && !player.InvList[i].isScrollBook());
	leveltype = DTYPE_TOWN;
}

TEST_F(ScrollBooksTest, DroppedAndBoughtScrollsGoIntoTheTome)
{
	Player &player = *MyPlayer;
	ClearInventory(player);
	invflag = true;
	InspectPlayer = MyPlayer;
	Item tome = MakeItem(TomeOf(SpellID::Identify));
	tome._iCharges = 10;
	ASSERT_TRUE(AutoPlaceItemInInventory(player, tome, true));
	int tomeGridCell = 0;
	while (tomeGridCell < InventoryGridCells && player.InvGrid[tomeGridCell] <= 0)
		++tomeGridCell;
	ASSERT_LT(tomeGridCell, InventoryGridCells);
	const Rectangle &cell = InvRect[SLOTXY_INV_FIRST + tomeGridCell];
	const Point tomeCell = GetRightPanel().position + Displacement { cell.position.x + 2, cell.position.y + 2 };

	// Drop a held scroll onto the tome.
	player.HoldItem = MakeItem(IDI_IDENTIFY);
	MousePosition = tomeCell;
	CheckInvItem(false, false);
	EXPECT_TRUE(player.HoldItem.isEmpty()) << "the scroll left the cursor";
	EXPECT_EQ(CountScrollsOf(player, SpellID::Identify), 11);
	EXPECT_EQ(player._pNumInv, 1) << "no loose scroll in the pack";

	// Buying one from a store.
	Item bought = MakeItem(IDI_IDENTIFY);
	EXPECT_TRUE(StoreAutoPlace(bought, false));
	EXPECT_TRUE(StoreAutoPlace(bought, true));
	EXPECT_EQ(CountScrollsOf(player, SpellID::Identify), 12);
	EXPECT_EQ(player._pNumInv, 1);

	// A different scroll is not taken: it swaps with the tome as usual.
	player.HoldItem = MakeItem(IDI_PORTAL);
	MousePosition = tomeCell;
	CheckInvItem(false, false);
	EXPECT_TRUE(player.HoldItem.isScrollBook()) << "the tome is now on the cursor";
	EXPECT_EQ(player.HoldItem._iCharges, 12);
	player.HoldItem.clear();
	invflag = false;
}

TEST_F(ScrollBooksTest, ChargesSurviveSaving)
{
	for (const bool hellfire : { false, true }) {
		SCOPED_TRACE(hellfire ? "Hellfire" : "Diablo");
		gbIsHellfire = hellfire;
	const _item_indexes tomeIdx = TomeOf(SpellID::Healing);
	ASSERT_NE(tomeIdx, IDI_NONE);
	Item tome = MakeItem(tomeIdx);
	tome._iCreateInfo = 16 | CF_WITCH;
	tome._iCharges = 37;

	ItemPack packed {};
	PackItem(packed, tome, gbIsHellfire);
	Item restored {};
	UnPackItem(packed, *MyPlayer, restored, gbIsHellfire);
	EXPECT_TRUE(restored.isScrollBook());
	EXPECT_EQ(restored._iSpell, SpellID::Healing);
	EXPECT_EQ(restored._iCharges, 37);
	EXPECT_EQ(restored._iMaxCharges, ScrollBookCapacity);
	}
	gbIsHellfire = false;
}

class GoldMagnetTest : public UITest {
protected:
	static void SetGoldMagnet(bool enabled)
	{
		GetOptions().Mods.AddModEntry("Gold Magnet");
		for (OptionEntryBase *entry : GetOptions().Mods.GetEntries()) {
			if (entry->GetName() == "Gold Magnet")
				static_cast<OptionEntryBoolean *>(entry)->SetValue(enabled);
		}
	}

	/** Drops a gold pile `distance` tiles east of the hero and returns its item index. */
	static int DropGold(int distance)
	{
		const int ii = AllocateItem();
		Item &gold = Items[ii];
		InitializeItem(gold, IDI_GOLD);
		gold._ivalue = 100;
		gold._iRequest = false;
		gold.position = MyPlayer->position.tile + Displacement { distance, 0 };
		dItem[gold.position.x][gold.position.y] = ii + 1;
		return ii;
	}

	void TearDown() override
	{
		SetGoldMagnet(false);
		for (int x = 0; x < MAXDUNX; ++x)
			for (int y = 0; y < MAXDUNY; ++y)
				dItem[x][y] = 0;
		InitItems();
		UITest::TearDown();
	}
};

TEST_F(GoldMagnetTest, ReachesFiveTilesOnlyWithTheMod)
{
	leveltype = DTYPE_CATHEDRAL;
	MyPlayer->position.tile = { 40, 40 };
	GetOptions().Gameplay.autoGoldPickup.SetValue(true);
	InitItems();

	const int near = DropGold(1);
	const int five = DropGold(5);
	const int six = DropGold(6);

	SetGoldMagnet(false);
	AutoPickup(*MyPlayer);
	EXPECT_TRUE(Items[near]._iRequest) << "next to the hero: always";
	EXPECT_FALSE(Items[five]._iRequest) << "5 tiles away needs the mod";

	SetGoldMagnet(true);
	AutoPickup(*MyPlayer);
	EXPECT_TRUE(Items[five]._iRequest);
	EXPECT_FALSE(Items[six]._iRequest) << "6 tiles is out of reach";
	leveltype = DTYPE_TOWN;
}

} // namespace
} // namespace devilution
