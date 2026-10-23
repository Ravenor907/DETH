#include <memory>
#include <optional>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/button.h"
#include "DiabloUI/diabloui.h"
#include "DiabloUI/deth_screen.hpp"
#include "engine/render/primitive_render.hpp"
#include "DiabloUI/ui_item.h"
#include "controls/input.h"
#include "controls/menu_controls.h"
#include "engine/clx_sprite.hpp"
#include "engine/dx.h"
#include "engine/load_pcx.hpp"
#include "engine/point.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/surface.hpp"
#include "utils/display.h"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"
#include "utils/ui_fwd.h"

namespace devilution {
namespace {
/** DETH: the loading bar is drawn by the theme (panel with a gold bar), no popup art. */
constexpr int BarWidth = 260;
constexpr int BarHeight = 10;

Rectangle PanelRect()
{
	return { { GetCenterOffset(320), GetCenterOffset(120, gnScreenHeight) }, { 320, 120 } };
}

std::vector<std::unique_ptr<UiItemBase>> vecProgress;
bool endMenu;

void DialogActionCancel()
{
	endMenu = true;
}

void ProgressLoadBackground()
{
	UiLoadBlackBackground();
}

void ProgressLoadForeground()
{
	const Rectangle panel = PanelRect();
	const int buttonWidth = 140;
	const int buttonHeight = 34;
	const SDL_Rect rect3 = { static_cast<Sint16>(panel.position.x + (panel.size.width - buttonWidth) / 2), static_cast<Sint16>(panel.position.y + panel.size.height - buttonHeight - 16), static_cast<Uint16>(buttonWidth), static_cast<Uint16>(buttonHeight) };
	auto button = std::make_unique<UiArtTextButton>(_("Cancel"), &DialogActionCancel, rect3, UiFlags::FontSize24 | UiFlags::ColorUiGold);
	button->themed = true;
	vecProgress.push_back(std::move(button));
}

void ProgressFreeBackground()
{
	ArtBackground = std::nullopt;
}

void ProgressFreeForeground()
{
	vecProgress.clear();
}

void ProgressRenderBackground()
{
	theme::DrawBackground(Surface(DiabloUiSurface()));
}

void ProgressRenderForeground(int progress)
{
	const Surface &out = Surface(DiabloUiSurface());
	ProgressRenderBackground();
	const Rectangle panel = PanelRect();
	theme::DrawPanel(out, panel);
	const theme::Colors &c = theme::GetColors();
	const int barX = panel.position.x + (panel.size.width - BarWidth) / 2;
	const int barY = panel.position.y + 30;
	FillRect(out, barX, barY, BarWidth, BarHeight, c.stoneDark);
	DrawHorizontalLine(out, { barX, barY }, BarWidth, c.stone);
	DrawHorizontalLine(out, { barX, barY + BarHeight - 1 }, BarWidth, c.stone);
	if (progress > 0) {
		const int w = std::max(2, BarWidth * progress / 100);
		FillRect(out, barX, barY + 1, w, BarHeight - 2, c.gold);
		DrawHorizontalLine(out, { barX, barY + 1 }, w, c.goldBright);
	}
}

} // namespace

bool UiProgressDialog(int (*fnfunc)())
{
	// Blit the background once and then free it.
	ProgressLoadBackground();

	ProgressRenderBackground();

	if (RenderDirectlyToOutputSurface && PalSurface != nullptr) {
		// Render into all the backbuffers if there are multiple.
		const void *initialPixels = PalSurface->pixels;
		UiFadeIn();
		while (PalSurface->pixels != initialPixels) {
			ProgressRenderBackground();
			UiFadeIn();
		}
	}

	ProgressFreeBackground();

	ProgressLoadForeground();

	endMenu = false;
	int progress = 0;

	SDL_Event event;
	while (!endMenu && progress < 100) {
		progress = fnfunc();
		ProgressRenderForeground(progress);
		UiRenderItems(vecProgress);
		DrawMouse();
		UiFadeIn();

		while (PollEvent(&event)) {
			switch (event.type) {
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
				UiItemMouseEvents(&event, vecProgress);
				break;
			case SDL_EVENT_KEY_DOWN:
				switch (SDLC_EventKey(event)) {
#ifndef USE_SDL1
				case SDLK_KP_ENTER:
#endif
				case SDLK_ESCAPE:
				case SDLK_RETURN:
				case SDLK_SPACE:
					endMenu = true;
					break;
				default:
					break;
				}
				break;
			default:
				for (const MenuAction menuAction : GetMenuActions(event)) {
					if (IsNoneOf(menuAction, MenuAction_BACK, MenuAction_SELECT))
						continue;
					endMenu = true;
					break;
				}
				break;
			}
			UiHandleEvents(&event);
		}
	}
	ProgressFreeForeground();

	return progress == 100;
}

} // namespace devilution
