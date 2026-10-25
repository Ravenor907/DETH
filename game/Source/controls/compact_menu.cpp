#include "controls/compact_menu.hpp"

#include <memory>

#include "control/control.hpp"
#include "controls/control_mode.hpp"
#include "diablo.h"
#include "engine/backbuffer_state.hpp"
#include "engine/dx.h"
#include "gamemenu.h"
#include "gmenu.h"
#include "options.h"
#include "player.h"
#include "utils/display.h"
#include "utils/png.h"

namespace devilution {

namespace {

// The touch strip's art (ui_art\menu.png, 535x162): six 64x62 buttons, 4 apart, 30 from the top.
constexpr int ArtWidth = 535;
constexpr int ArtHeight = 162;
constexpr int ButtonTop = 30;
constexpr int ButtonWidth = 64;
constexpr int ButtonHeight = 62;
constexpr int ButtonPitch = 68;

SDL_Texture *Art[2] = {};
/** The renderer the textures belong to (a new one after a display change needs new textures). */
SDL_Renderer *ArtRenderer = nullptr;

/** Scale from art to game pixels. Fixed, so the strip follows UI Size like the rest of the interface. */
float Scale()
{
	return 0.55F;
}

/**
 * The strip is drawn over the finished frame, which already holds the software cursor. Where the
 * cursor touches the strip it is drawn again on top: its pixels are the ones that differ from what
 * the game saved behind it.
 */
void RenderCursorOnTop(SDL_Renderer *renderer, int stripLeft, int stripBottom)
{
	const DrawnCursor &cursor = GetDrawnCursor();
	const Rectangle &rect = cursor.rect;
	const int w = rect.size.width;
	const int h = rect.size.height;
	if (w <= 0 || h <= 0 || w * h > static_cast<int>(sizeof(cursor.behindBuffer)) || PalSurface == nullptr || Palette == nullptr)
		return;
	if (rect.position.x + w <= stripLeft || rect.position.y >= stripBottom)
		return;
	if (rect.position.x < 0 || rect.position.y < 0 || rect.position.x + w > PalSurface->w || rect.position.y + h > PalSurface->h)
		return;
#ifdef USE_SDL3
	SDL_Surface *overlay = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
#else
	SDL_Surface *overlay = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
#endif
	if (overlay == nullptr)
		return;
	const auto *frame = static_cast<const uint8_t *>(PalSurface->pixels);
	const SDL_Color *colors = Palette->colors;
	for (int y = 0; y < h; y++) {
		const uint8_t *now = frame + (rect.position.y + y) * PalSurface->pitch + rect.position.x;
		const uint8_t *behind = cursor.behindBuffer + y * w;
		auto *out = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(overlay->pixels) + y * overlay->pitch);
		for (int x = 0; x < w; x++) {
			if (now[x] == behind[x]) {
				out[x] = 0;
				continue;
			}
			const SDL_Color &c = colors[now[x]];
			out[x] = 0xFF000000U | (static_cast<uint32_t>(c.r) << 16) | (static_cast<uint32_t>(c.g) << 8) | c.b;
		}
	}
	SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, overlay);
	if (texture != nullptr) {
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
#ifdef USE_SDL3
		SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
		const SDL_FRect dst { static_cast<float>(rect.position.x), static_cast<float>(rect.position.y), static_cast<float>(w), static_cast<float>(h) };
		SDL_RenderTexture(renderer, texture, nullptr, &dst);
#else
		SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
		const SDL_Rect dst { rect.position.x, rect.position.y, w, h };
		SDL_RenderCopy(renderer, texture, nullptr, &dst);
#endif
		SDL_DestroyTexture(texture);
	}
#ifdef USE_SDL3
	SDL_DestroySurface(overlay);
#else
	SDL_FreeSurface(overlay);
#endif
}

} // namespace

bool CompactMenuStripVisible()
{
#ifdef __ANDROID__
	return false;
#else
	return gbRunGame && *GetOptions().Gameplay.compactPanel && *GetOptions().Gameplay.compactMenuButtons
	    && ControlMode != ControlTypes::VirtualGamepad && !InGameSettingsOpen
	    && !IsRightPanelOpen() // the inventory and spellbook use that corner
	    && !gmenu_is_active(); // the game menu has its own controls there
#endif
}

int CompactMenuStripBottom()
{
	if (!CompactMenuStripVisible())
		return 0;
	return static_cast<int>((ButtonTop + ButtonHeight + 6) * Scale());
}

int CompactMenuButtonAt(Point position)
{
	if (!CompactMenuStripVisible())
		return -1;
	const float s = Scale();
	const int top = static_cast<int>(ButtonTop * s);
	const int height = static_cast<int>(ButtonHeight * s);
	if (position.y < top || position.y >= top + height)
		return -1;
	for (int k = 0; k < 6; k++) {
		const int x = gnScreenWidth - static_cast<int>((6 - k) * ButtonPitch * s);
		if (position.x >= x && position.x < x + static_cast<int>(ButtonWidth * s))
			return k;
	}
	return -1;
}

void RenderCompactMenuStrip(SDL_Renderer *renderer)
{
	if (!CompactMenuStripVisible())
		return;
	if (ArtRenderer != renderer) {
		ArtRenderer = renderer;
		const char *files[2] = { "ui_art\\menu.png", "ui_art\\menu-levelup.png" };
		for (int i = 0; i < 2; i++) {
			SDL_Surface *surface = LoadPNG(files[i]);
			if (surface == nullptr)
				continue;
			Art[i] = SDL_CreateTextureFromSurface(renderer, surface);
#ifdef USE_SDL3
			SDL_DestroySurface(surface);
#else
			SDL_FreeSurface(surface);
#endif
		}
	}
	SDL_Texture *art = Art[MyPlayer != nullptr && MyPlayer->_pStatPts > 0 && Art[1] != nullptr ? 1 : 0];
	if (art == nullptr)
		return;
	const float s = Scale();
	const int w = static_cast<int>(ArtWidth * s);
	const int h = static_cast<int>(ArtHeight * s);
#ifdef USE_SDL3
	const SDL_FRect dst { static_cast<float>(gnScreenWidth - w), 0, static_cast<float>(w), static_cast<float>(h) };
	SDL_RenderTexture(renderer, art, nullptr, &dst);
#else
	const SDL_Rect dst { gnScreenWidth - w, 0, w, h };
	SDL_RenderCopy(renderer, art, nullptr, &dst);
#endif
	// DETH: the mouse cursor stays on top of the strip.
	RenderCursorOnTop(renderer, gnScreenWidth - w, h);
}

} // namespace devilution
