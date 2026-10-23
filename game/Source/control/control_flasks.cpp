#include "control_flasks.hpp"
#include "control.hpp"

#include "engine/palette.h"
#include "utils/palette_blending.hpp"
#include "engine/surface.hpp"
#include "items.h"
#include "player.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include "utils/str_cat.hpp"

namespace devilution {

std::optional<OwnedSurface> pLifeBuff;
std::optional<OwnedSurface> pManaBuff;

namespace {

Rectangle FlaskTopRect { { 11, 3 }, { 62, 13 } };
Rectangle FlaskBottomRect { { 0, 16 }, { 88, 69 } };

/**
 * Draws the dome of the flask that protrudes above the panel top line.
 * It draws a rectangle of fixed width 59 and height 'h' from the source buffer
 * into the target buffer.
 * @param out The target buffer.
 * @param celBuf Buffer of the flask cel.
 * @param targetPosition Target buffer coordinate.
 */
void DrawFlaskAbovePanel(const Surface &out, const Surface &celBuf, Point targetPosition)
{
	out.BlitFromSkipColorIndexZero(celBuf, MakeSdlRect(0, 0, celBuf.w(), celBuf.h()), targetPosition);
}

/**
 * @brief Draws the part of the life/mana flasks protruding above the bottom panel
 * @see DrawFlaskLower()
 * @param out The display region to draw to
 * @param sourceBuffer A sprite representing the appropriate background/empty flask style
 * @param offset X coordinate offset for where the flask should be drawn
 * @param fillPer How full the flask is (a value from 0 to 81)
 */
void DrawFlaskUpper(const Surface &out, const Surface &sourceBuffer, int offset, int fillPer)
{
	const Rectangle &rect = FlaskTopRect;
	const int emptyRows = std::clamp(81 - fillPer, 0, rect.size.height);
	const int filledRows = rect.size.height - emptyRows;

	// Draw the empty part of the flask
	DrawFlaskAbovePanel(out,
	    sourceBuffer.subregion(rect.position.x, rect.position.y, rect.size.width, rect.size.height),
	    GetMainPanel().position + Displacement { offset, -rect.size.height });

	// Draw the filled part of the flask over the empty part
	if (filledRows > 0) {
		DrawFlaskAbovePanel(out,
		    BottomBuffer->subregion(offset, rect.position.y + emptyRows, rect.size.width, filledRows),
		    GetMainPanel().position + Displacement { offset, -rect.size.height + emptyRows });
	}
}

/**
 * Draws a section of the empty flask cel on top of the panel to create the illusion
 * of the flask getting empty. This function takes a cel and draws a
 * horizontal stripe of height (max-min) onto the given buffer.
 * @param out Target buffer.
 * @param celBuf Buffer of the flask cel.
 * @param targetPosition Target buffer coordinate.
 */
void DrawFlaskOnPanel(const Surface &out, const Surface &celBuf, Point targetPosition)
{
	out.BlitFrom(celBuf, MakeSdlRect(0, 0, celBuf.w(), celBuf.h()), targetPosition);
}

/**
 * @brief Draws the part of the life/mana flasks inside the bottom panel
 * @see DrawFlaskUpper()
 * @param out The display region to draw to
 * @param sourceBuffer A sprite representing the appropriate background/empty flask style
 * @param offset X coordinate offset for where the flask should be drawn
 * @param fillPer How full the flask is (a value from 0 to 80)
 * @param drawFilledPortion Indicates whether to draw the filled portion of the flask
 */
void DrawFlaskLower(const Surface &out, const Surface &sourceBuffer, int offset, int fillPer, bool drawFilledPortion)
{
	const Rectangle &rect = FlaskBottomRect;
	const int filledRows = std::clamp(fillPer, 0, rect.size.height);
	const int emptyRows = rect.size.height - filledRows;

	// Draw the empty part of the flask
	if (emptyRows > 0) {
		DrawFlaskOnPanel(out,
		    sourceBuffer.subregion(rect.position.x, rect.position.y, rect.size.width, emptyRows),
		    GetMainPanel().position + Displacement { offset, 0 });
	}

	// Draw the filled part of the flask
	if (drawFilledPortion && filledRows > 0) {
		DrawFlaskOnPanel(out,
		    BottomBuffer->subregion(offset, rect.position.y + emptyRows, rect.size.width, filledRows),
		    GetMainPanel().position + Displacement { offset, emptyRows });
	}
}

} // namespace

/**
 * @brief DETH: a glass sphere drawn from scratch: liquid up to `fillPer` (0..80), dark glass above it
 * showing the world through, a highlight, and a thin gold rim.
 */
/** Nearest entry of the current level palette to an RGB color (index 0 is reserved). */
uint8_t NearestPaletteIndex(int r, int g, int b)
{
	int best = 1;
	int bestDistance = std::numeric_limits<int>::max();
	for (int i = 1; i < 256; ++i) {
		const SDL_Color &c = logical_palette[i];
		const int dr = c.r - r;
		const int dg = c.g - g;
		const int db = c.b - b;
		const int distance = dr * dr * 2 + dg * dg * 3 + db * db;
		if (distance < bestDistance) {
			bestDistance = distance;
			best = i;
		}
	}
	return static_cast<uint8_t>(best);
}

struct OrbRamp {
	uint8_t liquid[16]; // 0 bright .. 15 dark
	uint8_t rimLight;
	uint8_t rimDark;
	uint8_t highlight;
};

/** Builds a 16-step ramp between two colors from whatever the level palette offers. */
OrbRamp MakeOrbRamp(SDL_Color bright, SDL_Color dark)
{
	OrbRamp ramp {};
	for (int i = 0; i < 16; ++i) {
		const float t = static_cast<float>(i) / 15.0F;
		ramp.liquid[i] = NearestPaletteIndex(
		    static_cast<int>(bright.r + (dark.r - bright.r) * t),
		    static_cast<int>(bright.g + (dark.g - bright.g) * t),
		    static_cast<int>(bright.b + (dark.b - bright.b) * t));
	}
	ramp.rimLight = NearestPaletteIndex(190, 150, 70);
	ramp.rimDark = NearestPaletteIndex(100, 70, 20);
	ramp.highlight = NearestPaletteIndex(150, 140, 130);
	return ramp;
}

/** DETH: tarnished bronze and dark iron for the weathered HUD frames. */
struct MetalColors {
	uint8_t iron;     // near-black outer edge and grooves
	uint8_t grime;    // dark brown dirt in the metal
	uint8_t bronze;   // dull base metal
	uint8_t worn;     // rubbed, slightly brighter spots
	uint8_t glint;    // rare scratch highlight
};

MetalColors GetMetalColors()
{
	return {
		NearestPaletteIndex(12, 10, 8),
		NearestPaletteIndex(32, 25, 16),
		NearestPaletteIndex(60, 46, 28),
		NearestPaletteIndex(86, 68, 42),
		NearestPaletteIndex(122, 102, 66),
	};
}

/** Stable per-pixel noise so the wear does not flicker. */
unsigned WearNoise(int x, int y)
{
	unsigned h = static_cast<unsigned>(x) * 374761393U + static_cast<unsigned>(y) * 668265263U;
	h = (h ^ (h >> 13)) * 1274126177U;
	return (h ^ (h >> 16)) & 0xFF;
}

/** Weathered metal shade for one pixel of a band; `t` is 0 at the outer edge .. 1 at the inner edge. */
uint8_t WeatheredMetal(const MetalColors &metal, int x, int y, float t, unsigned coarse)
{
	if (t < 0.18F || t > 0.86F)
		return metal.iron;
	const unsigned n = WearNoise(x, y);
	if (n < 10)
		return metal.glint;
	if (coarse < 70 || n < 60)
		return metal.grime;
	if (coarse > 200 && n > 150)
		return metal.worn;
	return metal.bronze;
}

void DrawCompactOrb(const Surface &out, Point center, int radius, int fillPer, const OrbRamp &ramp)
{
	constexpr float RimWidth = 5.0F;
	const MetalColors metal = GetMetalColors();
	const float r = static_cast<float>(radius);
	const float cx = static_cast<float>(center.x) + 0.5F;
	const float cy = static_cast<float>(center.y) + 0.5F;
	// Liquid surface (screen y) for the fill level; 80 = full.
	const float level = cy + r - (2.0F * r * static_cast<float>(std::clamp(fillPer, 0, 80)) / 80.0F);
	for (int y = center.y - radius; y <= center.y + radius; ++y) {
		if (y < 0 || y >= out.h())
			continue;
		const float dy = static_cast<float>(y) + 0.5F - cy;
		uint8_t *row = out.at(0, y);
		for (int x = center.x - radius; x <= center.x + radius; ++x) {
			if (x < 0 || x >= out.w())
				continue;
			const float dx = static_cast<float>(x) + 0.5F - cx;
			const float d = std::sqrt(dx * dx + dy * dy);
			if (d > r)
				continue;
			// Rim: a band of weathered bronze between dark iron edges.
			if (d > r - RimWidth) {
				const float angle = std::atan2(dy, dx);
				const auto coarse = WearNoise(static_cast<int>((angle + 3.2F) * 9.0F), 7);
				row[x] = WeatheredMetal(metal, x, y, (r - d) / RimWidth, coarse);
				continue;
			}
			// Lighting: brightest towards the upper left, darker at the edges.
			const float lx = (dx + 0.45F * r) / r;
			const float ly = (dy + 0.45F * r) / r;
			const float light = std::clamp(1.0F - std::sqrt(lx * lx + ly * ly) * 0.9F, 0.0F, 1.0F);
			const float edge = d / r; // 0 center .. 1 rim
			const bool liquid = static_cast<float>(y) + 0.5F >= level;
			if (liquid) {
				// 16-color ramp: +1 bright .. +14 dark.
				float shade = 12.0F - light * 8.0F + edge * edge * 4.0F;
				if (std::abs(static_cast<float>(y) + 0.5F - level) < 1.0F)
					shade -= 4.0F; // bright line on the surface
				row[x] = ramp.liquid[std::clamp(static_cast<int>(shade), 0, 15)];
			} else {
				// Dark glass: blend a dark gray with the world behind, lighter near the highlight.
				const auto glass = static_cast<uint8_t>(PAL16_GRAY + static_cast<int>(std::clamp(14.0F - light * 5.0F, 9.0F, 15.0F)));
				row[x] = paletteTransparencyLookup[glass][row[x]];
			}
		}
	}
	// Specular highlight.
	const int hx = center.x - radius * 2 / 5;
	const int hy = center.y - radius * 2 / 5;
	for (int y = -2; y <= 2; ++y) {
		for (int x = -4; x <= 4; ++x) {
			if (x * x * 4 + y * y * 16 > 64)
				continue;
			const int px = hx + x;
			const int py = hy + y;
			if (px < 0 || py < 0 || px >= out.w() || py >= out.h())
				continue;
			const bool liquid = static_cast<float>(py) + 0.5F >= level;
			out.at(0, py)[px] = liquid ? ramp.liquid[2] : ramp.highlight;
		}
	}
}

void DrawCompactOrbs(const Surface &out)
{
	const Point panel = GetMainPanel().position;
	// Palettes differ per level, so the ramps are matched each frame (cheap).
	// Dark, blood-like liquids (DETH grim look).
	const OrbRamp life = MakeOrbRamp({ 160, 36, 28, 0 }, { 26, 0, 0, 0 });
	const OrbRamp mana = MakeOrbRamp({ 70, 84, 170, 0 }, { 4, 4, 34, 0 });
	DrawCompactOrb(out, panel + Displacement { CompactLifeOrbCenter.x, CompactLifeOrbCenter.y }, CompactOrbRadius, MyPlayer->_pHPPer, life);
	const int manaPer = (HasAnyOf(MyPlayer->_pIFlags, ItemSpecialEffect::NoMana) || MyPlayer->hasNoMana()) ? 0 : MyPlayer->_pManaPer;
	DrawCompactOrb(out, panel + Displacement { CompactManaOrbCenter.x, CompactManaOrbCenter.y }, CompactOrbRadius, manaPer, mana);
	// Weathered frame around the belt (the slots are drawn on top, inside it).
	DrawWeatheredFrame(out, { GetBeltPanelPosition() + Displacement { 205, 5 }, Size { 232, 29 } }, 3);
}

void DrawWeatheredFrame(const Surface &out, Rectangle inner, int thickness)
{
	const MetalColors metal = GetMetalColors();
	const int x0 = inner.position.x - thickness;
	const int y0 = inner.position.y - thickness;
	const int x1 = inner.position.x + inner.size.width + thickness;
	const int y1 = inner.position.y + inner.size.height + thickness;
	for (int y = std::max(0, y0); y < std::min(out.h(), y1); ++y) {
		uint8_t *row = out.at(0, y);
		for (int x = std::max(0, x0); x < std::min(out.w(), x1); ++x) {
			const int depth = std::min({ x - x0, y - y0, x1 - 1 - x, y1 - 1 - y });
			if (depth >= thickness)
				continue;
			const float t = (static_cast<float>(depth) + 0.5F) / static_cast<float>(thickness);
			row[x] = WeatheredMetal(metal, x, y, t, WearNoise(x / 6, y / 6 + 3));
		}
	}
}

void DrawLifeFlaskUpper(const Surface &out)
{
	constexpr int LifeFlaskUpperOffset = 107;
	DrawFlaskUpper(out, *pLifeBuff, LifeFlaskUpperOffset, MyPlayer->_pHPPer);
}

void DrawManaFlaskUpper(const Surface &out)
{
	constexpr int ManaFlaskUpperOffset = 475;
	DrawFlaskUpper(out, *pManaBuff, ManaFlaskUpperOffset, MyPlayer->_pManaPer);
}

void DrawLifeFlaskLower(const Surface &out, bool drawFilledPortion)
{
	constexpr int LifeFlaskLowerOffset = 96;
	DrawFlaskLower(out, *pLifeBuff, LifeFlaskLowerOffset, MyPlayer->_pHPPer, drawFilledPortion);
}

void DrawManaFlaskLower(const Surface &out, bool drawFilledPortion)
{
	constexpr int ManaFlaskLowerOffset = 464;
	DrawFlaskLower(out, *pManaBuff, ManaFlaskLowerOffset, MyPlayer->_pManaPer, drawFilledPortion);
}

void DrawFlaskValues(const Surface &out, Point pos, int currValue, int maxValue)
{
	const UiFlags color = (currValue > 0 ? (currValue == maxValue ? UiFlags::ColorGold : UiFlags::ColorWhite) : UiFlags::ColorRed);

	auto drawStringWithShadow = [out, color](std::string_view text, Point pos) {
		DrawString(out, text, pos + Displacement { -1, -1 },
		    { .flags = UiFlags::ColorBlack | UiFlags::KerningFitSpacing, .spacing = 0 });
		DrawString(out, text, pos,
		    { .flags = color | UiFlags::KerningFitSpacing, .spacing = 0 });
	};

	const std::string currText = StrCat(currValue);
	drawStringWithShadow(currText, pos - Displacement { GetLineWidth(currText, GameFont12) + 1, 0 });
	drawStringWithShadow("/", pos);
	drawStringWithShadow(StrCat(maxValue), pos + Displacement { GetLineWidth("/", GameFont12) + 1, 0 });
}

void UpdateLifeManaPercent()
{
	MyPlayer->UpdateManaPercentage();
	MyPlayer->UpdateHitPointPercentage();
}

} // namespace devilution
