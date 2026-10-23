#include "notice.h"

#include <cstdint>
#include <string>

#ifdef USE_SDL3
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "engine/render/automap_render.hpp"
#include "utils/sdl_compat.h"
#include "engine/render/text_render.hpp"
#include "utils/display.h"

namespace devilution {

namespace {

constexpr uint32_t NoticeFadeInMs = 250;
constexpr uint32_t NoticeFadeOutMs = 600;
constexpr uint32_t NoticeTotalMs = 2000;
/** Under the clock mod's line (drawn at y 6 with the 12 px font). */
constexpr int NoticeTop = 22;
constexpr int NoticeRightMargin = 10;

std::string NoticeText;
uint32_t NoticeStart;

/** Opacity in sixteenths at `elapsed` ms: a quick fade in, a hold, a slower fade out. */
int NoticeOpacity(uint32_t elapsed)
{
	if (elapsed >= NoticeTotalMs)
		return 0;
	if (elapsed < NoticeFadeInMs)
		return static_cast<int>(elapsed * 16 / NoticeFadeInMs);
	if (elapsed > NoticeTotalMs - NoticeFadeOutMs)
		return static_cast<int>((NoticeTotalMs - elapsed) * 16 / NoticeFadeOutMs);
	return 16;
}

} // namespace

void ShowNotice(std::string_view text)
{
	NoticeText = text;
	NoticeStart = static_cast<uint32_t>(SDL_GetTicks());
}

void DrawNotice(const Surface &out)
{
	if (NoticeText.empty())
		return;
	const uint32_t elapsed = static_cast<uint32_t>(SDL_GetTicks()) - NoticeStart;
	const int opacity = NoticeOpacity(elapsed);
	if (opacity <= 0) {
		if (elapsed >= NoticeTotalMs)
			NoticeText.clear();
		return;
	}

	const int width = GetLineWidth(NoticeText, GameFont12, 1) + 2;
	const int height = GetLineHeight(NoticeText, GameFont12) + 2;
	const Point position { gnScreenWidth - NoticeRightMargin - width, NoticeTop };
	if (position.x < 0 || width > out.w() || position.y + height > out.h())
		return;

	// Glyphs are palette indices, so the fade is a palette blend of the text over what is already
	// drawn: render the line off screen first, index 0 (black) marking the pixels to leave alone.
	OwnedSurface text(width, height);
	SDL_FillSurfaceRect(text.surface, nullptr, 0);
	DrawString(text, NoticeText, Point { 1, 1 }, { .flags = UiFlags::ColorWhite | UiFlags::FontSize12 });

	for (int y = 0; y < height; ++y) {
		const uint8_t *src = text.at(0, y);
		uint8_t *dst = out.at(position.x, position.y + y);
		for (int x = 0; x < width; ++x) {
			if (src[x] == 0)
				continue;
			dst[x] = opacity >= 16 ? src[x] : BlendPaletteColor(src[x], dst[x], opacity);
		}
	}
}

} // namespace devilution
