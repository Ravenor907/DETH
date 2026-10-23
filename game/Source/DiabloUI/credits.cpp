#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/credits_lines.h"
#include "DiabloUI/diabloui.h"
#include "DiabloUI/deth_theme.hpp"
#include "DiabloUI/support_lines.h"
#include "DiabloUI/ui_flags.hpp"
#include "controls/input.h"
#include "controls/menu_controls.h"
#include "engine/load_clx.hpp"
#include "engine/point.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/render/text_render.hpp"
#include "engine/surface.hpp"
#include "hwcursor.hpp"
#include "utils/display.h"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"
#include "utils/sdl_geometry.h"
#include "utils/ui_fwd.h"

namespace devilution {

namespace {

// DETH: the text scrolls through a viewport that covers most of the screen (computed per screen size).
SDL_Rect VIEWPORT = { 0, 114, 640, 251 };
const int LINE_H = 22;

SDL_Rect ComputeViewport()
{
	const theme::Layout layout = theme::GetLayout();
	const int width = std::min<int>(640, gnScreenWidth - 2 * layout.margin);
	const int top = layout.margin;
	const int bottom = gnScreenHeight - layout.margin;
	return SDL_Rect { static_cast<Sint16>((gnScreenWidth - width) / 2), static_cast<Sint16>(top), static_cast<Uint16>(width), static_cast<Uint16>(bottom - top) };
}

// The maximum number of visible lines is the number of whole lines
// (VIEWPORT.h / LINE_H) rounded up, plus one extra line for when
// a line is leaving the screen while another one is entering.
#define MAX_VISIBLE_LINES ((VIEWPORT.h - 1) / LINE_H + 2)

class CreditsRenderer {

public:
	CreditsRenderer(const char *const *text, std::size_t textLines)
	{
		for (size_t i = 0; i < textLines; i++) {
			const std::string_view orgText = _(text[i]);

			uint16_t offset = 0;
			size_t indexFirstNotTab = 0;
			while (indexFirstNotTab < orgText.size() && orgText[indexFirstNotTab] == '\t') {
				offset += 40;
				indexFirstNotTab++;
			}

			const std::string paragraphs = WordWrapString(orgText.substr(indexFirstNotTab), VIEWPORT.w - 32 - offset, FontSizeDialog);

			size_t previous = 0;
			while (true) {
				const size_t next = paragraphs.find('\n', previous);
				linesToRender.emplace_back(LineContent { offset, paragraphs.substr(previous, next - previous) });
				if (next == std::string::npos)
					break;
				previous = next + 1;
			}
		}

		ticks_begin_ = SDL_GetTicks();
		prev_offset_y_ = 0;
		finished_ = false;
	}

	~CreditsRenderer()
	{
		ArtBackgroundWidescreen = std::nullopt;
		ArtBackground = std::nullopt;
	}

	void Render();

	[[nodiscard]] bool Finished() const
	{
		return finished_;
	}

private:
	struct LineContent {
		uint16_t offset;
		std::string text;
	};

	std::vector<LineContent> linesToRender;
	bool finished_;
	Uint32 ticks_begin_;
	int prev_offset_y_;
};

void CreditsRenderer::Render()
{
	const int offsetY = -VIEWPORT.h + ((SDL_GetTicks() - ticks_begin_) / 40);
	if (offsetY == prev_offset_y_)
		return;
	prev_offset_y_ = offsetY;

	theme::DrawBackground(Surface(DiabloUiSurface()));
	const Point uiPosition { 0, 0 };
	// Thin frame lines above and below the scrolling text.
	theme::DrawDivider(Surface(DiabloUiSurface()), { VIEWPORT.x, VIEWPORT.y - 4 }, VIEWPORT.w);
	theme::DrawDivider(Surface(DiabloUiSurface()), { VIEWPORT.x, VIEWPORT.y + VIEWPORT.h + 3 }, VIEWPORT.w);

	const std::size_t linesBegin = std::max(offsetY / LINE_H, 0);
	const std::size_t linesEnd = std::min(linesBegin + MAX_VISIBLE_LINES, linesToRender.size());

	if (linesBegin >= linesEnd) {
		if (linesEnd == linesToRender.size())
			finished_ = true;
		return;
	}

	SDL_Rect viewport = VIEWPORT;
	viewport.x += uiPosition.x;
	viewport.y += uiPosition.y;
	ScaleOutputRect(&viewport);

	// We use unscaled coordinates for calculation throughout.
	auto destY = static_cast<Sint16>(uiPosition.y + VIEWPORT.y - (offsetY - linesBegin * LINE_H));
	for (std::size_t i = linesBegin; i < linesEnd; ++i, destY += LINE_H) {
		const Sint16 destX = uiPosition.x + VIEWPORT.x + 16;

		auto &lineContent = linesToRender[i];

		SDL_Rect dstRect = MakeSdlRect(destX + lineContent.offset, destY, 0, 0);
		ScaleOutputRect(&dstRect);
		dstRect.x -= viewport.x;
		dstRect.y -= viewport.y;

		const Surface &out = Surface(DiabloUiSurface(), viewport);
		DrawString(out, lineContent.text, Point { dstRect.x, dstRect.y },
		    { .flags = UiFlags::FontSizeDialog | UiFlags::ColorDialogWhite, .spacing = -1 });
	}
}

bool TextDialog(const char *const *text, std::size_t textLines)
{
	VIEWPORT = ComputeViewport();
	CreditsRenderer creditsRenderer(text, textLines);
	bool endMenu = false;

	if (IsHardwareCursor())
		SetHardwareCursorVisible(false);

	SDL_Event event;
	do {
		creditsRenderer.Render();
		UiFadeIn();
		while (PollEvent(&event)) {
			switch (event.type) {
			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
				endMenu = true;
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
	} while (!endMenu && !creditsRenderer.Finished());

	return true;
}

} // namespace

bool UiCreditsDialog()
{
	LoadBackgroundArt("ui_art\\credits");

	return TextDialog(CreditLines, CreditLinesSize);
}

bool UiSupportDialog()
{
	LoadBackgroundArt("ui_art\\credits");

	return TextDialog(SupportLines, SupportLinesSize);
}

} // namespace devilution
