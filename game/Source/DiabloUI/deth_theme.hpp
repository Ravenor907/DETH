/**
 * @file deth_theme.hpp
 *
 * DETH: procedurally drawn theme for the screens before the game (dark stone, gold, embers).
 * Everything is drawn into the 8-bit surface with colors matched from the current palette,
 * so it works with every UI palette and needs no art files.
 */
#pragma once

#include <cstdint>
#include <string_view>

#include "DiabloUI/ui_flags.hpp"
#include "engine/rectangle.hpp"
#include "engine/surface.hpp"

namespace devilution {

namespace theme {

/** Sizes of one screen, derived from the internal resolution. */
struct Layout {
	int screenWidth;
	int screenHeight;
	/** Horizontal margin from the screen edge. */
	int margin;
	/** A centered content column (menus). */
	Rectangle column;
	/** A wider centered column (settings, lists with values). */
	Rectangle wideColumn;
	/** Height of a touchable row. */
	int rowHeight;
	/** Gap between rows. */
	int rowGap;
	/** Font flags for rows and for titles. */
	UiFlags rowFont;
	UiFlags titleFont;
	/** Height of the title area at the top of the column. */
	int titleHeight;
	/** True when the screen is short (phones): tighter spacing, smaller titles. */
	bool compact;
	/** Touch rules in effect (see TouchMode()). */
	bool touch;
};

Layout GetLayout();

/** True when the screens follow touch rules (Android, or a forced test mode on desktop). */
bool TouchMode();
/** Touch mode with the Large Text option on: bigger fonts and taller rows. */
bool LargeText();
/** Extra row height for a two-line (label + description) row. */
int SubTextHeight();
/** Size in internal pixels of a comfortable touch target (48dp), or 0 when unknown. */
int TouchTarget();
/** Display density in internal pixels per inch (0 when unknown). */
float InternalDpi();

/** Palette changed: match the theme colors again and rebuild the blend table. */
void OnPaletteChanged();

/** Size or palette may have changed: invalidate the cached background. */
void Invalidate();

/** Fills the whole surface with the themed background (gradient, vignette, embers). */
void DrawBackground(const Surface &out);

/** A framed panel over the background. */
void DrawPanel(const Surface &out, const Rectangle &rect, bool opaque = false);

enum class RowState : uint8_t {
	Normal,
	Focused,
	Pressed,
	Disabled,
};

/** A touchable row/button background. Text is drawn by the caller. */
void DrawRow(const Surface &out, const Rectangle &rect, RowState state);

/** A centered title with a thin divider under it. */
void DrawTitle(const Surface &out, std::string_view text, const Rectangle &rect, UiFlags font);

/** A thin ornamental divider with a diamond in the middle. */
void DrawOrnament(const Surface &out, Point center, int width);

/** A thin divider line with fading ends. */
void DrawDivider(const Surface &out, Point from, int width);

/** A toggle switch, drawn to the right side of `rect`. */
void DrawToggle(const Surface &out, const Rectangle &rect, bool on, bool focused);
/** A square checkbox filling the smaller side of `rect`, with a check mark when `checked`. */
void DrawCheckBox(const Surface &out, const Rectangle &rect, bool checked);

/** Slim scrollbar for a list. */
void DrawScrollbar(const Surface &out, const Rectangle &rect, int first, int visible, int total);

/** A framed button. */
void DrawButton(const Surface &out, const Rectangle &rect, bool disabled);

/** Darkens the whole screen (behind a sheet). */
void DrawDim(const Surface &out);

/** A horizontal gauge showing step `index` of `count`. */
void DrawGauge(const Surface &out, const Rectangle &rect, int index, int count);
/** DETH: the same gauge standing up; it fills from the bottom. */
void DrawVerticalGauge(const Surface &out, const Rectangle &rect, int index, int count, std::string_view (*label)(int) = nullptr);

/** Text field box. */
void DrawEditBox(const Surface &out, const Rectangle &rect, bool focused);

/** Theme colors (palette indices) matched to the current palette. */
struct Colors {
	uint8_t black;
	uint8_t stoneDark;
	uint8_t stone;
	uint8_t stoneLight;
	uint8_t goldDark;
	uint8_t gold;
	uint8_t goldBright;
	uint8_t ember;
	uint8_t emberBright;
	uint8_t redDark;
	uint8_t highlight;
	uint8_t textDark;
	uint8_t text;
	uint8_t textBright;
	uint8_t textHighlight;
};
const Colors &GetColors();

/** Horizontal inset for text inside a row. */
int TextInset();

} // namespace theme

} // namespace devilution
