/**
 * @file layout_editor.cpp
 *
 * DETH: the touch layout editor. The player drags the on-screen controls where they like them,
 * sets each one's size and the opacity of all of them. Positions are kept in permille of the
 * screen (so they survive a UI size change) and sizes in percent of the default size, in the
 * "Touch Layout" setting: "control:x,y,size;..." for the controls that were changed.
 */
#include "controls/touch/layout_editor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <string>

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/deth_theme.hpp"
#include "controls/control_mode.hpp"
#include "controls/input.h"
#include "controls/touch/gamepad.h"
#include "engine/backbuffer_state.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/scrollrt.h"
#include "engine/render/text_render.hpp"
#include "game_mode.hpp"
#include "options.h"
#include "utils/display.h"
#include "utils/language.h"
#include "utils/sdl_compat.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

enum Control : uint8_t {
	Stick,
	Stand,
	Attack,
	Interact,
	Back,
	Spell,
	HealthPotion,
	ManaPotion,
	MenuBar,
	ControlCount,
};

struct ControlSetting {
	bool set = false;
	int x = 0; // permille of the screen width (centre)
	int y = 0; // permille of the screen height (centre)
	int size = 100; // percent of the default size
};

using LayoutSettings = std::array<ControlSetting, ControlCount>;

constexpr int MinSize = 50;
constexpr int MaxSize = 200;
constexpr int SizeStep = 10;

bool Active = false;
bool Requested = false;
int Selected = -1;
bool Dragging = false;
Displacement DragOffset {};
int SliderDrag = 0; // 1 size, 2 opacity

LayoutSettings Parse()
{
	LayoutSettings settings {};
	const std::string &text = GetOptions().Controller.touchLayout;
	size_t start = 0;
	while (start < text.size()) {
		size_t end = text.find(';', start);
		if (end == std::string::npos)
			end = text.size();
		const std::string entry = text.substr(start, end - start);
		int id = 0;
		int x = 0;
		int y = 0;
		int size = 100;
		if (std::sscanf(entry.c_str(), "%d:%d,%d,%d", &id, &x, &y, &size) == 4 && id >= 0 && id < ControlCount) {
			settings[id] = { true, std::clamp(x, 0, 1000), std::clamp(y, 0, 1000), std::clamp(size, MinSize, MaxSize) };
		}
		start = end + 1;
	}
	return settings;
}

void Store(const LayoutSettings &settings)
{
	std::string text;
	for (int id = 0; id < ControlCount; id++) {
		const ControlSetting &s = settings[id];
		if (!s.set)
			continue;
		if (!text.empty())
			text += ';';
		text += StrCat(id, ":", s.x, ",", s.y, ",", s.size);
	}
	GetOptions().Controller.touchLayout = text;
}

Circle *CircleFor(int control)
{
	VirtualGamepad &pad = VirtualGamepadState;
	switch (control) {
	case Stick: return &pad.directionPad.area;
	case Stand: return &pad.standButton.area;
	case Attack: return &pad.primaryActionButton.area;
	case Interact: return &pad.secondaryActionButton.area;
	case Back: return &pad.cancelButton.area;
	case Spell: return &pad.spellActionButton.area;
	case HealthPotion: return &pad.healthButton.area;
	case ManaPotion: return &pad.manaButton.area;
	default: return nullptr;
	}
}

std::array<Rectangle *, 7> MenuBarRects()
{
	VirtualMenuPanel &panel = VirtualGamepadState.menuPanel;
	return { &panel.area, &panel.charButton.area, &panel.questsButton.area, &panel.inventoryButton.area,
		&panel.spellbookButton.area, &panel.menuButton.area, &panel.mapButton.area };
}

Point CentreOf(int control)
{
	if (const Circle *circle = CircleFor(control))
		return circle->position;
	const Rectangle &area = VirtualGamepadState.menuPanel.area;
	return area.position + Displacement { area.size.width / 2, area.size.height / 2 };
}

/** Half the size of a control, to keep it on screen. */
Size HalfExtent(int control)
{
	if (const Circle *circle = CircleFor(control))
		return { circle->radius, circle->radius };
	const Rectangle &area = VirtualGamepadState.menuPanel.area;
	return { area.size.width / 2, area.size.height / 2 };
}

void ApplySetting(int control, const ControlSetting &s)
{
	if (!s.set)
		return;
	Point target { s.x * gnScreenWidth / 1000, s.y * gnScreenHeight / 1000 };
	if (Circle *circle = CircleFor(control)) {
		circle->radius = std::max(8, circle->radius * s.size / 100);
		circle->position = {
			std::clamp(target.x, circle->radius, std::max(circle->radius, gnScreenWidth - circle->radius)),
			std::clamp(target.y, circle->radius, std::max(circle->radius, gnScreenHeight - circle->radius)),
		};
		if (control == Stick)
			VirtualGamepadState.directionPad.position = circle->position;
		return;
	}
	// The menu bar: scale every button with the bar, then move the bar.
	const std::array<Rectangle *, 7> rects = MenuBarRects();
	const Point origin = rects[0]->position;
	for (Rectangle *rect : rects) {
		rect->position = origin + Displacement { (rect->position.x - origin.x) * s.size / 100, (rect->position.y - origin.y) * s.size / 100 };
		rect->size = { rect->size.width * s.size / 100, rect->size.height * s.size / 100 };
	}
	const Size half { rects[0]->size.width / 2, rects[0]->size.height / 2 };
	target.x = std::clamp(target.x, half.width, std::max(half.width, gnScreenWidth - half.width));
	target.y = std::clamp(target.y, half.height, std::max(half.height, gnScreenHeight - half.height));
	const Displacement move = target - (rects[0]->position + Displacement { half.width, half.height });
	for (Rectangle *rect : rects)
		rect->position += move;
}

/** The editor's panel: title, hint, the two sliders and the two buttons. */
struct PanelLayout {
	Rectangle panel;
	Rectangle title;
	Rectangle hint;
	Rectangle sizeLabel;
	Rectangle sizeGauge;
	Rectangle opacityLabel;
	Rectangle opacityGauge;
	Rectangle reset;
	Rectangle done;
};

PanelLayout GetPanelLayout()
{
	PanelLayout l;
	const int width = std::min(gnScreenWidth - 40, 440);
	const int height = 214;
	const int x = (gnScreenWidth - width) / 2;
	const int y = std::max(8, (gnScreenHeight - height) / 2 - gnScreenHeight / 8);
	l.panel = { { x, y }, { width, height } };
	l.title = { { x, y + 8 }, { width, 30 } };
	l.hint = { { x + 12, y + 40 }, { width - 24, 18 } };
	const int labelWidth = 150;
	l.sizeLabel = { { x + 16, y + 68 }, { labelWidth, 32 } };
	l.sizeGauge = { { x + 16 + labelWidth, y + 68 }, { width - 32 - labelWidth, 32 } };
	l.opacityLabel = { { x + 16, y + 110 }, { labelWidth, 32 } };
	l.opacityGauge = { { x + 16 + labelWidth, y + 110 }, { width - 32 - labelWidth, 32 } };
	const int buttonWidth = (width - 48) / 2;
	l.reset = { { x + 16, y + 158 }, { buttonWidth, 42 } };
	l.done = { { x + 32 + buttonWidth, y + 158 }, { buttonWidth, 42 } };
	return l;
}

int GaugeStep(const Rectangle &gauge, int x, int count)
{
	const int rel = std::clamp(x - gauge.position.x, 0, std::max(1, gauge.size.width - 1));
	return std::clamp((rel * (count - 1) + gauge.size.width / 2) / std::max(1, gauge.size.width - 1), 0, count - 1);
}

void SetSizeFromX(int x)
{
	const PanelLayout l = GetPanelLayout();
	if (Selected < 0) {
		// Nothing picked: all the buttons together (the stick keeps its size).
		auto &buttons = GetOptions().Controller.touchButtonsSize;
		buttons.SetActiveListIndex(static_cast<size_t>(GaugeStep(l.sizeGauge, x, static_cast<int>(buttons.GetListSize()))));
		InitializeVirtualGamepad();
		return;
	}
	const int steps = (MaxSize - MinSize) / SizeStep + 1;
	LayoutSettings settings = Parse();
	ControlSetting &s = settings[Selected];
	if (!s.set) {
		const Point centre = CentreOf(Selected);
		s = { true, centre.x * 1000 / std::max<int>(1, gnScreenWidth), centre.y * 1000 / std::max<int>(1, gnScreenHeight), 100 };
	}
	s.size = MinSize + GaugeStep(l.sizeGauge, x, steps) * SizeStep;
	Store(settings);
	InitializeVirtualGamepad();
}

void SetOpacityFromX(int x)
{
	auto &opacity = GetOptions().Controller.touchControlsOpacity;
	const PanelLayout l = GetPanelLayout();
	opacity.SetActiveListIndex(static_cast<size_t>(GaugeStep(l.opacityGauge, x, static_cast<int>(opacity.GetListSize()))));
}

void MoveSelectedTo(Point point)
{
	LayoutSettings settings = Parse();
	ControlSetting &s = settings[Selected];
	const int size = s.set ? s.size : 100;
	const Point centre = point - DragOffset;
	s = { true, std::clamp(centre.x * 1000 / std::max<int>(1, gnScreenWidth), 0, 1000), std::clamp(centre.y * 1000 / std::max<int>(1, gnScreenHeight), 0, 1000), size };
	Store(settings);
	InitializeVirtualGamepad();
}

int ControlAt(Point point)
{
	// Smaller controls first, so a button on top of the stick area still gets picked.
	for (int control = ControlCount - 1; control >= 0; control--) {
		if (control == MenuBar) {
			if (VirtualGamepadState.menuPanel.area.contains(point))
				return control;
			continue;
		}
		const Circle *circle = CircleFor(control);
		const int dx = point.x - circle->position.x;
		const int dy = point.y - circle->position.y;
		const int reach = circle->radius * 6 / 5;
		if (dx * dx + dy * dy <= reach * reach)
			return control;
	}
	return -1;
}

/** Returns true when the editor should close. */
bool PointerDown(Point point)
{
	const PanelLayout l = GetPanelLayout();
	if (l.panel.contains(point)) {
		if (l.sizeGauge.contains(point)) {
			SliderDrag = 1;
			SetSizeFromX(point.x);
		} else if (l.opacityGauge.contains(point)) {
			SliderDrag = 2;
			SetOpacityFromX(point.x);
		} else if (l.reset.contains(point)) {
			GetOptions().Controller.touchLayout.clear();
			Selected = -1;
			InitializeVirtualGamepad();
		} else if (l.done.contains(point)) {
			return true;
		}
		return false;
	}
	Selected = ControlAt(point);
	if (Selected >= 0) {
		Dragging = true;
		DragOffset = point - CentreOf(Selected);
	}
	return false;
}

void PointerMove(Point point)
{
	if (SliderDrag == 1)
		SetSizeFromX(point.x);
	else if (SliderDrag == 2)
		SetOpacityFromX(point.x);
	else if (Dragging && Selected >= 0)
		MoveSelectedTo(point);
}

void PointerUp()
{
	Dragging = false;
	SliderDrag = 0;
}

std::string_view ControlName(int control)
{
	switch (control) {
	case Stick: return _("Stick");
	case Stand: return _("Stand Still");
	case Attack: return _("Attack");
	case Interact: return _("Interact");
	case Back: return _("Back");
	case Spell: return _("Spell");
	case HealthPotion: return _("Health Potion");
	case ManaPotion: return _("Mana Potion");
	case MenuBar: return _("Menu Bar");
	default: return {};
	}
}

void DrawRing(const Surface &out, Point centre, int radius, uint8_t color)
{
	for (int r = radius; r < radius + 3; r++) {
		const int steps = std::max(32, r * 7);
		for (int i = 0; i < steps; i++) {
			const float angle = 6.2831853F * static_cast<float>(i) / static_cast<float>(steps);
			const Point p { centre.x + static_cast<int>(std::lround(r * std::cos(angle))), centre.y + static_cast<int>(std::lround(r * std::sin(angle))) };
			if (p.x >= 0 && p.y >= 0 && p.x < out.w() && p.y < out.h())
				out.SetPixel(p, color);
		}
	}
}

} // namespace

bool TouchLayoutEditorAvailable()
{
#ifdef __ANDROID__
	return true;
#else
	// Desktops and handhelds play with a mouse or a controller; this is only for testing there.
	static const bool forced = std::getenv("DETH_TOUCH_LAYOUT_EDITOR") != nullptr;
	return forced;
#endif
}

void ApplyTouchLayout()
{
	const LayoutSettings settings = Parse();
	for (int control = 0; control < ControlCount; control++)
		ApplySetting(control, settings[control]);
}

bool TouchLayoutEditorActive()
{
	return Active;
}

void RequestTouchLayoutEditor()
{
	Requested = true;
}

void RunRequestedTouchLayoutEditor()
{
	if (!Requested)
		return;
	Requested = false;
	RunTouchLayoutEditor();
}

namespace {
ControlTypes PreviousMode = ControlTypes::None;

void CloseTouchLayoutEditor()
{
	Active = false;
	Selected = -1;
	Dragging = false;
	SliderDrag = 0;
	theme::Invalidate();
	SaveOptions();
	ControlMode = PreviousMode;
	RedrawEverything();
}
} // namespace

void RunTouchLayoutEditor()
{
	// DETH: an overlay over the running game, so it works in multiplayer too (the game keeps going).
	if (!gbRunGame || Active)
		return;
	Active = true;
	Selected = -1;
	Dragging = false;
	SliderDrag = 0;
	PreviousMode = ControlMode;
	ControlMode = ControlTypes::VirtualGamepad;
	ActivateVirtualGamepad();
	InitializeVirtualGamepad();
	theme::Invalidate(); // the theme colours are matched to the palette in use: the game's here
	RedrawEverything();
}

bool HandleTouchLayoutEditorEvent(SDL_Event &event)
{
	if (!Active)
		return false;
	bool done = false;
	switch (event.type) {
	case SDL_QUIT:
		CloseTouchLayoutEditor();
		return false; // leave it for the game
	case SDL_KEYDOWN:
		if (SDLC_EventKey(event) == SDLK_ESCAPE)
			done = true;
		break;
#ifndef USE_SDL1
	case SDL_FINGERDOWN:
		done = PointerDown({ static_cast<int>(std::lround(event.tfinger.x * gnScreenWidth)), static_cast<int>(std::lround(event.tfinger.y * gnScreenHeight)) });
		break;
	case SDL_FINGERMOTION:
		PointerMove({ static_cast<int>(std::lround(event.tfinger.x * gnScreenWidth)), static_cast<int>(std::lround(event.tfinger.y * gnScreenHeight)) });
		break;
	case SDL_FINGERUP:
		PointerUp();
		break;
#endif
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
	case SDL_MOUSEMOTION: {
#ifndef USE_SDL1
		// Touches also arrive as mouse events; the finger events above handle those.
		if ((event.type == SDL_MOUSEMOTION ? event.motion.which : event.button.which) == SDL_TOUCH_MOUSEID)
			break;
#endif
		if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
			done = PointerDown({ event.button.x, event.button.y });
		else if (event.type == SDL_MOUSEBUTTONUP)
			PointerUp();
		else if (event.type == SDL_MOUSEMOTION && (event.motion.state & SDL_BUTTON_LMASK) != 0)
			PointerMove({ event.motion.x, event.motion.y });
		break;
	}
	default:
		break;
	}
	ControlMode = ControlTypes::VirtualGamepad;
	if (done)
		CloseTouchLayoutEditor();
	// While the editor is open every input is its own: the hero stands still.
	return true;
}

void DrawTouchLayoutEditor(const Surface &out)
{
	if (!Active)
		return;
	const theme::Colors &c = theme::GetColors();

	if (Selected >= 0) {
		if (Selected == MenuBar) {
			const Rectangle &area = VirtualGamepadState.menuPanel.area;
			for (int i = 0; i < 3; i++) {
				DrawHorizontalLine(out, area.position + Displacement { 0, i }, area.size.width, c.goldBright);
				DrawHorizontalLine(out, area.position + Displacement { 0, area.size.height - 1 - i }, area.size.width, c.goldBright);
				DrawVerticalLine(out, area.position + Displacement { i, 0 }, area.size.height, c.goldBright);
				DrawVerticalLine(out, area.position + Displacement { area.size.width - 1 - i, 0 }, area.size.height, c.goldBright);
			}
		} else {
			const Circle &circle = *CircleFor(Selected);
			DrawRing(out, circle.position, circle.radius + 4, c.goldBright);
		}
	}

	const PanelLayout l = GetPanelLayout();
	theme::DrawPanel(out, l.panel, true);
	DrawString(out, _("Touch Controls"), l.title, { .flags = UiFlags::FontSize24 | UiFlags::ColorWhitegold | UiFlags::AlignCenter | UiFlags::VerticalCenter });
	DrawString(out, _("Drag a control to move it. Tap one to size it alone."), l.hint, { .flags = UiFlags::FontSize12 | UiFlags::ColorWhite | UiFlags::AlignCenter | UiFlags::VerticalCenter });

	const LayoutSettings settings = Parse();
	if (Selected >= 0) {
		const int size = settings[Selected].set ? settings[Selected].size : 100;
		DrawString(out, StrCat(ControlName(Selected), " ", size, "%"), l.sizeLabel, { .flags = UiFlags::FontSize12 | UiFlags::ColorGold | UiFlags::VerticalCenter });
		theme::DrawGauge(out, l.sizeGauge, (size - MinSize) / SizeStep, (MaxSize - MinSize) / SizeStep + 1);
	} else {
		auto &buttons = GetOptions().Controller.touchButtonsSize;
		DrawString(out, StrCat(_("All buttons"), " ", *buttons, "%"), l.sizeLabel, { .flags = UiFlags::FontSize12 | UiFlags::ColorGold | UiFlags::VerticalCenter });
		theme::DrawGauge(out, l.sizeGauge, static_cast<int>(buttons.GetActiveListIndex()), static_cast<int>(buttons.GetListSize()));
	}
	auto &opacity = GetOptions().Controller.touchControlsOpacity;
	DrawString(out, StrCat(_("Opacity"), " ", *opacity, "%"), l.opacityLabel, { .flags = UiFlags::FontSize12 | UiFlags::ColorGold | UiFlags::VerticalCenter });
	theme::DrawGauge(out, l.opacityGauge, static_cast<int>(opacity.GetActiveListIndex()), static_cast<int>(opacity.GetListSize()));

	theme::DrawButton(out, l.reset, false);
	DrawString(out, _("Reset"), l.reset, { .flags = UiFlags::FontSize24 | UiFlags::ColorGold | UiFlags::AlignCenter | UiFlags::VerticalCenter });
	theme::DrawButton(out, l.done, false);
	DrawString(out, _("Done"), l.done, { .flags = UiFlags::FontSize24 | UiFlags::ColorWhitegold | UiFlags::AlignCenter | UiFlags::VerticalCenter });
}

} // namespace devilution
