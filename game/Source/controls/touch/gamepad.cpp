#include <cmath>

#ifdef USE_SDL3
#include <SDL3/SDL_video.h>
#else
#include <SDL.h>
#endif

#include "control/control.hpp"
#include "controls/touch/event_handlers.h"
#include "controls/touch/gamepad.h"
#include "controls/touch/layout_editor.h"

#include <algorithm>
#include <cstdlib>

#include "options.h"
#include "panels/quest_log.hpp"
#include "utils/display.h"
#include "utils/ui_fwd.h"

namespace devilution {

VirtualGamepad VirtualGamepadState;

namespace {

constexpr float Pi = 3.141592653589793F;

int roundToInt(float value)
{
	return static_cast<int>(round(value));
}

constexpr bool PointsUp(float angle)
{
	constexpr float UpAngle = Pi / 2;
	constexpr float MinAngle = UpAngle - (3 * Pi / 8);
	constexpr float MaxAngle = UpAngle + (3 * Pi / 8);
	return MinAngle <= angle && angle <= MaxAngle;
}

constexpr bool PointsDown(float angle)
{
	constexpr float DownAngle = -Pi / 2;
	constexpr float MinAngle = DownAngle - (3 * Pi / 8);
	constexpr float MaxAngle = DownAngle + (3 * Pi / 8);
	return MinAngle <= angle && angle <= MaxAngle;
}

constexpr bool PointsLeft(float angle)
{
	constexpr float MaxAngle = Pi - (3 * Pi / 8);
	constexpr float MinAngle = -Pi + (3 * Pi / 8);
	return MinAngle >= angle || angle >= MaxAngle;
}

constexpr bool PointsRight(float angle)
{
	constexpr float MinAngle = -3 * Pi / 8;
	constexpr float MaxAngle = 3 * Pi / 8;
	return MinAngle <= angle && angle <= MaxAngle;
}

} // namespace

void InitializeVirtualGamepad()
{
	const float sqrt2 = sqrtf(2.0F);

	const int screenPixels = std::min(gnScreenWidth, gnScreenHeight);
	int inputMargin = screenPixels / 10;
	int menuButtonWidth = screenPixels / 10;
	int directionPadSize = screenPixels / 4;
	int padButtonSize = roundToInt(1.1F * screenPixels / 10);
	int padButtonSpacing = inputMargin / 3;

#ifdef USE_SDL3
	const float dpi = SDL_GetWindowDisplayScale(ghMainWnd);
#else
	float dpi = 0.0F;
	float hdpi;
	float vdpi;
	const int displayIndex = SDL_GetWindowDisplayIndex(ghMainWnd);
	if (SDL_GetDisplayDPI(displayIndex, nullptr, &hdpi, &vdpi) == 0) {
		int clientWidth;
		int clientHeight;
		if (renderer != nullptr)
			SDL_GetRendererOutputSize(renderer, &clientWidth, &clientHeight);
		else
			SDL_GetWindowSize(ghMainWnd, &clientWidth, &clientHeight);

		hdpi *= static_cast<float>(gnScreenWidth) / clientWidth;
		vdpi *= static_cast<float>(gnScreenHeight) / clientHeight;

		dpi = std::min(hdpi, vdpi);
	}
#endif

	// Debug aid for screenshots on a desktop: pretend the display has this DPI.
	if (const char *forcedDpi = std::getenv("DETH_TOUCH_DPI"); forcedDpi != nullptr) {
		SDL_DisplayMode mode;
		const int desktopWidth = SDL_GetDesktopDisplayMode(0, &mode) == 0 ? std::max(mode.w, mode.h) : gnScreenWidth;
		dpi = static_cast<float>(std::atof(forcedDpi)) * static_cast<float>(gnScreenWidth) / static_cast<float>(std::max(1, desktopWidth));
	}

	if (dpi != 0.0F) {
		inputMargin = roundToInt(0.25F * dpi);
		menuButtonWidth = roundToInt(0.2F * dpi);
		directionPadSize = roundToInt(dpi);
		padButtonSize = roundToInt(0.3F * dpi);
		padButtonSpacing = roundToInt(0.1F * dpi);
	}

	// DETH: Settings > Controller > Touch Controls Size.
	// DETH: Stick Size and Button Size are separate, so the buttons can grow without the stick.
	const float stickFactor = *GetOptions().Controller.touchControlsSize / 100.0F;
	const float buttonFactor = *GetOptions().Controller.touchButtonsSize / 100.0F;
	menuButtonWidth = roundToInt(menuButtonWidth * buttonFactor);
	directionPadSize = roundToInt(directionPadSize * stickFactor);
	padButtonSize = roundToInt(padButtonSize * buttonFactor);
	padButtonSpacing = roundToInt(padButtonSpacing * buttonFactor);
	inputMargin = roundToInt(inputMargin * std::max(0.5F, std::max(stickFactor, buttonFactor) * 0.75F));

	const int menuPanelTopMargin = 30;
	const int menuPanelButtonSpacing = 4;
	const Size menuPanelButtonSize = { 64, 62 };
	// DETH: six cells: Character, Quests, Inventory, Spellbook, Menu, Map (panel art is 535 px wide).
	const int rightMarginMenuButton6 = menuPanelButtonSpacing + menuPanelButtonSize.width;
	const int rightMarginMenuButton5 = rightMarginMenuButton6 + menuPanelButtonSpacing + menuPanelButtonSize.width;
	const int rightMarginMenuButton4 = rightMarginMenuButton5 + menuPanelButtonSpacing + menuPanelButtonSize.width;
	const int rightMarginMenuButton3 = rightMarginMenuButton4 + menuPanelButtonSpacing + menuPanelButtonSize.width;
	const int rightMarginMenuButton2 = rightMarginMenuButton3 + menuPanelButtonSpacing + menuPanelButtonSize.width;
	const int rightMarginMenuButton1 = rightMarginMenuButton2 + menuPanelButtonSpacing + menuPanelButtonSize.width;
	constexpr int MenuPanelArtWidth = 535;


	Rectangle &charButtonArea = VirtualGamepadState.menuPanel.charButton.area;
	charButtonArea.position.x = gnScreenWidth - rightMarginMenuButton1 * menuButtonWidth / menuPanelButtonSize.width;
	charButtonArea.position.y = menuPanelTopMargin * menuButtonWidth / menuPanelButtonSize.width;
	charButtonArea.size.width = menuButtonWidth;
	charButtonArea.size.height = menuPanelButtonSize.height * menuButtonWidth / menuPanelButtonSize.width;

	Rectangle &questsButtonArea = VirtualGamepadState.menuPanel.questsButton.area;
	questsButtonArea.position.x = gnScreenWidth - rightMarginMenuButton2 * menuButtonWidth / menuPanelButtonSize.width;
	questsButtonArea.position.y = menuPanelTopMargin * menuButtonWidth / menuPanelButtonSize.width;
	questsButtonArea.size.width = menuButtonWidth;
	questsButtonArea.size.height = menuPanelButtonSize.height * menuButtonWidth / menuPanelButtonSize.width;

	Rectangle &inventoryButtonArea = VirtualGamepadState.menuPanel.inventoryButton.area;
	inventoryButtonArea.position.x = gnScreenWidth - rightMarginMenuButton3 * menuButtonWidth / menuPanelButtonSize.width;
	inventoryButtonArea.position.y = menuPanelTopMargin * menuButtonWidth / menuPanelButtonSize.width;
	inventoryButtonArea.size.width = menuButtonWidth;
	inventoryButtonArea.size.height = menuPanelButtonSize.height * menuButtonWidth / menuPanelButtonSize.width;

	Rectangle &spellbookButtonArea = VirtualGamepadState.menuPanel.spellbookButton.area;
	spellbookButtonArea.position.x = gnScreenWidth - rightMarginMenuButton4 * menuButtonWidth / menuPanelButtonSize.width;
	spellbookButtonArea.position.y = menuPanelTopMargin * menuButtonWidth / menuPanelButtonSize.width;
	spellbookButtonArea.size.width = menuButtonWidth;
	spellbookButtonArea.size.height = menuPanelButtonSize.height * menuButtonWidth / menuPanelButtonSize.width;

	Rectangle &menuButtonArea = VirtualGamepadState.menuPanel.menuButton.area;
	menuButtonArea.position.x = gnScreenWidth - rightMarginMenuButton5 * menuButtonWidth / menuPanelButtonSize.width;
	menuButtonArea.position.y = menuPanelTopMargin * menuButtonWidth / menuPanelButtonSize.width;
	menuButtonArea.size.width = menuButtonWidth;
	menuButtonArea.size.height = menuPanelButtonSize.height * menuButtonWidth / menuPanelButtonSize.width;

	Rectangle &mapButtonArea = VirtualGamepadState.menuPanel.mapButton.area;
	mapButtonArea.position.x = gnScreenWidth - rightMarginMenuButton6 * menuButtonWidth / menuPanelButtonSize.width;
	mapButtonArea.position.y = menuPanelTopMargin * menuButtonWidth / menuPanelButtonSize.width;
	mapButtonArea.size.width = menuButtonWidth;
	mapButtonArea.size.height = menuPanelButtonSize.height * menuButtonWidth / menuPanelButtonSize.width;

	Rectangle &menuPanelArea = VirtualGamepadState.menuPanel.area;
	menuPanelArea.position.x = gnScreenWidth - MenuPanelArtWidth * menuButtonWidth / menuPanelButtonSize.width;
	menuPanelArea.position.y = 0;
	menuPanelArea.size.width = MenuPanelArtWidth * menuButtonWidth / menuPanelButtonSize.width;
	menuPanelArea.size.height = 162 * menuButtonWidth / menuPanelButtonSize.width;

	VirtualDirectionPad &directionPad = VirtualGamepadState.directionPad;
	Circle &directionPadArea = directionPad.area;
	directionPadArea.position.x = inputMargin + directionPadSize / 2;
	directionPadArea.position.y = gnScreenHeight - inputMargin - directionPadSize / 2;
	directionPadArea.radius = directionPadSize / 2;
	directionPad.position = directionPadArea.position;

	const int standButtonDiagonalOffset = directionPadArea.radius + (padButtonSpacing / 2) + (padButtonSize / 2);
	const int standButtonOffset = roundToInt(standButtonDiagonalOffset / sqrt2);
	Circle &standButtonArea = VirtualGamepadState.standButton.area;
	standButtonArea.position.x = directionPadArea.position.x - standButtonOffset;
	standButtonArea.position.y = directionPadArea.position.y + standButtonOffset;
	standButtonArea.radius = padButtonSize / 2;

	// DETH: a big attack button (A) lifted off the corner, with interact (X), spells (Y) and back (B)
	// in an arc around it. Everything can be moved and resized in the touch layout editor.
	const int bigButtonSize = roundToInt(padButtonSize * 1.45F);
	const int attackX = gnScreenWidth - roundToInt(1.6F * inputMargin) - bigButtonSize / 2;
	const int attackY = gnScreenHeight - roundToInt(1.4F * inputMargin) - bigButtonSize / 2;
	const int arcRadius = bigButtonSize / 2 + padButtonSpacing + padButtonSize / 2;
	const auto onArc = [&](Circle &circle, float degrees) {
		const float radians = degrees * 3.14159265F / 180.0F;
		circle.position = { attackX + roundToInt(arcRadius * std::cos(radians)), attackY - roundToInt(arcRadius * std::sin(radians)) };
		circle.radius = padButtonSize / 2;
	};

	Circle &primaryActionButtonArea = VirtualGamepadState.primaryActionButton.area;
	primaryActionButtonArea.position = { attackX, attackY };
	primaryActionButtonArea.radius = bigButtonSize / 2;
	onArc(VirtualGamepadState.secondaryActionButton.area, 180.0F);
	onArc(VirtualGamepadState.spellActionButton.area, 135.0F);
	onArc(VirtualGamepadState.cancelButton.area, 90.0F);

	VirtualPadButton &healthButton = VirtualGamepadState.healthButton;
	Circle &healthButtonArea = healthButton.area;
	healthButtonArea.position.x = directionPad.area.position.x - (padButtonSize + padButtonSpacing) / 2;
	healthButtonArea.position.y = directionPad.area.position.y - (directionPadSize + padButtonSize + padButtonSpacing) / 2;
	healthButtonArea.radius = padButtonSize / 2;
	healthButton.isUsable = []() { return !CharFlag && !QuestLogIsOpen; };

	VirtualPadButton &manaButton = VirtualGamepadState.manaButton;
	Circle &manaButtonArea = manaButton.area;
	manaButtonArea.position.x = directionPad.area.position.x + (padButtonSize + padButtonSpacing) / 2;
	manaButtonArea.position.y = directionPad.area.position.y - (directionPadSize + padButtonSize + padButtonSpacing) / 2;
	manaButtonArea.radius = padButtonSize / 2;
	manaButton.isUsable = []() { return !CharFlag && !QuestLogIsOpen; };

	// DETH: the player's own positions and sizes from the touch layout editor.
	ApplyTouchLayout();
}

void ActivateVirtualGamepad()
{
	VirtualGamepadState.isActive = true;
}

void DeactivateVirtualGamepad()
{
	VirtualGamepadState.Deactivate();
	DeactivateTouchEventHandlers();
}

void VirtualGamepad::Deactivate()
{
	isActive = false;

	menuPanel.Deactivate();
	directionPad.Deactivate();
	standButton.Deactivate();

	primaryActionButton.Deactivate();
	secondaryActionButton.Deactivate();
	spellActionButton.Deactivate();
	cancelButton.Deactivate();

	healthButton.Deactivate();
	manaButton.Deactivate();
}

void VirtualMenuPanel::Deactivate()
{
	charButton.Deactivate();
	questsButton.Deactivate();
	inventoryButton.Deactivate();
	spellbookButton.Deactivate();
	menuButton.Deactivate();
	mapButton.Deactivate();
}

void VirtualDirectionPad::UpdatePosition(Point touchCoordinates)
{
	position = touchCoordinates;

	const Displacement diff = position - area.position;
	if (diff == Displacement { 0, 0 }) {
		isUpPressed = false;
		isDownPressed = false;
		isLeftPressed = false;
		isRightPressed = false;
		return;
	}

	if (!area.contains(position)) {
		int x = diff.deltaX;
		int y = diff.deltaY;
		const float dist = sqrtf(static_cast<float>((x * x) + (y * y)));
		x = roundToInt(x * area.radius / dist);
		y = roundToInt(y * area.radius / dist);
		position.x = area.position.x + x;
		position.y = area.position.y + y;
	}

	const float angle = atan2f(static_cast<float>(-diff.deltaY), static_cast<float>(diff.deltaX));

	isUpPressed = PointsUp(angle);
	isDownPressed = PointsDown(angle);
	isLeftPressed = PointsLeft(angle);
	isRightPressed = PointsRight(angle);
}

void VirtualDirectionPad::Deactivate()
{
	position = area.position;
	isUpPressed = false;
	isDownPressed = false;
	isLeftPressed = false;
	isRightPressed = false;
}

void VirtualButton::Deactivate()
{
	isHeld = false;
	didStateChange = false;
}

} // namespace devilution
