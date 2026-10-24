/**
 * @file deth_theme.cpp
 *
 * DETH: procedurally drawn theme for the screens before the game.
 */
#include "DiabloUI/deth_theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <SDL.h>

#include "controls/touch/touch_mode.hpp"
#include "engine/palette.h"
#include "options.h"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "utils/palette_blending.hpp"
#include "utils/display.h"
#include "utils/ui_fwd.h"

#include <cstdlib>

namespace devilution {

namespace theme {

namespace {

Colors colors {};
bool colorsValid = false;

/** Cached static background (gradient, noise, vignette) for the current size and palette. */
std::vector<uint8_t> backgroundCache;
int cacheWidth = 0;
int cacheHeight = 0;

struct Ember {
	float x;
	float y;
	float speed;
	float drift;
	float phase;
	float life; // 0..1
	uint8_t bright;
};
std::vector<Ember> embers;
uint32_t lastEmberTick = 0;
uint32_t rngState = 0x9E3779B9U;

uint32_t NextRandom()
{
	rngState ^= rngState << 13;
	rngState ^= rngState >> 17;
	rngState ^= rngState << 5;
	return rngState;
}

float RandomUnit()
{
	return static_cast<float>(NextRandom() & 0xFFFF) / 65535.0F;
}

uint8_t Nearest(int r, int g, int b)
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

void MatchColors()
{
	colors.black = 0;
	colors.stoneDark = Nearest(18, 15, 14);
	colors.stone = Nearest(38, 32, 30);
	colors.stoneLight = Nearest(70, 60, 54);
	colors.goldDark = Nearest(110, 80, 30);
	colors.gold = Nearest(170, 130, 50);
	colors.goldBright = Nearest(230, 190, 90);
	colors.ember = Nearest(200, 70, 20);
	colors.emberBright = Nearest(255, 170, 60);
	colors.redDark = Nearest(60, 14, 10);
	colors.highlight = Nearest(240, 225, 200);
	// The antique gold of the menu text, for controls that sit beside it.
	colors.textDark = Nearest(72, 64, 39);
	colors.text = Nearest(97, 88, 57);
	colors.textBright = Nearest(117, 108, 71);
	colors.textHighlight = Nearest(150, 140, 98);
	colorsValid = true;
}

/** Small integer hash for the stone noise. */
uint32_t Hash(uint32_t x, uint32_t y)
{
	uint32_t h = x * 374761393U + y * 668265263U;
	h = (h ^ (h >> 13)) * 1274126177U;
	return h ^ (h >> 16);
}

void BuildBackground(int width, int height)
{
	backgroundCache.assign(static_cast<size_t>(width) * height, 0);
	cacheWidth = width;
	cacheHeight = height;

	// Near black to dark cold stone. Matched once.
	uint8_t ramp[16];
	for (int i = 0; i < 16; ++i) {
		const float t = static_cast<float>(i) / 15.0F;
		ramp[i] = Nearest(static_cast<int>(3 + 19 * t), static_cast<int>(3 + 17 * t), static_cast<int>(4 + 18 * t));
	}

	const float cx = width / 2.0F;
	for (int y = 0; y < height; ++y) {
		const float v = static_cast<float>(y) / std::max(1, height - 1);
		for (int x = 0; x < width; ++x) {
			const float dx = (x - cx) / cx;
			// Faint light from the top center (behind the logo), fading into black at the edges.
			float level = 0.55F * std::max(0.0F, 1.0F - (dx * dx * 1.3F + v * v * 1.1F));
			// Strong vignette.
			level *= 1.0F - 0.6F * dx * dx;
			// Stone grain, very subtle.
			const uint32_t n1 = Hash(x >> 1, y >> 1) & 0xFF;
			const uint32_t n2 = Hash(x >> 3, y >> 3) & 0xFF;
			level += (static_cast<float>(n1) / 255.0F - 0.5F) * 0.05F + (static_cast<float>(n2) / 255.0F - 0.5F) * 0.07F;
			backgroundCache[static_cast<size_t>(y) * width + x] = ramp[std::clamp(static_cast<int>(level * 15.0F), 0, 15)];
		}
	}
}

void SpawnEmber(Ember &ember, int width, int height, bool initial)
{
	ember.x = RandomUnit() * width;
	ember.y = initial ? RandomUnit() * height : height + 2.0F;
	ember.speed = 8.0F + RandomUnit() * 18.0F;
	ember.drift = (RandomUnit() - 0.5F) * 10.0F;
	ember.phase = RandomUnit() * 6.283F;
	ember.life = initial ? RandomUnit() : 1.0F;
	ember.bright = static_cast<uint8_t>(NextRandom() % 3);
}

void UpdateAndDrawEmbers(const Surface &out)
{
	const int width = out.w();
	const int height = out.h();
	const size_t count = static_cast<size_t>(std::clamp(width * height / 30000, 8, 36));
	if (embers.size() != count) {
		embers.resize(count);
		for (Ember &ember : embers)
			SpawnEmber(ember, width, height, true);
		lastEmberTick = SDL_GetTicks();
	}
	const uint32_t now = SDL_GetTicks();
	const float dt = std::min(0.1F, static_cast<float>(now - lastEmberTick) / 1000.0F);
	lastEmberTick = now;

	const Colors &c = GetColors();
	for (Ember &ember : embers) {
		ember.y -= ember.speed * dt;
		ember.phase += dt * 1.5F;
		ember.x += (ember.drift + std::sin(ember.phase) * 6.0F) * dt;
		ember.life -= dt * 0.08F;
		if (ember.y < -2.0F || ember.life <= 0.0F || ember.x < -2.0F || ember.x > width + 2.0F) {
			SpawnEmber(ember, width, height, false);
			continue;
		}
		const int x = static_cast<int>(ember.x);
		const int y = static_cast<int>(ember.y);
		if (!out.InBounds({ x, y }))
			continue;
		// Fade in near the bottom and out as the ember dies.
		const float visibility = std::min(ember.life * 3.0F, 1.0F) * std::min((height - ember.y) / 40.0F + 0.2F, 1.0F);
		uint8_t color;
		if (visibility > 0.75F)
			color = ember.bright == 0 ? c.emberBright : c.ember;
		else if (visibility > 0.35F)
			color = c.ember;
		else
			color = c.redDark;
		out.SetPixel({ x, y }, color);
		if (ember.bright == 0 && visibility > 0.6F && out.InBounds({ x + 1, y }))
			out.SetPixel({ x + 1, y }, c.ember);
	}
}

} // namespace

const Colors &GetColors()
{
	if (!colorsValid)
		MatchColors();
	return colors;
}

void OnPaletteChanged()
{
	MatchColors();
	GenerateBlendedLookupTable(logical_palette.data());
	backgroundCache.clear();
}

void Invalidate()
{
	colorsValid = false;
	backgroundCache.clear();
}

bool LargeText()
{
	return TouchMode() && *GetOptions().Graphics.largeText;
}

int SubTextHeight()
{
	return LargeText() ? 30 : 20;
}

bool TouchMode()
{
#ifdef USE_SDL1
	return false;
#else
	return touch::IsTouchMode();
#endif
}

float InternalDpi()
{
	float dpi = 0.0F;
#if !defined(USE_SDL1) && !defined(USE_SDL3)
	if (ghMainWnd != nullptr) {
		float hdpi = 0.0F;
		float vdpi = 0.0F;
		if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(ghMainWnd), nullptr, &hdpi, &vdpi) == 0) {
			int clientWidth = gnScreenWidth;
			int clientHeight = gnScreenHeight;
			if (renderer != nullptr)
				SDL_GetRendererOutputSize(renderer, &clientWidth, &clientHeight);
			else
				SDL_GetWindowSize(ghMainWnd, &clientWidth, &clientHeight);
			if (clientWidth > 0 && clientHeight > 0)
				dpi = std::min(hdpi * gnScreenWidth / clientWidth, vdpi * gnScreenHeight / clientHeight);
		}
	}
#endif
	// Debug aid on a desktop: pretend the display has this DPI (same variable the touch overlay uses).
	if (const char *forcedDpi = std::getenv("DETH_TOUCH_DPI"); forcedDpi != nullptr) {
		SDL_DisplayMode mode;
		const int desktopWidth = SDL_GetDesktopDisplayMode(0, &mode) == 0 ? std::max(mode.w, mode.h) : gnScreenWidth;
		dpi = static_cast<float>(std::atof(forcedDpi)) * static_cast<float>(gnScreenWidth) / static_cast<float>(std::max(1, desktopWidth));
	}
	return dpi;
}

int TouchTarget()
{
	const float dpi = InternalDpi();
	if (dpi <= 0.0F)
		return 0;
	return static_cast<int>(std::lround(48.0F * dpi / 160.0F));
}

Layout GetLayout()
{
	Layout layout {};
	layout.screenWidth = gnScreenWidth;
	layout.screenHeight = gnScreenHeight;
	layout.compact = gnScreenHeight < 500;
	layout.touch = TouchMode();
	layout.margin = std::max(12, gnScreenWidth / 40);
	layout.rowHeight = std::clamp(gnScreenHeight / 11, 30, 50);
	layout.rowGap = layout.compact ? 4 : 6;
	if (layout.touch) {
		// Rows are finger sized: at least a 48dp target plus breathing room, never more than a fifth of the screen.
		const int target = std::max(TouchTarget(), 40);
		layout.rowHeight = std::clamp(std::max(layout.rowHeight, target + target / 6), 40, gnScreenHeight / 5);
		layout.rowGap = 8;
		if (LargeText())
			layout.rowHeight = std::clamp(layout.rowHeight * 4 / 3, 40, gnScreenHeight / 4);
	}
	layout.rowFont = layout.rowHeight >= 44 ? UiFlags::FontSize30 : UiFlags::FontSize24;
	layout.titleFont = layout.compact ? UiFlags::FontSize30 : UiFlags::FontSize42;
	layout.titleHeight = layout.compact ? 44 : 64;
	if (layout.touch)
		layout.titleHeight = std::max(layout.titleHeight, std::max(TouchTarget(), 40) + 8);

	const int columnWidth = std::min(gnScreenWidth - 2 * layout.margin, 520);
	layout.column = Rectangle { { (gnScreenWidth - columnWidth) / 2, 0 }, { columnWidth, gnScreenHeight } };
	const int wideWidth = std::min(gnScreenWidth - 2 * layout.margin, 760);
	layout.wideColumn = Rectangle { { (gnScreenWidth - wideWidth) / 2, 0 }, { wideWidth, gnScreenHeight } };
	return layout;
}

int TextInset()
{
	return 16;
}

void DrawBackground(const Surface &out)
{
	if (backgroundCache.empty() || cacheWidth != out.w() || cacheHeight != out.h())
		BuildBackground(out.w(), out.h());
	for (int y = 0; y < out.h(); ++y)
		std::copy_n(backgroundCache.data() + static_cast<size_t>(y) * cacheWidth, cacheWidth, out.at(0, y));
	UpdateAndDrawEmbers(out);
}

void DrawPanel(const Surface &out, const Rectangle &rect, bool opaque)
{
	const Colors &c = GetColors();
	const int x = rect.position.x;
	const int y = rect.position.y;
	const int w = rect.size.width;
	const int h = rect.size.height;
	if (w <= 4 || h <= 4)
		return;
	if (opaque) {
		FillRect(out, x, y, w, h, c.stoneDark);
	} else {
		DrawHalfTransparentRectTo(out, x, y, w, h);
		DrawHalfTransparentRectTo(out, x + 1, y + 1, w - 2, h - 2);
	}
	// Outer dark edge, gold frame, inner highlight.
	DrawHorizontalLine(out, { x, y }, w, c.black);
	DrawHorizontalLine(out, { x, y + h - 1 }, w, c.black);
	DrawVerticalLine(out, { x, y }, h, c.black);
	DrawVerticalLine(out, { x + w - 1, y }, h, c.black);
	DrawHorizontalLine(out, { x + 1, y + 1 }, w - 2, c.goldDark);
	DrawHorizontalLine(out, { x + 1, y + h - 2 }, w - 2, c.goldDark);
	DrawVerticalLine(out, { x + 1, y + 1 }, h - 2, c.goldDark);
	DrawVerticalLine(out, { x + w - 2, y + 1 }, h - 2, c.goldDark);
	DrawHorizontalLine(out, { x + 2, y + 2 }, w - 4, c.stoneLight);
	// Corner accents.
	const int accent = std::min(12, w / 8);
	DrawHorizontalLine(out, { x + 1, y + 1 }, accent, c.gold);
	DrawHorizontalLine(out, { x + w - 1 - accent, y + 1 }, accent, c.gold);
	DrawHorizontalLine(out, { x + 1, y + h - 2 }, accent, c.gold);
	DrawHorizontalLine(out, { x + w - 1 - accent, y + h - 2 }, accent, c.gold);
	DrawVerticalLine(out, { x + 1, y + 1 }, accent, c.gold);
	DrawVerticalLine(out, { x + w - 2, y + 1 }, accent, c.gold);
	DrawVerticalLine(out, { x + 1, y + h - 1 - accent }, accent, c.gold);
	DrawVerticalLine(out, { x + w - 2, y + h - 1 - accent }, accent, c.gold);
}

void DrawRow(const Surface &out, const Rectangle &rect, RowState state)
{
	const Colors &c = GetColors();
	const int x = rect.position.x;
	const int y = rect.position.y;
	const int w = rect.size.width;
	const int h = rect.size.height;
	if (w <= 4 || h <= 4)
		return;
	const int inset = TextInset() / 2;
	switch (state) {
	case RowState::Normal:
	case RowState::Disabled:
		// No box: a hairline under the row keeps the list readable and touch targets implied.
		DrawHorizontalLine(out, { x + inset, y + h - 1 }, w - 2 * inset, c.stoneDark);
		break;
	case RowState::Focused:
	case RowState::Pressed: {
		// Dark crimson wash, a gold accent bar on the left and a gold hairline under the row.
		DrawHalfTransparentRectTo(out, x, y, w, h, c.redDark);
		if (state == RowState::Pressed)
			DrawHalfTransparentRectTo(out, x, y, w, h, c.redDark);
		FillRect(out, x, y, 3, h, c.goldBright);
		DrawVerticalLine(out, { x + 3, y }, h, c.goldDark);
		DrawHorizontalLine(out, { x + inset, y + h - 1 }, w - 2 * inset, c.gold);
		DrawHorizontalLine(out, { x + inset, y }, w - 2 * inset, c.goldDark);
		break;
	}
	}
}

/** A thin ornamental divider: fading line with a small diamond in the middle. */
void DrawOrnament(const Surface &out, Point center, int width)
{
	const Colors &c = GetColors();
	const int half = width / 2;
	for (int i = 0; i < half; ++i) {
		const uint8_t color = i < half / 3 ? c.gold : (i < half * 2 / 3 ? c.goldDark : c.stoneLight);
		out.SetPixel({ center.x - 6 - i, center.y }, color);
		out.SetPixel({ center.x + 6 + i, center.y }, color);
	}
	for (int d = -3; d <= 3; ++d) {
		const int span = 3 - std::abs(d);
		DrawHorizontalLine(out, { center.x - span, center.y + d }, 2 * span + 1, d == 0 ? c.goldBright : c.gold);
	}
}

void DrawTitle(const Surface &out, std::string_view text, const Rectangle &rect, UiFlags font)
{
	DrawString(out, text, rect, { .flags = font | UiFlags::ColorUiGold | UiFlags::AlignCenter | UiFlags::VerticalCenter });
	DrawDivider(out, { rect.position.x, rect.position.y + rect.size.height - 2 }, rect.size.width);
}

void DrawDivider(const Surface &out, Point from, int width)
{
	const Colors &c = GetColors();
	if (width < 8)
		return;
	const int fade = std::min(width / 4, 60);
	DrawHorizontalLine(out, { from.x, from.y }, fade, c.goldDark);
	DrawHorizontalLine(out, { from.x + fade, from.y }, width - 2 * fade, c.gold);
	DrawHorizontalLine(out, { from.x + width - fade, from.y }, fade, c.goldDark);
	DrawHorizontalLine(out, { from.x + fade / 2, from.y + 1 }, width - fade, c.stoneDark);
}

void DrawToggle(const Surface &out, const Rectangle &rect, bool on, bool focused)
{
	const Colors &c = GetColors();
	const int h = std::clamp(rect.size.height - 12, 14, 22);
	const int w = h * 2;
	const int x = rect.position.x + rect.size.width - w - TextInset();
	const int y = rect.position.y + (rect.size.height - h) / 2;
	// Track.
	FillRect(out, x + 1, y, w - 2, h, on ? c.goldDark : c.stoneDark);
	DrawHorizontalLine(out, { x + 1, y }, w - 2, focused ? c.gold : c.stoneLight);
	DrawHorizontalLine(out, { x + 1, y + h - 1 }, w - 2, focused ? c.gold : c.stoneLight);
	DrawVerticalLine(out, { x, y + 1 }, h - 2, focused ? c.gold : c.stoneLight);
	DrawVerticalLine(out, { x + w - 1, y + 1 }, h - 2, focused ? c.gold : c.stoneLight);
	// Knob.
	const int knob = h - 4;
	const int kx = on ? x + w - knob - 2 : x + 2;
	FillRect(out, kx, y + 2, knob, knob, on ? c.goldBright : c.stoneLight);
	DrawHorizontalLine(out, { kx, y + 2 }, knob, c.highlight);
}

void DrawCheckBox(const Surface &out, const Rectangle &rect, bool checked)
{
	const Colors &c = GetColors();
	const int size = std::min(rect.size.width, rect.size.height);
	const int x = rect.position.x;
	const int y = rect.position.y + (rect.size.height - size) / 2;
	FillRect(out, x + 1, y + 1, size - 2, size - 2, checked ? c.goldDark : c.stoneDark);
	const uint8_t border = checked ? c.gold : c.stoneLight;
	DrawHorizontalLine(out, { x, y }, size, border);
	DrawHorizontalLine(out, { x, y + size - 1 }, size, border);
	DrawVerticalLine(out, { x, y }, size, border);
	DrawVerticalLine(out, { x + size - 1, y }, size, border);
	if (!checked)
		return;
	// Check mark: a short stroke down-right, then a long one up-right, 3 px thick.
	const int s = size;
	const Point a { x + s * 2 / 9, y + s / 2 };
	const Point b { x + s * 4 / 9, y + s * 3 / 4 };
	const Point d { x + s * 7 / 9, y + s / 4 };
	auto stroke = [&](Point from, Point to) {
		const int steps = std::max(std::abs(to.x - from.x), std::abs(to.y - from.y));
		for (int i = 0; i <= steps; ++i) {
			const int px = from.x + (to.x - from.x) * i / std::max(steps, 1);
			const int py = from.y + (to.y - from.y) * i / std::max(steps, 1);
			for (int t = -1; t <= 1; ++t)
				out.SetPixel({ px, py + t }, c.goldBright);
		}
	};
	stroke(a, b);
	stroke(b, d);
}

void DrawScrollbar(const Surface &out, const Rectangle &rect, int first, int visible, int total)
{
	const Colors &c = GetColors();
	if (total <= visible || rect.size.height < 8)
		return;
	const int x = rect.position.x + rect.size.width / 2 - 2;
	FillRect(out, x, rect.position.y, 4, rect.size.height, c.stoneDark);
	const int thumbHeight = std::max(10, rect.size.height * visible / total);
	const int travel = rect.size.height - thumbHeight;
	const int thumbY = rect.position.y + (total - visible > 0 ? travel * first / (total - visible) : 0);
	FillRect(out, x, thumbY, 4, thumbHeight, c.gold);
	DrawVerticalLine(out, { x, thumbY }, thumbHeight, c.goldBright);
}

void DrawButton(const Surface &out, const Rectangle &rect, bool disabled)
{
	const Colors &c = GetColors();
	const int x = rect.position.x;
	const int y = rect.position.y;
	const int w = rect.size.width;
	const int h = rect.size.height;
	if (w <= 4 || h <= 4)
		return;
	DrawHalfTransparentRectTo(out, x, y, w, h);
	const uint8_t edge = disabled ? c.stoneDark : c.goldDark;
	const uint8_t bright = disabled ? c.stone : c.gold;
	DrawHorizontalLine(out, { x, y }, w, bright);
	DrawHorizontalLine(out, { x, y + h - 1 }, w, edge);
	DrawVerticalLine(out, { x, y }, h, edge);
	DrawVerticalLine(out, { x + w - 1, y }, h, edge);
	// Small corner marks give it the gothic frame without a heavy border.
	const int mark = std::min(8, w / 6);
	DrawHorizontalLine(out, { x, y + h - 1 }, mark, bright);
	DrawHorizontalLine(out, { x + w - mark, y + h - 1 }, mark, bright);
	DrawVerticalLine(out, { x, y }, mark, bright);
	DrawVerticalLine(out, { x + w - 1, y }, mark, bright);
}

void DrawDim(const Surface &out)
{
	DrawHalfTransparentRectTo(out, 0, 0, out.w(), out.h());
	DrawHalfTransparentRectTo(out, 0, 0, out.w(), out.h());
}

void DrawGauge(const Surface &out, const Rectangle &rect, int index, int count)
{
	const Colors &c = GetColors();
	const int x = rect.position.x;
	const int y = rect.position.y + rect.size.height / 2 - 4;
	const int w = rect.size.width;
	if (w < 8)
		return;
	FillRect(out, x, y, w, 8, c.stoneDark);
	DrawHorizontalLine(out, { x, y }, w, c.stone);
	DrawHorizontalLine(out, { x, y + 7 }, w, c.stone);
	if (count > 1) {
		const int fill = std::clamp(w * index / (count - 1), 0, w);
		FillRect(out, x, y + 1, fill, 6, c.gold);
		DrawHorizontalLine(out, { x, y + 1 }, fill, c.goldBright);
		// Knob.
		const int kx = std::clamp(x + fill - 5, x, x + w - 10);
		FillRect(out, kx, y - 4, 10, 16, c.goldBright);
		DrawHorizontalLine(out, { kx, y - 4 }, 10, c.highlight);
	}
}

namespace {

/** A framed box in the menu's style: black edge, gold frame, bright corner marks. */
/** The slider's colors: the menu text's gold instead of the brighter frame gold. */
Colors SliderColors()
{
	Colors m = GetColors();
	m.goldDark = m.textDark;
	m.gold = m.text;
	m.goldBright = m.textBright;
	m.highlight = m.textHighlight;
	return m;
}

void DrawFramedBox(const Surface &out, int x, int y, int w, int h, uint8_t fill, bool bright)
{
	const Colors c = SliderColors();
	FillRect(out, x, y, w, h, fill);
	DrawHorizontalLine(out, { x, y }, w, c.black);
	DrawHorizontalLine(out, { x, y + h - 1 }, w, c.black);
	DrawVerticalLine(out, { x, y }, h, c.black);
	DrawVerticalLine(out, { x + w - 1, y }, h, c.black);
	const uint8_t frame = bright ? c.goldBright : c.goldDark;
	DrawHorizontalLine(out, { x + 1, y + 1 }, w - 2, frame);
	DrawHorizontalLine(out, { x + 1, y + h - 2 }, w - 2, frame);
	DrawVerticalLine(out, { x + 1, y + 1 }, h - 2, frame);
	DrawVerticalLine(out, { x + w - 2, y + 1 }, h - 2, frame);
	const int mark = std::min(8, std::min(w, h) / 4);
	const uint8_t accent = bright ? c.highlight : c.gold;
	DrawHorizontalLine(out, { x + 1, y + 1 }, mark, accent);
	DrawHorizontalLine(out, { x + w - 1 - mark, y + 1 }, mark, accent);
	DrawHorizontalLine(out, { x + 1, y + h - 2 }, mark, accent);
	DrawHorizontalLine(out, { x + w - 1 - mark, y + h - 2 }, mark, accent);
	DrawVerticalLine(out, { x + 1, y + 1 }, mark, accent);
	DrawVerticalLine(out, { x + w - 2, y + 1 }, mark, accent);
	DrawVerticalLine(out, { x + 1, y + h - 1 - mark }, mark, accent);
	DrawVerticalLine(out, { x + w - 2, y + h - 1 - mark }, mark, accent);
}

/** DrawOrnament in the given colors. */
void DrawSliderOrnament(const Surface &out, const Colors &c, Point center, int width)
{
	const int half = width / 2;
	for (int i = 0; i < half; ++i) {
		const uint8_t color = i < half / 3 ? c.gold : (i < half * 2 / 3 ? c.goldDark : c.stoneLight);
		out.SetPixel({ center.x - 6 - i, center.y }, color);
		out.SetPixel({ center.x + 6 + i, center.y }, color);
	}
	for (int d = -3; d <= 3; ++d) {
		const int span = 3 - std::abs(d);
		DrawHorizontalLine(out, { center.x - span, center.y + d }, 2 * span + 1, d == 0 ? c.goldBright : c.gold);
	}
}

/** The rail: a dark, gold-framed channel that fills with gold from the bottom. */
void DrawRail(const Surface &out, int cx, int top, int h, int railW, int fillTo, int index, int count)
{
	const Colors c = SliderColors();
	const int x = cx - railW / 2;
	DrawFramedBox(out, x, top, railW, h, c.stoneDark, false);
	if (fillTo < top + h - 3)
		FillRect(out, x + 3, fillTo, railW - 6, top + h - 3 - fillTo, c.gold);
	if (fillTo < top + h - 3)
		DrawVerticalLine(out, { x + 3, fillTo }, top + h - 3 - fillTo, c.goldBright);
	// A notch on each side for every step.
	for (int i = 0; i < count; i++) {
		const int ty = top + h - 1 - (h - 1) * i / std::max(1, count - 1);
		DrawHorizontalLine(out, { x - 7, ty }, 5, i <= index ? c.gold : c.goldDark);
		DrawHorizontalLine(out, { x + railW + 2, ty }, 5, i <= index ? c.gold : c.goldDark);
	}
}

} // namespace

void DrawVerticalGauge(const Surface &out, const Rectangle &rect, int index, int count, std::string_view (* /*label*/)(int))
{
	const Colors c = SliderColors();
	const int cx = rect.position.x + rect.size.width / 2;
	const int y = rect.position.y;
	const int h = rect.size.height;
	if (h < 40 || count < 2)
		return;
	// A wide channel and a big framed handle with the diamond from the title ornament.
	const int knobH = 34;
	const int top = y + knobH / 2;
	const int railH = h - knobH;
	const int ky = top + railH - 1 - (railH - 1) * index / (count - 1);
	DrawRail(out, cx, top, railH, 22, ky, index, count);
	const int knobW = rect.size.width;
	DrawFramedBox(out, cx - knobW / 2, ky - knobH / 2, knobW, knobH, c.redDark, true);
	DrawSliderOrnament(out, c, { cx, ky }, knobW - 16);
}

void DrawEditBox(const Surface &out, const Rectangle &rect, bool focused)
{
	const Colors &c = GetColors();
	const int x = rect.position.x;
	const int y = rect.position.y;
	const int w = rect.size.width;
	const int h = rect.size.height;
	DrawHalfTransparentRectTo(out, x, y, w, h);
	DrawHalfTransparentRectTo(out, x, y, w, h);
	DrawHorizontalLine(out, { x, y }, w, focused ? c.gold : c.stoneLight);
	DrawHorizontalLine(out, { x, y + h - 1 }, w, focused ? c.goldBright : c.stoneLight);
	DrawVerticalLine(out, { x, y }, h, focused ? c.gold : c.stoneLight);
	DrawVerticalLine(out, { x + w - 1, y }, h, focused ? c.gold : c.stoneLight);
}

} // namespace theme

} // namespace devilution
