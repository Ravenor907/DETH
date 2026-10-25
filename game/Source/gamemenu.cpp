/**
 * @file gamemenu.cpp
 *
 * Implementation of the in-game menu functions.
 */
#include "controls/touch/layout_editor.h"
#include "gamemenu.h"

#include <algorithm>
#include <array>

#ifdef USE_SDL3
#include <SDL3/SDL_timer.h>
#endif

#include "DiabloUI/deth_theme.hpp"
#include "engine/render/text_render.hpp"
#include "utils/str_cat.hpp"
#include "engine/rectangle.hpp"
#include "cursor.h"
#include "diablo_msg.hpp"
#include "DiabloUI/diabloui.h"
#include "DiabloUI/settingsmenu.h"
#include "control/control.hpp"
#include "engine/backbuffer_state.hpp"
#include "engine/render/scrollrt.h"
#include "engine/render/world_view.hpp"
#include "engine/demomode.h"
#include "engine/events.hpp"
#include "engine/palette.h"
#include "engine/sound.h"
#include "engine/sound_defs.hpp"
#include "game_mode.hpp"
#include "gmenu.h"
#include "headless_mode.hpp"
#include "loadsave.h"
#include "monster.h"
#include "multi.h"
#include "options.h"
#include "pfile.h"
#include "qol/stash.h"
#include "player.h"
#include "utils/format.hpp"
#include "qol/floatingnumbers.h"
#include "utils/language.h"

#ifndef USE_SDL1
#include "controls/touch/renderers.h"
#include "controls/touch/touch_mode.hpp"
#include "controls/control_mode.hpp"
#include "controls/devices/game_controller.h"
#endif

namespace devilution {

bool isGameMenuOpen = false;

namespace {

// Forward-declare menu handlers, used by the global menu structs below.
void GamemenuPrevious(bool bActivate);
void GamemenuNewGame(bool bActivate);
void GamemenuExitToMainMenu(bool bActivate);
void GamemenuSaveCharacter(bool bActivate);
void GamemenuOptions(bool bActivate);
void GamemenuSettings(bool bActivate);
#ifdef __ANDROID__
void GamemenuControlsSize(bool bActivate);
#endif

void GamemenuMusicVolume(bool bActivate);
void GamemenuSoundVolume(bool bActivate);
void GamemenuBrightness(bool bActivate);
void GamemenuSpeed(bool bActivate);
void GamemenuZoom(bool bActivate);
void GamemenuTouchControls(bool bActivate);
void GamemenuMonsterStrength(bool bActivate);
void GamemenuSettings(bool bActivate);

/** DETH: names for the touch-controls toggle. Only shown on touch devices. */
const char *const TouchControlsNames[] = {
	N_("Touch Controls: On"),
	N_("Touch Controls: Off"),
};

/** Contains the game menu items of the single player menu. */
TMenuItem sgSingleMenu[] = {
	// clang-format off
	// dwFlags,      pszStr,                  fnMenu
	{ GMENU_ENABLED, N_("Options"),           &GamemenuOptions    },
	{ GMENU_ENABLED, N_("Save Game"),         &gamemenu_save_game },
	{ GMENU_ENABLED, N_("Load Game"),         &gamemenu_load_game },
	{ GMENU_ENABLED, N_("Exit to Main Menu"), &GamemenuExitToMainMenu },
	{ GMENU_ENABLED, N_("Quit Game"),         &gamemenu_quit_game },
	{ GMENU_ENABLED, N_("Touch Controls: On"), &GamemenuTouchControls },
	{ GMENU_ENABLED, nullptr,                 nullptr             },
	// clang-format on
};
/** Contains the game menu items of the multi player menu. */
TMenuItem sgMultiMenu[] = {
	// clang-format off
	// dwFlags,      pszStr,                  fnMenu
	{ GMENU_ENABLED, N_("Options"),           &GamemenuOptions    },
	{ GMENU_ENABLED, N_("Save Character"),    &GamemenuSaveCharacter },
	{ GMENU_ENABLED, N_("Exit to Main Menu"), &GamemenuExitToMainMenu },
	{ GMENU_ENABLED, N_("Quit Game"),         &gamemenu_quit_game },
	{ GMENU_ENABLED, N_("Touch Controls: On"), &GamemenuTouchControls },
	{ GMENU_ENABLED, nullptr,                 nullptr             },
	// clang-format on
};
TMenuItem sgOptionsMenu[] = {
	// clang-format off
	// dwFlags,                     pszStr,              fnMenu
	{ GMENU_ENABLED | GMENU_SLIDER, nullptr,             &GamemenuMusicVolume  },
	{ GMENU_ENABLED | GMENU_SLIDER, nullptr,             &GamemenuSoundVolume  },
	{ GMENU_ENABLED | GMENU_SLIDER, N_("Gamma"),         &GamemenuBrightness   },
	{ GMENU_ENABLED | GMENU_SLIDER, N_("Speed"),         &GamemenuSpeed        },
	{ GMENU_ENABLED | GMENU_SLIDER, N_("Zoom"),          &GamemenuZoom         },
	{ GMENU_ENABLED               , N_("All Settings"),  &GamemenuSettings     },
	{ GMENU_ENABLED               , N_("Previous Menu"), &GamemenuPrevious     }, // DETH: Controls Size on touch devices, see GamemenuOptions
	{ GMENU_ENABLED               , nullptr,             nullptr               },
	{ GMENU_ENABLED               , nullptr,             nullptr               },
	// clang-format on
};

/** DETH: the controls size in percent on the slider bar. */

/** Specifies the menu names for music enabled and disabled. */
const char *const MusicToggleNames[] = {
	N_("Music"),
	N_("Music Disabled"),
};
/** Specifies the menu names for sound enabled and disabled. */
const char *const SoundToggleNames[] = {
	N_("Sound"),
	N_("Sound Disabled"),
};

// The toggle sits last in its menu; the item list ends at the first null fnMenu, so where it is
// not wanted it simply disappears. Android only: on desktop (Windows, Linux, SteamOS) the
// on-screen controls are switched in Settings > Controller instead.
void GamemenuUpdateTouchItem(TMenuItem &item)
{
	item.pszStr = TouchControlsNames[*GetOptions().Controller.onScreenControls ? 0 : 1];
#ifdef __ANDROID__
	item.fnMenu = &GamemenuTouchControls;
#else
	item.fnMenu = nullptr;
#endif
}

void GamemenuUpdateSingle()
{
	sgSingleMenu[2].setEnabled(gbValidSaveFile);

	const bool enable = MyPlayer->_pmode != PM_DEATH && !MyPlayerIsDead;

	sgSingleMenu[0].setEnabled(enable);
	GamemenuUpdateTouchItem(sgSingleMenu[5]);
}

void GamemenuUpdateMulti()
{
	GamemenuUpdateTouchItem(sgMultiMenu[4]);
}

void GamemenuTouchControls(bool bActivate)
{
	if (!bActivate)
		return;
	GetOptions().Controller.onScreenControls.SetValue(!*GetOptions().Controller.onScreenControls);
	SaveOptions();
	gamemenu_on();
}

void GamemenuPrevious(bool /*bActivate*/)
{
	gamemenu_on();
}

/** @brief DETH: multiplayer: saves the hero (and the stash) now, kept in the recent saves as a manual save. */
void GamemenuSaveCharacter(bool /*bActivate*/)
{
	gamemenu_off();
	if (MyPlayer->_pmode == PM_DEATH || MyPlayerIsDead)
		return;
	pfile_write_hero(/*writeGameData=*/false);
	sfile_write_stash();
	pfile_multiplayer_saved(/*autosave=*/false);
	InitDiabloMsg(EMSG_GAME_SAVED);
}

/** @brief DETH: Exit to Main Menu goes to the main menu, not back to the hero or game list. */
void GamemenuExitToMainMenu(bool bActivate)
{
	GamemenuNewGame(bActivate);
	ReturnToMainMenu = true;
}

void GamemenuNewGame(bool /*bActivate*/)
{
	// DETH: leaving on purpose saves the multiplayer hero as a manual save.
	MultiplayerLeftByChoice = true;
	for (Player &player : Players) {
		player._pmode = PM_QUIT;
		player._pInvincible = true;
	}

	MyPlayerIsDead = false;
	if (!HeadlessMode) {
		RedrawEverything();
		scrollrt_draw_game_screen();
	}
	CornerStone.activated = false;
	gbRunGame = false;
	gamemenu_off();
}

void GamemenuSoundMusicToggle(const char *const *names, TMenuItem *menuItem, int volume)
{
	if (gbSndInited) {
		menuItem->addFlags(GMENU_ENABLED | GMENU_SLIDER);
		menuItem->pszStr = names[0];
		gmenu_slider_steps(menuItem, VOLUME_STEPS);
		gmenu_slider_set(menuItem, VOLUME_MIN, VOLUME_MAX, volume);
		return;
	}

	menuItem->removeFlags(GMENU_ENABLED | GMENU_SLIDER);
	menuItem->pszStr = names[1];
}

int GamemenuSliderMusicSound(TMenuItem *menuItem)
{
	return gmenu_slider_get(menuItem, VOLUME_MIN, VOLUME_MAX);
}

void GamemenuGetMusic()
{
	GamemenuSoundMusicToggle(MusicToggleNames, sgOptionsMenu, sound_get_or_set_music_volume(1));
}

void GamemenuGetSound()
{
	GamemenuSoundMusicToggle(SoundToggleNames, &sgOptionsMenu[1], sound_get_or_set_sound_volume(1));
}

void GamemenuGetBrightness()
{
	gmenu_slider_steps(&sgOptionsMenu[2], 21);
	gmenu_slider_set(&sgOptionsMenu[2], 0, 100, UpdateBrightness(-1));
}

void GamemenuGetSpeed()
{
	if (gbIsMultiplayer) {
		sgOptionsMenu[3].removeFlags(GMENU_ENABLED | GMENU_SLIDER);
		if (sgGameInitInfo.nTickRate >= 50)
			sgOptionsMenu[3].pszStr = _("Speed: Fastest").data();
		else if (sgGameInitInfo.nTickRate >= 40)
			sgOptionsMenu[3].pszStr = _("Speed: Faster").data();
		else if (sgGameInitInfo.nTickRate >= 30)
			sgOptionsMenu[3].pszStr = _("Speed: Fast").data();
		else if (sgGameInitInfo.nTickRate == 20)
			sgOptionsMenu[3].pszStr = _("Speed: Normal").data();
		return;
	}

	sgOptionsMenu[3].addFlags(GMENU_ENABLED | GMENU_SLIDER);

	sgOptionsMenu[3].pszStr = _("Speed").data();
	gmenu_slider_steps(&sgOptionsMenu[3], 46);
	gmenu_slider_set(&sgOptionsMenu[3], 20, 50, sgGameInitInfo.nTickRate);
}

int GamemenuSliderBrightness()
{
	return gmenu_slider_get(&sgOptionsMenu[2], 0, 100);
}

/** @brief DETH: zoom slider, one notch per zoom level. */
void GamemenuGetZoom()
{
	auto &zoom = GetOptions().Graphics.zoom;
	const int last = static_cast<int>(zoom.GetListSize()) - 1;
	gmenu_slider_steps(&sgOptionsMenu[4], last);
	gmenu_slider_set(&sgOptionsMenu[4], 0, last, static_cast<int>(zoom.GetActiveListIndex()));
}

/**
 * @brief DETH: in multiplayer the "All Settings" slot (unavailable there) holds Monster Strength:
 * a slider for the host, the current value for everyone else.
 */
void GamemenuGetSettingsSlot()
{
	TMenuItem &item = sgOptionsMenu[5];
	if (!gbIsMultiplayer) {
		item.dwFlags = GMENU_ENABLED;
		item.sliderText = nullptr;
		item.pszStr = N_("All Settings");
		item.fnMenu = &GamemenuSettings;
		return;
	}
	static std::string label;
	static std::string value;
	item.fnMenu = &GamemenuMonsterStrength;
	item.sliderText = nullptr;
	if (MyPlayerId == 0) {
		label = _("Foes");
		value = FormatRuntime(_("Monster Strength {:d}%"), MonsterStrengthPercent);
		item.sliderText = value.c_str();
		item.dwFlags = GMENU_ENABLED | GMENU_SLIDER;
		gmenu_slider_steps(&item, (MonsterStrengthMax - MonsterStrengthMin) / MonsterStrengthStep);
		gmenu_slider_set(&item, MonsterStrengthMin, MonsterStrengthMax, MonsterStrengthPercent);
	} else {
		label = FormatRuntime(_("Monster Strength: {:d}%"), MonsterStrengthPercent);
		item.dwFlags = 0;
	}
	item.pszStr = label.c_str();
}

void GamemenuMonsterStrength(bool bActivate)
{
	if (MyPlayerId != 0)
		return;
	int percent;
	if (bActivate)
		percent = MonsterStrengthPercent == 100 ? 150 : 100; // clicking the label: back to normal, or a step up
	else
		percent = gmenu_slider_get(&sgOptionsMenu[5], MonsterStrengthMin, MonsterStrengthMax);
	SetHostMonsterStrength(percent);
	GamemenuGetSettingsSlot();
}

/** @brief DETH: the full settings menu, in game. The game is paused meanwhile, so it is single player only. */
void GamemenuSettings(bool /*bActivate*/)
{
	if (gbIsMultiplayer)
		return;
	gamemenu_off();
	ClearFloatingNumbers();
	NewCursor(CURSOR_NONE);

	// The settings screen is main menu UI: it needs the menu art (freed when a
	// game starts) and swaps in the menu palette.
	const std::array<SDL_Color, 256> levelPalette = logical_palette;
	PaletteFadeOut(8, levelPalette);
	InvalidateWorldLayer();
	UiInitialize();
	InGameSettingsOpen = true;
	UiSettingsMenu();
	InGameSettingsOpen = false;
	UnloadUiGFX();
	SaveOptions();

	// Settings such as UI Scale change the internal resolution.
	CalcViewportGeometry();
	CalculatePanelAreas();
	logical_palette = levelPalette;
	// The menu rebuilt the blend table from its own palette; see-through walls need the level's.
	InitLevelBlending();
	InvalidateWorldLayer();
	NewCursor(CURSOR_HAND);
	RedrawEverything();
	DrawAndBlit();
	PaletteFadeIn(8, levelPalette);
	interface_msg_pump();
#ifndef USE_SDL1
	// DETH: Settings > Controls > Controller > Edit Touch Controls opens the editor over the game.
	RunRequestedTouchLayoutEditor();
#endif
}

#ifndef USE_SDL1
/** @brief DETH: the touch layout editor, from the game menu. */
void GamemenuEditControls(bool /*bActivate*/)
{
	gamemenu_off();
	RunTouchLayoutEditor();
}
#endif

void GamemenuOptions(bool /*bActivate*/)
{
	GamemenuGetMusic();
	GamemenuGetSound();
	GamemenuGetBrightness();
	GamemenuGetSpeed();
	GamemenuGetZoom();
	GamemenuGetSettingsSlot();
	// DETH: on touch devices the on-screen controls size is here too, so it can change in multiplayer
	// (where the full settings screen is unavailable). "Back" reads clearer on a touch screen.
	// Phones and tablets only: desktops and handhelds use the full Settings screen.
	size_t back = 6;
#ifndef USE_SDL1
	if (TouchLayoutEditorAvailable() && *GetOptions().Controller.onScreenControls) {
		// The layout editor (move and size every control), over the running game.
		sgOptionsMenu[6] = { GMENU_ENABLED, N_("Edit Controls"), &GamemenuEditControls };
		back = 7;
	}
#endif
#ifdef __ANDROID__
	if (back == 6 && touch::IsTouchDevice()) {
		sgOptionsMenu[6] = { GMENU_ENABLED | GMENU_SLIDER, N_("Buttons"), &GamemenuControlsSize };
		auto &size = GetOptions().Controller.touchButtonsSize;
		const int last = static_cast<int>(size.GetListSize()) - 1;
		gmenu_slider_steps(&sgOptionsMenu[6], last);
		gmenu_slider_set(&sgOptionsMenu[6], 0, last, static_cast<int>(size.GetActiveListIndex()));
		back = 7;
	}
#endif
	sgOptionsMenu[back] = { GMENU_ENABLED, back == 7 ? N_("Back") : N_("Previous Menu"), &GamemenuPrevious };
	sgOptionsMenu[back + 1] = { GMENU_ENABLED, nullptr, nullptr };
	gmenu_set_items(sgOptionsMenu, nullptr);
	gmenu_set_back_handler(&gamemenu_on);
}

void GamemenuMusicVolume(bool bActivate)
{
	if (bActivate) {
		if (gbMusicOn) {
			gbMusicOn = false;
			music_stop();
			sound_get_or_set_music_volume(VOLUME_MIN);
		} else {
			gbMusicOn = true;
			sound_get_or_set_music_volume(VOLUME_MAX);
			music_start(GetLevelMusic(leveltype));
		}
	} else {
		const int volume = GamemenuSliderMusicSound(&sgOptionsMenu[0]);
		sound_get_or_set_music_volume(volume);
		if (volume == VOLUME_MIN) {
			if (gbMusicOn) {
				gbMusicOn = false;
				music_stop();
			}
		} else if (!gbMusicOn) {
			gbMusicOn = true;
			music_start(GetLevelMusic(leveltype));
		}
	}

	GamemenuGetMusic();
}

void GamemenuSoundVolume(bool bActivate)
{
	if (bActivate) {
		if (gbSoundOn) {
			gbSoundOn = false;
			sound_stop();
			sound_get_or_set_sound_volume(VOLUME_MIN);
		} else {
			gbSoundOn = true;
			sound_get_or_set_sound_volume(VOLUME_MAX);
		}
	} else {
		const int volume = GamemenuSliderMusicSound(&sgOptionsMenu[1]);
		sound_get_or_set_sound_volume(volume);
		if (volume == VOLUME_MIN) {
			if (gbSoundOn) {
				gbSoundOn = false;
				sound_stop();
			}
		} else if (!gbSoundOn) {
			gbSoundOn = true;
		}
	}
	PlaySFX(SfxID::MenuMove);
	GamemenuGetSound();
}

void GamemenuBrightness(bool bActivate)
{
	int brightness;
	if (bActivate) {
		brightness = UpdateBrightness(-1);
		brightness = (brightness == 0) ? 100 : 0;
	} else {
		brightness = GamemenuSliderBrightness();
	}

	UpdateBrightness(brightness);
	GamemenuGetBrightness();
}

void GamemenuSpeed(bool bActivate)
{
	if (bActivate) {
		if (sgGameInitInfo.nTickRate != 20)
			sgGameInitInfo.nTickRate = 20;
		else
			sgGameInitInfo.nTickRate = 50;
		gmenu_slider_set(&sgOptionsMenu[3], 20, 50, sgGameInitInfo.nTickRate);
	} else {
		sgGameInitInfo.nTickRate = gmenu_slider_get(&sgOptionsMenu[3], 20, 50);
	}

	GetOptions().Gameplay.tickRate.SetValue(sgGameInitInfo.nTickRate);
	gnTickDelay = 1000 / sgGameInitInfo.nTickRate;
}

void GamemenuZoom(bool bActivate)
{
	auto &zoom = GetOptions().Graphics.zoom;
	const int last = static_cast<int>(zoom.GetListSize()) - 1;
	size_t index;
	if (bActivate) {
		// Clicking the label jumps between native size and the default.
		index = zoom.GetActiveListIndex() == 0 ? static_cast<size_t>(std::min(last, 4)) : 0;
	} else {
		index = static_cast<size_t>(gmenu_slider_get(&sgOptionsMenu[4], 0, last));
	}
	if (index != zoom.GetActiveListIndex())
		zoom.SetActiveListIndex(index);
	GamemenuGetZoom();
}

#ifdef __ANDROID__
void GamemenuControlsSize(bool bActivate)
{
	// DETH: the touch buttons' size (the stick keeps its own, in Settings).
	auto &size = GetOptions().Controller.touchButtonsSize;
	const int last = static_cast<int>(size.GetListSize()) - 1;
	size_t index;
	if (bActivate)
		index = size.GetActiveListIndex() == 2 ? static_cast<size_t>(std::min(last, 4)) : 2; // label: normal (100%) or large
	else
		index = static_cast<size_t>(gmenu_slider_get(&sgOptionsMenu[6], 0, last));
	if (index != size.GetActiveListIndex()) {
		size.SetActiveListIndex(index);
		SaveOptions();
	}
	gmenu_slider_set(&sgOptionsMenu[6], 0, last, static_cast<int>(size.GetActiveListIndex()));
}
#endif

} // namespace

void gamemenu_exit_game(bool bActivate)
{
	GamemenuNewGame(bActivate);
}

void gamemenu_quit_game(bool bActivate)
{
	GamemenuNewGame(bActivate);
#ifndef NOEXIT
	gbRunGameResult = false;
#else
	ReturnToMainMenu = true;
#endif
}

void gamemenu_load_game(bool /*bActivate*/)
{
	EventHandler saveProc = SetEventHandler(DisableInputEventHandler);
	gamemenu_off();
	ClearFloatingNumbers();
	NewCursor(CURSOR_NONE);
	InitDiabloMsg(EMSG_LOADING);
	RedrawEverything();
	DrawAndBlit();

	const std::array<SDL_Color, 256> prevPalette = logical_palette;
#ifndef USE_SDL1
	DeactivateVirtualGamepad();
	FreeVirtualGamepadTextures();
#endif
	if (std::expected<void, std::string> result = LoadGame(false); !result.has_value()) {
		app_fatal(result.error());
	}
#if !defined(USE_SDL1) && !defined(__vita__)
	if (renderer != nullptr) {
		InitVirtualGamepadTextures(*renderer);
	}
#endif
	ClrDiabloMsg();
	PaletteFadeOut(8, prevPalette);

	LoadPWaterPalette();
	NewCursor(CURSOR_HAND);
	CornerStone.activated = false;
	MyPlayerIsDead = false;
	RedrawEverything();
	DrawAndBlit();
	PaletteFadeIn(8);
	NewCursor(CURSOR_HAND);
	interface_msg_pump();
	SetEventHandler(saveProc);
}

void gamemenu_save_game(bool /*bActivate*/)
{
	if (pcurs != CURSOR_HAND) {
		return;
	}

	if (MyPlayer->_pmode == PM_DEATH || MyPlayerIsDead) {
		gamemenu_off();
		return;
	}

	EventHandler saveProc = SetEventHandler(DisableInputEventHandler);
	NewCursor(CURSOR_NONE);
	gamemenu_off();
	InitDiabloMsg(EMSG_SAVING);
	RedrawEverything();
	DrawAndBlit();
	const uint32_t currentTime = SDL_GetTicks();
	SaveGame();
	pfile_manual_saved(); // DETH: the working copy becomes the manual save
	ClrDiabloMsg();
	InitDiabloMsg(EMSG_GAME_SAVED, currentTime + 1000 - SDL_GetTicks());
	RedrawEverything();
	NewCursor(CURSOR_HAND);
	if (CornerStone.activated) {
		CornerstoneSave();
		if (!demo::IsRunning()) SaveOptions();
	}
	interface_msg_pump();
	SetEventHandler(saveProc);
}


// ---- DETH: the sliders at the sides of the game menu ----
namespace {

struct SideSetting {
	const char *name;
	/** The list option the slider moves. */
	OptionEntryListBase &(*option)();
	/** Whether this build and device use it. */
	bool (*shown)();
};

bool Always() { return true; }
bool ControllerInUse() { return !GameController::All().empty() || ControlDevice == ControlTypes::Gamepad; }
bool TouchInUse()
{
#ifdef __ANDROID__
	return *GetOptions().Controller.onScreenControls;
#else
	return false;
#endif
}

// DETH: settings that only live on the Options page (zoom, music, sound, brightness) are not repeated here.
const SideSetting SideSettings[] = {
	{ N_("Footsteps"), []() -> OptionEntryListBase & { return GetOptions().Audio.footstepVolume; }, &Always },
	{ N_("Map"), []() -> OptionEntryListBase & { return GetOptions().Gameplay.mapOpacity; }, &Always },
	{ N_("Minimap"), []() -> OptionEntryListBase & { return GetOptions().Gameplay.minimapOpacity; }, &Always },
	{ N_("Cursor"), []() -> OptionEntryListBase & { return GetOptions().Controller.cursorSpeed; }, &ControllerInUse },
	{ N_("Dead Zone"), []() -> OptionEntryListBase & { return GetOptions().Controller.stickDeadZone; }, &ControllerInUse },
	{ N_("Touch"), []() -> OptionEntryListBase & { return GetOptions().Controller.touchControlsOpacity; }, &TouchInUse },
	{ N_("Buttons"), []() -> OptionEntryListBase & { return GetOptions().Controller.touchButtonsSize; }, &TouchInUse },
	{ N_("Stick"), []() -> OptionEntryListBase & { return GetOptions().Controller.touchControlsSize; }, &TouchInUse },
};
constexpr int SideSettingCount = static_cast<int>(sizeof(SideSettings) / sizeof(SideSettings[0]));

/** The stored choice, or the next one this device uses (Footsteps always is). */
int LeftSettingIndex()
{
	const int stored = std::clamp(*GetOptions().Graphics.gameMenuSlider, 0, SideSettingCount - 1);
	for (int i = 0; i < SideSettingCount; ++i) {
		const int index = (stored + i) % SideSettingCount;
		if (SideSettings[index].shown())
			return index;
	}
	return 0;
}

void NextLeftSetting()
{
	const int current = LeftSettingIndex();
	for (int i = 1; i <= SideSettingCount; ++i) {
		const int index = (current + i) % SideSettingCount;
		if (SideSettings[index].shown()) {
			GetOptions().Graphics.gameMenuSlider.SetValue(index);
			return;
		}
	}
}

int SideCount(const SideSetting &s) { return static_cast<int>(s.option().GetListSize()); }
int SideIndex(const SideSetting &s) { return static_cast<int>(s.option().GetActiveListIndex()); }
void SideSet(const SideSetting &s, int i)
{
	if (static_cast<size_t>(i) != s.option().GetActiveListIndex())
		s.option().SetActiveListIndex(static_cast<size_t>(i));
}
std::string SideText(const SideSetting &s, int i) { return std::string(s.option().GetListDescription(static_cast<size_t>(i))); }

/** 0 none, 1 the left slider, 2 the UI Size slider. */
int SideDrag = 0;
/** The UI Size step under the finger; applied when it lets go (the whole screen is laid out again). */
int PendingUiScale = -1;

const SideSetting &LeftSetting()
{
	return SideSettings[LeftSettingIndex()];
}

struct SideLayout {
	Rectangle leftLabel, leftGauge, leftValue, panelCaption, panelToggle;
	Rectangle rightLabel, rightGauge, rightValue;
};

SideLayout GetSideLayout()
{
	const int labelW = 110;
	const int labelH = 22;
	const int gaugeW = 44;
	const int gaugeH = std::clamp(gnScreenHeight - 200, 120, 260);
	const int top = (gnScreenHeight - gaugeH) / 2;
	const int leftCx = 16 + labelW / 2;
	const int rightCx = gnScreenWidth - 16 - labelW / 2;
	SideLayout l;
	l.leftLabel = { { leftCx - labelW / 2, top - labelH - 8 }, { labelW, labelH } };
	l.leftGauge = { { leftCx - gaugeW / 2, top }, { gaugeW, gaugeH } };
	l.leftValue = { { leftCx - labelW / 2, top + gaugeH + 6 }, { labelW, labelH } };
	// Above the left slider, the same size as its name box (the standard panel covers the bottom corners).
	l.panelToggle = { { l.leftLabel.position.x, l.leftLabel.position.y - labelH - 10 }, l.leftLabel.size };
	l.panelCaption = { { l.leftLabel.position.x, l.panelToggle.position.y - 16 }, { l.leftLabel.size.width, 14 } };
	l.rightLabel = { { rightCx - labelW / 2, top - labelH - 8 }, { labelW, labelH } };
	l.rightGauge = { { rightCx - gaugeW / 2, top }, { gaugeW, gaugeH } };
	l.rightValue = { { rightCx - labelW / 2, top + gaugeH + 6 }, { labelW, labelH } };
	return l;
}

bool ShowUiSizeSlider()
{
	return *GetOptions().Graphics.uiScaleSlider;
}

/** The step a finger at `y` points to on a gauge filling from the bottom. */
int StepAt(const Rectangle &gauge, int y, int count)
{
	if (count <= 1)
		return 0;
	const int fromBottom = gauge.position.y + gauge.size.height - y;
	return std::clamp((fromBottom * (count - 1) + gauge.size.height / 2) / std::max(1, gauge.size.height), 0, count - 1);
}

/** A touch target a bit wider than the drawn rail. */
bool Grabs(const Rectangle &gauge, Point p)
{
	const Rectangle area { { gauge.position.x - 24, gauge.position.y - 12 }, { gauge.size.width + 48, gauge.size.height + 24 } };
	return area.contains(p);
}

} // namespace

void GamemenuDrawSides(const Surface &out)
{
	const SideLayout l = GetSideLayout();
	const SideSetting &left = LeftSetting();
	const int leftCount = SideCount(left);
	const int leftIndex = std::clamp(SideIndex(left), 0, std::max(0, leftCount - 1));
	// The name is a button: tap it for the next setting.
	theme::DrawButton(out, l.leftLabel, false);
	DrawString(out, _(left.name), l.leftLabel, { .flags = UiFlags::FontSize12 | UiFlags::ColorWhitegold | UiFlags::AlignCenter | UiFlags::VerticalCenter });
	theme::DrawVerticalGauge(out, l.leftGauge, leftIndex, leftCount);
	DrawString(out, SideText(left, leftIndex), l.leftValue, { .flags = UiFlags::FontSize12 | UiFlags::ColorWhite | UiFlags::AlignCenter | UiFlags::VerticalCenter });
	// The bottom panel: compact or standard, switched on the spot.
	DrawString(out, _("Panel"), l.panelCaption, { .flags = UiFlags::FontSize12 | UiFlags::ColorWhite | UiFlags::AlignCenter | UiFlags::VerticalCenter });
	theme::DrawButton(out, l.panelToggle, false);
	DrawString(out, *GetOptions().Gameplay.compactPanel ? _("Compact") : _("Standard"), l.panelToggle, { .flags = UiFlags::FontSize12 | UiFlags::ColorWhitegold | UiFlags::AlignCenter | UiFlags::VerticalCenter });

	if (!ShowUiSizeSlider())
		return;
	auto &uiScale = GetOptions().Graphics.uiScale;
	const int count = static_cast<int>(uiScale.GetListSize());
	const int index = PendingUiScale >= 0 ? PendingUiScale : static_cast<int>(uiScale.GetActiveListIndex());
	DrawString(out, _("UI Size"), l.rightLabel, { .flags = UiFlags::FontSize12 | UiFlags::ColorWhitegold | UiFlags::AlignCenter | UiFlags::VerticalCenter });
	theme::DrawVerticalGauge(out, l.rightGauge, index, count);
	DrawString(out, uiScale.GetListDescription(static_cast<size_t>(index)), l.rightValue, { .flags = UiFlags::FontSize12 | UiFlags::ColorWhite | UiFlags::AlignCenter | UiFlags::VerticalCenter });
}

bool GamemenuSidesMouse(bool isDown)
{
	if (!isDown) {
		if (SideDrag == 0)
			return false;
		if (SideDrag == 2 && PendingUiScale >= 0) {
			auto &uiScale = GetOptions().Graphics.uiScale;
			const size_t index = static_cast<size_t>(PendingUiScale);
			PendingUiScale = -1;
			if (index != uiScale.GetActiveListIndex())
				uiScale.SetActiveListIndex(index);
		}
		SideDrag = 0;
		SaveOptions();
		return true;
	}
	if (!gmenu_is_active())
		return false;
	const SideLayout l = GetSideLayout();
	const Point p = MousePosition;
	if (l.panelToggle.contains(p)) {
		auto &compact = GetOptions().Gameplay.compactPanel;
		compact.SetValue(!*compact);
		SaveOptions();
		PlaySFX(SfxID::MenuMove);
		return true;
	}
	if (l.leftLabel.contains(p)) {
		NextLeftSetting();
		PlaySFX(SfxID::MenuMove);
		return true;
	}
	if (Grabs(l.leftGauge, p)) {
		SideDrag = 1;
		GamemenuSidesMove();
		return true;
	}
	if (ShowUiSizeSlider() && Grabs(l.rightGauge, p)) {
		SideDrag = 2;
		GamemenuSidesMove();
		return true;
	}
	return false;
}

bool GamemenuSidesMove()
{
	if (SideDrag == 0)
		return false;
	const SideLayout l = GetSideLayout();
	if (SideDrag == 1) {
		const SideSetting &left = LeftSetting();
		const int step = StepAt(l.leftGauge, MousePosition.y, SideCount(left));
		if (step != SideIndex(left))
			SideSet(left, step);
	} else {
		PendingUiScale = StepAt(l.rightGauge, MousePosition.y, static_cast<int>(GetOptions().Graphics.uiScale.GetListSize()));
	}
	return true;
}

bool InGameSettingsOpen = false;
bool MultiplayerLeftByChoice = false;

void gamemenu_on()
{
	isGameMenuOpen = true;
	theme::Invalidate(); // DETH: the side sliders match their colours to the game's palette
	if (!gbIsMultiplayer) {
		gmenu_set_items(sgSingleMenu, GamemenuUpdateSingle);
	} else {
		gmenu_set_items(sgMultiMenu, GamemenuUpdateMulti);
	}
	PressEscKey();
}

void gamemenu_off()
{
	isGameMenuOpen = false;
	gmenu_set_items(nullptr, nullptr);
}

void gamemenu_handle_previous()
{
	if (gmenu_is_active())
		gamemenu_off();
	else
		gamemenu_on();
}

} // namespace devilution
