#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "DiabloUI/text_input.hpp"
#include "DiabloUI/ui_flags.hpp"
#include "engine/clx_sprite.hpp"
#include "engine/render/text_render.hpp"
#include "utils/enum_traits.h"
#include "utils/string_or_view.hpp"
#include "utils/stubs.h"

namespace devilution {

enum class UiType : uint8_t {
	Text,
	ArtText,
	ArtTextButton,
	ImageClx,
	ImageAnimatedClx,
	Button,
	List,
	Scrollbar,
	Edit,
	Ornament,
	ThemeScrollbar,
	Panel,
	Gauge,
	CheckBox,
};

class UiItemBase {
public:
	virtual ~UiItemBase() = default;

	[[nodiscard]] UiType GetType() const
	{
		return type_;
	}

	[[nodiscard]] bool IsType(UiType testType) const
	{
		return type_ == testType;
	}

	[[nodiscard]] UiFlags GetFlags() const
	{
		return uiFlags_;
	}

	[[nodiscard]] bool IsHidden() const
	{
		return HasAnyOf(uiFlags_, UiFlags::ElementHidden);
	}

	[[nodiscard]] bool IsNotInteractive() const
	{
		return HasAnyOf(uiFlags_, UiFlags::ElementHidden | UiFlags::ElementDisabled);
	}

	void Hide()
	{
		uiFlags_ |= UiFlags::ElementHidden;
	}

	void Show()
	{
		uiFlags_ &= ~UiFlags::ElementHidden;
	}

protected:
	UiItemBase(UiType type, SDL_Rect rect, UiFlags flags)
	    : type_(type)
	    , m_rect(rect)
	    , uiFlags_(flags)
	{
	}

	void SetFlags(UiFlags flags)
	{
		uiFlags_ = flags;
	}

private:
	UiType type_;

public:
	SDL_Rect m_rect;

private:
	UiFlags uiFlags_;
};

//=============================================================================
class UiImageClx : public UiItemBase {
public:
	UiImageClx(ClxSprite sprite, SDL_Rect rect, UiFlags flags = UiFlags::None)
	    : UiItemBase(UiType::ImageClx, rect, flags)
	    , sprite_(sprite)
	{
	}

	[[nodiscard]] bool isCentered() const
	{
		return HasAnyOf(GetFlags(), UiFlags::AlignCenter);
	}

	[[nodiscard]] ClxSprite sprite() const
	{
		return sprite_;
	}

	void setSprite(ClxSprite sprite)
	{
		sprite_ = sprite;
	}

private:
	ClxSprite sprite_;
};

//=============================================================================
class UiImageAnimatedClx : public UiItemBase {
public:
	UiImageAnimatedClx(ClxSpriteList list, SDL_Rect rect, UiFlags flags = UiFlags::None)
	    : UiItemBase(UiType::ImageAnimatedClx, rect, flags)
	    , list_(list)
	{
	}

	[[nodiscard]] bool isCentered() const
	{
		return HasAnyOf(GetFlags(), UiFlags::AlignCenter);
	}

	[[nodiscard]] ClxSprite sprite(uint16_t frame) const
	{
		return list_[frame];
	}

	[[nodiscard]] uint16_t numFrames() const
	{
		return list_.numSprites();
	}

private:
	ClxSpriteList list_;
};

//=============================================================================

class UiArtText : public UiItemBase {
public:
	/**
	 * @brief Constructs a UI element containing a (presumed to be) static line of text
	 * @param text Pointer to the first character of a c-string
	 * @param rect screen region defining the area to draw the text
	 * @param flags UiFlags controlling color/alignment/size
	 * @param spacing Spacing between characters
	 * @param lineHeight Vertical distance between text lines
	 */
	UiArtText(const char *text, SDL_Rect rect, UiFlags flags = UiFlags::None, int spacing = 1, int lineHeight = -1)
	    : UiItemBase(UiType::ArtText, rect, flags)
	    , text_(text)
	    , spacing_(spacing)
	    , lineHeight_(lineHeight)
	{
	}

	/**
	 * @brief Constructs a UI element containing a line of text that may change between frames
	 * @param ptext Pointer to a c-string (pointer to a pointer to the first character)
	 * @param rect screen region defining the area to draw the text
	 * @param flags UiFlags controlling color/alignment/size
	 * @param spacing Spacing between characters
	 * @param lineHeight Vertical distance between text lines
	 */
	UiArtText(const char **ptext, SDL_Rect rect, UiFlags flags = UiFlags::None, int spacing = 1, int lineHeight = -1)
	    : UiItemBase(UiType::ArtText, rect, flags)
	    , textPointer_(ptext)
	    , spacing_(spacing)
	    , lineHeight_(lineHeight)
	{
	}

	[[nodiscard]] std::string_view GetText() const
	{
		if (text_ != nullptr)
			return text_;
		return *textPointer_;
	}

	[[nodiscard]] int GetSpacing() const
	{
		return spacing_;
	}

	[[nodiscard]] int GetLineHeight() const
	{
		return lineHeight_;
	}

private:
	const char *text_ = nullptr;
	const char **textPointer_ = nullptr;
	int spacing_;
	int lineHeight_;
};

//=============================================================================

class UiScrollbar : public UiItemBase {
public:
	UiScrollbar(ClxSprite bg, ClxSprite thumb, ClxSpriteList arrow, SDL_Rect rect, UiFlags flags = UiFlags::None)
	    : UiItemBase(UiType::Scrollbar, rect, flags)
	    , m_bg(bg)
	    , m_thumb(thumb)
	    , m_arrow(arrow)
	{
	}

	// private:
	ClxSprite m_bg;
	ClxSprite m_thumb;
	ClxSpriteList m_arrow;
};

//=============================================================================

class UiArtTextButton : public UiItemBase {
public:
	using Callback = void (*)();

	UiArtTextButton(std::string_view text, Callback action, SDL_Rect rect, UiFlags flags = UiFlags::None)
	    : UiItemBase(UiType::ArtTextButton, rect, flags)
	    , text_(text)
	    , action_(action)
	{
	}

	void SetFlags(UiFlags flags)
	{
		UiItemBase::SetFlags(flags);
	}

	[[nodiscard]] std::string_view GetText() const
	{
		return text_;
	}

	void Activate() const
	{
		action_();
	}

	/** DETH: draw as a themed button. */
	bool themed = false;

private:
	std::string_view text_;
	Callback action_;
};

//=============================================================================

/** DETH: a stepped gauge (theme::DrawGauge) that follows two integers. */
/** DETH: a read-only checkbox (theme::DrawCheckBox). */
class UiCheckBox : public UiItemBase {
public:
	UiCheckBox(SDL_Rect rect, bool checked)
	    : UiItemBase(UiType::CheckBox, rect, UiFlags::None)
	    , checked(checked)
	{
	}
	bool checked;
};

class UiGauge : public UiItemBase {
public:
	UiGauge(SDL_Rect rect, const int *index, const int *count, void (*onSet)(int) = nullptr, bool vertical = false, void (*onRelease)() = nullptr, std::string_view (*label)(int) = nullptr)
	    : UiItemBase(UiType::Gauge, rect, UiFlags::None)
	    , index_(index)
	    , count_(count)
	    , onSet_(onSet)
	    , onRelease_(onRelease)
	    , label_(label)
	    , vertical_(vertical)
	{
	}
	/** DETH: the text for a step (vertical gauges show it), or nullptr. */
	[[nodiscard]] std::string_view (*label() const)(int) { return label_; }
	/** DETH: a vertical gauge has its top step at the top. */
	[[nodiscard]] bool vertical() const { return vertical_; }
	void SetFromPoint(int x, int y) const
	{
		if (!vertical_) {
			SetFromX(x);
			return;
		}
		if (onSet_ == nullptr || *count_ <= 1)
			return;
		const int h = std::max(1, static_cast<int>(m_rect.h) - 1);
		const int rel = h - std::clamp(y - m_rect.y, 0, h);
		const int step = (rel * (*count_ - 1) + h / 2) / h;
		onSet_(std::clamp(step, 0, *count_ - 1));
	}
	/** DETH: called when the finger or mouse lets go. */
	void Release() const
	{
		if (onRelease_ != nullptr)
			onRelease_();
	}
	[[nodiscard]] int index() const { return *index_; }
	[[nodiscard]] int count() const { return *count_; }
	/** DETH: tapping or dragging along the gauge picks the step under the finger. */
	[[nodiscard]] bool interactive() const { return onSet_ != nullptr; }
	void SetFromX(int x) const
	{
		if (onSet_ == nullptr || *count_ <= 1)
			return;
		const int rel = std::clamp(x - m_rect.x, 0, std::max(1, static_cast<int>(m_rect.w) - 1));
		const int step = (rel * (*count_ - 1) + m_rect.w / 2) / std::max(1, static_cast<int>(m_rect.w) - 1);
		onSet_(std::clamp(step, 0, *count_ - 1));
	}

private:
	const int *index_;
	const int *count_;
	void (*onSet_)(int);
	void (*onRelease_)();
	std::string_view (*label_)(int);
	bool vertical_;
};

/** DETH: a framed panel (theme::DrawPanel). */
class UiPanel : public UiItemBase {
public:
	explicit UiPanel(SDL_Rect rect, bool opaque = false)
	    : UiItemBase(UiType::Panel, rect, UiFlags::None)
	    , opaque(opaque)
	{
	}
	/** DETH: sheets are solid so the page behind cannot bleed through the text. */
	bool opaque;
};

/** DETH: slim scrollbar for the screen's list, drawn by the theme from the list state. */
class UiThemeScrollbar : public UiItemBase {
public:
	explicit UiThemeScrollbar(SDL_Rect rect)
	    : UiItemBase(UiType::ThemeScrollbar, rect, UiFlags::None)
	{
	}
};

/** DETH: a thin ornamental divider (theme::DrawOrnament) centered in its rect. */
class UiOrnament : public UiItemBase {
public:
	explicit UiOrnament(SDL_Rect rect)
	    : UiItemBase(UiType::Ornament, rect, UiFlags::None)
	{
	}
};

//=============================================================================

class UiEdit : public UiItemBase {
public:
	UiEdit(std::string_view hint, char *value, std::size_t maxLength, bool allowEmpty, SDL_Rect rect, UiFlags flags = UiFlags::None)
	    : UiItemBase(UiType::Edit, rect, flags)
	    , m_hint(hint)
	    , m_value(value)
	    , m_max_length(maxLength)
	    , m_allowEmpty(allowEmpty)
	{
	}

	// private:
	std::string_view m_hint;
	char *m_value;
	std::size_t m_max_length;
	TextInputCursorState m_cursor;
	bool m_allowEmpty;
	/** DETH: draw as a themed text box. */
	bool themed = false;
	/** DETH: what the field holds; picks the Android keyboard layout. */
	enum class InputKind : uint8_t {
		Text,
		Address,
		Password,
	};
	InputKind inputKind = InputKind::Text;
};

//=============================================================================

// Plain text

class UiText : public UiItemBase {
public:
	UiText(std::string_view text, SDL_Rect rect, UiFlags flags = UiFlags::ColorDialogWhite)
	    : UiItemBase(UiType::Text, rect, flags)
	    , text_(text)
	{
	}

	[[nodiscard]] std::string_view GetText() const
	{
		return text_;
	}

private:
	std::string_view text_;
};

//=============================================================================

// A button (uses Diablo sprites)

class UiButton : public UiItemBase {
public:
	using Callback = void (*)();

	UiButton(std::string_view text, Callback action, SDL_Rect rect, UiFlags flags = UiFlags::None)
	    : UiItemBase(UiType::Button, rect, flags)
	    , text_(text)
	    , action_(action)
	    , pressed_(false)
	{
	}

	[[nodiscard]] std::string_view GetText() const
	{
		return text_;
	}

	void Activate() const
	{
		action_();
	}

	[[nodiscard]] bool IsPressed() const
	{
		return pressed_;
	}

	void Press()
	{
		pressed_ = true;
	}

	void Release()
	{
		pressed_ = false;
	}

private:
	std::string_view text_;
	Callback action_;

	// State
	bool pressed_;
};

//=============================================================================

class UiListItem {
public:
	UiListItem(StringOrView &&text = {}, int value = 0, UiFlags uiFlags = UiFlags::None)
	    : m_text(std::move(text))
	    , m_value(value)
	    , uiFlags(uiFlags)
	{
	}

	UiListItem(StringOrView &&text, std::vector<DrawStringFormatArg> &args, int value = 0, UiFlags uiFlags = UiFlags::None)
	    : m_text(std::move(text))
	    , args(args)
	    , m_value(value)
	    , uiFlags(uiFlags)
	{
	}

	// private:
	StringOrView m_text;
	std::vector<DrawStringFormatArg> args;
	int m_value;
	UiFlags uiFlags;
	/** DETH (themed lists): value text drawn right-aligned in silver. */
	std::string rightText;
	/** DETH (themed lists): -1 no toggle, 0 off, 1 on; drawn as a switch on the right. */
	int8_t toggle = -1;
	/** DETH (themed lists): one-line description drawn under the text in small type. */
	std::string subText;
	/** DETH: this row's height in percent of the list's row height (Continue is taller, recent saves shorter). */
	uint8_t heightPercent = 100;
};

class UiList : public UiItemBase {
public:
	using vUiListItem = std::vector<std::unique_ptr<UiListItem>>;

	UiList(const vUiListItem &vItems, size_t viewportMaxSize, Sint16 x, Sint16 y, Uint16 item_width, Uint16 item_height, UiFlags flags = UiFlags::None, int spacing = 1)
	    : UiList(PrivateConstructor {}, vItems, std::min<size_t>(viewportMaxSize, vItems.size()), x, y, item_width, item_height, flags, spacing)
	{
	}

	[[nodiscard]] SDL_Rect itemRect(int i) const
	{
		SDL_Rect tmp;
		tmp.x = m_x;
		tmp.y = m_y + m_height * i;
		tmp.w = m_width;
		tmp.h = m_height;

		return tmp;
	}

	/** DETH: true when some rows have their own height (see UiListItem::heightPercent). */
	[[nodiscard]] bool VariableHeights() const
	{
		for (const UiListItem *item : m_vecItems) {
			if (item->heightPercent != 100)
				return true;
		}
		return false;
	}
	[[nodiscard]] int RowHeight(size_t i) const
	{
		return m_height * m_vecItems[i]->heightPercent / 100;
	}
	/** DETH: the rectangle of row i when the list shows rows from `offset` on. */
	[[nodiscard]] SDL_Rect itemRectFrom(size_t offset, size_t i) const
	{
		int y = m_y;
		for (size_t k = offset; k < i; k++)
			y += RowHeight(k);
		return { m_x, static_cast<Sint16>(y), m_width, static_cast<Uint16>(RowHeight(i)) };
	}
	/** DETH: how many whole rows fit in the list's area from `offset` on. */
	[[nodiscard]] size_t RowsFitting(size_t offset) const
	{
		int used = 0;
		size_t rows = 0;
		for (size_t k = offset; k < m_vecItems.size(); k++) {
			used += RowHeight(k);
			if (used > m_rect.h)
				break;
			rows++;
		}
		return std::max<size_t>(rows, 1);
	}
	/** DETH: the row at `y` pixels below the top of the list area, or the item count. */
	[[nodiscard]] size_t IndexFrom(size_t offset, int y) const
	{
		int top = 0;
		for (size_t k = offset; k < m_vecItems.size(); k++) {
			const int h = RowHeight(k);
			if (y < top + h)
				return k;
			top += h;
		}
		return m_vecItems.size();
	}

	[[nodiscard]] size_t indexAt(Sint16 y) const
	{
		ASSERT(y >= m_rect.y);
		const size_t index = (y - m_rect.y) / m_height;
		ASSERT(index < m_vecItems.size());
		return index;
	}

	[[nodiscard]] UiListItem *GetItem(std::size_t i) const
	{
		return m_vecItems[i];
	}

	[[nodiscard]] int GetSpacing() const
	{
		return spacing_;
	}

	[[nodiscard]] bool IsPressed(size_t index) const
	{
		return pressed_item_index_ == index;
	}

	void Press(size_t index)
	{
		pressed_item_index_ = index;
	}

	void Release()
	{
		pressed_item_index_ = -1;
	}

	/** DETH: the selected row is held down (touch or mouse). */
	[[nodiscard]] bool pressed() const
	{
		return pressed_item_index_ != static_cast<size_t>(-1);
	}

	/** DETH: draw rows as themed cards instead of bare text with the pentagram selector. */
	bool themed = false;

	// private:
	size_t viewportSize;
	Sint16 m_x, m_y;
	Uint16 m_width, m_height;
	std::vector<UiListItem *> m_vecItems;

private:
	struct PrivateConstructor final {
	};

	UiList(PrivateConstructor tag, const vUiListItem &vItems, size_t viewportSize, Sint16 x, Sint16 y, Uint16 item_width, Uint16 item_height, UiFlags flags, int spacing)
	    : UiItemBase(UiType::List, { x, y, item_width, static_cast<Uint16>(item_height * viewportSize) }, flags)
	    , viewportSize(viewportSize)
	    , m_x(x)
	    , m_y(y)
	    , m_width(item_width)
	    , m_height(item_height)
	    , spacing_(spacing)
	{
		for (const auto &item : vItems)
			m_vecItems.push_back(item.get());

		pressed_item_index_ = -1;
	}

	int spacing_;

	// State
	size_t pressed_item_index_;
};
} // namespace devilution
