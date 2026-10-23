#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include <function_ref.hpp>

#include "DiabloUI/diabloui.h"
#include "DiabloUI/deth_screen.hpp"
#include "DiabloUI/scrollbar.h"
#include "DiabloUI/ui_flags.hpp"
#include "DiabloUI/ui_item.h"
#include "controls/controller.h"
#include "controls/controller_buttons.h"
#include "controls/controller_motion.h"
#include "controls/plrctrls.h"
#include "controls/remap_keyboard.h"
#include "engine/assets.hpp"
#include "engine/rectangle.hpp"
#include "engine/render/text_render.hpp"
#include "game_mode.hpp"
#include "hwcursor.hpp"
#include "items.h"
#include "options.h"
#include "utils/enum_traits.h"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"
#include "utils/sdl_geometry.h"
#include "utils/static_vector.hpp"
#include "utils/str_cat.hpp"
#include "utils/ui_fwd.h"
#include "utils/utf8.hpp"

namespace devilution {
namespace {

constexpr size_t IndexKeyOrPadInput = 1;
constexpr size_t IndexPadTimerText = 2;

bool endMenu = false;
bool backToMain = false;

std::vector<std::unique_ptr<UiListItem>> vecDialogItems;
std::vector<std::unique_ptr<UiItemBase>> vecDialog;
std::vector<OptionEntryBase *> vecOptions;

/** DETH touch sheets: the Settings page drawn dimmed behind a sub-level, and the stepped value editor. */
std::vector<std::unique_ptr<UiItemBase>> vecBehind;
std::vector<std::unique_ptr<UiListItem>> vecBehindItems;
std::vector<OptionEntryBase *> vecBehindOptions;
size_t behindSelected = 0;
size_t behindOffset = 0;
bool sheetActive = false;
bool steppedSheet = false;
int steppedIndex = 0;
int steppedCount = 0;
char steppedValueText[64];

OptionCategoryBase *selectedCategory = nullptr;
OptionEntryBase *selectedOption = nullptr;

void StepValue(int delta)
{
	auto *pOptionList = static_cast<OptionEntryListBase *>(selectedOption);
	steppedIndex = std::clamp(steppedIndex + delta, 0, steppedCount - 1);
	CopyUtf8(steppedValueText, pOptionList->GetListDescription(static_cast<size_t>(steppedIndex)), sizeof(steppedValueText));
}
void StepDown() { StepValue(-1); }
void StepUp() { StepValue(1); }
void StepTo(int index) { StepValue(index - steppedIndex); }

enum class ShownMenuType : uint8_t {
	Categories,
	Settings,
	ListOption,
	KeyInput,
	PadInput,
};

ShownMenuType shownMenu;

char optionDescription[512];

Rectangle rectList;
Rectangle rectDescription;

enum class SpecialMenuEntry : int8_t {
	None = -1,
	PreviousMenu = -2,
	UnbindKey = -3,
	BindPadButton = -4,
	UnbindPadButton = -5,
};

ControllerButtonCombo padEntryCombo {};
Uint32 padEntryStartTime = 0;
std::string padEntryTimerText;

bool IsValidEntry(OptionEntryBase *pOptionEntry)
{
	auto flags = pOptionEntry->GetFlags();
	if (HasAnyOf(flags, OptionEntryFlags::NeedDiabloMpq) && !HaveIntro())
		return false;
	if (gbRunGame && HasAnyOf(flags, OptionEntryFlags::CantChangeInGame))
		return false;
	return HasNoneOf(flags, OptionEntryFlags::Invisible | (gbIsHellfire ? OptionEntryFlags::OnlyDiablo : OptionEntryFlags::OnlyHellfire));
}

/** DETH: fills the value side of a themed settings row (toggle for booleans, text otherwise). */
void UpdateRowValue(UiListItem &item, OptionEntryBase *pEntry)
{
	if (pEntry->GetType() == OptionEntryType::Boolean) {
		item.toggle = **static_cast<OptionEntryBoolean *>(pEntry) ? 1 : 0;
		item.rightText.clear();
	} else {
		item.toggle = -1;
		item.rightText = std::string(pEntry->GetValueDescription());
	}
}

void CleanUpSettingsUI()
{
	UiInitList_clear();
	UiThemeBackground = false;
	vecBehind.clear();
	vecBehindItems.clear();
	vecBehindOptions.clear();
	sheetActive = false;

	vecDialogItems.clear();
	vecDialog.clear();
	vecOptions.clear();

	ArtBackground = std::nullopt;
	ArtBackgroundWidescreen = std::nullopt;
	UnloadScrollBar();
}

void GoBackOneMenuLevel()
{
	endMenu = true;
	switch (shownMenu) {
	case ShownMenuType::Categories:
		backToMain = true;
		break;
	case ShownMenuType::Settings:
		shownMenu = ShownMenuType::Categories;
		break;
	default:
		shownMenu = ShownMenuType::Settings;
		break;
	}
}

void StartPadEntryTimer()
{
	padEntryCombo = ControllerButton_NONE;
	padEntryStartTime = SDL_GetTicks();
	if (padEntryStartTime == 0)
		padEntryStartTime++;
	// Removes access to these dialog items while entering bindings
	for (size_t i = IndexPadTimerText + 1; i < vecDialogItems.size(); i++)
		vecDialogItems[i]->uiFlags |= UiFlags::ElementHidden;
}

void StopPadEntryTimer()
{
	padEntryCombo = ControllerButton_NONE;
	padEntryStartTime = 0;
	padEntryTimerText = "";
	vecDialogItems[IndexPadTimerText]->m_text = padEntryTimerText;
	// Restores access to these dialog items after binding is complete
	for (size_t i = IndexPadTimerText + 1; i < vecDialogItems.size(); i++)
		vecDialogItems[i]->uiFlags &= ~UiFlags::ElementHidden;
}

void UpdatePadEntryTimerText()
{
	if (shownMenu != ShownMenuType::PadInput)
		return;
	const Uint32 elapsed = SDL_GetTicks() - padEntryStartTime;
	if (padEntryStartTime == 0 || elapsed > 10000) {
		StopPadEntryTimer();
		return;
	}
	padEntryTimerText = StrCat(_("Press gamepad buttons to change."), " ", 10 - (elapsed / 1000));
	vecDialogItems[IndexPadTimerText]->m_text = padEntryTimerText;
}

void UpdateDescription(const OptionEntryBase &option)
{
	auto paragraphs = WordWrapString(option.GetDescription(), rectDescription.size.width, GameFont12, 1);
	CopyUtf8(optionDescription, paragraphs, sizeof(optionDescription));
}

void UpdateDescription(const OptionCategoryBase &category)
{
	auto paragraphs = WordWrapString(category.GetDescription(), rectDescription.size.width, GameFont12, 1);
	CopyUtf8(optionDescription, paragraphs, sizeof(optionDescription));
}

void ItemFocused(size_t value)
{
	switch (shownMenu) {
	case ShownMenuType::Categories: {
		auto &vecItem = vecDialogItems[value];
		optionDescription[0] = '\0';
		if (vecItem->m_value < 0)
			return;
		auto *pCategory = GetOptions().GetCategories()[vecItem->m_value];
		UpdateDescription(*pCategory);
	} break;
	case ShownMenuType::Settings: {
		auto &vecItem = vecDialogItems[value];
		optionDescription[0] = '\0';
		if (vecItem->m_value < 0)
			return;
		auto *pOption = vecOptions[vecItem->m_value];
		UpdateDescription(*pOption);
	} break;
	default:
		break;
	}
}


bool ChangeOptionValue(OptionEntryBase *pOption, size_t listIndex)
{
	if (HasAnyOf(pOption->GetFlags(), OptionEntryFlags::RecreateUI)) {
		endMenu = true;
		// Clean up all UI related Data
		CleanUpSettingsUI();
		if (!gbRunGame) {
			UnloadUiGFX();
			FreeItemGFX();
		}
		selectedOption = pOption;
	}

	switch (pOption->GetType()) {
	case OptionEntryType::Boolean: {
		auto *pOptionBoolean = static_cast<OptionEntryBoolean *>(pOption);
		pOptionBoolean->SetValue(!**pOptionBoolean);
	} break;
	case OptionEntryType::List: {
		auto *pOptionList = static_cast<OptionEntryListBase *>(pOption);
		pOptionList->SetActiveListIndex(listIndex);
	} break;
	case OptionEntryType::Key:
	case OptionEntryType::PadButton:
		break;
	}

	if (HasAnyOf(pOption->GetFlags(), OptionEntryFlags::RecreateUI)) {
		// Reinitialize UI with changed settings (for example game mode, language or resolution)
		if (!gbRunGame) {
			UiInitialize();
			InitItemGFX();
		}
		SetHardwareCursor(CursorInfo::UnknownCursor());
		return false;
	}

	return true;
}

void EscPressed();

void ItemSelected(size_t value)
{
	auto &vecItem = vecDialogItems[value];
	const int vecItemValue = vecItem->m_value;
	if (vecItemValue < 0) {
		auto specialMenuEntry = static_cast<SpecialMenuEntry>(vecItemValue);
		switch (specialMenuEntry) {
		case SpecialMenuEntry::None:
			break;
		case SpecialMenuEntry::PreviousMenu:
			GoBackOneMenuLevel();
			break;
		case SpecialMenuEntry::UnbindKey: {
			auto *pOptionKey = static_cast<KeymapperOptions::Action *>(selectedOption);
			pOptionKey->SetValue(SDLK_UNKNOWN);
			vecDialogItems[IndexKeyOrPadInput]->m_text = selectedOption->GetValueDescription();
			break;
		}
		case SpecialMenuEntry::BindPadButton:
			StartPadEntryTimer();
			break;
		case SpecialMenuEntry::UnbindPadButton:
			auto *pOptionPad = static_cast<PadmapperOptions::Action *>(selectedOption);
			pOptionPad->SetValue(ControllerButton_NONE);
			vecDialogItems[IndexKeyOrPadInput]->m_text = selectedOption->GetValueDescription();
			break;
		}
		return;
	}

	switch (shownMenu) {
	case ShownMenuType::Categories: {
		selectedCategory = GetOptions().GetCategories()[vecItemValue];
		endMenu = true;
		shownMenu = ShownMenuType::Settings;
	} break;
	case ShownMenuType::Settings: {
		auto *pOption = vecOptions[vecItemValue];
		bool updateValueDescription = false;
		if (pOption->GetType() == OptionEntryType::List) {
			auto *pOptionList = static_cast<OptionEntryListBase *>(pOption);
			if (pOptionList->GetListSize() > 2) {
				selectedOption = pOption;
				endMenu = true;
				shownMenu = ShownMenuType::ListOption;
			} else {
				// If the list contains only two items, we don't show a submenu and instead change the option value instantly
				size_t nextIndex = pOptionList->GetActiveListIndex() + 1;
				if (nextIndex >= pOptionList->GetListSize())
					nextIndex = 0;
				updateValueDescription = ChangeOptionValue(pOption, nextIndex);
			}
		} else if (pOption->GetType() == OptionEntryType::Key) {
			selectedOption = pOption;
			endMenu = true;
			shownMenu = ShownMenuType::KeyInput;
		} else if (pOption->GetType() == OptionEntryType::PadButton) {
			selectedOption = pOption;
			endMenu = true;
			shownMenu = ShownMenuType::PadInput;
		} else {
			updateValueDescription = ChangeOptionValue(pOption, 0);
		}
		if (updateValueDescription)
			UpdateRowValue(*vecItem, pOption);
	} break;
	case ShownMenuType::ListOption: {
		if (steppedSheet) {
			// The value row is not a choice; Back applies the stepped value.
			EscPressed();
			break;
		}
		ChangeOptionValue(selectedOption, vecItemValue);
		GoBackOneMenuLevel();
	} break;
	case ShownMenuType::KeyInput:
	case ShownMenuType::PadInput:
		break;
	}
}

void EscPressed()
{
	if (steppedSheet) {
		steppedSheet = false;
		auto *pOptionList = static_cast<OptionEntryListBase *>(selectedOption);
		if (static_cast<size_t>(steppedIndex) != pOptionList->GetActiveListIndex())
			ChangeOptionValue(selectedOption, static_cast<size_t>(steppedIndex));
	}
	GoBackOneMenuLevel();
}

void FullscreenChanged()
{
	auto *fullscreenOption = &GetOptions().Graphics.fullscreen;

	for (auto &vecItem : vecDialogItems) {
		const int vecItemValue = vecItem->m_value;
		if (vecItemValue < 0 || static_cast<size_t>(vecItemValue) >= vecOptions.size())
			continue;

		auto *pOption = vecOptions[vecItemValue];
		if (pOption != fullscreenOption)
			continue;

		UpdateRowValue(*vecItem, pOption);
		break;
	}
}

} // namespace

/** The first sentence of a description, for the one-line hint under a touch row. */
std::string FirstSentence(std::string_view text)
{
	const size_t end = text.find(". ");
	if (end != std::string_view::npos)
		return std::string(text.substr(0, end + 1));
	const size_t newline = text.find('\n');
	if (newline != std::string_view::npos)
		return std::string(text.substr(0, newline));
	return std::string(text);
}

/** Builds the Settings page (rows of the selected category) into the "behind" vectors for a sheet. */
void BuildBehindPage(const theme::ScreenFrame &frame)
{
	vecBehind.clear();
	vecBehindItems.clear();
	vecBehindOptions.clear();
	theme::AddTitle(vecBehind, selectedCategory->GetName().data(), frame);
	behindSelected = 0;
	for (OptionEntryBase *pEntry : selectedCategory->GetEntries()) {
		if (!IsValidEntry(pEntry))
			continue;
		if (selectedOption == pEntry)
			behindSelected = vecBehindItems.size();
		auto row = std::make_unique<UiListItem>(pEntry->GetName(), static_cast<int>(vecBehindOptions.size()), UiFlags::ColorUiGold);
		UpdateRowValue(*row, pEntry);
		row->subText = FirstSentence(pEntry->GetDescription());
		vecBehindItems.push_back(std::move(row));
		vecBehindOptions.push_back(pEntry);
	}
	const int listTop = frame.content.position.y;
	const size_t visibleRows = std::max<size_t>(1, static_cast<size_t>(frame.content.size.height / (frame.RowPitch() + theme::SubTextHeight())));
	auto list = theme::MakeList(vecBehindItems, frame, listTop, visibleRows, UiFlags::FontSize24, theme::SubTextHeight());
	const size_t shown = std::min(visibleRows, vecBehindItems.size());
	behindOffset = behindSelected >= shown ? behindSelected - shown + 1 : 0;
	vecBehind.push_back(std::move(list));
}

/** Draws the behind page with its own selection state (the list renderer reads globals). */
void RenderBehindPage()
{
	const size_t savedSelected = SelectedItem;
	const size_t savedOffset = listOffset;
	const int savedPixelOffset = listPixelOffset;
	listPixelOffset = 0;
	const size_t savedMax = SelectedItemMax;
	const size_t savedViewport = ListViewportSize;
	SelectedItem = behindSelected;
	listOffset = behindOffset;
	SelectedItemMax = vecBehindItems.empty() ? 0 : vecBehindItems.size() - 1;
	for (const auto &item : vecBehind) {
		if (item->IsType(UiType::List))
			ListViewportSize = static_cast<const UiList *>(item.get())->viewportSize;
	}
	UiRenderItems(vecBehind);
	SelectedItem = savedSelected;
	listOffset = savedOffset;
	listPixelOffset = savedPixelOffset;
	SelectedItemMax = savedMax;
	ListViewportSize = savedViewport;
	theme::DrawDim(Surface(DiabloUiSurface()));
}

/** Lays out the current sub-level (vecDialogItems) as a centered sheet with a Back button. */
void BuildSheet(const theme::ScreenFrame &frame, size_t &itemToSelect)
{
	const theme::Layout &layout = frame.layout;
	const int width = std::min(layout.wideColumn.size.width, 640);
	const int x = (gnScreenWidth - width) / 2;
	const int pad = 16;
	const int titleHeight = 44;
	const int descriptionLineHeight = IsSmallFontTall() ? 20 : 18;
	const std::string wrappedDescription = WordWrapString(selectedOption->GetDescription(), width - 2 * pad, GameFont12, 1);
	const int descriptionLines = std::clamp(static_cast<int>(std::count(wrappedDescription.begin(), wrappedDescription.end(), '\n')) + 1, 1, 4);
	const int descriptionHeight = descriptionLineHeight * descriptionLines;
	const int buttonHeight = std::clamp(layout.rowHeight, 40, 56);
	const int pitch = frame.RowPitch();

	int bodyHeight;
	size_t visibleRows = 1;
	if (steppedSheet) {
		bodyHeight = pitch + 20 + 40;
	} else {
		const int maxBody = gnScreenHeight - 2 * layout.margin - pad * 2 - titleHeight - 6 - descriptionHeight - 10 - buttonHeight - 10;
		visibleRows = std::max<size_t>(1, std::min(vecDialogItems.size(), static_cast<size_t>(std::max(1, maxBody / pitch))));
		bodyHeight = static_cast<int>(visibleRows) * pitch;
	}
	const int height = pad + titleHeight + 6 + descriptionHeight + 10 + bodyHeight + 10 + buttonHeight + pad;
	const int y = std::max(layout.margin / 2, (gnScreenHeight - height) / 2);

	vecDialog.push_back(std::make_unique<UiPanel>(MakeSdlRect(x, y, width, height), /*opaque=*/true));
	int cy = y + pad;
	vecDialog.push_back(std::make_unique<UiArtText>(selectedOption->GetName().data(), MakeSdlRect(x + pad, cy, width - 2 * pad, titleHeight - 8), UiFlags::FontSize30 | UiFlags::ColorUiGold | UiFlags::AlignCenter | UiFlags::VerticalCenter));
	vecDialog.push_back(std::make_unique<UiOrnament>(MakeSdlRect(x + width / 4, cy + titleHeight - 6, width / 2, 6)));
	cy += titleHeight + 6;
	rectDescription = { { x + pad, cy }, Size { width - 2 * pad, descriptionHeight } };
	UpdateDescription(*selectedOption);
	vecDialog.push_back(std::make_unique<UiArtText>(optionDescription, MakeSdlRect(rectDescription), UiFlags::FontSize12 | UiFlags::ColorUiSilverDark | UiFlags::AlignCenter, 1, descriptionLineHeight));
	cy += descriptionHeight + 10;

	if (steppedSheet) {
		auto *pOptionList = static_cast<OptionEntryListBase *>(selectedOption);
		steppedCount = static_cast<int>(pOptionList->GetListSize());
		steppedIndex = static_cast<int>(pOptionList->GetActiveListIndex());
		StepValue(0);
		// [ - ]   value   [ + ]
		const int buttonWidth = std::max(buttonHeight, 120);
		const int rowHeight = layout.rowHeight;
		auto minus = std::make_unique<UiArtTextButton>(_("Less"), &StepDown, MakeSdlRect(x + pad, cy, buttonWidth, rowHeight), UiFlags::FontSize24 | UiFlags::ColorUiGold);
		minus->themed = true;
		vecDialog.push_back(std::move(minus));
		auto plus = std::make_unique<UiArtTextButton>(_("More"), &StepUp, MakeSdlRect(x + width - pad - buttonWidth, cy, buttonWidth, rowHeight), UiFlags::FontSize24 | UiFlags::ColorUiGold);
		plus->themed = true;
		vecDialog.push_back(std::move(plus));
		vecDialogItems.clear();
		vecDialogItems.push_back(std::make_unique<UiListItem>(std::string_view(steppedValueText), static_cast<int>(SpecialMenuEntry::None), UiFlags::ColorUiGold));
		auto list = std::make_unique<UiList>(vecDialogItems, 1, static_cast<Sint16>(x + pad + buttonWidth + 12), static_cast<Sint16>(cy), static_cast<Uint16>(width - 2 * pad - 2 * buttonWidth - 24), static_cast<Uint16>(rowHeight), UiFlags::FontSize30 | UiFlags::ColorUiGold | UiFlags::AlignCenter, 0);
		list->themed = true;
		vecDialog.push_back(std::move(list));
		cy += pitch + 10;
		// Tall enough to be a comfortable finger target; the bar itself is drawn centered.
		vecDialog.push_back(std::make_unique<UiGauge>(MakeSdlRect(x + pad + 8, cy, width - 2 * pad - 16, 40), &steppedIndex, &steppedCount, &StepTo));
		cy += 40;
		itemToSelect = 0;
	} else {
		const int listWidth = width - 2 * pad - (visibleRows < vecDialogItems.size() ? 18 : 0);
		auto list = std::make_unique<UiList>(vecDialogItems, visibleRows, static_cast<Sint16>(x + pad), static_cast<Sint16>(cy), static_cast<Uint16>(listWidth), static_cast<Uint16>(pitch), UiFlags::FontSize24 | UiFlags::AlignCenter, layout.rowGap);
		list->themed = true;
		rectList = { { list->m_rect.x, list->m_rect.y }, Size { list->m_rect.w, list->m_rect.h } };
		vecDialog.push_back(std::move(list));
		if (visibleRows < vecDialogItems.size())
			vecDialog.push_back(std::make_unique<UiThemeScrollbar>(MakeSdlRect(x + width - pad - 12, cy, 12, bodyHeight)));
		cy += bodyHeight;
	}
	cy += 10;
	auto back = std::make_unique<UiArtTextButton>(_("Back"), &UiFocusNavigationEsc, MakeSdlRect(x + width / 4, cy, width / 2, buttonHeight), UiFlags::FontSize24 | UiFlags::ColorUiGold);
	back->themed = true;
	vecDialog.push_back(std::move(back));
}

void UiSettingsMenu()
{
	backToMain = false;
	shownMenu = ShownMenuType::Categories;
	selectedCategory = nullptr;
	selectedOption = nullptr;

	do {
		endMenu = false;

		UiLoadBlackBackground();
		UiThemeBackground = true;
		LoadScrollBar();

		// Touch: the Back button sits in the corner and descriptions are inline, so no bottom row or panel.
		const bool touch = theme::TouchMode();
		const theme::ScreenFrame frame = theme::MakeScreenFrame(/*wide=*/true, /*sidePane=*/false, /*buttons=*/!touch);
		const int descriptionLineHeight = IsSmallFontTall() ? 20 : 18;
		const int descriptionHeight = touch ? 0 : descriptionLineHeight * 3 + 8;
		const int touchExtraRowHeight = touch ? theme::SubTextHeight() : 0;

		optionDescription[0] = '\0';

		std::string_view titleText;
		switch (shownMenu) {
		case ShownMenuType::Categories:
			titleText = _("Settings");
			break;
		case ShownMenuType::Settings:
			titleText = selectedCategory->GetName();
			break;
		default:
			titleText = selectedOption->GetName();
			break;
		}
		theme::AddTitle(vecDialog, titleText.data(), frame);

		size_t itemToSelect = 0;
		std::optional<tl::function_ref<bool(SDL_Event &)>> eventHandler;

		switch (shownMenu) {
		case ShownMenuType::Categories: {
			size_t catIndex = 0;
			for (OptionCategoryBase *pCategory : GetOptions().GetCategories()) {
				for (OptionEntryBase *pEntry : pCategory->GetEntries()) {
					if (!IsValidEntry(pEntry))
						continue;
					if (selectedCategory == pCategory)
						itemToSelect = vecDialogItems.size();
					auto row = std::make_unique<UiListItem>(pCategory->GetName(), static_cast<int>(catIndex), UiFlags::ColorUiGold);
					if (touch)
						row->subText = std::string(pCategory->GetDescription());
					vecDialogItems.push_back(std::move(row));
					break;
				}
				catIndex++;
			}
		} break;
		case ShownMenuType::Settings: {
			for (OptionEntryBase *pEntry : selectedCategory->GetEntries()) {
				if (!IsValidEntry(pEntry))
					continue;
				if (selectedOption == pEntry)
					itemToSelect = vecDialogItems.size();
				const int optionId = static_cast<int>(vecOptions.size());
				auto row = std::make_unique<UiListItem>(pEntry->GetName(), optionId, UiFlags::ColorUiGold);
				UpdateRowValue(*row, pEntry);
				if (touch)
					row->subText = FirstSentence(pEntry->GetDescription());
				vecDialogItems.push_back(std::move(row));
				vecOptions.push_back(pEntry);
			}
		} break;
		case ShownMenuType::ListOption: {
			auto *pOptionList = static_cast<OptionEntryListBase *>(selectedOption);
			for (size_t i = 0; i < pOptionList->GetListSize(); i++) {
				vecDialogItems.push_back(std::make_unique<UiListItem>(pOptionList->GetListDescription(i), static_cast<int>(i), UiFlags::ColorUiGold));
			}
			itemToSelect = pOptionList->GetActiveListIndex();
			UpdateDescription(*pOptionList);
		} break;
		case ShownMenuType::KeyInput: {
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("Bound key:"), static_cast<int>(SpecialMenuEntry::None), UiFlags::ColorWhitegold | UiFlags::ElementDisabled));
			vecDialogItems.push_back(std::make_unique<UiListItem>(std::string(selectedOption->GetValueDescription()), static_cast<int>(SpecialMenuEntry::None), UiFlags::ColorUiGold));
			assert(IndexKeyOrPadInput == vecDialogItems.size() - 1);
			itemToSelect = IndexKeyOrPadInput;
			eventHandler = [](SDL_Event &event) {
				if (SelectedItem != IndexKeyOrPadInput)
					return false;
				uint32_t key = SDLK_UNKNOWN;
				switch (event.type) {
				case SDL_EVENT_KEY_DOWN: {
					SDL_Keycode keycode = SDLC_EventKey(event);
					remap_keyboard_key(&keycode);
					key = static_cast<uint32_t>(keycode);
					if (key >= SDLK_A && key <= SDLK_Z) {
						key -= 'a' - 'A';
					}
				} break;
				case SDL_EVENT_MOUSE_BUTTON_DOWN:
					switch (event.button.button) {
					case SDL_BUTTON_MIDDLE:
					case SDL_BUTTON_X1:
					case SDL_BUTTON_X2:
						key = event.button.button | KeymapperMouseButtonMask;
						break;
					}
					break;
#if SDL_VERSION_ATLEAST(2, 0, 0)
				case SDL_EVENT_MOUSE_WHEEL:
					if (SDLC_EventWheelIntY(event) > 0) {
						key = MouseScrollUpButton;
					} else if (SDLC_EventWheelIntY(event) < 0) {
						key = MouseScrollDownButton;
					} else if (SDLC_EventWheelIntX(event) > 0) {
						key = MouseScrollLeftButton;
					} else if (SDLC_EventWheelIntX(event) < 0) {
						key = MouseScrollRightButton;
					}
					break;
#endif
				}
				// Ignore unknown keys
				if (key == SDLK_UNKNOWN)
					return false;
				auto *pOptionKey = static_cast<KeymapperOptions::Action *>(selectedOption);
				if (!pOptionKey->SetValue(key))
					return false;
				vecDialogItems[IndexKeyOrPadInput]->m_text = selectedOption->GetValueDescription();
				return true;
			};
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("Press any key to change."), static_cast<int>(SpecialMenuEntry::None), UiFlags::ColorUiSilver | UiFlags::ElementDisabled));
			vecDialogItems.push_back(std::make_unique<UiListItem>(std::string_view {}, static_cast<int>(SpecialMenuEntry::None), UiFlags::ElementDisabled));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("Unbind key"), static_cast<int>(SpecialMenuEntry::UnbindKey), UiFlags::ColorUiGold));
			UpdateDescription(*selectedOption);
		} break;
		case ShownMenuType::PadInput: {
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("Bound button combo:"), static_cast<int>(SpecialMenuEntry::None), UiFlags::ColorWhitegold | UiFlags::ElementDisabled));
			vecDialogItems.push_back(std::make_unique<UiListItem>(selectedOption->GetValueDescription(), static_cast<int>(SpecialMenuEntry::BindPadButton), UiFlags::ColorUiGold));
			assert(IndexKeyOrPadInput == vecDialogItems.size() - 1);
			itemToSelect = IndexKeyOrPadInput;

			vecDialogItems.push_back(std::make_unique<UiListItem>(std::string_view(padEntryTimerText), static_cast<int>(SpecialMenuEntry::None), UiFlags::ColorUiSilver | UiFlags::ElementDisabled));
			assert(IndexPadTimerText == vecDialogItems.size() - 1);

			vecDialogItems.push_back(std::make_unique<UiListItem>(std::string_view {}, static_cast<int>(SpecialMenuEntry::None), UiFlags::ElementDisabled));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("Unbind button combo"), static_cast<int>(SpecialMenuEntry::UnbindPadButton), UiFlags::ColorUiGold));

			padEntryStartTime = 0;
			eventHandler = [](SDL_Event &event) {
				if (padEntryStartTime == 0)
					return false;

				const StaticVector<ControllerButtonEvent, 4> ctrlEvents = ToControllerButtonEvents(event);
				for (const ControllerButtonEvent ctrlEvent : ctrlEvents) {
					const bool isGamepadMotion = IsControllerMotion(event);
					DetectInputMethod(event, ctrlEvent);
					if (event.type == SDL_EVENT_KEY_UP && SDLC_EventKey(event) == SDLK_ESCAPE) {
						StopPadEntryTimer();
						return true;
					}
					if (isGamepadMotion || IsAnyOf(ctrlEvent.button, ControllerButton_NONE, ControllerButton_IGNORE)) {
						continue;
					}

					const bool modifierPressed = padEntryCombo.modifier != ControllerButton_NONE && IsControllerButtonPressed(padEntryCombo.modifier);
					const bool buttonPressed = padEntryCombo.button != ControllerButton_NONE && IsControllerButtonPressed(padEntryCombo.button);
					if (ctrlEvent.up) {
						// When the player has released all relevant inputs, assume the binding is finished and stop the timer
						if (padEntryCombo.button != ControllerButton_NONE && !modifierPressed && !buttonPressed) {
							StopPadEntryTimer();
							return true;
						}
						continue;
					}

					auto *pOptionPad = static_cast<PadmapperOptions::Action *>(selectedOption);
					if (!modifierPressed && buttonPressed)
						padEntryCombo.modifier = padEntryCombo.button;
					padEntryCombo.button = ctrlEvent.button;
					if (pOptionPad->SetValue(padEntryCombo))
						vecDialogItems[IndexKeyOrPadInput]->m_text = selectedOption->GetValueDescription();
				}
				return true;
			};
			UpdateDescription(*selectedOption);
		} break;
		}

		const bool subLevel = shownMenu != ShownMenuType::Categories && shownMenu != ShownMenuType::Settings;
		sheetActive = frame.layout.touch && subLevel;
		steppedSheet = sheetActive && shownMenu == ShownMenuType::ListOption && static_cast<OptionEntryListBase *>(selectedOption)->IsStepped();
		if (sheetActive) {
			// The sub-level is a sheet over the dimmed Settings page.
			vecDialog.clear();
			BuildBehindPage(frame);
			BuildSheet(frame, itemToSelect);
		} else {
			// Description panel at the bottom of the content area; the list takes the rest.
			rectDescription = { { frame.content.position.x + 12, frame.content.position.y + frame.content.size.height - descriptionHeight },
				Size { frame.content.size.width - 24, descriptionHeight } };
			const int listTop = frame.content.position.y;
			const int listMaxHeight = touch ? frame.content.size.height : rectDescription.position.y - 10 - listTop;
			const size_t visibleRows = std::max<size_t>(1, static_cast<size_t>(listMaxHeight / (frame.RowPitch() + touchExtraRowHeight)));
			auto list = theme::MakeList(vecDialogItems, frame, listTop, visibleRows, UiFlags::FontSize24 | (shownMenu == ShownMenuType::Settings || touch ? UiFlags::None : UiFlags::AlignCenter), touchExtraRowHeight);
			rectList = { { list->m_rect.x, list->m_rect.y }, Size { list->m_rect.w, list->m_rect.h } };
			const int listHeight = list->m_rect.h;
			if (!touch)
				vecDialog.push_back(std::make_unique<UiArtText>(optionDescription, MakeSdlRect(rectDescription),
				    UiFlags::FontSize12 | UiFlags::ColorUiSilverDark | UiFlags::AlignCenter, 1, descriptionLineHeight));
			vecDialog.push_back(std::move(list));
			theme::AddScrollbar(vecDialog, frame, listTop, listHeight);
			theme::AddButtons(vecDialog, frame, { { _("Back"), &UiFocusNavigationEsc } });
		}

		UiInitList(ItemFocused, ItemSelected, EscPressed, vecDialog, true, FullscreenChanged, nullptr, itemToSelect);

		while (!endMenu) {
			UiClearScreen();
			if (sheetActive)
				RenderBehindPage();
			UpdatePadEntryTimerText();
			UiPollAndRender(eventHandler);
		}

		CleanUpSettingsUI();
	} while (!backToMain);

	SaveOptions();
}

} // namespace devilution
