#include "DiabloUI/selgametype.h"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <memory>
#include <optional>
#include <vector>

#include "DiabloUI/diabloui.h"
#include "DiabloUI/deth_screen.hpp"
#include "DiabloUI/deth_theme.hpp"
#include "DiabloUI/scrollbar.h"
#include "DiabloUI/ui_flags.hpp"
#include "DiabloUI/ui_item.h"
#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "hwcursor.hpp"
#include "items.h"
#include "loadsave.h"
#include "lua/lua_global.hpp"
#include "options.h"
#include "utils/language.h"
#include "utils/str_cat.hpp"

namespace devilution {

GameType CurrentGameType()
{
	if (gbIsSpawn)
		return GameType::Shareware;
	return gbIsHellfire ? GameType::Hellfire : GameType::Diablo;
}

std::string_view GameTypeName(GameType type)
{
	switch (type) {
	case GameType::Shareware:
		return _("Shareware");
	case GameType::Hellfire:
		return _("Hellfire");
	default:
		return _("Diablo");
	}
}

bool IsGameTypeAvailable(GameType type)
{
	switch (type) {
	case GameType::Shareware:
		return true; // spawn.mpq ships with the game; DIABDAT.MPQ also plays the shareware rules
	case GameType::Diablo:
		return HasDiabdatMpq;
	case GameType::Hellfire:
		// All four are required: the engine exits with an error if any is missing.
		return HasDiabdatMpq && CountHellfireMpqs() == 4;
	}
	return false;
}

std::string MissingGameTypeFiles(GameType type)
{
	std::string missing;
	auto need = [&](bool present, std::string_view file) {
		if (present)
			return;
		if (!missing.empty())
			missing += ", ";
		missing += file;
	};
	if (type == GameType::Diablo || type == GameType::Hellfire)
		need(HasDiabdatMpq, "DIABDAT.MPQ");
	if (type == GameType::Hellfire) {
		need(HaveMpqFile("hellfire"), "hellfire.mpq");
		need(HaveMpqFile("hfmonk"), "hfmonk.mpq");
		need(HaveMpqFile("hfmusic"), "hfmusic.mpq");
		need(HaveMpqFile("hfvoice"), "hfvoice.mpq");
	}
	return missing;
}

std::string_view GameTypeRequiredFiles(GameType type)
{
	switch (type) {
	case GameType::Shareware:
		return "spawn.mpq";
	case GameType::Diablo:
		return "DIABDAT.MPQ";
	case GameType::Hellfire:
		return "DIABDAT.MPQ, hellfire.mpq, hfmonk.mpq, hfmusic.mpq, hfvoice.mpq";
	}
	return {};
}

namespace {

bool endMenu;
std::vector<std::unique_ptr<UiListItem>> vecItems;
std::vector<std::unique_ptr<UiItemBase>> vecDialog;
std::optional<GameType> picked;

void ItemSelected(size_t value)
{
	const auto type = static_cast<GameType>(vecItems[value]->m_value);
	if (!IsGameTypeAvailable(type))
		return; // the row says which files are missing
	picked = type;
	endMenu = true;
}

void EscPressed()
{
	endMenu = true;
}

void ApplyGameType(GameType type)
{
	UnloadUiGFX();
	FreeItemGFX();
	// "Diablo" here means: do not ask at startup, use the choice below.
	GetOptions().GameMode.gameMode.SetValue(StartUpGameMode::Diablo);
	GetOptions().GameMode.shareware.SetValue(type == GameType::Shareware);
	gbIsSpawn = type == GameType::Shareware || !HasDiabdatMpq;
	const bool wantHellfire = type == GameType::Hellfire;
	if (wantHellfire) {
		// DETH: the Hellfire mod is only listed when the archives were seen at startup; files
		// imported since then would leave nothing to switch on.
		HasHellfireMpq = true;
		const std::vector<std::string_view> mods = GetOptions().Mods.GetModList();
		if (std::find(mods.begin(), mods.end(), "hf") == mods.end())
			GetOptions().Mods.AddModEntry("hf");
	}
	GetOptions().Mods.SetHellfireEnabled(wantHellfire);
	// The change callback reloads the mods; do it here when it did not run (a new entry has none).
	if (gbIsHellfire != wantHellfire)
		LuaReloadActiveMods();
	gbIsHellfireSaveGame = gbIsHellfire;
	SaveOptions();
	UiInitialize();
	InitItemGFX();
	SetHardwareCursor(CursorInfo::UnknownCursor());
}

} // namespace

void UiSelectGameType()
{
	picked = std::nullopt;
	UiLoadBlackBackground();
	UiThemeBackground = true;
	LoadScrollBar();

	const theme::ScreenFrame frame = theme::MakeScreenFrame(/*wide=*/true, /*sidePane=*/false, /*buttons=*/!theme::TouchMode());
	theme::AddTitle(vecDialog, _("Game").data(), frame);
	int y = frame.content.position.y;
	y += theme::AddHeading(vecDialog, _("Choose the game to play").data(), frame, y);

	const GameType current = CurrentGameType();
	size_t selected = 0;
	for (const GameType type : { GameType::Shareware, GameType::Diablo, GameType::Hellfire }) {
		const bool available = IsGameTypeAvailable(type);
		auto item = std::make_unique<UiListItem>(GameTypeName(type), static_cast<int>(type), available ? UiFlags::ColorUiGold : UiFlags::ColorUiSilverDark);
		if (!available)
			item->subText = StrCat(_("Missing: "), MissingGameTypeFiles(type));
		else if (type == current)
			item->subText = std::string(_("Playing now"));
		else if (type == GameType::Shareware)
			item->subText = std::string(_("The free demo: play with anyone, including friends without the full game"));
		else if (type == GameType::Diablo)
			item->subText = std::string(_("The full original game"));
		else
			item->subText = std::string(_("Diablo with the Hellfire expansion"));
		if (type == current)
			selected = vecItems.size();
		vecItems.push_back(std::move(item));
	}
	auto list = theme::MakeList(vecItems, frame, y, vecItems.size(), UiFlags::FontSize24 | UiFlags::AlignCenter, theme::SubTextHeight());
	vecDialog.push_back(std::move(list));
	if (!theme::TouchMode()) {
		theme::AddButtons(vecDialog, frame, { { _("OK"), &UiFocusNavigationSelect }, { _("Back"), &UiFocusNavigationEsc } });
	}

	UiInitList(nullptr, ItemSelected, EscPressed, vecDialog, true, nullptr, nullptr, selected);
	endMenu = false;
	while (!endMenu) {
		UiClearScreen();
		UiRenderItems(vecDialog);
		UiPollAndRender();
	}
	UiInitList_clear();
	UiThemeBackground = false;
	vecDialog.clear();
	vecItems.clear();
	ArtBackground = std::nullopt;
	ArtBackgroundWidescreen = std::nullopt;
	UnloadScrollBar();

	if (picked && *picked != current)
		ApplyGameType(*picked);
}

} // namespace devilution
