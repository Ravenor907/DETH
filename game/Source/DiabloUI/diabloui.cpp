#include "DiabloUI/diabloui.h"
#include "DiabloUI/deth_theme.hpp"

#if defined(__ANDROID__) && !defined(TERMUX)
#include "platform/android/android.hpp"
#endif

#ifndef USE_SDL1
#include "controls/touch/event_handlers.h"
#include "controls/touch/gamepad.h"
#include "controls/touch/touch_mode.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include <function_ref.hpp>

#include "DiabloUI/button.h"
#include "DiabloUI/scrollbar.h"
#include "DiabloUI/text_input.hpp"
#include "DiabloUI/ui_flags.hpp"
#include "DiabloUI/ui_item.h"
#include "appfat.h"
#include "controls/control_mode.hpp"
#include "controls/controller.h"
#include "controls/input.h"
#include "controls/menu_controls.h"
#include "diablo.h"
#include "discord/discord.h"
#include "effects.h"
#include "engine/clx_sprite.hpp"
#include "engine/dx.h"
#include "engine/load_pcx.hpp"
#include "engine/palette.h"
#include "engine/render/clx_render.hpp"
#include "engine/render/text_render.hpp"
#include "engine/sound.h"
#include "engine/surface.hpp"
#include "engine/ticks.hpp"
#include "headless_mode.hpp"
#include "hwcursor.hpp"
#include "init.hpp"
#include "options.h"
#include "player.h"
#include "sound_effect_enums.h"
#include "tables/playerdat.hpp"
#include "utils/algorithm/container.hpp"
#include "utils/display.h"
#include "utils/enum_traits.h"
#include "utils/is_of.hpp"
#include "utils/screen_reader.hpp"
#include "utils/sdl_compat.h"
#include "utils/sdl_geometry.h"
#include "utils/str_cat.hpp"
#include "utils/ui_fwd.h"
#include "utils/utf8.hpp"

#ifdef __SWITCH__
// for virtual keyboard on Switch
#include "platform/switch/keyboard.h"
#endif
#ifdef __vita__
// for virtual keyboard on Vita
#include "platform/vita/keyboard.h"
#endif
#ifdef __3DS__
// for virtual keyboard on 3DS
#include "platform/ctr/keyboard.h"
#endif

namespace devilution {

OptionalOwnedClxSpriteList ArtLogo;
OptionalOwnedClxSpriteList DifficultyIndicator;

std::array<OptionalOwnedClxSpriteList, 3> ArtFocus;

OptionalOwnedClxSpriteList ArtBackgroundWidescreen;
OptionalOwnedClxSpriteList ArtBackground;
OptionalOwnedClxSpriteList ArtCursor;

std::size_t SelectedItem = 0;
std::size_t SelectedItemMax;
std::size_t ListViewportSize = 1;
std::size_t listOffset = 0;
int listPixelOffset = 0;

/** DETH: Left and Right on a list (the main menu's corner buttons). Cleared by every UiInitList. */
void (*gfnListLeft)();
void (*gfnListRight)();

namespace {

OptionalOwnedClxSpriteList ArtHero;
std::vector<uint8_t> ArtHeroPortraitOrder;
std::vector<OptionalOwnedClxSpriteList> ArtHeroOverrides;

void (*gfnListFocus)(size_t value);
void (*gfnListSelect)(size_t value);
void (*gfnListEsc)();
void (*gfnFullscreen)();
bool (*gfnListYesNo)();
std::vector<UiItemBase *> gUiItems;
UiList *gUiList = nullptr;
bool UiItemsWraps;

std::optional<TextInputState> UiTextInputState;
bool allowEmptyTextInput = false;

constexpr Uint32 ListDoubleClickTimeMs = 500;
std::size_t lastListClickIndex = static_cast<std::size_t>(-1);
Uint32 lastListClickTicks = 0;

struct ScrollBarState {
	bool upArrowPressed;
	bool downArrowPressed;

	ScrollBarState()
	{
		upArrowPressed = false;
		downArrowPressed = false;
	}
} scrollBarState;

void AdjustListOffset(std::size_t itemIndex)
{
	if (itemIndex >= listOffset + ListViewportSize) {
		listOffset = itemIndex - (ListViewportSize - 1);
		listPixelOffset = 0;
	}
	if (itemIndex < listOffset) {
		listOffset = itemIndex;
		listPixelOffset = 0;
	}
}

uint32_t fadeTc;
int fadeValue;

void StartUiFadeIn()
{
	fadeValue = 0;
	fadeTc = 0;
}

void UiUpdateFadePalette()
{
	if (fadeValue == 256) return;
	if (fadeValue == 0 && fadeTc == 0) {
		// Start the fade-in.
		fadeTc = SDL_GetTicks();
		fadeValue = 0;
		BlackPalette();
		// We can skip hardware cursor update for fade level 0 (everything is black).
		return;
	}

	const int prevFadeValue = fadeValue;
	fadeValue = static_cast<int>((SDL_GetTicks() - fadeTc) / 2.083); // 32 frames @ 60hz
	if (fadeValue == prevFadeValue) return;

	if (fadeValue >= 256) {
		// Finish the fade-in:
		fadeValue = 256;
		fadeTc = 0;
		ApplyGlobalBrightness(system_palette.data(), logical_palette.data());
		SystemPaletteUpdated();
		if (IsHardwareCursor()) ReinitializeHardwareCursor();
		return;
	}

	SDL_Color palette[256];
	ApplyGlobalBrightness(palette, logical_palette.data());
	ApplyFadeLevel(fadeValue, system_palette.data(), palette);

	SystemPaletteUpdated();
	if (IsHardwareCursor()) ReinitializeHardwareCursor();
}

#ifdef __3DS__
void HandleCtrTextInput(std::string_view textInput)
{
	UiTextInputState->assign(textInput);
}
#endif

} // namespace

#if defined(__ANDROID__) && !defined(TERMUX)
void OpenAndroidTextInput(const UiEdit &edit)
{
	AndroidShowTextInput(edit.m_hint, edit.m_value, edit.m_max_length, static_cast<int>(edit.inputKind));
}

/** DETH: applies the native dialog's answer; OK submits the screen like pressing Enter. */
void HandleAndroidTextInputResult()
{
	std::optional<AndroidTextInputResult> result = AndroidTakeTextInputResult();
	if (!result || !UiTextInputState.has_value())
		return;
	if (!result->ok)
		return; // cancelled: stay on the screen; tapping the field reopens the dialog
	UiTextInputState->assign(result->text);
	UiFocusNavigationSelect();
}
#endif

bool IsTextInputActive()
{
	return UiTextInputState.has_value();
}

void UiInitList(void (*fnFocus)(size_t value), void (*fnSelect)(size_t value), void (*fnEsc)(), const std::vector<std::unique_ptr<UiItemBase>> &items, bool itemsWraps, void (*fnFullscreen)(), bool (*fnYesNo)(), size_t selectedItem /*= 0*/)
{
	SelectedItem = selectedItem;
	SelectedItemMax = 0;
	ListViewportSize = 0;
	gfnListFocus = fnFocus;
	gfnListSelect = fnSelect;
	gfnListEsc = fnEsc;
	gfnListLeft = nullptr;
	gfnListRight = nullptr;
	gfnFullscreen = fnFullscreen;
	gfnListYesNo = fnYesNo;
	gUiItems.clear();
	for (const auto &item : items)
		gUiItems.push_back(item.get());
	UiItemsWraps = itemsWraps;
	listOffset = 0;
	listPixelOffset = 0;
	if (fnFocus != nullptr)
		fnFocus(selectedItem);

#ifndef __SWITCH__
	SDLC_StopTextInput(ghMainWnd); // input is enabled by default
#endif
	UiScrollbar *uiScrollbar = nullptr;
	for (const auto &item : items) {
		if (item->IsType(UiType::Edit)) {
			auto *pItemUIEdit = static_cast<UiEdit *>(item.get());
			SDL_SetTextInputArea(ghMainWnd, &item->m_rect, 0);
			allowEmptyTextInput = pItemUIEdit->m_allowEmpty;
#ifdef __SWITCH__
			switch_start_text_input(pItemUIEdit->m_hint, pItemUIEdit->m_value, pItemUIEdit->m_max_length);
#elif defined(__vita__)
			vita_start_text_input(pItemUIEdit->m_hint, pItemUIEdit->m_value, pItemUIEdit->m_max_length);
#elif defined(__3DS__)
			ctr_vkbdInput(pItemUIEdit->m_hint, pItemUIEdit->m_value, &HandleCtrTextInput);
#elif defined(__ANDROID__) && !defined(TERMUX)
			// DETH: a native dialog instead of SDL's keyboard over the game surface.
			OpenAndroidTextInput(*pItemUIEdit);
#else
			SDLC_StartTextInput(ghMainWnd);
#endif
			UiTextInputState.emplace(TextInputState::Options {
			    .value = pItemUIEdit->m_value,
			    .cursor = &pItemUIEdit->m_cursor,
			    .maxLength = pItemUIEdit->m_max_length,
			});
		} else if (item->IsType(UiType::List)) {
			auto *uiList = static_cast<UiList *>(item.get());
			SelectedItemMax = std::max(uiList->m_vecItems.size() - 1, static_cast<size_t>(0));
			ListViewportSize = uiList->viewportSize;
			gUiList = uiList;
			if (selectedItem <= SelectedItemMax && HasAnyOf(uiList->GetItem(selectedItem)->uiFlags, UiFlags::NeedsNextElement))
				AdjustListOffset(selectedItem + 1);
			SpeakText(uiList->GetItem(selectedItem)->m_text);
		} else if (item->IsType(UiType::Scrollbar)) {
			uiScrollbar = static_cast<UiScrollbar *>(item.get());
		}
	}

	AdjustListOffset(selectedItem);

	if (uiScrollbar != nullptr) {
		if (ListViewportSize >= static_cast<std::size_t>(SelectedItemMax + 1)) {
			uiScrollbar->Hide();
		} else {
			uiScrollbar->Show();
		}
	}
}

void UiRenderListItems()
{
	UiRenderItems(gUiItems);
}

void UiInitList_clear()
{
	SelectedItem = 0;
	SelectedItemMax = 0;
	ListViewportSize = 1;
	gfnListFocus = nullptr;
	gfnListSelect = nullptr;
	gfnListEsc = nullptr;
	gfnFullscreen = nullptr;
	gfnListYesNo = nullptr;
	gUiList = nullptr;
	gUiItems.clear();
	UiItemsWraps = false;
}

void UiPlayMoveSound()
{
	effects_play_sound(SfxID::MenuMove);
}

void UiPlaySelectSound()
{
	effects_play_sound(SfxID::MenuSelect);
}

namespace {

void UiFocus(std::size_t itemIndex, bool checkUp, bool ignoreItemsWraps = false)
{
	if (SelectedItem == itemIndex)
		return;

	AdjustListOffset(itemIndex);

	const auto *pItem = gUiList->GetItem(itemIndex);
	while (HasAnyOf(pItem->uiFlags, UiFlags::ElementHidden | UiFlags::ElementDisabled)) {
		if (checkUp) {
			if (itemIndex > 0)
				itemIndex -= 1;
			else if (UiItemsWraps && !ignoreItemsWraps)
				itemIndex = SelectedItemMax;
			else
				checkUp = false;
		} else {
			if (itemIndex < SelectedItemMax)
				itemIndex += 1;
			else if (UiItemsWraps && !ignoreItemsWraps)
				itemIndex = 0;
			else
				checkUp = true;
		}
		pItem = gUiList->GetItem(itemIndex);
	}
	SpeakText(pItem->m_text);

	if (HasAnyOf(pItem->uiFlags, UiFlags::NeedsNextElement))
		AdjustListOffset(itemIndex + 1);
	AdjustListOffset(itemIndex);

	SelectedItem = itemIndex;

	UiPlayMoveSound();

	if (gfnListFocus != nullptr)
		gfnListFocus(itemIndex);
}

void UiFocusUp()
{
	if (SelectedItem > 0)
		UiFocus(SelectedItem - 1, true);
	else if (UiItemsWraps)
		UiFocus(SelectedItemMax, true);
}

void UiFocusDown()
{
	if (SelectedItem < SelectedItemMax)
		UiFocus(SelectedItem + 1, false);
	else if (UiItemsWraps)
		UiFocus(0, false);
}

// UiFocusPageUp/Down mimics the slightly weird behaviour of actual Diablo.

void UiFocusPageUp()
{
	if (listOffset == 0) {
		UiFocus(0, true, true);
	} else {
		const std::size_t relpos = SelectedItem - listOffset;
		std::size_t prevPageStart = SelectedItem - relpos;
		if (prevPageStart >= ListViewportSize)
			prevPageStart -= ListViewportSize;
		else
			prevPageStart = 0;
		AdjustListOffset(prevPageStart);
		UiFocus(listOffset + relpos, true, true);
	}
}

void UiFocusPageDown()
{
	if (listOffset + ListViewportSize > SelectedItemMax) {
		UiFocus(SelectedItemMax, false, true);
	} else {
		const std::size_t relpos = SelectedItem - listOffset;
		std::size_t nextPageEnd = SelectedItem + (ListViewportSize - relpos - 1);
		if (nextPageEnd + ListViewportSize <= SelectedItemMax)
			nextPageEnd += ListViewportSize;
		else
			nextPageEnd = SelectedItemMax;
		AdjustListOffset(nextPageEnd);
		UiFocus(listOffset + relpos, false, true);
	}
}

bool HandleMenuAction(MenuAction menuAction)
{
	switch (menuAction) {
	case MenuAction_SELECT:
		UiFocusNavigationSelect();
		return true;
	case MenuAction_UP:
		UiFocusUp();
		return true;
	case MenuAction_DOWN:
		UiFocusDown();
		return true;
	case MenuAction_PAGE_UP:
		UiFocusPageUp();
		return true;
	case MenuAction_PAGE_DOWN:
		UiFocusPageDown();
		return true;
	case MenuAction_DELETE:
		UiFocusNavigationYesNo();
		return true;
	case MenuAction_BACK:
		if (gfnListEsc == nullptr)
			return false;
		UiFocusNavigationEsc();
		return true;
	case MenuAction_LEFT:
		if (gfnListLeft == nullptr)
			return false;
		gfnListLeft();
		return true;
	case MenuAction_RIGHT:
		if (gfnListRight == nullptr)
			return false;
		gfnListRight();
		return true;
	default:
		return false;
	}
}

void UiOnBackgroundChange()
{
	theme::OnPaletteChanged();
	StartUiFadeIn();

	if (IsHardwareCursorEnabled() && ArtCursor && ControlDevice == ControlTypes::KeyboardAndMouse && GetCurrentCursorInfo().type() != CursorType::UserInterface) {
		SetHardwareCursor(CursorInfo::UserInterfaceCursor());
	}

	// It may take some time to get to the first `UiFadeIn()` call from here
	// if there is non-trivial initialization work, such as loading the list
	// of single-player characters.
	//
	// Black out the screen immediately to make it appear more smooth.
	SDL_FillSurfaceRect(DiabloUiSurface(), nullptr, 0);
	if (DiabloUiSurface() == PalSurface)
		BltFast(nullptr, nullptr);
	RenderPresent();
}

bool IsInsideRect(const SDL_Event &event, const SDL_Rect &rect);

/** DETH touch: state of a finger drag over the list. */
struct TouchDragState {
	bool tracking = false;
	bool moved = false;
	int startY = 0;
	int lastY = 0;
	int accumulated = 0;
	bool stoppedFling = false;
};
TouchDragState TouchDrag;
/** DETH: the gauge a finger is dragging, if any. */
const UiGauge *DraggingGauge = nullptr;

/** DETH touch: velocity of the glide after a fling, in pixels per millisecond (0 = still). */
float ListFlingVelocity = 0;
Uint32 ListFlingTicks = 0;
/** Recent finger samples, for the fling speed. */
struct DragSample {
	int y;
	Uint32 ticks;
};
DragSample DragSamples[6];
int DragSampleCount = 0;

/** Scrolls the list by `dy` pixels (positive = towards the end), keeping the selection visible. */
bool ScrollListByPixels(int dy)
{
	if (gUiList == nullptr || SelectedItemMax + 1 <= ListViewportSize)
		return false;
	const int pitch = std::max<int>(1, gUiList->m_height);
	const int maxScroll = static_cast<int>(SelectedItemMax + 1 - ListViewportSize) * pitch;
	const int current = static_cast<int>(listOffset) * pitch + listPixelOffset;
	const int target = std::clamp(current + dy, 0, maxScroll);
	listOffset = static_cast<std::size_t>(target / pitch);
	listPixelOffset = target % pitch;
	if (SelectedItem < listOffset)
		SelectedItem = listOffset;
	else if (SelectedItem >= listOffset + ListViewportSize)
		SelectedItem = listOffset + ListViewportSize - 1;
	return target != current;
}

/** Advances the glide after a fling; called once per frame. */
void UpdateListFling()
{
	if (ListFlingVelocity == 0)
		return;
	const Uint32 now = SDL_GetTicks();
	const float dt = static_cast<float>(std::min<Uint32>(now - ListFlingTicks, 50));
	ListFlingTicks = now;
	static float carry = 0;
	carry += ListFlingVelocity * dt;
	const int step = static_cast<int>(carry);
	carry -= static_cast<float>(step);
	const bool moved = step == 0 || ScrollListByPixels(step);
	// Slow down like a physical list: about a third of a second to lose most of the speed.
	ListFlingVelocity *= std::exp(-dt / 325.0F);
	if (!moved || std::abs(ListFlingVelocity) < 0.02F || gUiList == nullptr) {
		ListFlingVelocity = 0;
		carry = 0;
	}
}

/** Scrolls the list viewport by `rows`, keeping the selection visible. */
void ScrollListBy(int rows)
{
	if (gUiList == nullptr || SelectedItemMax + 1 <= ListViewportSize)
		return;
	const int maxOffset = static_cast<int>(SelectedItemMax + 1 - ListViewportSize);
	const int newOffset = std::clamp(static_cast<int>(listOffset) + rows, 0, maxOffset);
	if (newOffset == static_cast<int>(listOffset))
		return;
	listOffset = static_cast<std::size_t>(newOffset);
	// Keep the selection inside the viewport without triggering a focus callback.
	if (SelectedItem < listOffset)
		SelectedItem = listOffset;
	else if (SelectedItem >= listOffset + ListViewportSize)
		SelectedItem = listOffset + ListViewportSize - 1;
}

void UiFocusNavigation(SDL_Event *event)
{
	switch (event->type) {
	case SDL_EVENT_KEY_UP:
	case SDL_EVENT_MOUSE_BUTTON_UP:
	case SDL_EVENT_MOUSE_MOTION:
	case SDL_EVENT_JOYSTICK_BUTTON_UP:
	case SDL_EVENT_JOYSTICK_AXIS_MOTION:
	case SDL_EVENT_JOYSTICK_BALL_MOTION:
	case SDL_EVENT_JOYSTICK_HAT_MOTION:
#ifndef USE_SDL1
	case SDL_EVENT_MOUSE_WHEEL:
	case SDL_EVENT_FINGER_UP:
	case SDL_EVENT_FINGER_MOTION:
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
#endif
#ifdef USE_SDL3
	case SDL_EVENT_WINDOW_EXPOSED:
#else
#ifndef USE_SDL1
	case SDL_WINDOWEVENT:
#endif
	case SDL_SYSWMEVENT:
#endif
		mainmenu_restart_repintro();
		break;
	default:
		break;
	}

	bool menuActionHandled = false;
	for (const MenuAction menuAction : GetMenuActions(*event))
		menuActionHandled |= HandleMenuAction(menuAction);
	if (menuActionHandled)
		return;

#ifndef USE_SDL1
	if (event->type == SDL_EVENT_MOUSE_WHEEL) {
		if (SDLC_EventWheelIntY(*event) > 0) {
			UiFocusUp();
		} else if (SDLC_EventWheelIntY(*event) < 0) {
			UiFocusDown();
		}
		return;
	}
#else
	if (event->type == SDL_MOUSEBUTTONDOWN) {
		switch (event->button.button) {
		case SDL_BUTTON_WHEELUP:
			UiFocusUp();
			return;
		case SDL_BUTTON_WHEELDOWN:
			UiFocusDown();
			return;
		}
	}
#endif

	if (UiTextInputState.has_value() && HandleTextInputEvent(*event, *UiTextInputState)) {
		return;
	}

	// DETH: a finger (or mouse) held on a gauge slides it.
	if (DraggingGauge != nullptr) {
		if (event->type == SDL_EVENT_MOUSE_MOTION) {
			DraggingGauge->SetFromX(SDLC_EventMotionIntX(*event));
			return;
		}
		if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
			DraggingGauge = nullptr;
			return;
		}
	}

	// DETH touch: dragging a finger over the list scrolls it; a drag is never a tap.
	if (theme::TouchMode() && gUiList != nullptr) {
		if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT && IsInsideRect(*event, gUiList->m_rect)) {
			TouchDrag.tracking = true;
			TouchDrag.moved = false;
			TouchDrag.startY = SDLC_EventButtonIntY(*event);
			TouchDrag.accumulated = 0;
			// A touch during a glide only stops it; it does not also tap a row.
			TouchDrag.stoppedFling = ListFlingVelocity != 0;
			ListFlingVelocity = 0;
			DragSampleCount = 0;
		} else if (event->type == SDL_EVENT_MOUSE_MOTION && TouchDrag.tracking) {
			const int y = static_cast<int>(event->motion.y);
			const int dy = TouchDrag.lastY - y;
			TouchDrag.lastY = y;
			if (!TouchDrag.moved && std::abs(y - TouchDrag.startY) > 8) {
				TouchDrag.moved = true;
				gUiList->Release();
			}
			if (TouchDrag.moved) {
				// The list follows the finger pixel for pixel.
				ScrollListByPixels(dy);
				if (DragSampleCount == 6) {
					std::move(DragSamples + 1, DragSamples + 6, DragSamples);
					DragSampleCount = 5;
				}
				DragSamples[DragSampleCount++] = { y, SDL_GetTicks() };
			}
			return;
		} else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT && TouchDrag.tracking) {
			TouchDrag.tracking = false;
			if (TouchDrag.moved) {
				gUiList->Release();
				// Fling: keep the speed of the last ~100 ms of the drag.
				const Uint32 now = SDL_GetTicks();
				int first = DragSampleCount - 1;
				while (first > 0 && now - DragSamples[first - 1].ticks <= 100)
					--first;
				if (DragSampleCount >= 2 && first < DragSampleCount - 1 && now - DragSamples[DragSampleCount - 1].ticks < 60) {
					const float dt = static_cast<float>(std::max<Uint32>(1, DragSamples[DragSampleCount - 1].ticks - DragSamples[first].ticks));
					const float velocity = static_cast<float>(DragSamples[first].y - DragSamples[DragSampleCount - 1].y) / dt;
					if (std::abs(velocity) > 0.15F) {
						ListFlingVelocity = std::clamp(velocity, -6.0F, 6.0F);
						ListFlingTicks = now;
					}
				}
				return;
			}
			if (TouchDrag.stoppedFling) {
				gUiList->Release();
				return;
			}
		}
		if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN)
			TouchDrag.lastY = SDLC_EventButtonIntY(*event);
	}

	if (IsAnyOf(event->type, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP)
	    && UiItemMouseEvents(event, gUiItems)) {
		return;
	}
}

} // namespace

void UiHandleEvents(SDL_Event *event)
{
	if (event->type == SDL_EVENT_MOUSE_MOTION) {
		MousePosition = { SDLC_EventMotionIntX(*event), SDLC_EventMotionIntY(*event) };
		return;
	}

	if (event->type == SDL_EVENT_KEY_DOWN && SDLC_EventKey(*event) == SDLK_RETURN) {
		const auto *state = SDLC_GetKeyState();
		if (state[SDLC_KEYSTATE_LALT] != 0 || state[SDLC_KEYSTATE_RALT] != 0) {
			GetOptions().Graphics.fullscreen.SetValue(!IsFullScreen());
			SaveOptions();
			if (gfnFullscreen != nullptr)
				gfnFullscreen();
			return;
		}
	}

	if (event->type == SDL_EVENT_QUIT) {
		diablo_quit(0);
	}

#ifndef USE_SDL1
	HandleControllerAddedOrRemovedEvent(*event);

#ifdef USE_SDL3
	switch (event->type) {
	case SDL_EVENT_WINDOW_SHOWN:
	case SDL_EVENT_WINDOW_EXPOSED:
	case SDL_EVENT_WINDOW_RESTORED:
		gbActive = true;
		break;
	case SDL_EVENT_WINDOW_HIDDEN:
	case SDL_EVENT_WINDOW_MINIMIZED:
		gbActive = false;
		break;
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		DoReinitializeHardwareCursor();
		break;
	case SDL_EVENT_WINDOW_FOCUS_LOST:
		if (*GetOptions().Gameplay.pauseOnFocusLoss) music_mute();
		break;
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
		if (*GetOptions().Gameplay.pauseOnFocusLoss) diablo_focus_unpause();
		break;
	default:
		break;
	}
#else
	if (event->type == SDL_WINDOWEVENT) {
		if (IsAnyOf(event->window.event, SDL_WINDOWEVENT_SHOWN, SDL_WINDOWEVENT_EXPOSED, SDL_WINDOWEVENT_RESTORED)) {
			gbActive = true;
		} else if (IsAnyOf(event->window.event, SDL_WINDOWEVENT_HIDDEN, SDL_WINDOWEVENT_MINIMIZED)) {
			gbActive = false;
		} else if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
			// We reinitialize immediately (by calling `DoReinitializeHardwareCursor` instead of `ReinitializeHardwareCursor`)
			// because the cursor's Enabled state may have changed, resulting in changes to visibility.
			//
			// For example, if the previous size was too large for a hardware cursor then it was invisible
			// but may now become visible.
			DoReinitializeHardwareCursor();
		} else if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST && *GetOptions().Gameplay.pauseOnFocusLoss) {
			music_mute();
		} else if (event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED && *GetOptions().Gameplay.pauseOnFocusLoss) {
			diablo_focus_unpause();
		}
	}
#endif
#else
	if (event->type == SDL_ACTIVEEVENT && (event->active.state & SDL_APPINPUTFOCUS) != 0) {
		if (event->active.gain == 0)
			music_mute();
		else
			diablo_focus_unpause();
	}
#endif
}

void UiFocusNavigationSelect()
{
	UiPlaySelectSound();
	if (UiTextInputState.has_value()) {
		if (!allowEmptyTextInput && UiTextInputState->empty()) {
			return;
		}
#ifndef __SWITCH__
		SDLC_StopTextInput(ghMainWnd);
#endif
		UiTextInputState = std::nullopt;
	}
	if (gfnListSelect != nullptr)
		gfnListSelect(SelectedItem);
}

void UiFocusNavigationEsc()
{
	UiPlaySelectSound();
	if (UiTextInputState.has_value()) {
#ifndef __SWITCH__
		SDLC_StopTextInput(ghMainWnd);
#endif
		UiTextInputState = std::nullopt;
	}
	if (gfnListEsc != nullptr)
		gfnListEsc();
}

void UiFocusNavigationYesNo()
{
	if (gfnListYesNo == nullptr)
		return;

	if (gfnListYesNo())
		UiPlaySelectSound();
}

namespace {

bool IsInsideRect(const SDL_Event &event, const SDL_Rect &rect)
{
	const SDL_Point point = { SDLC_EventButtonIntX(event), SDLC_EventButtonIntY(event) };
	return SDLC_PointInRect(&point, &rect);
}

void LoadHeros()
{
	constexpr unsigned PortraitHeight = 76;
	ArtHero = LoadPcxSpriteList("ui_art\\heros", -static_cast<int>(PortraitHeight));
	if (!ArtHero)
		return;
	const uint16_t numPortraits = ClxSpriteList { *ArtHero }.numSprites();

	ArtHeroPortraitOrder.resize(GetNumPlayerClasses() + 1);
	for (size_t i = 0; i < GetNumPlayerClasses(); ++i) {
		const PlayerData &playerClassData = GetPlayerDataForClass(static_cast<HeroClass>(i));
		ArtHeroPortraitOrder[i] = playerClassData.portrait;
	}
	ArtHeroPortraitOrder.back() = 3;
	if (numPortraits >= 6) {
		ArtHeroPortraitOrder.back() = 5;
	}

	ArtHeroOverrides.resize(GetNumPlayerClasses() + 1);
	for (size_t i = 0; i <= GetNumPlayerClasses(); ++i) {
		char portraitPath[18];
		*BufCopy(portraitPath, "ui_art\\hero", i) = '\0';
		ArtHeroOverrides[i] = LoadPcx(portraitPath, /*transparentColor=*/std::nullopt, /*outPalette=*/nullptr, /*logError=*/false);
	}
}

void LoadUiGFX()
{
	ArtLogo = LoadPcxSpriteList("ui_art\\hf_logo2", /*numFrames=*/16, /*transparentColor=*/0, nullptr, false);
	if (!ArtLogo.has_value()) {
		ArtLogo = LoadPcxSpriteList("ui_art\\smlogo", /*numFrames=*/15, /*transparentColor=*/250);
	}
	DifficultyIndicator = LoadPcx("ui_art\\r1_gry", /*transparentColor=*/0);
	ArtFocus[FOCUS_SMALL] = LoadPcxSpriteList("ui_art\\focus16", /*numFrames=*/8, /*transparentColor=*/250);
	ArtFocus[FOCUS_MED] = LoadPcxSpriteList("ui_art\\focus", /*numFrames=*/8, /*transparentColor=*/250);
	ArtFocus[FOCUS_BIG] = LoadPcxSpriteList("ui_art\\focus42", /*numFrames=*/8, /*transparentColor=*/250);

	ArtCursor = LoadPcx("ui_art\\cursor", /*transparentColor=*/0);

	LoadHeros();
}

} // namespace

ClxSprite UiGetHeroDialogSprite(size_t heroClassIndex)
{
	return ArtHeroOverrides[heroClassIndex]
	    ? (*ArtHeroOverrides[heroClassIndex])[0]
	    : (*ArtHero)[ArtHeroPortraitOrder[heroClassIndex]];
}

void UnloadUiGFX()
{
	ArtHero = std::nullopt;
	for (OptionalOwnedClxSpriteList &override : ArtHeroOverrides)
		override = std::nullopt;
	ArtCursor = std::nullopt;
	for (auto &art : ArtFocus)
		art = std::nullopt;
	ArtLogo = std::nullopt;
	DifficultyIndicator = std::nullopt;
}

void UiInitialize()
{
	LoadUiGFX();

	if (ArtCursor) {
		if (!SDLC_HideCursor()) ErrSdl();
	}
}

void UiDestroy()
{
	UnloadFonts();
	UnloadUiGFX();
}

bool UiValidPlayerName(std::string_view name)
{
	if (name.empty())
		return false;

	// Currently only allow saving PlayerNameLength bytes as a player name, so if the name is too long we'd have to truncate it.
	// That said the input buffer is only 16 bytes long...
	if (name.size() > PlayerNameLength)
		return false;

	if (name.find_first_of(",<>%&\\\"?*#/: ") != name.npos)
		return false;

	// Only basic latin alphabet is supported for multiplayer characters to avoid rendering issues for players who do
	// not have fonts.mpq installed
	if (!c_all_of(name, IsBasicLatin))
		return false;

	const std::string_view bannedNames[] = {
		"gvdl",
		"dvou",
		"tiju",
		"cjudi",
		"bttipmf",
		"ojhhfs",
		"cmj{{bse",
		"benjo",
	};

	std::string buffer { name };
	for (char &character : buffer)
		character++;

	const std::string_view tempName { buffer };
	for (const std::string_view bannedName : bannedNames) {
		if (tempName.find(bannedName) != tempName.npos)
			return false;
	}

	return true;
}

Sint16 GetCenterOffset(Sint16 w, Sint16 bw)
{
	if (bw == 0) {
		bw = gnScreenWidth;
	}

	return (bw - w) / 2;
}

void UiLoadDefaultPalette()
{
	LoadPalette("ui_art\\diablo.pal");
	UpdateSystemPalette(logical_palette);
}

bool UiLoadBlackBackground()
{
	ArtBackground = std::nullopt;
	UiLoadDefaultPalette();
	UiOnBackgroundChange();
	return true;
}

void LoadBackgroundArt(const char *pszFile, int frames)
{
	ArtBackground = std::nullopt;
	ArtBackground = LoadPcxSpriteList(pszFile, static_cast<uint16_t>(frames), /*transparentColor=*/std::nullopt, logical_palette.data());
	if (!ArtBackground)
		return;

	UpdateSystemPalette(logical_palette);
	UiOnBackgroundChange();
}

void UiAddBackground(std::vector<std::unique_ptr<UiItemBase>> *vecDialog)
{
	const SDL_Rect rect = MakeSdlRect(0, GetUIRectangle().position.y, 0, 0);
	if (ArtBackgroundWidescreen) {
		vecDialog->push_back(std::make_unique<UiImageClx>((*ArtBackgroundWidescreen)[0], rect, UiFlags::AlignCenter));
	}
	if (ArtBackground) {
		vecDialog->push_back(std::make_unique<UiImageClx>((*ArtBackground)[0], rect, UiFlags::AlignCenter));
	}
}

void UiAddLogo(std::vector<std::unique_ptr<UiItemBase>> *vecDialog, int y)
{
	vecDialog->push_back(std::make_unique<UiImageAnimatedClx>(
	    *ArtLogo, MakeSdlRect(0, y, 0, 0), UiFlags::AlignCenter));
}

void UiFadeIn()
{
	if (HeadlessMode) return;
	UiUpdateFadePalette();
	if (DiabloUiSurface() == PalSurface) {
		BltFast(nullptr, nullptr);
	}
	RenderPresent();
}

namespace {
ClxSpriteList GetListSelectorSprites(int itemHeight)
{
	int size;
	if (itemHeight >= 42) {
		size = FOCUS_BIG;
	} else if (itemHeight >= 30) {
		size = FOCUS_MED;
	} else {
		size = FOCUS_SMALL;
	}
	return *ArtFocus[size];
}
} // namespace

void DrawSelector(const SDL_Rect &rect)
{
	const ClxSpriteList sprites = GetListSelectorSprites(rect.h);
	const ClxSprite sprite = sprites[GetAnimationFrame(sprites.numSprites())];

	// TODO FOCUS_MED appears higher than the box
	const int y = rect.y + ((rect.h - static_cast<int>(sprite.height())) / 2);

	const Surface &out = Surface(DiabloUiSurface());
	RenderClxSprite(out, sprite, { rect.x, y });
	RenderClxSprite(out, sprite, { rect.x + rect.w - sprite.width(), y });
}

/** DETH: screens built on the theme draw the procedural background instead of a PCX. */
bool UiThemeBackground = false;

void UiClearScreen()
{
	if (UiThemeBackground) {
		theme::DrawBackground(Surface(DiabloUiSurface()));
		return;
	}
	if (!ArtBackground || gnScreenWidth > (*ArtBackground)[0].width() || gnScreenHeight > (*ArtBackground)[0].height()) {
		SDL_FillSurfaceRect(DiabloUiSurface(), nullptr, 0);
	}
}

#ifndef USE_SDL1
/** DETH: a finger is down on the touch overlay, so its touch must not reach the menu items. */
bool TouchOverlayHeld = false;
#endif

void UiPollAndRender(std::optional<tl::function_ref<bool(SDL_Event &)>> eventHandler)
{
	UpdateListFling();
#if defined(__ANDROID__) && !defined(TERMUX)
	HandleAndroidTextInputResult();
#endif
	SDL_Event event;
	while (PollEvent(&event)) {
		if (eventHandler && (*eventHandler)(event))
			continue;
		if (!SDLC_ConvertEventToRenderCoordinates(renderer, &event)) {
			LogWarn(LogCategory::Application, "SDL_ConvertEventToRenderCoordinates: {}", SDL_GetError());
			SDL_ClearError();
		}
#ifndef USE_SDL1
		// DETH: the touch overlay works in menus too: pad navigates, A selects, B goes back.
		// Under touch rules the screens are tapped directly, so the overlay stays out of the menus.
		if (*GetOptions().Controller.onScreenControls && !theme::TouchMode()) {
			if (!VirtualGamepadState.isActive)
				ActivateVirtualGamepad();
			HandleTouchEvent(event);
			if (IsTouchFingerDown(event)) {
				bool consumed = true;
				if (VirtualGamepadState.primaryActionButton.isHeld && VirtualGamepadState.primaryActionButton.didStateChange)
					UiFocusNavigationSelect();
				else if (VirtualGamepadState.cancelButton.isHeld && VirtualGamepadState.cancelButton.didStateChange)
					UiFocusNavigationEsc();
				else if (VirtualGamepadState.directionPad.isUpPressed || VirtualGamepadState.directionPad.isDownPressed)
					; // up/down come from GetMenuHeldUpDownAction(), which reads the virtual pad and repeats while held
				else if (VirtualGamepadState.directionPad.isLeftPressed)
					HandleMenuAction(MenuAction_LEFT);
				else if (VirtualGamepadState.directionPad.isRightPressed)
					HandleMenuAction(MenuAction_RIGHT);
				else
					consumed = false;
				if (consumed) {
					TouchOverlayHeld = true;
					continue;
				}
			} else if (TouchOverlayHeld) {
				// Swallow the rest of that touch, including the mouse events SDL synthesizes from it.
				const bool touchMouse = (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP || event.type == SDL_MOUSEMOTION)
				    && (event.type == SDL_MOUSEMOTION ? event.motion.which : event.button.which) == SDL_TOUCH_MOUSEID;
				if (touchMouse && event.type == SDL_MOUSEBUTTONUP)
					TouchOverlayHeld = false;
				if (touchMouse || IsTouchFingerUp(event) || event.type == SDL_FINGERMOTION)
					continue;
			}
		}
#endif
		UiFocusNavigation(&event);
		UiHandleEvents(&event);
	}
	HandleMenuAction(GetMenuHeldUpDownAction());
	UiRenderListItems();
	DrawMouse();
	UiFadeIn();

	// Must happen after at least one call to `UiFadeIn` with non-zero fadeValue.
	// `UiFadeIn` reinitializes the hardware cursor only for fadeValue > 0.
	if (IsHardwareCursor() && fadeValue != 0)
		SetHardwareCursorVisible(ControlDevice == ControlTypes::KeyboardAndMouse);

#ifdef __3DS__
	// Keyboard blocks until input is finished
	// so defer until after render and fade-in
	ctr_vkbdFlush();
#endif

	discord_manager::UpdateMenu();
}

namespace {

void Render(const UiText &uiText)
{
	const Surface &out = Surface(DiabloUiSurface());
	DrawString(out, uiText.GetText(), MakeRectangle(uiText.m_rect),
	    { .flags = uiText.GetFlags() | UiFlags::FontSizeDialog });
}

void Render(const UiArtText &uiArtText)
{
	const Surface &out = Surface(DiabloUiSurface());
	DrawString(out, uiArtText.GetText(), MakeRectangle(uiArtText.m_rect),
	    { .flags = uiArtText.GetFlags(), .spacing = uiArtText.GetSpacing(), .lineHeight = uiArtText.GetLineHeight() });
}

void Render(const UiImageClx &uiImage)
{
	const ClxSprite sprite = uiImage.sprite();
	int x = uiImage.m_rect.x;
	if (uiImage.isCentered()) {
		x += GetCenterOffset(sprite.width(), uiImage.m_rect.w);
	}
	RenderClxSprite(Surface(DiabloUiSurface()), sprite, { x, uiImage.m_rect.y });
}

void Render(const UiImageAnimatedClx &uiImage)
{
	const ClxSprite sprite = uiImage.sprite(GetAnimationFrame(uiImage.numFrames()));
	int x = uiImage.m_rect.x;
	if (uiImage.isCentered()) {
		x += GetCenterOffset(sprite.width(), uiImage.m_rect.w);
	}
	RenderClxSprite(Surface(DiabloUiSurface()), sprite, { x, uiImage.m_rect.y });
}

void Render(const UiArtTextButton &uiButton)
{
	const Surface &out = Surface(DiabloUiSurface());
	if (uiButton.themed) {
		theme::DrawButton(out, MakeRectangle(uiButton.m_rect), HasAnyOf(uiButton.GetFlags(), UiFlags::ElementDisabled));
		DrawString(out, uiButton.GetText(), MakeRectangle(uiButton.m_rect), { .flags = uiButton.GetFlags() | UiFlags::AlignCenter | UiFlags::VerticalCenter });
		return;
	}
	DrawString(out, uiButton.GetText(), MakeRectangle(uiButton.m_rect), { .flags = uiButton.GetFlags() });
}

void Render(const UiList &uiList)
{
	// DETH touch: when scrolled between rows, draw into the list's own area (so the half rows at
	// the top and bottom are cut off cleanly) and include the row that peeks in at the bottom.
	const bool smooth = listPixelOffset != 0 && &uiList == gUiList;
	const Surface screen = Surface(DiabloUiSurface());
	const Surface out = smooth ? screen.subregion(uiList.m_rect.x, uiList.m_rect.y, uiList.m_rect.w, uiList.m_rect.h) : screen;
	const int shiftX = smooth ? -uiList.m_rect.x : 0;
	const int shiftY = smooth ? -uiList.m_rect.y - listPixelOffset : 0;
	const std::size_t rows = ListViewportSize + (smooth ? 1 : 0);

	for (std::size_t i = listOffset; i < uiList.m_vecItems.size() && (i - listOffset) < rows; ++i) {
		SDL_Rect rect = uiList.itemRect(static_cast<int>(i - listOffset));
		rect.x += shiftX;
		rect.y += shiftY;
		const UiListItem &item = *uiList.GetItem(i);
		Rectangle rectangle;
		if (uiList.themed) {
			// DETH: rows are drawn as touchable cards; the gap between rows stays transparent.
			Rectangle card = MakeRectangle(rect);
			card.size.height -= uiList.GetSpacing();
			const bool disabled = HasAnyOf(item.uiFlags, UiFlags::ElementDisabled);
			const bool blank = item.m_text.str().empty();
			if (!blank) {
				theme::RowState state = theme::RowState::Normal;
				if (disabled)
					state = theme::RowState::Disabled;
				else if (i == SelectedItem)
					state = uiList.pressed() ? theme::RowState::Pressed : theme::RowState::Focused;
				theme::DrawRow(out, card, state);
			}
			rectangle = card.inset(Displacement(theme::TextInset(), 0));
		} else {
			if (i == SelectedItem)
				DrawSelector(rect);
			rectangle = MakeRectangle(rect).inset(
			    Displacement(GetListSelectorSprites(rect.h)[0].width(), 0));
		}

		UiFlags uiFlags = uiList.GetFlags() | item.uiFlags;
		// DETH: a row can ask for the small font (e.g. a less important choice).
		if (HasAnyOf(item.uiFlags, UiFlags::FontSize12))
			uiFlags = (uiFlags & ~(UiFlags::FontSize24 | UiFlags::FontSize30)) | UiFlags::FontSize12;
		const bool large = uiList.themed && theme::LargeText();
		if (large && HasAnyOf(uiFlags, UiFlags::FontSize24))
			uiFlags = (uiFlags & ~UiFlags::FontSize24) | UiFlags::FontSize30;
		const GameFontTables fontSize = GetFontSizeFromUiFlags(uiFlags);
		std::string_view text = item.m_text.str();
		while (GetLineWidth(text, fontSize, 1) > rectangle.size.width) {
			text = std::string_view(text.data(), FindLastUtf8Symbols(text));
		}

		UiFlags drawFlags = uiFlags;
		if (uiList.themed && !item.subText.empty() && rectangle.size.height >= 48) {
			// Two-line row: the text sits in the upper part, the description underneath.
			const int subHeight = large ? 28 : 18;
			const GameFontTables subFont = large ? GameFont24 : GameFont12;
			const Rectangle subRect { { rectangle.position.x, rectangle.position.y + rectangle.size.height - subHeight - 6 }, { rectangle.size.width, subHeight } };
			std::string_view sub = item.subText;
			std::string shortened;
			if (GetLineWidth(sub, subFont, 1) > subRect.size.width) {
				// Cut at a whole word and mark it, rather than mid-word.
				const int ellipsisWidth = GetLineWidth("...", subFont, 1);
				while (!sub.empty() && GetLineWidth(sub, subFont, 1) + ellipsisWidth > subRect.size.width)
					sub = std::string_view(sub.data(), FindLastUtf8Symbols(sub));
				const size_t space = sub.find_last_of(' ');
				if (space != std::string_view::npos && space > 0)
					sub = sub.substr(0, space);
				while (!sub.empty() && (sub.back() == ',' || sub.back() == ' '))
					sub.remove_suffix(1);
				shortened = StrCat(sub, "...");
				sub = shortened;
			}
			DrawString(out, sub, subRect, { .flags = (large ? UiFlags::FontSize24 | UiFlags::ColorUiSilver : UiFlags::FontSize12 | UiFlags::ColorUiSilverDark) | (uiFlags & UiFlags::AlignCenter) });
			rectangle.size.height -= subHeight + 2;
		}
		if (uiList.themed) {
			drawFlags |= UiFlags::VerticalCenter;
			if (item.toggle >= 0) {
				theme::DrawToggle(out, MakeRectangle(rect), item.toggle == 1, i == SelectedItem);
			} else if (!item.rightText.empty()) {
				const Rectangle valueRect = rectangle.inset(Displacement(4, 0));
				std::string_view value = item.rightText;
				const int maxValueWidth = valueRect.size.width / 2;
				while (GetLineWidth(value, fontSize, 1) > maxValueWidth && !value.empty())
					value = std::string_view(value.data(), FindLastUtf8Symbols(value));
				DrawString(out, value, valueRect, { .flags = (drawFlags & ~(UiFlags::ColorUiGold | UiFlags::AlignCenter)) | UiFlags::ColorUiSilver | UiFlags::AlignRight });
			}
		}
		if (item.args.empty()) {
			DrawString(out, text, rectangle, { .flags = drawFlags, .spacing = uiList.GetSpacing() });
		} else {
			DrawStringWithColors(out, text, item.args, rectangle, { .flags = drawFlags, .spacing = uiList.GetSpacing() });
		}
	}
}

void Render(const UiScrollbar &uiSb)
{
	const Surface out = Surface(DiabloUiSurface());

	// Bar background (tiled):
	{
		const int bgY = uiSb.m_rect.y + uiSb.m_arrow[0].height();
		const int bgH = DownArrowRect(uiSb).y - bgY;
		const Surface backgroundOut = out.subregion(uiSb.m_rect.x, bgY, ScrollBarBgWidth, bgH);
		int y = 0;
		while (y < bgH) {
			RenderClxSprite(backgroundOut, uiSb.m_bg, { 0, y });
			y += uiSb.m_bg.height();
		}
	}

	// Arrows:
	{
		const SDL_Rect rect = UpArrowRect(uiSb);
		const auto frame = static_cast<uint16_t>(scrollBarState.upArrowPressed ? ScrollBarArrowFrame_UP_ACTIVE : ScrollBarArrowFrame_UP);
		RenderClxSprite(out.subregion(rect.x, 0, ScrollBarArrowWidth, out.h()), uiSb.m_arrow[frame], { 0, rect.y });
	}
	{
		const SDL_Rect rect = DownArrowRect(uiSb);
		const auto frame = static_cast<uint16_t>(scrollBarState.downArrowPressed ? ScrollBarArrowFrame_DOWN_ACTIVE : ScrollBarArrowFrame_DOWN);
		RenderClxSprite(out.subregion(rect.x, 0, ScrollBarArrowWidth, out.h()), uiSb.m_arrow[frame], { 0, rect.y });
	}

	// Thumb:
	if (SelectedItemMax > 0) {
		const SDL_Rect rect = ThumbRect(uiSb, SelectedItem, SelectedItemMax + 1);
		RenderClxSprite(out, uiSb.m_thumb, { rect.x, rect.y });
	}
}

void Render(const UiEdit &uiEdit)
{
	const Surface &out = Surface(DiabloUiSurface());
	Rectangle rect;
	if (uiEdit.themed) {
		theme::DrawEditBox(out, MakeRectangle(uiEdit.m_rect), true);
		rect = MakeRectangle(uiEdit.m_rect).inset({ theme::TextInset(), 1 });
	} else {
		DrawSelector(uiEdit.m_rect);
		// To simulate padding we inset the region used to draw text in an edit control
		rect = MakeRectangle(uiEdit.m_rect).inset({ 43, 1 });
	}

	DrawString(out, uiEdit.m_value, rect,
	    {
	        .flags = uiEdit.themed ? uiEdit.GetFlags() | UiFlags::VerticalCenter : uiEdit.GetFlags(),
	        .cursorPosition = static_cast<int>(uiEdit.m_cursor.position),
	        .highlightRange = { static_cast<int>(uiEdit.m_cursor.selection.begin), static_cast<int>(uiEdit.m_cursor.selection.end) },
	        .highlightColor = 126,
	    });
}

bool HandleMouseEventArtTextButton(const SDL_Event &event, const UiArtTextButton *uiButton)
{
	if (event.type != SDL_EVENT_MOUSE_BUTTON_UP || event.button.button != SDL_BUTTON_LEFT) {
		return false;
	}

	uiButton->Activate();
	return true;
}

bool HandleMouseEventList(const SDL_Event &event, UiList *uiList)
{
	if (event.button.button != SDL_BUTTON_LEFT)
		return false;

	if (!IsAnyOf(event.type, SDL_EVENT_MOUSE_BUTTON_UP, SDL_EVENT_MOUSE_BUTTON_DOWN)) {
		return false;
	}

	// DETH touch: account for a list scrolled between rows.
	std::size_t index = (event.button.y - uiList->m_rect.y + (uiList == gUiList ? listPixelOffset : 0)) / std::max<int>(1, uiList->m_height);
	if (index >= uiList->m_vecItems.size())
		return false;
	if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		uiList->Press(index);
		return true;
	}

	if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && !uiList->IsPressed(index)) {
		return false;
	}

	index += listOffset;
	if (theme::TouchMode()) {
		// DETH touch: one tap focuses and activates the row.
		if (index > SelectedItemMax || HasAnyOf(uiList->GetItem(index)->uiFlags, UiFlags::ElementHidden | UiFlags::ElementDisabled))
			return false;
		if (SelectedItem != index)
			UiFocus(index, true, false);
		UiFocusNavigationSelect();
		return true;
	}
	const bool hasFocusCallback = gfnListFocus != nullptr;
	const Uint32 ticksNow = SDL_GetTicks();
	const bool recentlyClickedSameItem = hasFocusCallback && lastListClickIndex == index && ticksNow - lastListClickTicks <= ListDoubleClickTimeMs;
#ifndef USE_SDL1
	const bool sdlReportedDoubleClick = event.button.clicks >= 2;
#else
	const bool sdlReportedDoubleClick = false;
#endif
	const bool doubleClicked = recentlyClickedSameItem || sdlReportedDoubleClick;
	lastListClickIndex = index;
	lastListClickTicks = ticksNow;

	if (hasFocusCallback && SelectedItem != index) {
		UiFocus(index, true, false);
		return true;
	}

	if (hasFocusCallback && !doubleClicked) {
		return true;
	}

	if (HasAnyOf(uiList->GetItem(index)->uiFlags, UiFlags::ElementHidden | UiFlags::ElementDisabled))
		return false;
	SelectedItem = index;
	UiFocusNavigationSelect();

	return true;
}

bool HandleMouseEventScrollBar(const SDL_Event &event, const UiScrollbar *uiSb)
{
	if (event.button.button != SDL_BUTTON_LEFT)
		return false;
	if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
		if (scrollBarState.upArrowPressed && IsInsideRect(event, UpArrowRect(*uiSb))) {
			UiFocusUp();
			return true;
		}
		if (scrollBarState.downArrowPressed && IsInsideRect(event, DownArrowRect(*uiSb))) {
			UiFocusDown();
			return true;
		}
	} else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		if (IsInsideRect(event, BarRect(*uiSb))) {
			// Scroll up or down based on thumb position.
			const SDL_Rect thumbRect = ThumbRect(*uiSb, SelectedItem, SelectedItemMax + 1);
			if (event.button.y < thumbRect.y) {
				UiFocusPageUp();
			} else if (event.button.y > thumbRect.y + thumbRect.h) {
				UiFocusPageDown();
			}
			return true;
		}
		if (IsInsideRect(event, UpArrowRect(*uiSb))) {
			scrollBarState.upArrowPressed = true;
			return true;
		}
		if (IsInsideRect(event, DownArrowRect(*uiSb))) {
			scrollBarState.downArrowPressed = true;
			return true;
		}
	}
	return false;
}

bool HandleMouseEvent(const SDL_Event &event, UiItemBase *item)
{
	if (item->IsNotInteractive() || !IsInsideRect(event, item->m_rect))
		return false;
	switch (item->GetType()) {
	case UiType::ArtTextButton:
		return HandleMouseEventArtTextButton(event, static_cast<UiArtTextButton *>(item));
	case UiType::Button:
		return HandleMouseEventButton(event, static_cast<UiButton *>(item));
	case UiType::List:
		return HandleMouseEventList(event, static_cast<UiList *>(item));
	case UiType::Scrollbar:
		return HandleMouseEventScrollBar(event, static_cast<UiScrollbar *>(item));
#if defined(__ANDROID__) && !defined(TERMUX)
	case UiType::Edit:
		// DETH: tapping the field brings the text entry dialog back.
		if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN && event.type != SDL_EVENT_MOUSE_BUTTON_UP)
			return false;
		if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT && !AndroidTextInputOpen())
			OpenAndroidTextInput(*static_cast<UiEdit *>(item));
		return true;
#endif
	case UiType::Gauge: {
		const auto *gauge = static_cast<const UiGauge *>(item);
		if (!gauge->interactive() || event.button.button != SDL_BUTTON_LEFT)
			return false;
		if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
			DraggingGauge = gauge;
			gauge->SetFromX(SDLC_EventButtonIntX(event));
		}
		return true;
	}
	default:
		return false;
	}
}

} // namespace

void UiRenderItem(const UiItemBase &item)
{
	if (item.IsHidden())
		return;
	switch (item.GetType()) {
	case UiType::Text:
		Render(static_cast<const UiText &>(item));
		break;
	case UiType::ArtText:
		Render(static_cast<const UiArtText &>(item));
		break;
	case UiType::ImageClx:
		Render(static_cast<const UiImageClx &>(item));
		break;
	case UiType::ImageAnimatedClx:
		Render(static_cast<const UiImageAnimatedClx &>(item));
		break;
	case UiType::ArtTextButton:
		Render(static_cast<const UiArtTextButton &>(item));
		break;
	case UiType::Button:
		RenderButton(static_cast<const UiButton &>(item));
		break;
	case UiType::List:
		Render(static_cast<const UiList &>(item));
		break;
	case UiType::Scrollbar:
		Render(static_cast<const UiScrollbar &>(item));
		break;
	case UiType::Panel:
		theme::DrawPanel(Surface(DiabloUiSurface()), MakeRectangle(item.m_rect), static_cast<const UiPanel &>(item).opaque);
		break;
	case UiType::Gauge: {
		const auto &gauge = static_cast<const UiGauge &>(item);
		theme::DrawGauge(Surface(DiabloUiSurface()), MakeRectangle(item.m_rect), gauge.index(), gauge.count());
		break;
	}
	case UiType::CheckBox:
		theme::DrawCheckBox(Surface(DiabloUiSurface()), MakeRectangle(item.m_rect), static_cast<const UiCheckBox &>(item).checked);
		break;
	case UiType::ThemeScrollbar:
		{
			// In pixels, so the thumb glides with the list.
			const int pitch = gUiList != nullptr ? std::max<int>(1, gUiList->m_height) : 1;
			theme::DrawScrollbar(Surface(DiabloUiSurface()), MakeRectangle(item.m_rect), static_cast<int>(listOffset) * pitch + listPixelOffset, static_cast<int>(ListViewportSize) * pitch, (static_cast<int>(SelectedItemMax) + 1) * pitch);
		}
		break;
	case UiType::Ornament:
		theme::DrawOrnament(Surface(DiabloUiSurface()), { item.m_rect.x + item.m_rect.w / 2, item.m_rect.y + item.m_rect.h / 2 }, item.m_rect.w);
		break;
	case UiType::Edit:
		Render(static_cast<const UiEdit &>(item));
		break;
	}
}

void UiRenderItems(const std::vector<UiItemBase *> &items)
{
	for (const UiItemBase *item : items)
		UiRenderItem(*item);
}

void UiRenderItems(const std::vector<std::unique_ptr<UiItemBase>> &items)
{
	for (const std::unique_ptr<UiItemBase> &item : items)
		UiRenderItem(*item);
}

bool UiItemMouseEvents(SDL_Event *event, const std::vector<UiItemBase *> &items)
{
	if (items.empty()) {
		return false;
	}

	bool handled = false;
	for (const auto &item : items) {
		if (HandleMouseEvent(*event, item)) {
			handled = true;
			break;
		}
	}
#ifndef USE_SDL1
	if (handled && theme::TouchMode() && event->type == SDL_EVENT_MOUSE_BUTTON_UP)
		touch::TapFeedback();
#endif

	if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT) {
		scrollBarState.downArrowPressed = scrollBarState.upArrowPressed = false;
		for (const auto &item : items) {
			if (item->IsType(UiType::Button)) {
				HandleGlobalMouseUpButton(static_cast<UiButton *>(item));
			} else if (item->IsType(UiType::List)) {
				static_cast<UiList *>(item)->Release();
			}
		}
	}

	return handled;
}

bool UiItemMouseEvents(SDL_Event *event, const std::vector<std::unique_ptr<UiItemBase>> &items)
{
	if (items.empty()) {
		return false;
	}

	bool handled = false;
	for (const auto &item : items) {
		if (HandleMouseEvent(*event, item.get())) {
			handled = true;
			break;
		}
	}
#ifndef USE_SDL1
	if (handled && theme::TouchMode() && event->type == SDL_EVENT_MOUSE_BUTTON_UP)
		touch::TapFeedback();
#endif

	if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT) {
		scrollBarState.downArrowPressed = scrollBarState.upArrowPressed = false;
		for (const auto &item : items) {
			if (item->IsType(UiType::Button)) {
				HandleGlobalMouseUpButton(static_cast<UiButton *>(item.get()));
			} else if (item->IsType(UiType::List)) {
				static_cast<UiList *>(item.get())->Release();
			}
		}
	}

	return handled;
}

void DrawMouse()
{
	if (ControlDevice != ControlTypes::KeyboardAndMouse || IsHardwareCursor() || !ArtCursor)
		return;
	RenderClxSprite(Surface(DiabloUiSurface()), (*ArtCursor)[0], MousePosition);
}
} // namespace devilution
