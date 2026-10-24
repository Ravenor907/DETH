#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/diabloui.h"
#include "game_import.hpp"
#include "DiabloUI/deth_theme.hpp"
#include "DiabloUI/selgametype.h"
#include "deth_update.h"
#include "DiabloUI/ui_flags.hpp"
#include "DiabloUI/ui_item.h"
#include "engine/assets.hpp"
#include "engine/load_clx.hpp"
#include "engine/point.hpp"
#include "engine/render/text_render.hpp"
#include "game_mode.hpp"
#include "utils/language.h"
#include "utils/str_cat.hpp"
#include "utils/ui_fwd.h"

namespace devilution {
namespace {
int mainmenu_attract_time_out; // seconds
uint32_t dwAttractTicks;

std::vector<std::unique_ptr<UiItemBase>> vecMainMenuDialog;
std::vector<std::unique_ptr<UiListItem>> vecMenuItems;

_mainmenu_selections MainMenuResult;

void UiMainMenuSelect(size_t value)
{
	MainMenuResult = (_mainmenu_selections)vecMenuItems[value]->m_value;
}

#ifndef NOEXIT
void MainmenuEsc()
{
	const std::size_t last = vecMenuItems.size() - 1;
	if (SelectedItem == last) {
		UiMainMenuSelect(last);
	} else {
		SelectedItem = last;
	}
}
#endif

/** DETH: "Settings", with a note once the startup check found an update (Check for Updates is in Settings). */
std::string SettingsMenuLabel()
{
	if (BackgroundUpdateResult())
		return StrCat(_("Settings"), " ", _("(update ready)"));
	return std::string(_("Settings"));
}

void PickGameType()
{
	MainMenuResult = MAINMENU_GAME_TYPE;
}

void PickImport()
{
	MainMenuResult = MAINMENU_IMPORT_DATA;
}

void MainmenuLoad(const char *name)
{
	vecMenuItems.push_back(std::make_unique<UiListItem>(_("Single Player"), MAINMENU_SINGLE_PLAYER));
	vecMenuItems.push_back(std::make_unique<UiListItem>(_("Multi Player"), MAINMENU_MULTIPLAYER));
	if (UpdaterSupported())
		StartBackgroundUpdateCheck(); // once per launch, if enabled in Settings > Start Up
	vecMenuItems.push_back(std::make_unique<UiListItem>(SettingsMenuLabel(), MAINMENU_SETTINGS));
	// DETH: Import Game Files is a full entry until the full game is found, then a corner button.
#ifdef __ANDROID__
	const bool canImport = true;
#else
	const bool canImport = GameImportSupported();
#endif
	const bool importProminent = canImport && !HasDiabdatMpq;
	if (importProminent)
		vecMenuItems.push_back(std::make_unique<UiListItem>(_("Import Game Files"), MAINMENU_IMPORT_DATA));
#ifndef NOEXIT
	vecMenuItems.push_back(std::make_unique<UiListItem>(gbIsHellfire ? _("Exit Hellfire") : _("Exit Diablo"), MAINMENU_EXIT_DIABLO));
#endif

	// The art is loaded for its palette (the logo needs it); the background itself is drawn by the theme.
	if (!gbIsSpawn || gbIsHellfire) {
		LoadBackgroundArt("ui_art\\mainmenu");
	} else {
		LoadBackgroundArt("ui_art\\swmmenu");
	}
	UiThemeBackground = true;

	const theme::Layout layout = theme::GetLayout();
	int y = layout.margin / 2;
	if (!layout.compact) {
		UiAddLogo(&vecMainMenuDialog, y);
		y += 176;
		vecMainMenuDialog.push_back(std::make_unique<UiOrnament>(SDL_Rect { static_cast<Sint16>(layout.column.position.x + 40), static_cast<Sint16>(y), static_cast<Uint16>(layout.column.size.width - 80), 8 }));
		y += 16;
		if (gbIsSpawn && gbIsHellfire) {
			const SDL_Rect rect1 = { 0, static_cast<Sint16>(y - 40), static_cast<Uint16>(gnScreenWidth), 30 };
			vecMainMenuDialog.push_back(std::make_unique<UiArtText>(_("Shareware").data(), rect1, UiFlags::FontSize30 | UiFlags::ColorUiSilver | UiFlags::AlignCenter, 8));
		}
	} else {
		// Phones: no room for the flaming logo, a text title instead.
		const SDL_Rect rect1 = { static_cast<Sint16>(layout.column.position.x), static_cast<Sint16>(y), static_cast<Uint16>(layout.column.size.width), 40 };
		vecMainMenuDialog.push_back(std::make_unique<UiArtText>(gbIsHellfire ? "HELLFIRE" : (gbIsSpawn ? "DIABLO SHAREWARE" : "DIABLO"), rect1, UiFlags::FontSize30 | UiFlags::ColorUiGold | UiFlags::AlignCenter | UiFlags::VerticalCenter));
		y += 36;
		vecMainMenuDialog.push_back(std::make_unique<UiOrnament>(SDL_Rect { static_cast<Sint16>(layout.column.position.x + 40), static_cast<Sint16>(y), static_cast<Uint16>(layout.column.size.width - 80), 8 }));
		y += 14;
	}

	// Rows fill the remaining height; the row height shrinks if seven entries would not fit.
	const int rows = static_cast<int>(vecMenuItems.size());
	// DETH: the game files column goes left of the menu; without room there it becomes one row
	// under the menu, which takes a row of height from the list.
	// The column is sized to its text (24px font, like the heading) so labels never run into the counts.
	const int filesSpacing = 2;
	const int filesBox = 20;
	const int filesNeeded = filesBox + 12
	    + std::max(GetLineWidth(_("Diablo"), GameFont24, filesSpacing), GetLineWidth(_("Hellfire"), GameFont24, filesSpacing))
	    + 24 + GetLineWidth("4/4", GameFont24, filesSpacing);
	const int filesColumnWidth = std::min(std::max(filesNeeded, GetLineWidth(_("Game files"), GameFont24, 1)), layout.column.position.x - 2 * layout.margin);
	const bool filesBeside = filesColumnWidth >= filesNeeded;
	const int filesRowHeight = 30;
	const int available = gnScreenHeight - y - layout.margin - 20 - (filesBeside ? 0 : filesRowHeight);
	int rowHeight = std::min(layout.rowHeight + layout.rowGap, available / rows);
	rowHeight = std::max(rowHeight, 24);
	const UiFlags font = rowHeight >= 46 ? UiFlags::FontSize30 : UiFlags::FontSize24;
	auto list = std::make_unique<UiList>(vecMenuItems, vecMenuItems.size(), layout.column.position.x, y, layout.column.size.width, rowHeight, font | UiFlags::ColorUiGold | UiFlags::AlignCenter, layout.rowGap);
	list->themed = true;
	vecMainMenuDialog.push_back(std::move(list));

	// DETH: which games' files are installed (shareware always is), in the menu's font and gold.
	{
		static std::string labels[2];
		static std::string counts[2];
		const int have[2] = { HasDiabdatMpq ? 1 : 0, CountHellfireMpqs() };
		const int need[2] = { 1, 4 };
		const std::string_view names[2] = { _("Diablo"), _("Hellfire") };
		const int box = filesBox;
		if (filesBeside) {
			// Left edge, vertically centered: heading plus one row per game.
			const int rowH = std::max(30, rowHeight - 6);
			const int width = filesColumnWidth;
			const int px = layout.margin;
			int ry = (gnScreenHeight - rowH * 3) / 2;
			vecMainMenuDialog.push_back(std::make_unique<UiArtText>(_("Game files").data(), MakeSdlRect(px, ry, width, rowH), UiFlags::FontSize24 | UiFlags::ColorUiSilver | UiFlags::VerticalCenter));
			ry += rowH;
			for (size_t i = 0; i < 2; ++i) {
				const bool complete = have[i] == need[i];
				const UiFlags color = complete ? UiFlags::ColorUiGold : UiFlags::ColorUiGoldDark;
				vecMainMenuDialog.push_back(std::make_unique<UiCheckBox>(MakeSdlRect(px, ry + (rowH - box) / 2, box, box), complete));
				vecMainMenuDialog.push_back(std::make_unique<UiArtText>(names[i].data(), MakeSdlRect(px + box + 12, ry, width - box - 12, rowH), UiFlags::FontSize24 | color | UiFlags::VerticalCenter, filesSpacing));
				counts[i] = StrCat(have[i], "/", need[i]);
				vecMainMenuDialog.push_back(std::make_unique<UiArtText>(counts[i].c_str(), MakeSdlRect(px, ry, width, rowH), UiFlags::FontSize24 | color | UiFlags::VerticalCenter | UiFlags::AlignRight, filesSpacing));
				ry += rowH;
			}
		} else {
			// One centered row under the menu: [x] Diablo 1/1    [x] Hellfire 4/4
			const int ry = gnScreenHeight - 20 - filesRowHeight;
			const int gap = 40;
			int widths[2];
			for (size_t i = 0; i < 2; ++i) {
				labels[i] = StrCat(names[i], "  ", have[i], "/", need[i]);
				widths[i] = box + 10 + GetLineWidth(labels[i], GameFont24, filesSpacing);
			}
			int px = (gnScreenWidth - widths[0] - widths[1] - gap) / 2;
			for (size_t i = 0; i < 2; ++i) {
				const bool complete = have[i] == need[i];
				const UiFlags color = complete ? UiFlags::ColorUiGold : UiFlags::ColorUiGoldDark;
				vecMainMenuDialog.push_back(std::make_unique<UiCheckBox>(MakeSdlRect(px, ry + (filesRowHeight - box) / 2, box, box), complete));
				vecMainMenuDialog.push_back(std::make_unique<UiArtText>(labels[i].c_str(), MakeSdlRect(px + box + 10, ry, widths[i], filesRowHeight), UiFlags::FontSize24 | color | UiFlags::VerticalCenter, filesSpacing));
				px += widths[i] + gap;
			}
		}
	}

	// DETH: corner buttons, also on Left and Right: the game (top left) and Import Game Files (top right).
	{
		static std::string gameLabel;
		gameLabel = StrCat("< ", _("Game: "), GameTypeName(CurrentGameType()));
		static std::string importLabel;
		importLabel = StrCat(_("Import Game Files"), " >");
		const int cornerH = 34;
		const int cornerW = std::min(gnScreenWidth / 2 - layout.margin, 320);
		const int top = layout.margin / 2;
		vecMainMenuDialog.push_back(std::make_unique<UiArtTextButton>(gameLabel, &PickGameType, MakeSdlRect(layout.margin, top, cornerW, cornerH), UiFlags::FontSize24 | UiFlags::ColorUiSilver | UiFlags::VerticalCenter));
		if (canImport && !importProminent)
			vecMainMenuDialog.push_back(std::make_unique<UiArtTextButton>(importLabel, &PickImport, MakeSdlRect(gnScreenWidth - layout.margin - cornerW, top, cornerW, cornerH), UiFlags::FontSize24 | UiFlags::ColorUiSilver | UiFlags::AlignRight | UiFlags::VerticalCenter));
	}

	const SDL_Rect rect2 = { static_cast<Sint16>(layout.margin), static_cast<Sint16>(gnScreenHeight - 18), static_cast<Uint16>(gnScreenWidth - 2 * layout.margin), 14 };
	vecMainMenuDialog.push_back(std::make_unique<UiArtText>(name, rect2, UiFlags::FontSize12 | UiFlags::ColorUiSilverDark));

#ifndef NOEXIT
	UiInitList(nullptr, UiMainMenuSelect, MainmenuEsc, vecMainMenuDialog, true);
#else
	UiInitList(nullptr, UiMainMenuSelect, nullptr, vecMainMenuDialog, true);
#endif
	gfnListLeft = &PickGameType;
	if (canImport && !importProminent)
		gfnListRight = &PickImport;
}

void MainmenuFree()
{
	UiThemeBackground = false;
	ArtBackgroundWidescreen = std::nullopt;
	ArtBackground = std::nullopt;

	vecMainMenuDialog.clear();

	vecMenuItems.clear();
}

} // namespace

void mainmenu_restart_repintro()
{
	dwAttractTicks = SDL_GetTicks() + mainmenu_attract_time_out * 1000;
}

bool UiMainMenuDialog(const char *name, _mainmenu_selections *pdwResult, int attractTimeOut)
{
	MainMenuResult = MAINMENU_NONE;
	while (MainMenuResult == MAINMENU_NONE) {
		mainmenu_attract_time_out = attractTimeOut;
		MainmenuLoad(name);

		mainmenu_restart_repintro(); // for automatic starts

		bool updateLabelShown = BackgroundUpdateResult().has_value();
		while (MainMenuResult == MAINMENU_NONE) {
			// The startup check runs in the background; relabel the entry when it finds something.
			if (!updateLabelShown && BackgroundUpdateResult()) {
				updateLabelShown = true;
				for (auto &item : vecMenuItems) {
					if (item->m_value == MAINMENU_SETTINGS)
						item->m_text = SettingsMenuLabel();
				}
			}
			UiClearScreen();
			UiPollAndRender();
			if (SDL_GetTicks() >= dwAttractTicks && (HaveIntro() || gbIsHellfire)) {
				MainMenuResult = MAINMENU_ATTRACT_MODE;
			}
		}

		MainmenuFree();
	}

	*pdwResult = MainMenuResult;
	return true;
}

} // namespace devilution
