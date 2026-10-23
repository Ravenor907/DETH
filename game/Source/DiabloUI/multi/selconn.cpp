#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_rect.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/diabloui.h"
#include "DiabloUI/deth_screen.hpp"
#include "DiabloUI/ui_flags.hpp"
#include "DiabloUI/ui_item.h"
#include "engine/point.hpp"
#include "engine/render/text_render.hpp"
#include "multi.h"
#include "storm/storm_net.hpp"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/sdl_geometry.h"
#include "utils/ui_fwd.h"
#include "utils/utf8.hpp"

namespace devilution {

int provider;
const char *ConnectionNames[] {
	N_("Online"),
	N_("Client-Server (TCP)"),
	N_("Offline"),
};

namespace {

char selconn_MaxPlayers[64];
char selconn_Description[256];
char selconn_Gateway[129];
bool selconn_ReturnValue = false;
bool selconn_EndMenu = false;
GameData *selconn_GameData;

std::vector<std::unique_ptr<UiListItem>> vecConnItems;
std::vector<std::unique_ptr<UiItemBase>> vecSelConnDlg;

/** DETH: the screen layout. */
theme::ScreenFrame selconnFrame;
/** DETH: width available to the wrapped description in the side pane. */
int selconnDescriptionWidth = 176;

void SelconnEsc();
void SelconnFocus(size_t value);
void SelconnSelect(size_t value);

void SelconnLoad()
{
	// Loaded for its palette; the background itself comes from the theme.
	LoadBackgroundArt("ui_art\\selconn");
	UiThemeBackground = true;

#ifndef NONET
#ifndef DISABLE_ZERO_TIER
	vecConnItems.push_back(std::make_unique<UiListItem>(_(ConnectionNames[SELCONN_ZT]), SELCONN_ZT));
#endif
#ifndef DISABLE_TCP
	vecConnItems.push_back(std::make_unique<UiListItem>(_(ConnectionNames[SELCONN_TCP]), SELCONN_TCP));
#endif
#endif
	vecConnItems.push_back(std::make_unique<UiListItem>(_(ConnectionNames[SELCONN_LOOPBACK]), SELCONN_LOOPBACK));

	selconnFrame = theme::MakeScreenFrame(/*wide=*/true, /*sidePane=*/true, /*buttons=*/true);

	theme::AddTitle(vecSelConnDlg, _("Multi Player Game").data(), selconnFrame);

	// Side pane: players supported, requirements, gateway.
	const Rectangle side = selconnFrame.side;
	const bool hasSide = side.size.width > 0;
	if (hasSide) {
		const int textX = side.position.x + 12;
		const int textWidth = side.size.width - 24;
		selconnDescriptionWidth = textWidth;
		const bool large = theme::LargeText();
		const UiFlags descFlags = large ? UiFlags::FontSize24 | UiFlags::ColorUiSilver : UiFlags::FontSize12 | UiFlags::ColorUiSilverDark;
		const int lineHeight = large ? 28 : 16;

		// Room for the longest description (the Online one) without cutting it off.
		const int descriptionHeight = lineHeight * 10;
		const int panelHeight = std::min(side.size.height, 12 + 21 + 8 + 21 + descriptionHeight + 12);
		vecSelConnDlg.push_back(std::make_unique<UiPanel>(MakeSdlRect(side.position.x, side.position.y, side.size.width, panelHeight)));

		int sy = side.position.y + 12;
		vecSelConnDlg.push_back(std::make_unique<UiArtText>(selconn_MaxPlayers, MakeSdlRect(textX, sy, textWidth, 21), descFlags));
		sy += 21 + 8;
		vecSelConnDlg.push_back(std::make_unique<UiArtText>(_("Requirements:").data(), MakeSdlRect(textX, sy, textWidth, 21), descFlags));
		sy += 21;
		vecSelConnDlg.push_back(std::make_unique<UiArtText>(selconn_Description, MakeSdlRect(textX, sy, textWidth, descriptionHeight), descFlags, 1, lineHeight));
	}

	int y = selconnFrame.content.position.y;
	y += theme::AddHeading(vecSelConnDlg, _("Select Connection").data(), selconnFrame, y);

	vecSelConnDlg.push_back(theme::MakeList(vecConnItems, selconnFrame, y, vecConnItems.size(), UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::VerticalCenter | UiFlags::ColorUiGold));

	theme::AddButtons(vecSelConnDlg, selconnFrame, {
	    { _("OK"), &UiFocusNavigationSelect },
	    { _("Cancel"), &UiFocusNavigationEsc },
	});

	UiInitList(SelconnFocus, SelconnSelect, SelconnEsc, vecSelConnDlg, true);
}

void SelconnFree()
{
	UiThemeBackground = false;
	ArtBackground = std::nullopt;

	vecConnItems.clear();

	vecSelConnDlg.clear();
}

void SelconnEsc()
{
	selconn_ReturnValue = false;
	selconn_EndMenu = true;
}

void SelconnFocus(size_t value)
{
	int players = MAX_PLRS;
	switch (vecConnItems[value]->m_value) {
	case SELCONN_TCP:
		CopyUtf8(selconn_Description, _("All computers must be connected to a TCP-compatible network."), sizeof(selconn_Description));
		players = MAX_PLRS;
		break;
	case SELCONN_ZT:
		CopyUtf8(selconn_Description, _("Play with anyone on the internet. Host a game and share its join code, or join a friend with theirs. No account needed."), sizeof(selconn_Description));
		players = MAX_PLRS;
		break;
	case SELCONN_LOOPBACK:
		CopyUtf8(selconn_Description, _("Play by yourself with no network exposure."), sizeof(selconn_Description));
		players = 1;
		break;
	}

	CopyUtf8(selconn_MaxPlayers, FormatRuntime(_("Players Supported: {:d}"), players), sizeof(selconn_MaxPlayers));
	CopyUtf8(selconn_Description, WordWrapString(selconn_Description, selconnDescriptionWidth, theme::LargeText() ? GameFont24 : GameFont12), sizeof(selconn_Description));
}

void SelconnSelect(size_t value)
{
	provider = vecConnItems[value]->m_value;

	SelconnFree();
	selconn_EndMenu = SNetInitializeProvider(provider, selconn_GameData);
	SelconnLoad();
}

} // namespace

bool UiSelectProvider(GameData *gameData)
{
	selconn_GameData = gameData;
	SelconnLoad();

	selconn_ReturnValue = true;
	selconn_EndMenu = false;
	while (!selconn_EndMenu) {
		UiClearScreen();
		UiPollAndRender();
	}
	SelconnFree();

	return selconn_ReturnValue;
}

} // namespace devilution
