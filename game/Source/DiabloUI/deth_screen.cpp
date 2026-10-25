/**
 * @file deth_screen.cpp
 *
 * DETH: layout helpers for themed pre-game screens.
 */
#include "DiabloUI/deth_screen.hpp"

#include "engine/render/text_render.hpp"

#include <algorithm>

#include "DiabloUI/diabloui.h"
#include "DiabloUI/ui_flags.hpp"
#include "utils/language.h"
#include "utils/ui_fwd.h"

namespace devilution {

namespace theme {

int ScreenFrame::RowPitch() const
{
	return layout.rowHeight + layout.rowGap;
}

int ScreenFrame::RowsThatFit(int headerHeight) const
{
	return std::max(1, (content.size.height - headerHeight) / RowPitch());
}

ScreenFrame MakeScreenFrame(bool wide, bool sidePane, bool buttons)
{
	ScreenFrame f {};
	f.layout = GetLayout();
	const Layout &l = f.layout;
	const Rectangle column = wide ? l.wideColumn : l.column;
	// DETH: on a big screen (small UI scale) the screen keeps a sensible height and sits in the middle.
	constexpr int MaxFrameHeight = 720;
	const int height = std::min(gnScreenHeight - l.margin, MaxFrameHeight);
	const int top = (gnScreenHeight - height) / 2;
	const int bottom = top + height;
	f.frame = Rectangle { { column.position.x, top }, { column.size.width, bottom - top } };

	f.title = Rectangle { { column.position.x, top }, { column.size.width, l.titleHeight } };
	if (l.touch) {
		// A big Back button lives in the top-left corner of every touch screen.
		const int size = std::max(TouchTarget(), 40);
		f.back = Rectangle { { l.margin, top }, { std::max(size * 2, 96), size } };
	}
	int contentTop = top + l.titleHeight + 10;

	int contentBottom = bottom;
	if (buttons) {
		const int buttonHeight = std::clamp(l.rowHeight, 32, 44);
		f.buttons = Rectangle { { column.position.x, bottom - buttonHeight }, { column.size.width, buttonHeight } };
		contentBottom = f.buttons.position.y - 10;
	}

	int contentX = column.position.x;
	int contentWidth = column.size.width;
	if (sidePane && column.size.width >= 600) {
		const int sideWidth = LargeText() ? 300 : 200;
		f.side = Rectangle { { column.position.x, contentTop }, { sideWidth, contentBottom - contentTop } };
		contentX += sideWidth + 20;
		contentWidth -= sideWidth + 20;
	}
	f.content = Rectangle { { contentX, contentTop }, { contentWidth, contentBottom - contentTop } };
	return f;
}

namespace {

SDL_Rect ToSdl(const Rectangle &r)
{
	return SDL_Rect { static_cast<Sint16>(r.position.x), static_cast<Sint16>(r.position.y), static_cast<Uint16>(r.size.width), static_cast<Uint16>(r.size.height) };
}

void AddOrnament(std::vector<std::unique_ptr<UiItemBase>> &items, const ScreenFrame &frame)
{
	const Rectangle &t = frame.title;
	items.push_back(std::make_unique<UiOrnament>(SDL_Rect { static_cast<Sint16>(t.position.x + t.size.width / 6), static_cast<Sint16>(t.position.y + t.size.height - 2), static_cast<Uint16>(t.size.width * 2 / 3), 8 }));
}

} // namespace

void AddBackButton(std::vector<std::unique_ptr<UiItemBase>> &items, const ScreenFrame &frame)
{
	if (frame.back.size.width == 0)
		return;
	auto button = std::make_unique<UiArtTextButton>(_("Back"), &UiFocusNavigationEsc, ToSdl(frame.back), UiFlags::FontSize24 | UiFlags::ColorUiGold);
	button->themed = true;
	items.push_back(std::move(button));
}

void AddTitle(std::vector<std::unique_ptr<UiItemBase>> &items, const char *text, const ScreenFrame &frame, bool withBack)
{
	Rectangle r = frame.title;
	r.size.height -= 8;
	items.push_back(std::make_unique<UiArtText>(text, ToSdl(r), frame.layout.titleFont | UiFlags::ColorUiGold | UiFlags::AlignCenter | UiFlags::VerticalCenter));
	AddOrnament(items, frame);
	if (withBack)
		AddBackButton(items, frame);
}

void AddTitle(std::vector<std::unique_ptr<UiItemBase>> &items, const char **textPointer, const ScreenFrame &frame, bool withBack)
{
	Rectangle r = frame.title;
	r.size.height -= 8;
	items.push_back(std::make_unique<UiArtText>(textPointer, ToSdl(r), frame.layout.titleFont | UiFlags::ColorUiGold | UiFlags::AlignCenter | UiFlags::VerticalCenter));
	AddOrnament(items, frame);
	if (withBack)
		AddBackButton(items, frame);
}

int AddHeading(std::vector<std::unique_ptr<UiItemBase>> &items, const char *text, const ScreenFrame &frame, int y)
{
	const int height = frame.layout.compact ? 30 : 36;
	const Rectangle r { { frame.content.position.x, y }, { frame.content.size.width, height } };
	items.push_back(std::make_unique<UiArtText>(text, ToSdl(r), UiFlags::FontSize24 | UiFlags::ColorUiSilver | UiFlags::AlignCenter | UiFlags::VerticalCenter));
	return height + 6;
}

int AddNote(std::vector<std::unique_ptr<UiItemBase>> &items, std::string_view text, const ScreenFrame &frame, int y)
{
	// The strings must outlive the items, which keep only pointers.
	static std::vector<std::unique_ptr<std::string>> lines;
	lines.clear();
	const int width = frame.content.size.width - 24;
	const std::string wrapped = WordWrapString(text, static_cast<unsigned>(width), GameFont12);
	constexpr int LineHeight = 18;
	int used = 0;
	size_t start = 0;
	while (start <= wrapped.size()) {
		size_t end = wrapped.find('\n', start);
		if (end == std::string::npos)
			end = wrapped.size();
		lines.push_back(std::make_unique<std::string>(wrapped.substr(start, end - start)));
		const Rectangle r { { frame.content.position.x, y + used }, { frame.content.size.width, LineHeight } };
		items.push_back(std::make_unique<UiArtText>(lines.back()->c_str(), ToSdl(r), UiFlags::FontSize12 | UiFlags::ColorUiSilver | UiFlags::AlignCenter));
		used += LineHeight;
		start = end + 1;
	}
	return used + 8;
}

std::vector<UiArtTextButton *> AddButtons(std::vector<std::unique_ptr<UiItemBase>> &items, const ScreenFrame &frame, const std::vector<ButtonSpec> &specs)
{
	std::vector<UiArtTextButton *> result;
	if (specs.empty() || frame.buttons.size.height == 0)
		return result;
	// Touch mode has the Back button in the corner, so a bottom Cancel would be a duplicate target.
	std::vector<ButtonSpec> shown;
	for (const ButtonSpec &spec : specs) {
		if (frame.layout.touch && spec.action == &UiFocusNavigationEsc)
			continue;
		shown.push_back(spec);
	}
	if (shown.empty())
		return result;
	const int gap = 12;
	const int count = static_cast<int>(shown.size());
	const int maxWidth = 200;
	int width = (frame.buttons.size.width - gap * (count - 1)) / count;
	width = std::min(width, maxWidth);
	const int total = width * count + gap * (count - 1);
	int x = frame.buttons.position.x + (frame.buttons.size.width - total) / 2;
	for (const ButtonSpec &spec : shown) {
		const SDL_Rect rect { static_cast<Sint16>(x), static_cast<Sint16>(frame.buttons.position.y), static_cast<Uint16>(width), static_cast<Uint16>(frame.buttons.size.height) };
		auto button = std::make_unique<UiArtTextButton>(spec.text, spec.action, rect, UiFlags::FontSize24 | UiFlags::ColorUiGold | spec.extraFlags);
		button->themed = true;
		result.push_back(button.get());
		items.push_back(std::move(button));
		x += width + gap;
	}
	return result;
}

ScreenFrame ButtonsBelow(const ScreenFrame &frame, const Rectangle &column, int y)
{
	ScreenFrame moved = frame;
	if (frame.buttons.size.height > 0)
		moved.buttons = { { column.position.x, y }, { column.size.width, frame.buttons.size.height } };
	return moved;
}

std::unique_ptr<UiList> MakeList(const std::vector<std::unique_ptr<UiListItem>> &listItems, const ScreenFrame &frame, int y, size_t maxRows, UiFlags flags, int extraRowHeight)
{
	const int pitch = frame.RowPitch() + extraRowHeight;
	const int available = std::max(1, (frame.content.position.y + frame.content.size.height - y) / pitch);
	const size_t rows = std::min<size_t>({ maxRows, listItems.size(), static_cast<size_t>(available) });
	// Leave room for the scrollbar when the list scrolls.
	const int width = frame.content.size.width - (rows < listItems.size() ? 18 : 0);
	auto list = std::make_unique<UiList>(listItems, rows, static_cast<Sint16>(frame.content.position.x), static_cast<Sint16>(y), static_cast<Uint16>(width), static_cast<Uint16>(pitch), flags, frame.layout.rowGap);
	list->themed = true;
	return list;
}

void AddScrollbar(std::vector<std::unique_ptr<UiItemBase>> &items, const ScreenFrame &frame, int y, int height)
{
	items.push_back(std::make_unique<UiThemeScrollbar>(SDL_Rect { static_cast<Sint16>(frame.content.position.x + frame.content.size.width - 12), static_cast<Sint16>(y), 12, static_cast<Uint16>(height) }));
}

} // namespace theme

} // namespace devilution
