/**
 * @file touch_script.cpp
 *
 * DETH test aid: `DETH_TOUCH_SCRIPT=<file>` replays finger events from a text file so touch flows can be
 * verified on a desktop test rig. Lines: `<ms> down|move|up <x> <y>` with x and y as fractions of the window
 * (0..1). Finger events and the mouse events a touch screen would produce are both pushed.
 * `<ms> pad <button> down|up` drives a virtual game controller (button: a, b, x, y, back, start,
 * leftshoulder, rightshoulder, dpup, dpdown, dpleft, dpright; or lefttrigger / righttrigger).
 */
#include "controls/touch_script.h"

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <SDL.h>

#include "utils/display.h"
#include "utils/ui_fwd.h"

namespace devilution {

namespace {

struct Step {
	uint32_t ms;
	int kind; // 0 down, 1 move, 2 up, 3 key, 4 text, 5 clipboard, 6 pad down, 7 pad up
	float x;
	float y;
	std::string text;
};

std::vector<Step> steps;
size_t next = 0;
uint32_t startTicks = 0;
bool loaded = false;
bool enabled = false;

void Load()
{
	loaded = true;
	const char *path = std::getenv("DETH_TOUCH_SCRIPT");
	if (path == nullptr)
		return;
	std::ifstream in(path);
	std::string line;
	while (std::getline(in, line)) {
		std::istringstream ls(line);
		Step step {};
		std::string action;
		if (!(ls >> step.ms >> action))
			continue;
		if (action == "pad") {
			std::string state;
			ls >> step.text >> state;
			step.kind = state == "down" ? 6 : 7;
		} else if (action == "key" || action == "text" || action == "clipboard") {
			step.kind = action == "key" ? 3 : (action == "text" ? 4 : 5);
			std::getline(ls >> std::ws, step.text);
		} else {
			step.kind = action == "down" ? 0 : (action == "move" ? 1 : 2);
			ls >> step.x >> step.y;
		}
		steps.push_back(step);
	}
	enabled = !steps.empty();
	startTicks = SDL_GetTicks();
}

#if !defined(USE_SDL1) && !defined(USE_SDL3)
SDL_Joystick *VirtualPad = nullptr;

void PushPad(const Step &step)
{
	if (VirtualPad == nullptr) {
		SDL_VirtualJoystickDesc desc {};
		desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
		desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
		desc.naxes = SDL_CONTROLLER_AXIS_MAX;
		desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
		desc.name = "DETH script pad";
		const int index = SDL_JoystickAttachVirtualEx(&desc);
		if (index < 0)
			return;
		VirtualPad = SDL_JoystickOpen(index);
		if (VirtualPad == nullptr)
			return;
	}
	const bool down = step.kind == 6;
	// "pad rightx down" pushes a stick axis most of the way (rightx/righty/leftx/lefty; "-" in front for the other way).
	{
		std::string axisName = step.text;
		const bool negative = !axisName.empty() && axisName[0] == '-';
		if (negative)
			axisName.erase(0, 1);
		int percent = 73; // "rightx@10": a tenth of the way
		if (const size_t at = axisName.find('@'); at != std::string::npos) {
			percent = std::atoi(axisName.c_str() + at + 1);
			axisName.erase(at);
		}
		const SDL_GameControllerAxis axis = SDL_GameControllerGetAxisFromString(axisName.c_str());
		if (axis != SDL_CONTROLLER_AXIS_INVALID && axis != SDL_CONTROLLER_AXIS_TRIGGERLEFT && axis != SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
			SDL_JoystickSetVirtualAxis(VirtualPad, axis, down ? static_cast<Sint16>((negative ? -32767 : 32767) * percent / 100) : 0);
			return;
		}
	}
	if (step.text == "lefttrigger" || step.text == "righttrigger") {
		SDL_JoystickSetVirtualAxis(VirtualPad, step.text == "lefttrigger" ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT, down ? 32767 : 0);
		return;
	}
	const SDL_GameControllerButton button = SDL_GameControllerGetButtonFromString(step.text.c_str());
	if (button != SDL_CONTROLLER_BUTTON_INVALID)
		SDL_JoystickSetVirtualButton(VirtualPad, button, down ? 1 : 0);
}
#endif

void Push(const Step &step)
{
#if !defined(USE_SDL1) && !defined(USE_SDL3)
	if (ghMainWnd == nullptr)
		return;
	if (step.kind >= 6) {
		PushPad(step);
		return;
	}
	// Pushed mouse events are not scaled by SDL, so they are given in the game's logical coordinates.
	if (step.kind == 3) {
		const SDL_Keycode key = SDL_GetKeyFromName(step.text.c_str());
		SDL_Event e {};
		e.type = SDL_KEYDOWN;
		e.key.state = SDL_PRESSED;
		e.key.keysym.sym = key;
		e.key.keysym.scancode = SDL_GetScancodeFromKey(key);
		SDL_PushEvent(&e);
		e.type = SDL_KEYUP;
		e.key.state = SDL_RELEASED;
		SDL_PushEvent(&e);
		return;
	}
	if (step.kind == 4) {
		// "@<file>" types the file's contents (e.g. a join code another process wrote).
		std::string text = step.text;
		if (!text.empty() && text[0] == '@') {
			std::ifstream in(text.substr(1));
			std::getline(in, text);
		}
		SDL_Event e {};
		e.type = SDL_TEXTINPUT;
		std::snprintf(e.text.text, sizeof(e.text.text), "%s", text.c_str());
		SDL_PushEvent(&e);
		return;
	}
	if (step.kind == 5) {
		SDL_SetClipboardText(step.text.c_str());
		return;
	}
	const int px = static_cast<int>(step.x * gnScreenWidth);
	const int py = static_cast<int>(step.y * gnScreenHeight);
	const uint32_t now = SDL_GetTicks();

	SDL_Event finger {};
	finger.type = step.kind == 0 ? SDL_FINGERDOWN : (step.kind == 1 ? SDL_FINGERMOTION : SDL_FINGERUP);
	finger.tfinger.timestamp = now;
	finger.tfinger.touchId = 1;
	finger.tfinger.fingerId = 1;
	finger.tfinger.x = step.x;
	finger.tfinger.y = step.y;
	finger.tfinger.pressure = 1.0F;
	SDL_PushEvent(&finger);

	SDL_Event motion {};
	motion.type = SDL_MOUSEMOTION;
	motion.motion.timestamp = now;
	motion.motion.which = SDL_TOUCH_MOUSEID;
	motion.motion.state = step.kind == 2 ? 0 : SDL_BUTTON_LMASK;
	motion.motion.x = px;
	motion.motion.y = py;
	SDL_PushEvent(&motion);

	if (step.kind != 1) {
		SDL_Event button {};
		button.type = step.kind == 0 ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
		button.button.timestamp = now;
		button.button.which = SDL_TOUCH_MOUSEID;
		button.button.button = SDL_BUTTON_LEFT;
		button.button.state = step.kind == 0 ? SDL_PRESSED : SDL_RELEASED;
		button.button.clicks = 1;
		button.button.x = px;
		button.button.y = py;
		SDL_PushEvent(&button);
	}
#endif
}

} // namespace

void PumpScriptedTouch()
{
	if (!loaded)
		Load();
	if (!enabled || next >= steps.size())
		return;
	const uint32_t elapsed = SDL_GetTicks() - startTicks;
	while (next < steps.size() && steps[next].ms <= elapsed) {
		Push(steps[next]);
		++next;
	}
}

} // namespace devilution
