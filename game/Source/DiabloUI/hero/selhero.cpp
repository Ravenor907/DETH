#include "DiabloUI/hero/selhero.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_rect.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/diabloui.h"
#include "DiabloUI/deth_screen.hpp"
#include "DiabloUI/dialogs.h"
#include "DiabloUI/multi/selgame.h"
#include "DiabloUI/scrollbar.h"
#include "DiabloUI/selok.h"
#include "DiabloUI/selyesno.h"
#include "DiabloUI/ui_flags.hpp"
#include "DiabloUI/ui_item.h"
#include "controls/control_mode.hpp"
#include "engine/assets.hpp"
#include "engine/point.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "options.h"
#include "pfile.h"
#include "tables/playerdat.hpp"
#include "utils/enum_traits.h"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/sdl_geometry.h"
#include "utils/str_cat.hpp"
#include "utils/ui_fwd.h"
#include "utils/utf8.hpp"

namespace devilution {

bool selhero_endMenu;
bool selhero_isMultiPlayer;

bool (*gfnHeroInfo)(bool (*fninfofunc)(_uiheroinfo *));
bool (*gfnHeroCreate)(_uiheroinfo *);
void (*gfnHeroStats)(HeroClass, _uidefaultstats *);

bool SelheroLoadAutosave = false;
std::optional<std::string> SelheroLoadPath;

namespace {

std::size_t selhero_SaveCount = 0;
_uiheroinfo selhero_heros[MAX_CHARACTERS];
_uiheroinfo selhero_heroInfo;
char textStats[6][4];
const char *title = "";
_selhero_selections selhero_result;
bool selhero_navigateYesNo;
bool selhero_isSavegame;
/** DETH: the screen layout shared by every hero sub-screen. */
theme::ScreenFrame heroFrame;

std::vector<std::unique_ptr<UiItemBase>> vecSelHeroDialog;
std::vector<std::unique_ptr<UiListItem>> vecSelHeroDlgItems;
std::vector<std::unique_ptr<UiItemBase>> vecSelDlgItems;

UiImageClx *SELHERO_DIALOG_HERO_IMG;

void SelheroListFocus(size_t value);
void SelheroListSelect(size_t value);
void SelheroListEsc();
std::string DescribeAge(std::time_t when)
{
	const std::time_t now = std::time(nullptr);
	const long long minutes = now > when ? static_cast<long long>(now - when) / 60 : 0;
	if (minutes < 1)
		return std::string(_("just now"));
	if (minutes < 60)
		return FormatRuntime(ngettext("{:d} minute ago", "{:d} minutes ago", static_cast<int>(minutes)), static_cast<int>(minutes));
	const long long hours = minutes / 60;
	if (hours < 48)
		return FormatRuntime(ngettext("{:d} hour ago", "{:d} hours ago", static_cast<int>(hours)), static_cast<int>(hours));
	const long long days = hours / 24;
	return FormatRuntime(ngettext("{:d} day ago", "{:d} days ago", static_cast<int>(days)), static_cast<int>(days));
}

void SelheroLoadFocus(size_t value);
void SelheroLoadSelect(size_t value);
std::string DescribeAge(std::time_t when);
void SelheroNameSelect(size_t value);
void SelheroNameEsc();
void SelheroClassSelectorFocus(size_t value);
void SelheroClassSelectorSelect(size_t value);
void SelheroClassSelectorEsc();
const char *SelheroGenerateName(HeroClass heroClass);

void SelheroUiFocusNavigationYesNo()
{
	if (selhero_isSavegame)
		UiFocusNavigationYesNo();
}

void SelheroFree()
{
	UiThemeBackground = false;
	ArtBackground = std::nullopt;

	vecSelHeroDialog.clear();

	vecSelDlgItems.clear();
	vecSelHeroDlgItems.clear();
	UnloadScrollBar();
}

void SelheroSetStats()
{
	SELHERO_DIALOG_HERO_IMG->setSprite(UiGetHeroDialogSprite(static_cast<size_t>(selhero_heroInfo.heroclass)));
	CopyUtf8(textStats[0], StrCat(selhero_heroInfo.level), sizeof(textStats[0]));
	CopyUtf8(textStats[1], StrCat(selhero_heroInfo.strength), sizeof(textStats[1]));
	CopyUtf8(textStats[2], StrCat(selhero_heroInfo.magic), sizeof(textStats[2]));
	CopyUtf8(textStats[3], StrCat(selhero_heroInfo.dexterity), sizeof(textStats[3]));
	CopyUtf8(textStats[4], StrCat(selhero_heroInfo.vitality), sizeof(textStats[4]));
	CopyUtf8(textStats[5], StrCat(selhero_heroInfo.saveNumber), sizeof(textStats[5]));
}

void RenderDifficultyIndicators()
{
	if (!selhero_isSavegame)
		return;
	const uint16_t width = (*DifficultyIndicator)[0].width();
	const uint16_t height = (*DifficultyIndicator)[0].height();
	SDL_Rect rect = MakeSdlRect(
	    SELHERO_DIALOG_HERO_IMG->m_rect.x + 1,
	    SELHERO_DIALOG_HERO_IMG->m_rect.y + SELHERO_DIALOG_HERO_IMG->m_rect.h - height - 1,
	    width,
	    height);
	for (int i = 0; i <= DIFF_LAST; i++) {
		if (i >= selhero_heroInfo.herorank)
			break;
		UiRenderItem(UiImageClx((*DifficultyIndicator)[0], rect, UiFlags::None));
		rect.x += width;
	}
}

UiArtTextButton *SELLIST_DIALOG_DELETE_BUTTON;

bool SelHeroGetHeroInfo(_uiheroinfo *pInfo)
{
	selhero_heros[selhero_SaveCount] = *pInfo;

	selhero_SaveCount++;

	return true;
}

void SelheroHeroFocus(size_t value)
{
	const UiFlags baseFlags = UiFlags::AlignCenter | UiFlags::FontSize30;
	if (selhero_SaveCount != 0 && value < selhero_SaveCount) {
		memcpy(&selhero_heroInfo, &selhero_heros[value], sizeof(selhero_heroInfo));
		SelheroSetStats();
		SELLIST_DIALOG_DELETE_BUTTON->SetFlags(baseFlags | UiFlags::ColorUiGold);
		selhero_isSavegame = true;
		return;
	}

	SELHERO_DIALOG_HERO_IMG->setSprite(UiGetHeroDialogSprite(GetNumPlayerClasses()));
	for (char *textStat : textStats)
		strcpy(textStat, "--");
	SELLIST_DIALOG_DELETE_BUTTON->SetFlags(baseFlags | UiFlags::ColorUiSilver | UiFlags::ElementDisabled);
	selhero_isSavegame = false;
}

bool SelheroListDeleteYesNo()
{
	selhero_navigateYesNo = selhero_isSavegame;

	return selhero_navigateYesNo;
}

void SelheroHeroSelect(size_t value)
{
	if (static_cast<std::size_t>(value) == selhero_SaveCount) {
		vecSelDlgItems.clear();

		int y = heroFrame.content.position.y;
		y += theme::AddHeading(vecSelDlgItems, _("Choose Class").data(), heroFrame, y);

		vecSelHeroDlgItems.clear();
		for (size_t i = 0; i < GetNumPlayerClasses(); ++i) {
			const auto heroClass = static_cast<HeroClass>(i);

			if (heroClass == HeroClass::Monk && !gbIsHellfire) {
				continue;
			}

			if (heroClass == HeroClass::Bard && !HaveBardAssets() && !(*GetOptions().Gameplay.testBard)) {
				continue;
			}

			if (heroClass == HeroClass::Barbarian && !HaveBarbarianAssets() && !(*GetOptions().Gameplay.testBarbarian)) {
				continue;
			}

			const PlayerData &playerData = GetPlayerDataForClass(heroClass);
			vecSelHeroDlgItems.push_back(std::make_unique<UiListItem>(_(playerData.className), static_cast<int>(heroClass)));
		}
		auto list = theme::MakeList(vecSelHeroDlgItems, heroFrame, y, 6, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold);
		const int listHeight = list->m_rect.h;
		vecSelDlgItems.push_back(std::move(list));
		theme::AddScrollbar(vecSelDlgItems, heroFrame, y, listHeight);

		theme::AddButtons(vecSelDlgItems, heroFrame, {
		    { _("OK"), &UiFocusNavigationSelect },
		    { _("Cancel"), &UiFocusNavigationEsc },
		});

		theme::AddBackButton(vecSelDlgItems, heroFrame);
		UiInitList(SelheroClassSelectorFocus, SelheroClassSelectorSelect, SelheroClassSelectorEsc, vecSelDlgItems, true);
		memset(&selhero_heroInfo.name, 0, sizeof(selhero_heroInfo.name));
		selhero_heroInfo.saveNumber = pfile_ui_get_first_unused_save_num();
		SelheroSetStats();
		title = selhero_isMultiPlayer ? _("New Multi Player Hero").data() : _("New Single Player Hero").data();
		selhero_isSavegame = false;
		return;
	}

	// DETH: the autosave comes first; the manual save is smaller, underneath, so it is not picked by accident.
	const std::optional<std::time_t> autosaveTime = pfile_autosave_time(selhero_heroInfo.saveNumber);
	if (selhero_isMultiPlayer && autosaveTime) {
		vecSelDlgItems.clear();
		int y = heroFrame.content.position.y;
		y += theme::AddHeading(vecSelDlgItems, _("Character").data(), heroFrame, y);
		vecSelHeroDlgItems.clear();
		vecSelHeroDlgItems.push_back(std::make_unique<UiListItem>(_("Continue"), 0));
		vecSelHeroDlgItems.push_back(std::make_unique<UiListItem>(FormatRuntime(_("Restore Backup ({:s})"), DescribeAge(*autosaveTime)), 3, UiFlags::FontSize12));
		vecSelDlgItems.push_back(theme::MakeList(vecSelHeroDlgItems, heroFrame, y, 2, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold));
		theme::AddButtons(vecSelDlgItems, heroFrame, {
		    { _("OK"), &UiFocusNavigationSelect },
		    { _("Cancel"), &UiFocusNavigationEsc },
		});
		theme::AddBackButton(vecSelDlgItems, heroFrame);
		UiInitList(SelheroLoadFocus, SelheroLoadSelect, selhero_List_Init, vecSelDlgItems, true);
		title = _("Multi Player Characters").data();
		return;
	}

	if (selhero_heroInfo.hassaved || (!selhero_isMultiPlayer && autosaveTime)) {
		vecSelDlgItems.clear();

		int y = heroFrame.content.position.y;
		y += theme::AddHeading(vecSelDlgItems, _("Save File Exists").data(), heroFrame, y);

		vecSelHeroDlgItems.clear();
		if (autosaveTime)
			vecSelHeroDlgItems.push_back(std::make_unique<UiListItem>(FormatRuntime(_("Load Autosave ({:s})"), DescribeAge(*autosaveTime)), 2));
		if (selhero_heroInfo.hassaved)
			vecSelHeroDlgItems.push_back(std::make_unique<UiListItem>(_("Load Manual Save"), 0, autosaveTime ? UiFlags::FontSize12 : UiFlags::None));
		vecSelHeroDlgItems.push_back(std::make_unique<UiListItem>(_("New Game"), 1));
		vecSelDlgItems.push_back(theme::MakeList(vecSelHeroDlgItems, heroFrame, y, vecSelHeroDlgItems.size(), UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold));

		theme::AddButtons(vecSelDlgItems, heroFrame, {
		    { _("OK"), &UiFocusNavigationSelect },
		    { _("Cancel"), &UiFocusNavigationEsc },
		});

		theme::AddBackButton(vecSelDlgItems, heroFrame);
		UiInitList(SelheroLoadFocus, SelheroLoadSelect, selhero_List_Init, vecSelDlgItems, true);
		title = _("Single Player Characters").data();
		return;
	}

	SelheroLoadSelect(1);
}

// ---- DETH: Continue and the recent saves ----

std::vector<SaveHistoryEntry> RecentSaves;
/** Rows above the heroes: Continue plus the recent saves (0 without saves). */
size_t RecentRows()
{
	return RecentSaves.empty() ? 0 : RecentSaves.size() + 1;
}
const SaveHistoryEntry &RecentForRow(size_t row)
{
	return RecentSaves[row == 0 ? 0 : row - 1];
}
std::optional<size_t> PendingOlderSave;
bool selhero_confirmLoad = false;

std::string DescribeRecent(const SaveHistoryEntry &entry)
{
	return StrCat(entry.autosave ? _("autosave") : _("manual save"), "  ", DescribeAge(entry.time));
}

void SelheroListFocus(size_t value)
{
	if (value >= RecentRows()) {
		SelheroHeroFocus(value - RecentRows());
		return;
	}
	const SaveHistoryEntry &entry = RecentForRow(value);
	for (size_t i = 0; i < selhero_SaveCount; i++) {
		if (selhero_heros[i].saveNumber == entry.saveNum) {
			memcpy(&selhero_heroInfo, &selhero_heros[i], sizeof(selhero_heroInfo));
			SelheroSetStats();
			break;
		}
	}
	SELLIST_DIALOG_DELETE_BUTTON->SetFlags(UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiSilver | UiFlags::ElementDisabled);
	selhero_isSavegame = false;
}

void LoadRecentSave(const SaveHistoryEntry &entry)
{
	for (size_t i = 0; i < selhero_SaveCount; i++) {
		if (selhero_heros[i].saveNumber == entry.saveNum)
			memcpy(&selhero_heroInfo, &selhero_heros[i], sizeof(selhero_heroInfo));
	}
	selhero_heroInfo.saveNumber = entry.saveNum;
	if (selhero_isMultiPlayer) {
		// DETH: multiplayer saves hold the hero only: the picked copy becomes the hero, then on to the game.
		pfile_restore_hero_from(entry.saveNum, entry.path);
		selhero_result = SELHERO_NEW_DUNGEON;
		selhero_endMenu = true;
		return;
	}
	SelheroLoadPath = entry.path;
	selhero_result = SELHERO_CONTINUE;
	selhero_endMenu = true;
}

void SelheroListSelect(size_t value)
{
	if (value >= RecentRows()) {
		SelheroHeroSelect(value - RecentRows());
		return;
	}
	UiInitList_clear();
	// Continue and the newest entry load right away; an older save asks first.
	if (value <= 1) {
		LoadRecentSave(RecentSaves[0]);
		return;
	}
	PendingOlderSave = value - 1;
	selhero_confirmLoad = true;
}

void SelheroListEsc()
{
	UiInitList_clear();

	selhero_endMenu = true;
	selhero_result = SELHERO_PREVIOUS;
}

void SelheroClassSelectorFocus(size_t value)
{
	const auto heroClass = static_cast<HeroClass>(vecSelHeroDlgItems[value]->m_value);

	_uidefaultstats defaults;
	gfnHeroStats(heroClass, &defaults);

	selhero_heroInfo.level = 1;
	selhero_heroInfo.heroclass = heroClass;
	selhero_heroInfo.strength = defaults.strength;
	selhero_heroInfo.magic = defaults.magic;
	selhero_heroInfo.dexterity = defaults.dexterity;
	selhero_heroInfo.vitality = defaults.vitality;

	SelheroSetStats();
}

bool ShouldPrefillHeroName()
{
#if defined(PREFILL_PLAYER_NAME)
	return true;
#else
	return ControlMode != ControlTypes::KeyboardAndMouse;
#endif
}

void RemoveSelHeroBackground()
{
	UiThemeBackground = false;
	ArtBackground = std::nullopt;
}

void AddSelHeroBackground()
{
	// Loaded for its palette (portraits); the background itself comes from the theme.
	LoadBackgroundArt("ui_art\\selhero");
	UiThemeBackground = true;
}

void SelheroClassSelectorSelect(size_t value)
{
	auto hClass = static_cast<HeroClass>(vecSelHeroDlgItems[value]->m_value);
	if (gbIsSpawn && (hClass == HeroClass::Rogue || hClass == HeroClass::Sorcerer || (hClass == HeroClass::Bard && !HaveBardAssets()))) {
		RemoveSelHeroBackground();
		UiSelOkDialog(nullptr, _("The Rogue and Sorcerer are only available in the full retail version of Diablo. Visit https://www.gog.com/game/diablo to purchase.").data(), false);
		AddSelHeroBackground();
		SelheroHeroSelect(selhero_SaveCount);
		return;
	}

	title = selhero_isMultiPlayer ? _("New Multi Player Hero").data() : _("New Single Player Hero").data();
	memset(selhero_heroInfo.name, '\0', sizeof(selhero_heroInfo.name));
	if (ShouldPrefillHeroName())
		strcpy(selhero_heroInfo.name, SelheroGenerateName(selhero_heroInfo.heroclass));
	vecSelDlgItems.clear();
	int y = heroFrame.content.position.y;
	y += theme::AddHeading(vecSelDlgItems, _("Enter Name").data(), heroFrame, y);
	y += 8;

	const int editHeight = heroFrame.layout.rowHeight;
	const SDL_Rect rect2 = { static_cast<Sint16>(heroFrame.content.position.x), static_cast<Sint16>(y), static_cast<Uint16>(heroFrame.content.size.width), static_cast<Uint16>(editHeight) };
	auto edit = std::make_unique<UiEdit>(_("Enter Name"), selhero_heroInfo.name, 15, false, rect2, UiFlags::FontSize24 | UiFlags::ColorUiGold);
	edit->themed = true;
	vecSelDlgItems.push_back(std::move(edit));

	// DETH: OK and Cancel right under the name, so an on-screen keyboard does not cover them.
	theme::AddButtons(vecSelDlgItems, theme::ButtonsBelow(heroFrame, heroFrame.content, y + editHeight + 16), {
	    { _("OK"), &UiFocusNavigationSelect },
	    { _("Cancel"), &UiFocusNavigationEsc },
	});

	theme::AddBackButton(vecSelDlgItems, heroFrame);
	UiInitList(nullptr, SelheroNameSelect, SelheroNameEsc, vecSelDlgItems);
}

void SelheroClassSelectorEsc()
{
	vecSelDlgItems.clear();
	vecSelHeroDlgItems.clear();

	if (selhero_SaveCount != 0) {
		selhero_List_Init();
		return;
	}

	SelheroListEsc();
}

void SelheroNameSelect(size_t /*value*/)
{
	// only check names in multiplayer, we don't care about them in single
	if (selhero_isMultiPlayer && !UiValidPlayerName(selhero_heroInfo.name)) {
		RemoveSelHeroBackground();
		UiSelOkDialog(title, _("Invalid name. A name cannot contain spaces, reserved characters, or reserved words.\n").data(), false);
		AddSelHeroBackground();
	} else {
		if (gfnHeroCreate(&selhero_heroInfo)) {
			SelheroLoadSelect(1);
			return;
		}
		UiErrorOkDialog(_(/* TRANSLATORS: Error Message */ "Unable to create character."), vecSelHeroDialog);
	}

	memset(selhero_heroInfo.name, '\0', sizeof(selhero_heroInfo.name));
	SelheroClassSelectorSelect(0);
}

void SelheroNameEsc()
{
	SelheroHeroSelect(selhero_SaveCount);
}

void SelheroLoadFocus(size_t value)
{
}

void SelheroLoadSelect(size_t value)
{
	UiInitList_clear();
	selhero_endMenu = true;
	const int choice = vecSelHeroDlgItems[value]->m_value;
	SelheroLoadAutosave = choice == 2;
	if (selhero_isMultiPlayer) {
		// Multiplayer: Continue, or put the backup copy back first; then on as before.
		if (choice == 3)
			pfile_restore_backup(selhero_heroInfo.saveNumber);
		selhero_result = SELHERO_NEW_DUNGEON;
		return;
	}
	if (choice == 0 || choice == 2) {
		selhero_result = SELHERO_CONTINUE;
		return;
	}

	if (!selhero_isMultiPlayer) {
		// This is part of a dangerous hack to enable difficulty selection in single-player.
		// FIXME: Dialogs should not refer to each other's variables.

		// We disable `selhero_endMenu` and replace the background and art
		// and the item list with the difficulty selection ones.
		//
		// This means selhero's render loop will render selgame's items,
		// which happens to work because the render loops are similar.
		selhero_endMenu = false;

		// Set this to false so that we do not attempt to render difficulty indicators.
		selhero_isSavegame = false;

		SelheroFree();
		LoadBackgroundArt("ui_art\\selgame");
		selgame_GameSelection_Select(0);
	}

	selhero_result = SELHERO_NEW_DUNGEON;
}

const char *SelheroGenerateName(HeroClass heroClass)
{
	static const char *const Names[6][10] = {
		{
		    // Warrior
		    "Aidan",
		    "Qarak",
		    "Born",
		    "Cathan",
		    "Halbu",
		    "Lenalas",
		    "Maximus",
		    "Vane",
		    "Myrdgar",
		    "Rothat",
		},
		{
		    // Rogue
		    "Moreina",
		    "Akara",
		    "Kashya",
		    "Flavie",
		    "Divo",
		    "Oriana",
		    "Iantha",
		    "Shikha",
		    "Basanti",
		    "Elexa",
		},
		{
		    // Sorcerer
		    "Jazreth",
		    "Drognan",
		    "Armin",
		    "Fauztin",
		    "Jere",
		    "Kazzulk",
		    "Ranslor",
		    "Sarnakyle",
		    "Valthek",
		    "Horazon",
		},
		{
		    // Monk
		    "Akyev",
		    "Dvorak",
		    "Kekegi",
		    "Kharazim",
		    "Mikulov",
		    "Shenlong",
		    "Vedenin",
		    "Vhalit",
		    "Vylnas",
		    "Zhota",
		},
		{
		    // Bard (uses Rogue names)
		    "Moreina",
		    "Akara",
		    "Kashya",
		    "Flavie",
		    "Divo",
		    "Oriana",
		    "Iantha",
		    "Shikha",
		    "Basanti",
		    "Elexa",
		},
		{
		    // Barbarian
		    "Alaric",
		    "Barloc",
		    "Egtheow",
		    "Guthlaf",
		    "Heorogar",
		    "Hrothgar",
		    "Oslaf",
		    "Qual-Kehk",
		    "Ragnar",
		    "Ulf",
		},
	};

	const int iRand = rand() % 10;

	return Names[static_cast<std::size_t>(heroClass) % 6][iRand];
}

} // namespace



void selhero_Init()
{
	AddSelHeroBackground();
	LoadScrollBar();

	selhero_SaveCount = 0;
	RecentSaves.clear();
	gfnHeroInfo(SelHeroGetHeroInfo);
	std::reverse(selhero_heros, selhero_heros + selhero_SaveCount);

	heroFrame = theme::MakeScreenFrame(/*wide=*/true, /*sidePane=*/true, /*buttons=*/true);

	vecSelDlgItems.clear();
	// The title bar is shared by all hero screens; each screen adds its own tappable Back.
	theme::AddTitle(vecSelHeroDialog, &title, heroFrame, /*withBack=*/false);

	// Side pane: portrait on top, stats below.
	const Rectangle side = heroFrame.side;
	const bool hasSide = side.size.width > 0;
	const int statHeight = 24;
	const int statCount = 5
#ifdef _DEBUG
	    + 1
#endif
	    ;
	const int panelHeight = std::min(side.size.height, 10 + 76 + 12 + statCount * statHeight + 10);
	if (hasSide)
		vecSelHeroDialog.push_back(std::make_unique<UiPanel>(MakeSdlRect(side.position.x, side.position.y, side.size.width, panelHeight)));

	const SDL_Rect portraitRect = hasSide
	    ? MakeSdlRect(side.position.x + (side.size.width - 180) / 2, side.position.y + 10, 180, 76)
	    : MakeSdlRect(-200, -200, 180, 76); // off screen on narrow layouts
	auto heroImg = std::make_unique<UiImageClx>(UiGetHeroDialogSprite(0), portraitRect, UiFlags::None);
	SELHERO_DIALOG_HERO_IMG = heroImg.get();
	vecSelHeroDialog.push_back(std::move(heroImg));

	const UiFlags labelFlags = UiFlags::FontSize12 | UiFlags::ColorUiSilverDark | UiFlags::VerticalCenter;
	const UiFlags valueFlags = UiFlags::FontSize24 | UiFlags::ColorUiGold | UiFlags::AlignRight | UiFlags::VerticalCenter;
	const int labelX = side.position.x + 14;
	const int labelWidth = side.size.width - 28;
	int statY = portraitRect.y + portraitRect.h + 12;

	const char *statLabels[] {
		_("Level:").data(), _("Strength:").data(), _("Magic:").data(), _("Dexterity:").data(), _("Vitality:").data(),
#ifdef _DEBUG
		_("Savegame:").data()
#endif
	};
	for (size_t i = 0; i < sizeof(statLabels) / sizeof(statLabels[0]); ++i) {
		if (hasSide) {
			vecSelHeroDialog.push_back(std::make_unique<UiArtText>(statLabels[i], MakeSdlRect(labelX, statY, labelWidth, statHeight), labelFlags));
			vecSelHeroDialog.push_back(std::make_unique<UiArtText>(textStats[i], MakeSdlRect(labelX, statY, labelWidth, statHeight), valueFlags));
		}
		statY += statHeight;
	}
}

void selhero_List_Init()
{
	size_t selectedItem = 0;
	vecSelDlgItems.clear();

	int y = heroFrame.content.position.y;
	y += theme::AddHeading(vecSelDlgItems, _("Select Hero").data(), heroFrame, y);

	vecSelHeroDlgItems.clear();
	// DETH: Continue (the newest save) and the recent saves come first, in single player and multiplayer.
	RecentSaves = pfile_recent_saves(5);
	const size_t recentRows = RecentRows();
	if (recentRows > 0) {
		// Continue is the big row; the recent saves under it are single small lines.
		auto cont = std::make_unique<UiListItem>(_("Continue"), 0, UiFlags::FontSize42 | UiFlags::ColorUiGold);
		cont->subText = StrCat(RecentSaves[0].heroName, "  ", FormatRuntime(_("level {:d}"), RecentSaves[0].level), "  ", DescribeRecent(RecentSaves[0]));
		cont->heightPercent = static_cast<uint8_t>(std::min(250, 100 * (std::max(heroFrame.RowPitch() * 3 / 2, 64) + theme::SubTextHeight()) / std::max(1, heroFrame.RowPitch())));
		vecSelHeroDlgItems.push_back(std::move(cont));
		for (size_t i = 0; i < RecentSaves.size(); i++) {
			auto row = std::make_unique<UiListItem>(StrCat(RecentSaves[i].heroName, "  ", FormatRuntime(_("level {:d}"), RecentSaves[i].level), "  -  ", DescribeRecent(RecentSaves[i])), static_cast<int>(i + 1), UiFlags::FontSize12 | UiFlags::ColorUiSilver);
			row->heightPercent = 55;
			vecSelHeroDlgItems.push_back(std::move(row));
		}
	}
	for (std::size_t i = 0; i < selhero_SaveCount; i++) {
		vecSelHeroDlgItems.push_back(std::make_unique<UiListItem>(std::string_view(selhero_heros[i].name), static_cast<int>(recentRows + i)));
		if (recentRows == 0 && selhero_heros[i].saveNumber == selhero_heroInfo.saveNumber)
			selectedItem = i;
	}
	vecSelHeroDlgItems.push_back(std::make_unique<UiListItem>(_("New Hero"), static_cast<int>(recentRows + selhero_SaveCount)));

	auto list = theme::MakeList(vecSelHeroDlgItems, heroFrame, y, 8, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold);
	if (recentRows > 0) {
		// DETH: rows of different heights fill the whole content area.
		list->m_rect.h = static_cast<Uint16>(std::max<int>(list->m_height, heroFrame.content.position.y + heroFrame.content.size.height - y));
		list->viewportSize = list->RowsFitting(0);
	}
	const int listHeight = list->m_rect.h;
	vecSelDlgItems.push_back(std::move(list));
	theme::AddScrollbar(vecSelDlgItems, heroFrame, y, listHeight);

	std::vector<UiArtTextButton *> buttons = theme::AddButtons(vecSelDlgItems, heroFrame, {
	    { _("OK"), &UiFocusNavigationSelect },
	    { _("Delete"), &SelheroUiFocusNavigationYesNo, UiFlags::ElementDisabled },
	    { _("Cancel"), &UiFocusNavigationEsc },
	});
	SELLIST_DIALOG_DELETE_BUTTON = buttons[1];

	theme::AddBackButton(vecSelDlgItems, heroFrame);
	UiInitList(SelheroListFocus, SelheroListSelect, SelheroListEsc, vecSelDlgItems, false, nullptr, SelheroListDeleteYesNo, selectedItem);
	if (selhero_isMultiPlayer) {
		title = _("Multi Player Characters").data();
	} else {
		title = _("Single Player Characters").data();
	}
}

static void UiSelHeroDialog(
    bool (*fninfo)(bool (*fninfofunc)(_uiheroinfo *)),
    bool (*fncreate)(_uiheroinfo *),
    void (*fnstats)(HeroClass, _uidefaultstats *),
    bool (*fnremove)(_uiheroinfo *),
    _selhero_selections *dlgresult,
    uint32_t *saveNumber)
{
	do {
		gfnHeroInfo = fninfo;
		gfnHeroCreate = fncreate;
		gfnHeroStats = fnstats;
		selhero_result = *dlgresult;

		selhero_navigateYesNo = false;

		selhero_Init();

		if (selhero_SaveCount != 0) {
			selhero_heroInfo = {};
			// Search last used save and remember it as selected item
			for (size_t i = 0; i < selhero_SaveCount; i++) {
				if (selhero_heros[i].saveNumber == *saveNumber) {
					memcpy(&selhero_heroInfo, &selhero_heros[i], sizeof(selhero_heroInfo));
					break;
				}
			}
			selhero_List_Init();
		} else {
			SelheroHeroSelect(selhero_SaveCount);
		}

		selhero_endMenu = false;
		selhero_confirmLoad = false;
		while (!selhero_endMenu && !selhero_navigateYesNo && !selhero_confirmLoad) {
			UiClearScreen();
			UiRenderItems(vecSelHeroDialog);
			RenderDifficultyIndicators();
			UiPollAndRender();
		}
		SelheroFree();

		if (selhero_navigateYesNo) {
			char dialogTitle[128];
			char dialogText[256];
			if (selhero_isMultiPlayer) {
				CopyUtf8(dialogTitle, _("Delete Multi Player Hero"), sizeof(dialogTitle));
			} else {
				CopyUtf8(dialogTitle, _("Delete Single Player Hero"), sizeof(dialogTitle));
			}
			strcpy(dialogText, FormatRuntime(_("Are you sure you want to delete the character \"{:s}\"?"), selhero_heroInfo.name).c_str());

			if (UiSelHeroYesNoDialog(dialogTitle, dialogText))
				fnremove(&selhero_heroInfo);
		}
		if (selhero_confirmLoad && PendingOlderSave && *PendingOlderSave < RecentSaves.size()) {
			// DETH: an older save from the list: confirm, newer saves stay in the list.
			const SaveHistoryEntry entry = RecentSaves[*PendingOlderSave];
			const std::string text = FormatRuntime(_("Load {:s} from this {:s}?"), entry.heroName, DescribeRecent(entry));
			if (UiSelHeroYesNoDialog(_("Load Older Save").data(), text.c_str()))
				LoadRecentSave(entry);
			else
				selhero_confirmLoad = true; // show the list again
			if (selhero_endMenu)
				selhero_confirmLoad = false;
		}
		PendingOlderSave = std::nullopt;
	} while (selhero_navigateYesNo || selhero_confirmLoad);

	*dlgresult = selhero_result;
	*saveNumber = selhero_heroInfo.saveNumber;
}

void UiSelHeroSingDialog(
    bool (*fninfo)(bool (*fninfofunc)(_uiheroinfo *)),
    bool (*fncreate)(_uiheroinfo *),
    bool (*fnremove)(_uiheroinfo *),
    void (*fnstats)(HeroClass, _uidefaultstats *),
    _selhero_selections *dlgresult,
    uint32_t *saveNumber,
    _difficulty *difficulty)
{
	selhero_isMultiPlayer = false;
	UiSelHeroDialog(fninfo, fncreate, fnstats, fnremove, dlgresult, saveNumber);
	*difficulty = nDifficulty;
}

void UiSelHeroMultDialog(
    bool (*fninfo)(bool (*fninfofunc)(_uiheroinfo *)),
    bool (*fncreate)(_uiheroinfo *),
    bool (*fnremove)(_uiheroinfo *),
    void (*fnstats)(HeroClass, _uidefaultstats *),
    _selhero_selections *dlgresult,
    uint32_t *saveNumber)
{
	selhero_isMultiPlayer = true;
	UiSelHeroDialog(fninfo, fncreate, fnstats, fnremove, dlgresult, saveNumber);
}

} // namespace devilution
