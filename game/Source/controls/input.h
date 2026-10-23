#pragma once

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include "controls/controller.h"
#include "controls/controller_motion.h"
#include "controls/touch_script.h"

namespace devilution {

inline bool PollEvent(SDL_Event *event)
{
	PumpScriptedTouch();
#ifdef USE_SDL3
	const bool result = SDL_PollEvent(event);
#else
	const bool result = SDL_PollEvent(event) != 0;
#endif
	if (result) {
#if !defined(USE_SDL1)
		// DETH: the Android back button (and back gesture) behaves like Escape everywhere:
		// closes panels, steps back through menus and settings pages.
		if ((event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) && event->key.keysym.sym == SDLK_AC_BACK) {
			event->key.keysym.sym = SDLK_ESCAPE;
			event->key.keysym.scancode = SDL_SCANCODE_ESCAPE;
		}
#endif
		UnlockControllerState(*event);
		ProcessControllerMotion(*event);
	}

	return result;
}

} // namespace devilution
