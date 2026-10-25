#include "DiabloUI/multi/selgame.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/diabloui.h"
#include "DiabloUI/deth_screen.hpp"
#include "DiabloUI/hero/selhero.h"
#include "DiabloUI/selgametype.h"
#include "DiabloUI/selok.h"
#include "DiabloUI/selyesno.h"
#include "engine/assets.hpp"
#include "deth_version.hpp"
#include "DiabloUI/ui_flags.hpp"
#include "DiabloUI/ui_item.h"
#include "config.h"
#include "diablo.h"
#ifndef DISABLE_ZERO_TIER
#include "dvlnet/join_code.hpp"
#include "dvlnet/local_address.h"
#include "dvlnet/zerotier_native.h"
#endif
#include "engine/point.hpp"
#include "engine/render/text_render.hpp"
#include "levels/gendung.h"
#include "menu.h"
#include "multi.h"
#include "options.h"
#include "storm/storm_net.hpp"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/sdl_geometry.h"
#include "utils/str_cat.hpp"
#include "utils/ui_fwd.h"
#include "utils/utf8.hpp"

namespace devilution {

char selgame_Label[32];
char selgame_Ip[129] = "";
char selgame_Password[16] = "";
char selgame_Description[512];
std::string selgame_Title;
bool selgame_enteringGame;
size_t selgame_selectedGame;

std::string selgame_JoinCode;
/** DETH: the Wi-Fi code as typed, kept for the next time. */
std::string selgame_WifiCodeText;

std::string GenerateJoinCode()
{
	std::string code;
	uint64_t node = 0;
#ifndef DISABLE_ZERO_TIER
	// The node comes online a few seconds after Online was picked; wait for it briefly.
	for (int i = 0; i < 200 && node == 0; ++i) {
		node = net::zerotier_node_id();
		if (node == 0)
			SDL_Delay(50);
	}
#endif
	if (node != 0) {
		for (size_t i = 0; i < OnlineHostPartLength; ++i)
			code += JoinCodeAlphabet[(node >> (5 * (OnlineHostPartLength - 1 - i))) & 31];
	} else {
		code = RandomCodeChars(OnlineHostPartLength);
	}
	code += RandomCodeChars(JoinCodeLength - code.size());
	// Automated online test (.github/workflows/Online_test.yml): hand the code to the joiner.
	if (const char *file = std::getenv("DETH_ONLINE_TEST_CODE_FILE"); file != nullptr) {
		if (FILE *f = std::fopen(file, "w"); f != nullptr) {
			std::fputs(code.c_str(), f);
			std::fclose(f);
		}
	}
	return code;
}

std::string LowerAscii(std::string_view text)
{
	std::string out(text);
	for (char &c : out)
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	return out;
}
bool selgame_endMenu;
int *gdwPlayerId;
_difficulty nDifficulty;
int nTickRate;
int heroLevel;

static GameData *m_game_data;
extern int provider;

namespace {

const char *title = "";

/** DETH: the screen layout shared by every selgame sub-screen. */
theme::ScreenFrame selgameFrame;
/** DETH: width available to the wrapped description in the side pane. */
int DESCRIPTION_WIDTH = 176;

std::vector<std::unique_ptr<UiListItem>> vecSelGameDlgItems;
std::vector<std::unique_ptr<UiItemBase>> vecSelGameDialog;
std::vector<GameInfo> Gamelist;
uint32_t firstPublicGameInfoRequestSend = 0;
size_t HighlightedItem;

void selgame_FreeVectors()
{
	vecSelGameDlgItems.clear();

	vecSelGameDialog.clear();
}

void selgame_Init()
{
	// Loaded for its palette; the background itself comes from the theme.
	LoadBackgroundArt("ui_art\\selgame");
	UiThemeBackground = true;
}

void selgame_Free()
{
	UiThemeBackground = false;
	ArtBackground = std::nullopt;
	selgame_FreeVectors();
}

/**
 * @brief DETH: starts a themed sub-screen: lays out the frame and enables the theme background.
 * The items vector must already be cleared.
 */
void selgame_BeginScreen()
{
	UiThemeBackground = true;
	selgameFrame = theme::MakeScreenFrame(/*wide=*/true, /*sidePane=*/true, /*buttons=*/true);
}

/**
 * @brief DETH: adds the side pane with a label on top and the wrapped description under it.
 * Skipped on narrow layouts (no side pane).
 */
void selgame_AddSidePane(const char *label, UiFlags labelFlags)
{
	const Rectangle side = selgameFrame.side;
	if (side.size.width <= 0)
		return;
	const int textX = side.position.x + 12;
	const int textWidth = side.size.width - 24;
	DESCRIPTION_WIDTH = textWidth;
	const int labelHeight = 33;
	const int descriptionTop = side.position.y + 12 + labelHeight + 8;
	// Up to fourteen description lines; the panel hugs that instead of the whole column.
	const bool large = theme::LargeText();
	const int lineHeight = large ? 28 : 16;
	const int descriptionHeight = std::min(lineHeight * 14, std::max(lineHeight, side.position.y + side.size.height - 12 - descriptionTop));
	const int panelHeight = descriptionTop + descriptionHeight + 12 - side.position.y;

	vecSelGameDialog.push_back(std::make_unique<UiPanel>(MakeSdlRect(side.position.x, side.position.y, side.size.width, side.size.height)));
	vecSelGameDialog.push_back(std::make_unique<UiArtText>(label, MakeSdlRect(textX, side.position.y + 12, textWidth, labelHeight), labelFlags | UiFlags::VerticalCenter, 3));
	vecSelGameDialog.push_back(std::make_unique<UiArtText>(selgame_Description, MakeSdlRect(textX, descriptionTop, textWidth, descriptionHeight), large ? UiFlags::FontSize24 | UiFlags::ColorUiSilver : UiFlags::FontSize12 | UiFlags::ColorUiSilverDark, 1, lineHeight));
}

void selgame_AddButtons()
{
	theme::AddButtons(vecSelGameDialog, selgameFrame, {
	    { _("OK"), &UiFocusNavigationSelect },
	    { _("Cancel"), &UiFocusNavigationEsc },
	});
}

bool IsGameCompatible(const GameData &data)
{
	// TODO GetGameId is now only used cosmetic (gamelist icon), we need to check the enabled mods in addition to the below check
	return data.versionMajor == PROJECT_VERSION_MAJOR
	    && data.versionMinor == PROJECT_VERSION_MINOR
	    && data.versionPatch == PROJECT_VERSION_PATCH
	    && data.isSpawn == (gbIsSpawn ? 1 : 0)
	    && data.isHellfire == (gbIsHellfire ? 1 : 0)
	    && data.assetsFingerprint[0] == sgGameInitInfo.assetsFingerprint[0]
	    && data.assetsFingerprint[1] == sgGameInitInfo.assetsFingerprint[1];
}

void UiInitGameSelectionList(std::string_view search);

/** DETH: same game type and engine version; mods and files are checked after joining. */
bool IsGameTypeCompatible(const GameData &data)
{
	return data.versionMajor == PROJECT_VERSION_MAJOR
	    && data.versionMinor == PROJECT_VERSION_MINOR
	    && data.versionPatch == PROJECT_VERSION_PATCH
	    && data.isSpawn == (gbIsSpawn ? 1 : 0)
	    && data.isHellfire == (gbIsHellfire ? 1 : 0);
}

/** DETH: active mods other than Hellfire (which is the game type). */
std::vector<std::string> ActiveGameplayMods()
{
	std::vector<std::string> mods;
	for (const std::string_view mod : GetOptions().Mods.GetActiveModList()) {
		if (mod != "hf")
			mods.emplace_back(mod);
	}
	std::sort(mods.begin(), mods.end());
	return mods;
}

/** DETH: what this game tells joining players: "DETH <version>" and one mod per line. */
std::string BuildLocalExtras()
{
	std::string extras = StrCat("DETH ", DETH_VERSION);
	for (const std::string &mod : ActiveGameplayMods())
		StrAppend(extras, "\n", mod);
	return extras;
}

std::string ModDisplayList(const std::vector<std::string> &mods)
{
	std::string text;
	for (const std::string &mod : mods)
		StrAppend(text, "\n- ", mod);
	return text;
}

/**
 * DETH: once joined, makes sure the host runs the same DETH and the same mods. If only the mods
 * differ, offers to switch to the host's mods for this game. Returns false after leaving.
 */
bool CheckHostVersionAndMods()
{
	const char *title = _("Multi Player Game").data();
	auto leave = [&](const std::string &message) {
		SNetLeaveGame(leaveinfo_t::LEAVE_EXIT);
		GetOptions().Mods.RestoreTemporaryMods();
		InitGameInfo();
		selgame_Free();
		if (!message.empty())
			UiSelOkDialog(title, message.c_str(), false);
		selgame_Init();
		UiInitGameSelectionList("");
		return false;
	};

	const std::optional<std::string> extras = DvlNet_QueryHostExtras(5000);
	if (!extras || !extras->starts_with("DETH "))
		return leave(FormatRuntime(_("The host runs an older DETH. Both of you need DETH {:s} (Check for Updates on the main menu)."), DETH_VERSION));

	std::vector<std::string> hostMods;
	std::string hostVersion;
	size_t start = 0;
	while (start <= extras->size()) {
		size_t end = extras->find('\n', start);
		if (end == std::string::npos)
			end = extras->size();
		const std::string line = extras->substr(start, end - start);
		if (hostVersion.empty())
			hostVersion = line.substr(5);
		else if (!line.empty())
			hostMods.push_back(line);
		start = end + 1;
	}
	std::sort(hostMods.begin(), hostMods.end());
	if (hostVersion != DETH_VERSION)
		return leave(FormatRuntime(_("The host has DETH {:s} and you have {:s}. Both of you need the same version (Check for Updates on the main menu)."), hostVersion, DETH_VERSION));

	if (hostMods != ActiveGameplayMods()) {
		std::string question = hostMods.empty()
		    ? std::string(_("The host plays without mods."))
		    : StrCat(_("The host plays with these mods:"), ModDisplayList(hostMods));
		StrAppend(question, "\n\n", _("Join with the host's mods? They are used only for this game; your own mod settings come back when it ends."));
		// The automated online test answers Yes (.github/workflows/Online_test.yml).
		const bool autoAccept = std::getenv("DETH_ONLINE_TEST_ACCEPT_MODS") != nullptr;
		bool accept = autoAccept;
		if (!autoAccept) {
			selgame_Free();
			accept = UiSelHeroYesNoDialog(title, question.c_str());
			selgame_Init();
		}
		if (!accept)
			return leave({});
		std::vector<std::string> missing;
		if (!GetOptions().Mods.ApplyTemporaryMods(hostMods, missing))
			return leave(StrCat(_("You do not have these mods:"), ModDisplayList(missing)));
	}

	const uint64_t fingerprint = GetLogicAssetsFingerprint();
	if (m_game_data->assetsFingerprint[0] != static_cast<uint32_t>(fingerprint) || m_game_data->assetsFingerprint[1] != static_cast<uint32_t>(fingerprint >> 32)) {
		LogInfo("Game files differ from the host's (host {:08X}{:08X}, here {:016X})", m_game_data->assetsFingerprint[1], m_game_data->assetsFingerprint[0], fingerprint);
		return leave(std::string(_("Your game files differ from the host's (modified or extra files in the game folder). Use the same files as the host.")));
	}
	return true;
}

static std::string GetErrorMessageIncompatibility(const GameData &data)
{
	// DETH: say which game the host plays and what it takes to join it.
	const GameType hostType = data.isSpawn != 0 ? GameType::Shareware : (data.isHellfire != 0 ? GameType::Hellfire : GameType::Diablo);
	if (hostType != CurrentGameType()) {
		if (IsGameTypeAvailable(hostType))
			return FormatRuntime(_("The host is playing {:s}. Switch Game to {:s} on the main menu to join."), GameTypeName(hostType), GameTypeName(hostType));
		return FormatRuntime(_("The host is playing {:s}. To join, add the missing files: {:s}."), GameTypeName(hostType), MissingGameTypeFiles(hostType));
	}
	if (data.versionMajor == PROJECT_VERSION_MAJOR && data.versionMinor == PROJECT_VERSION_MINOR && data.versionPatch == PROJECT_VERSION_PATCH)
		return std::string(_("Your game files differ from the host's: different mods or modified files. Use the same mods as the host."));
	return FormatRuntime(_(/* TRANSLATORS: Error message when somebody tries to join a game running another version. */ "Your version {:s} does not match the host {:d}.{:d}.{:d}."), PROJECT_VERSION, data.versionMajor, data.versionMinor, data.versionPatch);
}

std::string_view DifficultyName(int difficulty)
{
	switch (difficulty) {
	case DIFF_NIGHTMARE:
		return _("Nightmare");
	case DIFF_HELL:
		return _("Hell");
	default:
		return _("Normal");
	}
}

/**
 * DETH: the actions sit side by side on top, the public games are listed under them with their
 * code, difficulty, players and ping. One short hint line describes the highlighted entry.
 */
void UiInitGameSelectionList(std::string_view search)
{
	selgame_enteringGame = false;
	selgame_selectedGame = 0;

	if (provider == SELCONN_LOOPBACK) {
		selgame_enteringGame = true;
		selgame_GameSelection_Select(0);
		return;
	}

	if (provider == SELCONN_ZT) {
		CopyUtf8(selgame_Ip, GetOptions().Network.szPreviousZTGame, sizeof(selgame_Ip));
	} else {
		CopyUtf8(selgame_Ip, selgame_WifiCodeText, sizeof(selgame_Ip));
	}

	selgame_FreeVectors();
	selgame_Label[0] = '\0';
	UiThemeBackground = true;
	selgameFrame = theme::MakeScreenFrame(/*wide=*/true, /*sidePane=*/false, /*buttons=*/true);
	theme::AddTitle(vecSelGameDialog, _(ConnectionNames[provider]).data(), selgameFrame);

	const bool online = provider == SELCONN_ZT;
#ifdef PACKET_ENCRYPTION
	vecSelGameDlgItems.push_back(std::make_unique<UiListItem>(online ? _("Host Private") : _("Host Game"), 0, UiFlags::ColorUiGold));
#endif
	// TCP games are joined with the Wi-Fi code, which public games do not have.
	if (online)
		vecSelGameDlgItems.push_back(std::make_unique<UiListItem>(_("Host Public"), 1, UiFlags::ColorUiGold));
	vecSelGameDlgItems.push_back(std::make_unique<UiListItem>(online ? _("Join with Code") : _("Join Game"), 2, UiFlags::ColorUiGold));
	const size_t actions = vecSelGameDlgItems.size();

	const Rectangle content = selgameFrame.content;
	const int pitch = selgameFrame.RowPitch();
	const int top = content.position.y;
	constexpr int HintHeight = 36;
	int space = HintHeight;

	// The hint under the actions: two short lines at most.
	DESCRIPTION_WIDTH = content.size.width - 24;
	vecSelGameDialog.push_back(std::make_unique<UiArtText>(selgame_Description, MakeSdlRect(content.position.x + 12, top + pitch + 4, DESCRIPTION_WIDTH, HintHeight - 4), UiFlags::FontSize12 | UiFlags::ColorUiSilver | UiFlags::AlignCenter, 1, 16));

	if (online) {
		const int headingY = top + pitch + space;
		space += theme::AddHeading(vecSelGameDialog, _("Public Games").data(), selgameFrame, headingY);
		// Column labels, lined up with the columns the list draws.
		const int labelY = top + pitch + space;
		const int inset = theme::TextInset();
		const int textWidth = content.size.width - 2 * inset;
		const UiFlags labelFlags = UiFlags::FontSize12 | UiFlags::ColorUiSilverDark;
		const int x0 = content.position.x + inset;
		vecSelGameDialog.push_back(std::make_unique<UiArtText>(_("Code").data(), MakeSdlRect(x0, labelY, textWidth, 16), labelFlags));
		vecSelGameDialog.push_back(std::make_unique<UiArtText>(_("Difficulty").data(), MakeSdlRect(x0 + textWidth * 42 / 100, labelY, textWidth / 4, 16), labelFlags));
		vecSelGameDialog.push_back(std::make_unique<UiArtText>(_("Players").data(), MakeSdlRect(x0 + textWidth * 66 / 100, labelY, textWidth / 4, 16), labelFlags));
		vecSelGameDialog.push_back(std::make_unique<UiArtText>(_("Ping").data(), MakeSdlRect(x0, labelY, textWidth - 4, 16), labelFlags | UiFlags::AlignRight));
		space += 18;

		const int bottom = content.position.y + content.size.height;
		const size_t fits = static_cast<size_t>(std::max(1, (bottom - (top + pitch + space)) / pitch));
		if (Gamelist.empty()) {
			// We expect the game list to be received after 3 seconds
			const bool loading = firstPublicGameInfoRequestSend == 0 || (SDL_GetTicks() - firstPublicGameInfoRequestSend) < 2000;
			vecSelGameDlgItems.push_back(std::make_unique<UiListItem>(loading ? _("Loading...") : _("None yet"), -1, UiFlags::ElementDisabled | UiFlags::ColorUiSilver));
		} else {
			// Closest first; only as many as fit (public games are few).
			std::stable_sort(Gamelist.begin(), Gamelist.end(), [](const GameInfo &a, const GameInfo &b) {
				return a.latency.value_or(9999) < b.latency.value_or(9999);
			});
			for (unsigned i = 0; i < Gamelist.size() && i < fits; i++) {
				const GameInfo &game = Gamelist[i];
				auto row = std::make_unique<UiListItem>(std::string_view(game.name), i + 3, UiFlags::ColorUiGold);
				if (IsGameCompatible(game.gameData)) {
					row->columns.emplace_back(DifficultyName(game.gameData.nDifficulty));
					row->columns.push_back(StrCat(game.players.size(), "/", MAX_PLRS));
				} else {
					row->columns.emplace_back(_("Can't join"));
				}
				if (game.latency)
					row->rightText = StrCat(*game.latency, " ms");
				vecSelGameDlgItems.push_back(std::move(row));
			}
		}
	}

	auto list = std::make_unique<UiList>(vecSelGameDlgItems, vecSelGameDlgItems.size(), static_cast<Sint16>(content.position.x), static_cast<Sint16>(top), static_cast<Uint16>(content.size.width), static_cast<Uint16>(pitch), UiFlags::FontSize24, selgameFrame.layout.rowGap);
	list->themed = true;
	list->rowGroup = actions;
	list->rowGroupSpace = space;
	list->m_rect.h = static_cast<Uint16>(pitch * (vecSelGameDlgItems.size() - actions + 1) + space);
	vecSelGameDialog.push_back(std::move(list));

	theme::AddButtons(vecSelGameDialog, selgameFrame, { { _("Back"), &UiFocusNavigationEsc } });

	auto selectFn = [](size_t index) {
		// UiListItem::m_value could be different from
		// the index if packet encryption is disabled
		selgame_GameSelection_Select(vecSelGameDlgItems[index]->m_value);
	};

	if (!search.empty()) {
		for (size_t i = 0; i < vecSelGameDlgItems.size(); i++) {
			const int gameIndex = vecSelGameDlgItems[i]->m_value - 3;
			if (gameIndex >= 0 && search == Gamelist[gameIndex].name)
				HighlightedItem = i;
		}
	}
	if (HighlightedItem >= vecSelGameDlgItems.size() || HasAnyOf(vecSelGameDlgItems[HighlightedItem]->uiFlags, UiFlags::ElementDisabled))
		HighlightedItem = 0;

	UiInitList(selgame_GameSelection_Focus, selectFn, selgame_GameSelection_Esc, vecSelGameDialog, false, nullptr, nullptr, HighlightedItem);
}

} // namespace

void selgame_GameSelection_Init()
{
	UiInitGameSelectionList("");
}

void selgame_GameSelection_Focus(size_t value)
{
	HighlightedItem = value;
	const UiListItem &item = *vecSelGameDlgItems[value];
	const bool online = provider == SELCONN_ZT;
	std::string hint;
	switch (item.m_value) {
	case -1:
		break;
	case 0:
		hint = online ? _("Friends join with the code you share. Hidden from the public list.") : _("Friends on your Wi-Fi join with a short code.");
		break;
	case 1:
		hint = online ? _("Listed under Public Games for anyone to join.") : _("Anyone on your network can join.");
		break;
	case 2:
		hint = online ? _("Enter the code a friend shared with you.") : _("Enter the Wi-Fi code from the host's game menu.");
		break;
	default: {
		const GameInfo &gameInfo = Gamelist[item.m_value - 3];
		if (!IsGameCompatible(gameInfo.gameData)) {
			hint = GetErrorMessageIncompatibility(gameInfo.gameData);
			break;
		}
		switch (gameInfo.gameData.nTickRate) {
		case 20:
			hint = _("Speed: Normal");
			break;
		case 25:
			hint = _("Speed: Fast");
			break;
		case 30:
			hint = _("Speed: Faster");
			break;
		case 35:
			hint = _("Speed: Fastest");
			break;
		default:
			hint = StrCat("Speed: ", gameInfo.gameData.nTickRate);
			break;
		}
		if (!gameInfo.players.empty()) {
			StrAppend(hint, "   ", _("Players:"));
			for (const auto &playerName : gameInfo.players)
				StrAppend(hint, " ", playerName);
		}
		if (gameInfo.peerIsRelayed.value_or(false))
			StrAppend(hint, "   ", _("(relayed)"));
		break;
	}
	}
	CopyUtf8(selgame_Description, WordWrapString(hint, DESCRIPTION_WIDTH, GameFont12), sizeof(selgame_Description));
}

/**
 * @brief Load the current hero level from save file
 * @param pInfo Hero info
 * @return always true
 */
bool UpdateHeroLevel(_uiheroinfo *pInfo)
{
	if (pInfo->saveNumber == gSaveNumber)
		heroLevel = pInfo->level;

	return true;
}

void selgame_GameSelection_Select(size_t value)
{
	selgame_enteringGame = true;
	selgame_selectedGame = value;

	gfnHeroInfo(UpdateHeroLevel);

	selgame_FreeVectors();

	if (value > 2) {
#ifndef DISABLE_ZERO_TIER
		net::ZtDirectHostNode = 0;
#endif
		CopyUtf8(selgame_Ip, Gamelist[value - 3].name, sizeof(selgame_Ip));
		selgame_Password_Select(value);
		return;
	}

	selgame_BeginScreen();

	theme::AddTitle(vecSelGameDialog, &title, selgameFrame);
	selgame_AddSidePane(selgame_Label, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiSilver);

	int y = selgameFrame.content.position.y;

	switch (value) {
	case 0:
	case 1: {
		title = _("Create Game").data();

		y += theme::AddHeading(vecSelGameDialog, _("Select Difficulty").data(), selgameFrame, y);

		vecSelGameDlgItems.push_back(std::make_unique<UiListItem>(_("Normal"), DIFF_NORMAL));
		vecSelGameDlgItems.push_back(std::make_unique<UiListItem>(_("Nightmare"), DIFF_NIGHTMARE));
		vecSelGameDlgItems.push_back(std::make_unique<UiListItem>(_("Hell"), DIFF_HELL));

		vecSelGameDialog.push_back(theme::MakeList(vecSelGameDlgItems, selgameFrame, y, vecSelGameDlgItems.size(), UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold));

		selgame_AddButtons();

		UiInitList(selgame_Diff_Focus, selgame_Diff_Select, selgame_Diff_Esc, vecSelGameDialog, true);
		break;
	}
	case 2: {
		selgame_Title = FormatRuntime(_("Join {:s} Games"), _(ConnectionNames[provider]));
		title = selgame_Title.c_str();

		const char *inputHint;
		if (provider == SELCONN_ZT) {
			inputHint = _("Enter Join Code").data();
		} else {
			inputHint = _("Enter Wi-Fi Code").data();
		}

		y += theme::AddHeading(vecSelGameDialog, inputHint, selgameFrame, y);
		y += 8;

		{
			const SDL_Rect editRect = MakeSdlRect(selgameFrame.content.position.x, y, selgameFrame.content.size.width, selgameFrame.layout.rowHeight);
			auto edit = std::make_unique<UiEdit>(inputHint, selgame_Ip, 128, false, editRect, UiFlags::FontSize24 | UiFlags::ColorUiGold);
			edit->themed = true;
			edit->inputKind = UiEdit::InputKind::Address;
			vecSelGameDialog.push_back(std::move(edit));
		}

		selgame_AddButtons();

		HighlightedItem = 0;

		if (provider == SELCONN_ZT) {
			UiInitList(nullptr, selgame_JoinCode_Select, selgame_GameSelection_Init, vecSelGameDialog);
			break;
		}
		// DETH: Same Wi-Fi games are joined with a code too; it carries the address and the password.
		UiInitList(nullptr, selgame_WifiCode_Select, selgame_GameSelection_Init, vecSelGameDialog);
		break;
	}
	}
}

void selgame_GameSelection_Esc()
{
	UiInitList_clear();
	selgame_enteringGame = false;
	selgame_endMenu = true;
}

void selgame_Diff_Focus(size_t value)
{
	switch (vecSelGameDlgItems[value]->m_value) {
	case DIFF_NORMAL:
		CopyUtf8(selgame_Label, _("Normal"), sizeof(selgame_Label));
		CopyUtf8(selgame_Description, _("Normal Difficulty\nThis is where a starting character should begin the quest to defeat Diablo."), sizeof(selgame_Description));
		break;
	case DIFF_NIGHTMARE:
		CopyUtf8(selgame_Label, _("Nightmare"), sizeof(selgame_Label));
		CopyUtf8(selgame_Description, _("Nightmare Difficulty\nThe denizens of the Labyrinth have been bolstered and will prove to be a greater challenge. This is recommended for experienced characters only."), sizeof(selgame_Description));
		break;
	case DIFF_HELL:
		CopyUtf8(selgame_Label, _("Hell"), sizeof(selgame_Label));
		CopyUtf8(selgame_Description, _("Hell Difficulty\nThe most powerful of the underworld's creatures lurk at the gateway into Hell. Only the most experienced characters should venture in this realm."), sizeof(selgame_Description));
		break;
	}
	CopyUtf8(selgame_Description, WordWrapString(selgame_Description, DESCRIPTION_WIDTH, theme::LargeText() ? GameFont24 : GameFont12), sizeof(selgame_Description));
}

bool IsDifficultyAllowed(int value)
{
	if (value == 0 || (value == 1 && heroLevel >= 20) || (value == 2 && heroLevel >= 30)) {
		return true;
	}

	selgame_Free();

	if (value == 1)
		UiSelOkDialog(title, _("Your character must reach level 20 before you can enter a multiplayer game of Nightmare difficulty.").data(), false);
	if (value == 2)
		UiSelOkDialog(title, _("Your character must reach level 30 before you can enter a multiplayer game of Hell difficulty.").data(), false);

	selgame_Init();

	return false;
}

void selgame_Diff_Select(size_t value)
{
	if (selhero_isMultiPlayer && !IsDifficultyAllowed(vecSelGameDlgItems[value]->m_value)) {
		selgame_GameSelection_Select(selgame_selectedGame);
		return;
	}

	nDifficulty = (_difficulty)vecSelGameDlgItems[value]->m_value;

	if (!selhero_isMultiPlayer) {
		// This is part of a dangerous hack to enable difficulty selection in single-player.
		// FIXME: Dialogs should not refer to each other's variables.

		// We're in the selhero loop instead of the selgame one.
		// Free the selgame data and flag the end of the selhero loop.
		selhero_endMenu = true;

		// We only call FreeVectors because ArtBackground.Unload()
		// will be called by selheroFree().
		selgame_FreeVectors();

		// We must clear the InitList because selhero's loop will perform
		// one more iteration after this function exits.
		UiInitList_clear();

		return;
	}

	selgame_GameSpeedSelection();
}

void selgame_Diff_Esc()
{
	if (!selhero_isMultiPlayer) {
		selgame_Free();

		selhero_Init();
		selhero_List_Init();
		return;
	}

	if (provider == SELCONN_LOOPBACK) {
		selgame_GameSelection_Esc();
		return;
	}

	HighlightedItem = 0;
	selgame_GameSelection_Init();
}

void selgame_GameSpeedSelection()
{
	gfnHeroInfo(UpdateHeroLevel);

	selgame_FreeVectors();

	selgame_BeginScreen();

	theme::AddTitle(vecSelGameDialog, _("Create Game").data(), selgameFrame);
	selgame_AddSidePane(selgame_Label, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiSilver);

	int y = selgameFrame.content.position.y;
	y += theme::AddHeading(vecSelGameDialog, _("Select Game Speed").data(), selgameFrame, y);

	vecSelGameDlgItems.push_back(std::make_unique<UiListItem>(_("Normal"), 20));
	vecSelGameDlgItems.push_back(std::make_unique<UiListItem>(_("Fast"), 30));
	vecSelGameDlgItems.push_back(std::make_unique<UiListItem>(_("Faster"), 40));
	vecSelGameDlgItems.push_back(std::make_unique<UiListItem>(_("Fastest"), 50));

	vecSelGameDialog.push_back(theme::MakeList(vecSelGameDlgItems, selgameFrame, y, vecSelGameDlgItems.size(), UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold));

	selgame_AddButtons();

	UiInitList(selgame_Speed_Focus, selgame_Speed_Select, selgame_Speed_Esc, vecSelGameDialog, true);
}

void selgame_Speed_Focus(size_t value)
{
	switch (vecSelGameDlgItems[value]->m_value) {
	case 20:
		CopyUtf8(selgame_Label, _("Normal"), sizeof(selgame_Label));
		CopyUtf8(selgame_Description, _("Normal Speed\nThis is where a starting character should begin the quest to defeat Diablo."), sizeof(selgame_Description));
		break;
	case 30:
		CopyUtf8(selgame_Label, _("Fast"), sizeof(selgame_Label));
		CopyUtf8(selgame_Description, _("Fast Speed\nThe denizens of the Labyrinth have been hastened and will prove to be a greater challenge. This is recommended for experienced characters only."), sizeof(selgame_Description));
		break;
	case 40:
		CopyUtf8(selgame_Label, _("Faster"), sizeof(selgame_Label));
		CopyUtf8(selgame_Description, _("Faster Speed\nMost monsters of the dungeon will seek you out quicker than ever before. Only an experienced champion should try their luck at this speed."), sizeof(selgame_Description));
		break;
	case 50:
		CopyUtf8(selgame_Label, _("Fastest"), sizeof(selgame_Label));
		CopyUtf8(selgame_Description, _("Fastest Speed\nThe minions of the underworld will rush to attack without hesitation. Only a true speed demon should enter at this pace."), sizeof(selgame_Description));
		break;
	}
	CopyUtf8(selgame_Description, WordWrapString(selgame_Description, DESCRIPTION_WIDTH, theme::LargeText() ? GameFont24 : GameFont12), sizeof(selgame_Description));
}

void selgame_Speed_Esc()
{
	selgame_GameSelection_Select(selgame_selectedGame);
}

void selgame_Speed_Select(size_t value)
{
	nTickRate = vecSelGameDlgItems[value]->m_value;

	if (provider == SELCONN_LOOPBACK || selgame_selectedGame == 1) {
		selgame_Password_Select(0);
		return;
	}

	if (provider == SELCONN_ZT) {
		selgame_JoinCode = GenerateJoinCode();
		CopyUtf8(selgame_Password, selgame_JoinCode, sizeof(selgame_Password));
		selgame_Password_Select(0);
		return;
	}

	// DETH: Same Wi-Fi hosts get a Wi-Fi code instead of typing a password; the code is the password.
	const std::string wifiCode = MakeWifiCode(net::GetLocalNetworkAddress());
	CopyUtf8(selgame_Password, wifiCode.empty() ? RandomCodeChars(WifiCodeLength) : wifiCode, sizeof(selgame_Password));
	selgame_Password_Select(0);
}

void selgame_Password_Init(size_t /*value*/)
{
	memset(&selgame_Password, 0, sizeof(selgame_Password));

	selgame_FreeVectors();

	selgame_BeginScreen();

	theme::AddTitle(vecSelGameDialog, _(ConnectionNames[provider]).data(), selgameFrame);
	selgame_AddSidePane(_("Description:").data(), UiFlags::FontSize24 | UiFlags::ColorUiSilver);
	// DETH: say what the password is for (this screen used to keep the previous screen's text).
	CopyUtf8(selgame_Description,
	    selgame_selectedGame == 2
	        ? _("Enter the host's game password. Leave it empty if the game has none.")
	        : _("Set a password for this game. Players need it to join."),
	    sizeof(selgame_Description));
	CopyUtf8(selgame_Description, WordWrapString(selgame_Description, DESCRIPTION_WIDTH, theme::LargeText() ? GameFont24 : GameFont12), sizeof(selgame_Description));

	int y = selgameFrame.content.position.y;
	y += theme::AddHeading(vecSelGameDialog, _("Enter Password").data(), selgameFrame, y);
	y += 8;

	// Allow password to be empty only when joining games
	const bool allowEmpty = selgame_selectedGame == 2;
	{
		const SDL_Rect editRect = MakeSdlRect(selgameFrame.content.position.x, y, selgameFrame.content.size.width, selgameFrame.layout.rowHeight);
		auto edit = std::make_unique<UiEdit>(_("Enter Password"), selgame_Password, 15, allowEmpty, editRect, UiFlags::FontSize24 | UiFlags::ColorUiGold);
		edit->themed = true;
		edit->inputKind = UiEdit::InputKind::Password;
		vecSelGameDialog.push_back(std::move(edit));
	}

	selgame_AddButtons();

	UiInitList(nullptr, selgame_Password_Select, selgame_Password_Esc, vecSelGameDialog);
}

static bool IsGameCompatibleWithErrorMessage(const GameData &data)
{
	if (IsGameCompatible(data))
		return IsDifficultyAllowed(data.nDifficulty);

	selgame_Free();

	const std::string errorMessage = GetErrorMessageIncompatibility(data);
	UiSelOkDialog(title, errorMessage.c_str(), false);

	selgame_Init();

	return false;
}

void selgame_Password_Select(size_t /*value*/)
{
	char *gamePassword = nullptr;
	if (selgame_selectedGame == 0)
		gamePassword = selgame_Password;
	if (selgame_selectedGame == 2 && strlen(selgame_Password) > 0)
		gamePassword = selgame_Password;

	// If there is an error, the error message won't necessarily be set.
	// Clear the error so that we display "Unknown network error"
	// instead of an arbitrary message in that case.
	SDL_ClearError();

	if (selgame_selectedGame > 1) {
		bool allowJoin = true;
		if (selgame_selectedGame > 2)
			allowJoin = IsGameTypeCompatible(Gamelist[selgame_selectedGame - 3].gameData);
		DvlNet_SetLocalExtras(BuildLocalExtras());
		if (provider == SELCONN_ZT) {
			if (selgame_selectedGame == 2)
				CopyUtf8(GetOptions().Network.szPreviousZTGame, selgame_JoinCode, sizeof(GetOptions().Network.szPreviousZTGame));
			CopyUtf8(selgame_Ip, LowerAscii(selgame_Ip), sizeof(selgame_Ip));
		} else if (!selgame_WifiCodeText.empty()) {
			CopyUtf8(GetOptions().Network.szPreviousHost, selgame_WifiCodeText, sizeof(GetOptions().Network.szPreviousHost));
		}
		if (allowJoin && SNetJoinGame(selgame_Ip, gamePassword, gdwPlayerId)) {
			if (IsGameTypeCompatible(*m_game_data) && !CheckHostVersionAndMods())
				return;
			if (!IsGameCompatibleWithErrorMessage(*m_game_data)) {
				InitGameInfo();
				selgame_GameSelection_Select(1);
				return;
			}

			UiInitList_clear();
			selgame_endMenu = true;
		} else {
			InitGameInfo();
			selgame_Free();
			std::string error;
			if (!allowJoin)
				error = GetErrorMessageIncompatibility(Gamelist[selgame_selectedGame - 3].gameData);
			else
				error = SDL_GetError();
			if (error.empty())
				error = "Unknown network error";
			UiSelOkDialog(_("Multi Player Game").data(), error.c_str(), false);
			selgame_Init();
			if (selgame_selectedGame == 2 && provider == SELCONN_ZT)
				selgame_GameSelection_Select(2);
			else if (selgame_selectedGame == 2)
				selgame_Password_Init(selgame_selectedGame);
			else
				UiInitGameSelectionList("");
		}
		return;
	}

	m_game_data->nDifficulty = nDifficulty;
	m_game_data->nTickRate = nTickRate;
	m_game_data->bRunInTown = *GetOptions().Gameplay.runInTown ? 1 : 0;
	m_game_data->bTheoQuest = *GetOptions().Gameplay.theoQuest ? 1 : 0;
	m_game_data->bCowQuest = *GetOptions().Gameplay.cowQuest ? 1 : 0;

	DvlNet_SetLocalExtras(BuildLocalExtras());
	GameData gameInitInfo = *m_game_data;
	SwapGameDataLE(gameInitInfo);
#ifndef DISABLE_ZERO_TIER
	net::ZtDirectHostNode = 0;
#endif
	std::string onlineName;
	if (provider == SELCONN_ZT && selgame_selectedGame == 0)
		onlineName = LowerAscii(selgame_JoinCode);
	if (SNetCreateGame(onlineName.empty() ? nullptr : onlineName.c_str(), gamePassword, reinterpret_cast<char *>(&gameInitInfo), sizeof(gameInitInfo), gdwPlayerId)) {
		UiInitList_clear();
		selgame_endMenu = true;
	} else {
		selgame_Free();
		std::string error = SDL_GetError();
		if (error.empty())
			error = "Unknown network error";
		UiSelOkDialog(_("Multi Player Game").data(), error.c_str(), false);
		selgame_Init();
		if (provider == SELCONN_ZT)
			UiInitGameSelectionList("");
		else
			selgame_Password_Init(0);
	}
}

void selgame_JoinCode_Select(size_t /*value*/)
{
	const std::string code = NormalizeJoinCode(selgame_Ip);
	memset(&selgame_Password, 0, sizeof(selgame_Password));
#ifndef DISABLE_ZERO_TIER
	net::ZtDirectHostNode = HostNodeFromCode(code);
#endif
	if (code.size() == JoinCodeLength) {
		// A private game: the code is the name and the password.
		selgame_JoinCode = code;
		CopyUtf8(selgame_Password, code, sizeof(selgame_Password));
		CopyUtf8(selgame_Ip, code, sizeof(selgame_Ip));
	} else if (code.empty()) {
		selgame_Free();
		UiSelOkDialog(_("Multi Player Game").data(), _("Enter the join code your friend shared, like 7Q2MXP4DK9.").data(), false);
		selgame_Init();
		selgame_GameSelection_Select(2);
		return;
	} else {
		// Anything else is the name of a public game.
		selgame_JoinCode = code;
		CopyUtf8(selgame_Ip, code, sizeof(selgame_Ip));
	}
	selgame_Password_Select(0);
}

void selgame_WifiCode_Select(size_t /*value*/)
{
	const std::string code = NormalizeJoinCode(selgame_Ip);
	const std::optional<std::string> address = ParseWifiCode(code);
	if (!address) {
		selgame_Free();
		UiSelOkDialog(_("Multi Player Game").data(), _("Enter the Wi-Fi code from the host's game menu, like 7Q2MXP.").data(), false);
		selgame_Init();
		selgame_GameSelection_Select(2);
		return;
	}
	selgame_WifiCodeText = code;
	CopyUtf8(selgame_Ip, *address, sizeof(selgame_Ip));
	CopyUtf8(selgame_Password, code, sizeof(selgame_Password));
	selgame_Password_Select(0);
}

void selgame_Password_Esc()
{
	if (selgame_selectedGame == 2)
		selgame_GameSelection_Select(2);
	else
		selgame_GameSpeedSelection();
}

void RefreshGameList()
{
	static uint32_t lastRequest = 0;
	static uint32_t lastUpdate = 0;

	if (selgame_enteringGame)
		return;

	const uint32_t currentTime = SDL_GetTicks();

	if ((lastRequest == 0 || currentTime - lastRequest > 30000) && DvlNet_SendInfoRequest()) {
		lastRequest = currentTime;
		lastUpdate = currentTime - 3000; // Give 2 sec for responses, but don't wait 5
		if (firstPublicGameInfoRequestSend == 0)
			firstPublicGameInfoRequestSend = currentTime;
	}

	if (lastUpdate == 0 || currentTime - lastUpdate > 5000) {
		const int gameIndex = vecSelGameDlgItems[HighlightedItem]->m_value - 3;
		const std::string gameSearch = gameIndex >= 0 ? Gamelist[gameIndex].name : "";
		std::vector<GameInfo> gamelist = DvlNet_GetGamelist();
		Gamelist.clear();
		for (unsigned i = 0; i < gamelist.size(); i++) {
			Gamelist.push_back(gamelist[i]);
		}
		UiInitGameSelectionList(gameSearch);
		lastUpdate = currentTime;
	}
}

bool UiSelectGame(GameData *gameData, int *playerId)
{
	firstPublicGameInfoRequestSend = 0;
	gdwPlayerId = playerId;
	m_game_data = gameData;
	selgame_Init();
	HighlightedItem = 0;
	selgame_GameSelection_Init();

	selgame_endMenu = false;

	DvlNet_ClearPassword();
	DvlNet_ClearGamelist();

	while (!selgame_endMenu) {
		UiClearScreen();
		UiPollAndRender();
		if (provider == SELCONN_ZT)
			RefreshGameList();
	}
	selgame_Free();

	return selgame_enteringGame;
}
} // namespace devilution
