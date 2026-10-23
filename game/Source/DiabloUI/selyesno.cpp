#include <algorithm>
#include "selyesno.h"

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
#include "utils/language.h"
#include "utils/ui_fwd.h"
#include "utils/utf8.hpp"

namespace devilution {
namespace {

bool selyesno_endMenu;
bool selyesno_value;
char selyesno_confirmationMessage[1024];

std::vector<std::unique_ptr<UiListItem>> vecSelYesNoDialogItems;
std::vector<std::unique_ptr<UiItemBase>> vecSelYesNoDialog;

#define MESSAGE_WIDTH 400

void SelyesnoFree()
{
	ArtBackground = std::nullopt;

	vecSelYesNoDialogItems.clear();

	vecSelYesNoDialog.clear();
}

void SelyesnoSelect(size_t value)
{
	selyesno_value = vecSelYesNoDialogItems[value]->m_value == 0;
	selyesno_endMenu = true;
}

void SelyesnoEsc()
{
	selyesno_value = false;
	selyesno_endMenu = true;
}

} // namespace

bool UiSelHeroYesNoDialog(const char *title, const char *body)
{
	UiLoadBlackBackground();
	UiThemeBackground = true;

	const theme::Layout layout = theme::GetLayout();
	const int width = std::min(layout.column.size.width, gnScreenWidth - 2 * layout.margin);
	const int messageWidth = width - 48;
	CopyUtf8(selyesno_confirmationMessage, WordWrapString(body, messageWidth, GameFont24), sizeof(selyesno_confirmationMessage));
	int lines = 1;
	for (const char *p = selyesno_confirmationMessage; *p != '\0'; ++p)
		if (*p == '\n') ++lines;
	const int lineHeight = 26;
	const int rowPitch = layout.rowHeight + layout.rowGap;
	const int height = 24 + 44 + lines * lineHeight + 20 + 2 * rowPitch + 20;
	const int x = (gnScreenWidth - width) / 2;
	const int y = std::max(layout.margin / 2, (gnScreenHeight - height) / 2);
	vecSelYesNoDialog.push_back(std::make_unique<UiPanel>(MakeSdlRect(x, y, width, height)));
	int cy = y + 24;
	vecSelYesNoDialog.push_back(std::make_unique<UiArtText>(title, MakeSdlRect(x, cy, width, 36), UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiGold));
	vecSelYesNoDialog.push_back(std::make_unique<UiOrnament>(MakeSdlRect(x + width / 4, cy + 38, width / 2, 6)));
	cy += 44;
	vecSelYesNoDialog.push_back(std::make_unique<UiArtText>(selyesno_confirmationMessage, MakeSdlRect(x + 24, cy, messageWidth, lines * lineHeight), UiFlags::FontSize24 | UiFlags::ColorUiSilver | UiFlags::AlignCenter, 1, lineHeight));
	cy += lines * lineHeight + 20;

	vecSelYesNoDialogItems.push_back(std::make_unique<UiListItem>(_("Yes"), 0));
	vecSelYesNoDialogItems.push_back(std::make_unique<UiListItem>(_("No"), 1));
	auto list = std::make_unique<UiList>(vecSelYesNoDialogItems, vecSelYesNoDialogItems.size(), static_cast<Sint16>(x + width / 4), static_cast<Sint16>(cy), static_cast<Uint16>(width / 2), static_cast<Uint16>(rowPitch), UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold, layout.rowGap);
	list->themed = true;
	vecSelYesNoDialog.push_back(std::move(list));

	UiInitList(nullptr, SelyesnoSelect, SelyesnoEsc, vecSelYesNoDialog, true);

	selyesno_value = true;
	selyesno_endMenu = false;
	while (!selyesno_endMenu) {
		UiClearScreen();
		UiRenderItems(vecSelYesNoDialog);
		UiPollAndRender();
	}

	SelyesnoFree();
	UiThemeBackground = false;

	return selyesno_value;
}
} // namespace devilution
