/**
 * @file deth_screen.hpp
 *
 * DETH: layout of a themed pre-game screen (title, optional side pane, content, button row)
 * and helpers that add the shared items to a screen's item list.
 */
#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "DiabloUI/deth_theme.hpp"
#include "DiabloUI/ui_item.h"
#include "engine/rectangle.hpp"

namespace devilution {

namespace theme {

struct ScreenFrame {
	Layout layout;
	/** The whole screen area used by the screen (centered column). */
	Rectangle frame;
	/** Title text area at the top; an ornament sits just under it. */
	Rectangle title;
	/** Left pane (portrait, stats); zero width when the screen has none. */
	Rectangle side;
	/** Content area (lists, text fields), to the right of the side pane. */
	Rectangle content;
	/** Button row at the bottom (zero height when there are no buttons). */
	Rectangle buttons;
	/** Touch mode: the Back button in the top-left corner (zero size otherwise). */
	Rectangle back;
	/** Number of list rows that fit in the content area, below `contentTop` lines of header. */
	int RowsThatFit(int headerHeight) const;
	/** Row pitch (row height plus gap). */
	int RowPitch() const;
};

/** Lays out a screen. `wide` uses the wider column; `sidePane` reserves the left pane; `buttons` reserves the bottom row. */
ScreenFrame MakeScreenFrame(bool wide, bool sidePane, bool buttons);

/** Adds the title (static text) and the ornament under it. */
void AddTitle(std::vector<std::unique_ptr<UiItemBase>> &items, const char *text, const ScreenFrame &frame, bool withBack = true);
/** Adds a title that follows a pointer (screens that change their title). */
void AddTitle(std::vector<std::unique_ptr<UiItemBase>> &items, const char **textPointer, const ScreenFrame &frame, bool withBack = true);

/**
 * The touch Back button (top-left). It must be in the same item list that is passed to UiInitList,
 * since only those items receive taps.
 */
void AddBackButton(std::vector<std::unique_ptr<UiItemBase>> &items, const ScreenFrame &frame);

/** A heading inside the content area (smaller than the title). Returns the height used. */
int AddHeading(std::vector<std::unique_ptr<UiItemBase>> &items, const char *text, const ScreenFrame &frame, int y);

/** Small wrapped text inside the content area (hints under a heading). Returns the height used. */
int AddNote(std::vector<std::unique_ptr<UiItemBase>> &items, std::string_view text, const ScreenFrame &frame, int y);

struct ButtonSpec {
	std::string_view text;
	void (*action)();
	UiFlags extraFlags = UiFlags::None;
};

/** Adds evenly spaced themed buttons in the button row. Returns pointers in the same order. */
std::vector<UiArtTextButton *> AddButtons(std::vector<std::unique_ptr<UiItemBase>> &items, const ScreenFrame &frame, const std::vector<ButtonSpec> &specs);

/** DETH: the same frame with its button row moved up to `y` under `column` (screens with a text field,
 * so OK and Cancel stay above an on-screen keyboard). */
ScreenFrame ButtonsBelow(const ScreenFrame &frame, const Rectangle &column, int y);

/** Builds a themed list filling the content area from `y` with up to `maxRows` visible rows. */
std::unique_ptr<UiList> MakeList(const std::vector<std::unique_ptr<UiListItem>> &listItems, const ScreenFrame &frame, int y, size_t maxRows, UiFlags flags, int extraRowHeight = 0);

/** Adds a slim themed scrollbar item next to a list (no art). */
void AddScrollbar(std::vector<std::unique_ptr<UiItemBase>> &items, const ScreenFrame &frame, int y, int height);

} // namespace theme

} // namespace devilution
