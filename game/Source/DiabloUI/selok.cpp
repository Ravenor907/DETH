#include <algorithm>
#include "DiabloUI/selok.h"

#include <cstddef>
#include <memory>
#include <optional>
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
#include "game_mode.hpp"
#include "utils/language.h"
#include "utils/ui_fwd.h"
#include "utils/utf8.hpp"

namespace devilution {

namespace {

char dialogText[1024];

} // namespace

bool selok_endMenu;

std::vector<std::unique_ptr<UiListItem>> vecSelOkDialogItems;
std::vector<std::unique_ptr<UiItemBase>> vecSelOkDialog;

#define MESSAGE_WIDTH 400

void selok_Free()
{
	ArtBackground = std::nullopt;

	vecSelOkDialogItems.clear();

	vecSelOkDialog.clear();
}

void selok_Select(size_t /*value*/)
{
	selok_endMenu = true;
}

void selok_Esc()
{
	selok_endMenu = true;
}

void UiSelOkDialog(const char *title, const char *body, bool background)
{
	if (!background) {
		UiLoadBlackBackground();
	} else {
		if (!gbIsSpawn) {
			LoadBackgroundArt("ui_art\\mainmenu");
		} else {
			LoadBackgroundArt("ui_art\\swmmenu");
		}
	}

	UiThemeBackground = true;

	// A centered dialog panel: title, message, OK.
	const theme::Layout layout = theme::GetLayout();
	const int width = std::min(layout.column.size.width, gnScreenWidth - 2 * layout.margin);
	const int messageWidth = width - 48;
	CopyUtf8(dialogText, WordWrapString(body, messageWidth, GameFont24), sizeof(dialogText));
	int lines = 1;
	for (const char *p = dialogText; *p != '\0'; ++p)
		if (*p == '\n') ++lines;
	const int lineHeight = 26;
	const int titleHeight = title != nullptr ? 44 : 0;
	const int height = 24 + titleHeight + lines * lineHeight + 20 + layout.rowHeight + 24;
	const int x = (gnScreenWidth - width) / 2;
	const int y = std::max(layout.margin / 2, (gnScreenHeight - height) / 2);
	vecSelOkDialog.push_back(std::make_unique<UiPanel>(MakeSdlRect(x, y, width, height)));
	int cy = y + 24;
	if (title != nullptr) {
		vecSelOkDialog.push_back(std::make_unique<UiArtText>(title, MakeSdlRect(x, cy, width, 36), UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiGold));
		vecSelOkDialog.push_back(std::make_unique<UiOrnament>(MakeSdlRect(x + width / 4, cy + 38, width / 2, 6)));
		cy += titleHeight;
	}
	vecSelOkDialog.push_back(std::make_unique<UiArtText>(dialogText, MakeSdlRect(x + 24, cy, messageWidth, lines * lineHeight), UiFlags::FontSize24 | UiFlags::ColorUiSilver | UiFlags::AlignCenter, 1, lineHeight));
	cy += lines * lineHeight + 20;

	vecSelOkDialogItems.push_back(std::make_unique<UiListItem>(_("OK"), 0));
	auto list = std::make_unique<UiList>(vecSelOkDialogItems, 1, static_cast<Sint16>(x + width / 4), static_cast<Sint16>(cy), static_cast<Uint16>(width / 2), static_cast<Uint16>(layout.rowHeight), UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold, 0);
	list->themed = true;
	vecSelOkDialog.push_back(std::move(list));

	UiInitList(nullptr, selok_Select, selok_Esc, vecSelOkDialog, false);

	selok_endMenu = false;
	while (!selok_endMenu) {
		UiClearScreen();
		UiRenderItems(vecSelOkDialog);
		UiPollAndRender();
	}

	selok_Free();
	UiThemeBackground = false;
}
} // namespace devilution
