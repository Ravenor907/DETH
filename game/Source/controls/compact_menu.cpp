#include "controls/compact_menu.hpp"

#include <memory>

#include "controls/control_mode.hpp"
#include "diablo.h"
#include "engine/dx.h"
#include "gamemenu.h"
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

} // namespace

bool CompactMenuStripVisible()
{
#ifdef __ANDROID__
	return false;
#else
	return gbRunGame && *GetOptions().Gameplay.compactPanel && *GetOptions().Gameplay.compactMenuButtons
	    && ControlMode != ControlTypes::VirtualGamepad && !InGameSettingsOpen;
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
}

} // namespace devilution
