#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <forward_list>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#ifdef USE_SDL3
#include <SDL3/SDL_version.h>

#ifndef NOSOUND
#include <SDL3/SDL_audio.h>
#endif
#else
#include <SDL_version.h>
#endif

#include <ankerl/unordered_dense.h>
#include <function_ref.hpp>

#include "appfat.h"
#include "controls/controller_buttons.h"
#include "engine/size.hpp"
#include "engine/sound_defs.hpp"
#include "mods/mod_identity.h"
#include "pack.h"
#include "quick_messages.hpp"
#include "utils/enum_traits.h"
#include "utils/string_view_hash.hpp"

namespace devilution {

#ifndef DEFAULT_WIDTH
#define DEFAULT_WIDTH 640
#endif
#ifndef DEFAULT_HEIGHT
#define DEFAULT_HEIGHT 480
#endif

enum class StartUpGameMode : uint8_t {
	/** @brief If hellfire is present, asks the user what game they want to start. */
	Ask = 0,
	Hellfire = 1,
	Diablo = 2,
};

enum class StartUpIntro : uint8_t {
	Off = 0,
	Once = 1,
	On = 2,
};

/** @brief Defines what splash screen should be shown at startup. */
/** @brief DETH: which GitHub releases the in-game updater offers. */
enum class UpdateChannel : uint8_t {
	/** Full releases only. */
	Stable = 0,
	/** Pre-releases (betas) as well. */
	Beta = 1,
};

enum class StartUpSplash : uint8_t {
	/** @brief Show no splash screen. */
	None = 0,
	/** @brief Show only TitleDialog. */
	TitleDialog = 1,
	/** @brief Show Logo and TitleDialog. */
	LogoAndTitleDialog = 2,
};

enum class ScalingQuality : uint8_t {
	NearestPixel,
	BilinearFiltering,
	AnisotropicFiltering,
};

enum class FrameRateControl : uint8_t {
	None = 0,
#ifndef USE_SDL1
	VerticalSync = 1,
#endif
	CPUSleep = 2,
};

enum class Resampler : uint8_t {
#ifdef DEVILUTIONX_RESAMPLER_SPEEX
	Speex = 0,
#endif
#ifdef DVL_AULIB_SUPPORTS_SDL_RESAMPLER
	SDL,
#endif
};

enum class StoreUi : uint8_t {
	/** @brief Vanilla Diablo UI. */
	Text = 0,
	/** @brief Show item graphics to the left of item descriptions in store menus. */
	ListWithItemGraphics = 1,
	/** @brief Use visual grid-based store UI instead of text-based menus. */
	VisualGrid = 2,
};

/** @brief DETH: screen corner of the minimap. */
enum class MinimapCorner : uint8_t {
	TopRight = 0,
	TopLeft = 1,
	BottomLeft = 2,
	BottomRight = 3,
};

enum class PartyPortraits : uint8_t {
	/** @brief No party portraits. */
	Disabled = 0,
	/** @brief All other players. */
	Players = 1,
	/** @brief Summons. */
	Summons = 2,
	/** @brief All other players and summons. */
	PlayersAndSummons = 3,
};

std::string_view ResamplerToString(Resampler resampler);
std::optional<Resampler> ResamplerFromString(std::string_view resampler);

enum class OptionEntryType : uint8_t {
	Boolean,
	List,
	Key,
	PadButton,
};

enum class OptionEntryFlags : uint8_t {
	/** @brief No special logic. */
	None = 0,
	/** @brief Shouldn't be shown in settings dialog. */
	Invisible = 1 << 0,
	/** @brief Need to restart the current running game (single- or multiplayer) to take effect. */
	CantChangeInGame = 1 << 1,
	/** @brief Need to restart the current running multiplayer game to take effect. */
	CantChangeInMultiPlayer = 1 << 2,
	/** @brief Option is only relevant for Hellfire. */
	OnlyHellfire = 1 << 3,
	/** @brief Option is only relevant for Diablo. */
	OnlyDiablo = 1 << 4,
	/** @brief After option is changed the UI needs to be recreated. */
	RecreateUI = 1 << 5,
	/** @brief diablo.mpq must be present. */
	NeedDiabloMpq = 1 << 6,
};
use_enum_as_flags(OptionEntryFlags);

class OptionEntryBase {
public:
	OptionEntryBase(std::string_view key, OptionEntryFlags flags, const char *name, const char *description)
	    : flags(flags)
	    , key(key)
	    , name(name)
	    , description(description)
	{
	}
	[[nodiscard]] virtual std::string_view GetName() const;
	[[nodiscard]] std::string_view GetDescription() const;
	[[nodiscard]] virtual OptionEntryType GetType() const = 0;
	[[nodiscard]] OptionEntryFlags GetFlags() const;

	void SetValueChangedCallback(tl::function_ref<void()> callback);

	[[nodiscard]] virtual std::string_view GetValueDescription() const = 0;
	virtual void LoadFromIni(std::string_view category) = 0;
	virtual void SaveToIni(std::string_view category) const = 0;

	OptionEntryFlags flags;

public:
	std::string_view key;

protected:
	const char *name;
	const char *description;
	void NotifyValueChanged();

private:
	std::optional<tl::function_ref<void()>> callback_;
};

class OptionEntryBoolean : public OptionEntryBase {
public:
	OptionEntryBoolean(std::string_view key, OptionEntryFlags flags, const char *name, const char *description, bool defaultValue)
	    : OptionEntryBase(key, flags, name, description)
	    , defaultValue(defaultValue)
	    , value(defaultValue)
	{
	}
	[[nodiscard]] bool operator*() const
	{
		return value;
	}
	void SetValue(bool value);

	[[nodiscard]] OptionEntryType GetType() const override;
	[[nodiscard]] std::string_view GetValueDescription() const override;
	void LoadFromIni(std::string_view category) override;
	void SaveToIni(std::string_view category) const override;

private:
	bool defaultValue;
	bool value;
};

class OptionEntryListBase : public OptionEntryBase {
public:
	[[nodiscard]] virtual size_t GetListSize() const = 0;
	[[nodiscard]] virtual std::string_view GetListDescription(size_t index) const = 0;
	[[nodiscard]] virtual size_t GetActiveListIndex() const = 0;
	virtual void SetActiveListIndex(size_t index) = 0;
	/** @brief DETH: numeric steps (percent, sizes); shown with minus/plus controls on touch screens. */
	[[nodiscard]] virtual bool IsStepped() const { return false; }

	[[nodiscard]] OptionEntryType GetType() const override;
	[[nodiscard]] std::string_view GetValueDescription() const override;

protected:
	OptionEntryListBase(std::string_view key, OptionEntryFlags flags, const char *name, const char *description)
	    : OptionEntryBase(key, flags, name, description)
	{
	}
};

class OptionEntryEnumBase : public OptionEntryListBase {
public:
	void LoadFromIni(std::string_view category) override;
	void SaveToIni(std::string_view category) const override;

	[[nodiscard]] size_t GetListSize() const override;
	[[nodiscard]] std::string_view GetListDescription(size_t index) const override;
	[[nodiscard]] size_t GetActiveListIndex() const override;
	void SetActiveListIndex(size_t index) override;

protected:
	OptionEntryEnumBase(std::string_view key, OptionEntryFlags flags, const char *name, const char *description, int defaultValue)
	    : OptionEntryListBase(key, flags, name, description)
	    , defaultValue(defaultValue)
	    , value(defaultValue)
	{
	}

	[[nodiscard]] int GetValueInternal() const
	{
		return value;
	}
	void SetValueInternal(int value);

	void AddEntry(int value, std::string_view name);

private:
	int defaultValue;
	int value;
	std::vector<std::string_view> entryNames;
	std::vector<int> entryValues;
};

template <typename T>
class OptionEntryEnum : public OptionEntryEnumBase {
public:
	OptionEntryEnum(std::string_view key, OptionEntryFlags flags, const char *name, const char *description, T defaultValue, std::initializer_list<std::pair<T, std::string_view>> entries)
	    : OptionEntryEnumBase(key, flags, name, description, static_cast<int>(defaultValue))
	{
		for (auto &&[entryValue, entryName] : entries) {
			AddEntry(static_cast<int>(entryValue), entryName);
		}
	}
	[[nodiscard]] T operator*() const
	{
		return static_cast<T>(GetValueInternal());
	}
	void SetValue(T value)
	{
		SetValueInternal(static_cast<int>(value));
	}
};

class OptionEntryIntBase : public OptionEntryListBase {
public:
	[[nodiscard]] bool IsStepped() const override { return GetListSize() > 2; }
	void LoadFromIni(std::string_view category) override;
	void SaveToIni(std::string_view category) const override;

	[[nodiscard]] size_t GetListSize() const override;
	[[nodiscard]] std::string_view GetListDescription(size_t index) const override;
	[[nodiscard]] size_t GetActiveListIndex() const override;
	void SetActiveListIndex(size_t index) override;

protected:
	OptionEntryIntBase(std::string_view key, OptionEntryFlags flags, const char *name, const char *description, int defaultValue)
	    : OptionEntryListBase(key, flags, name, description)
	    , defaultValue(defaultValue)
	    , value(defaultValue)
	{
	}

	[[nodiscard]] int GetValueInternal() const
	{
		return value;
	}
	void SetValueInternal(int value);

	void AddEntry(int value);

private:
	int defaultValue;
	int value;
	mutable std::vector<std::string> entryNames;
	std::vector<int> entryValues;
};

template <typename T>
class OptionEntryInt : public OptionEntryIntBase {
public:
	OptionEntryInt(std::string_view key, OptionEntryFlags flags, const char *name, const char *description, T defaultValue, std::initializer_list<T> entries)
	    : OptionEntryIntBase(key, flags, name, description, static_cast<int>(defaultValue))
	{
		for (auto entry : entries) {
			AddEntry(static_cast<int>(entry));
		}
	}
	OptionEntryInt(std::string_view key, OptionEntryFlags flags, const char *name, const char *description, T defaultValue)
	    : OptionEntryInt(key, flags, name, description, defaultValue, { defaultValue })
	{
	}
	[[nodiscard]] T operator*() const
	{
		return static_cast<T>(GetValueInternal());
	}
	void SetValue(T value)
	{
		SetValueInternal(static_cast<int>(value));
	}
};

/** @brief Percentage option (e.g. 150 shown as "150%"), used for zoom and UI scale. */
class OptionEntryScalePercent : public OptionEntryInt<int> {
public:
	OptionEntryScalePercent(std::string_view key, OptionEntryFlags flags, const char *name, const char *description, int defaultValue, std::initializer_list<int> entries)
	    : OptionEntryInt<int>(key, flags, name, description, defaultValue, entries)
	{
	}
	[[nodiscard]] std::string_view GetListDescription(size_t index) const override;

private:
	mutable std::vector<std::string> percentNames;
};

/** @brief DETH: a volume stored on the engine's scale (VOLUME_MIN..VOLUME_MAX), shown as 0%-100% in 10% steps. */
class OptionEntryVolume : public OptionEntryInt<int> {
public:
	OptionEntryVolume(std::string_view key, OptionEntryFlags flags, const char *name, const char *description, int defaultValue);
	[[nodiscard]] std::string_view GetListDescription(size_t index) const override;

private:
	mutable std::vector<std::string> percentNames;
};

class OptionEntryLanguageCode : public OptionEntryListBase {
public:
	OptionEntryLanguageCode();

	void LoadFromIni(std::string_view category) override;
	void SaveToIni(std::string_view category) const override;

	[[nodiscard]] size_t GetListSize() const override;
	[[nodiscard]] std::string_view GetListDescription(size_t index) const override;
	[[nodiscard]] size_t GetActiveListIndex() const override;
	void SetActiveListIndex(size_t index) override;

	std::string_view operator*() const
	{
		return szCode;
	}

	OptionEntryLanguageCode &operator=(std::string_view code)
	{
		assert(code.size() < 6);
		memcpy(szCode, code.data(), code.size());
		szCode[code.size()] = '\0';
		return *this;
	}

private:
	/** @brief Language code (ISO-15897) for text. */
	char szCode[6];
	mutable std::vector<std::pair<std::string, std::string>> languages;

	void CheckLanguagesAreInitialized() const;
};

class OptionEntryResolution : public OptionEntryListBase {
public:
	OptionEntryResolution();

	void LoadFromIni(std::string_view category) override;
	void SaveToIni(std::string_view category) const override;

	[[nodiscard]] size_t GetListSize() const override;
	[[nodiscard]] std::string_view GetListDescription(size_t index) const override;
	[[nodiscard]] size_t GetActiveListIndex() const override;
	void SetActiveListIndex(size_t index) override;

	void setAvailableResolutions(std::vector<std::pair<Size, std::string>> &&resolutions)
	{
		resolutions_ = std::move(resolutions);
	}

	Size operator*() const { return size_; }

	/** @brief DETH: true when the internal resolution follows the UI Scale option (the default). */
	[[nodiscard]] bool IsAutomatic() const { return size_.width == 0 || size_.height == 0; }

private:
	/** @brief View size; 0x0 means automatic (derived from the UI scale). */
	Size size_;
	std::vector<std::pair<Size, std::string>> resolutions_;
};

class OptionEntryResampler : public OptionEntryListBase {
public:
	OptionEntryResampler();

	void LoadFromIni(std::string_view category) override;
	void SaveToIni(std::string_view category) const override;

	[[nodiscard]] size_t GetListSize() const override;
	[[nodiscard]] std::string_view GetListDescription(size_t index) const override;
	[[nodiscard]] size_t GetActiveListIndex() const override;
	void SetActiveListIndex(size_t index) override;

	Resampler operator*() const
	{
		return resampler_;
	}

private:
	void UpdateDependentOptions() const;

	Resampler resampler_;
};

class OptionEntryAudioDevice : public OptionEntryListBase {
public:
	OptionEntryAudioDevice();

	void LoadFromIni(std::string_view category) override;
	void SaveToIni(std::string_view category) const override;

	[[nodiscard]] size_t GetListSize() const override;
	[[nodiscard]] std::string_view GetListDescription(size_t index) const override;
	[[nodiscard]] size_t GetActiveListIndex() const override;
	void SetActiveListIndex(size_t index) override;

	std::string operator*() const
	{
		for (size_t i = 0; i < GetListSize(); i++) {
			std::string_view deviceName = GetDeviceName(i);
			if (deviceName == deviceName_)
				return deviceName_;
		}
		return "";
	}

#ifdef USE_SDL3
	[[nodiscard]] SDL_AudioDeviceID id() const;
#endif

private:
	std::string_view GetDeviceName(size_t index) const;

	std::string deviceName_;
#ifdef USE_SDL3
	SDL_AudioDeviceID deviceId_ = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
#endif
};

struct OptionCategoryBase {
	OptionCategoryBase(std::string_view key, const char *name, const char *description)
	    : key(key)
	    , name(name)
	    , description(description)
	{
	}

	[[nodiscard]] std::string_view GetKey() const;
	[[nodiscard]] std::string_view GetName() const;
	[[nodiscard]] std::string_view GetDescription() const;

	virtual std::vector<OptionEntryBase *> GetEntries() = 0;

protected:
	std::string_view key;
	const char *name;
	const char *description;
};

struct GameModeOptions : OptionCategoryBase {
	GameModeOptions();
	std::vector<OptionEntryBase *> GetEntries() override;

	OptionEntryEnum<StartUpGameMode> gameMode;
	OptionEntryBoolean shareware;
};

struct StartUpOptions : OptionCategoryBase {
	StartUpOptions();
	std::vector<OptionEntryBase *> GetEntries() override;

	/** @brief Play game intro video on diablo startup. */
	OptionEntryEnum<StartUpIntro> diabloIntro;
	/** @brief Play game intro video on hellfire startup. */
	OptionEntryEnum<StartUpIntro> hellfireIntro;
	OptionEntryEnum<StartUpSplash> splash;
	/** @brief DETH: Stable or Beta releases for the in-game updater. */
	OptionEntryEnum<UpdateChannel> updateChannel;
	/** @brief DETH: look for an update when the game starts. */
	OptionEntryBoolean checkForUpdates;
};

struct DiabloOptions : OptionCategoryBase {
	DiabloOptions();
	std::vector<OptionEntryBase *> GetEntries() override;

	/** @brief Remembers what singleplayer hero/save was last used. */
	OptionEntryInt<std::uint32_t> lastSinglePlayerHero;
	/** @brief Remembers what multiplayer hero/save was last used. */
	OptionEntryInt<std::uint32_t> lastMultiplayerHero;
};

struct HellfireOptions : OptionCategoryBase {
	HellfireOptions();
	std::vector<OptionEntryBase *> GetEntries() override;

	/** @brief Cornerstone of the world item. */
	char szItem[sizeof(ItemPack) * 2 + 1];
	/** @brief Remembers what singleplayer hero/save was last used. */
	OptionEntryInt<std::uint32_t> lastSinglePlayerHero;
	/** @brief Remembers what multiplayer hero/save was last used. */
	OptionEntryInt<std::uint32_t> lastMultiplayerHero;
};

struct AudioOptions : OptionCategoryBase {
	AudioOptions();
	std::vector<OptionEntryBase *> GetEntries() override;

	/** @brief Movie and SFX volume. */
	OptionEntryVolume soundVolume;
	/** @brief Accessibility / navigation cues volume. */
	OptionEntryInt<int> audioCuesVolume;
	/** @brief Music volume. */
	OptionEntryVolume musicVolume;
	/** @brief Player emits sound when walking. */
	OptionEntryBoolean walkingSound;
	/** @brief DETH: footstep volume in percent of the sound volume (0 = off). */
	OptionEntryInt<int> footstepVolume;
	/** @brief Automatically equipping items on pickup emits the equipment sound. */
	OptionEntryBoolean autoEquipSound;
	/** @brief Picking up items emits the items pickup sound. */
	OptionEntryBoolean itemPickupSound;

	/** @brief Output sample rate (Hz). */
	OptionEntryInt<std::uint32_t> sampleRate;
	/** @brief The number of output channels (1 or 2) */
	OptionEntryInt<std::uint8_t> channels;
	/** @brief Buffer size (number of frames per channel) */
	OptionEntryInt<std::uint32_t> bufferSize;
	/** @brief Resampler implementation. */
	OptionEntryResampler resampler;
	/** @brief Quality of the resampler, from 0 (lowest) to 10 (highest). Available for the speex resampler only. */
	OptionEntryInt<std::uint8_t> resamplingQuality;
	/** @brief Audio device. */
	OptionEntryAudioDevice device;
};

struct GraphicsOptions : OptionCategoryBase {
	GraphicsOptions();
	std::vector<OptionEntryBase *> GetEntries() override;

	OptionEntryResolution resolution;
	/** @brief Run in fullscreen or windowed mode. */
	OptionEntryBoolean fullscreen;
#if !defined(USE_SDL1) || defined(__3DS__)
	/** @brief Expand the aspect ratio to match the screen. */
	OptionEntryBoolean fitToScreen;
#endif
#ifndef USE_SDL1
	/** @brief Scale the image after rendering. */
	OptionEntryBoolean upscale;
	/** @brief See SDL_HINT_RENDER_SCALE_QUALITY. */
	OptionEntryEnum<ScalingQuality> scaleQuality;
	/** @brief Only scale by values divisible by the width and height. */
	OptionEntryBoolean integerScaling;
#endif
	/** @brief Limit frame rate either for vsync or CPU load. */
	OptionEntryEnum<FrameRateControl> frameRateControl;
	/** @brief DETH: frames per second with Frame Rate Control set to Limit FPS. */
	OptionEntryInt<int> fpsLimit;
	/** @brief Brightness level. */
	OptionEntryInt<int> brightness;
	/** @brief World zoom in percent of screen pixels (100 = native art size). */
	OptionEntryScalePercent zoom;
	/** @brief DETH: which setting the left slider of the game menu shows (tap its name to switch). */
	OptionEntryInt<int> gameMenuSlider;
	/** @brief UI size in percent of screen pixels. Sets the internal resolution. */
	OptionEntryScalePercent uiScale;
	/** @brief DETH: bigger text and taller rows in the touch menus (Android). */
	OptionEntryBoolean largeText;
	/** @brief DETH: a UI Size slider on the main menu (on by default on Android). */
	OptionEntryBoolean uiScaleSlider;
	/** @brief Subtile lighting for smoother light gradients. */
	OptionEntryBoolean perPixelLighting;
	/** @brief Enable color cycling animations. */
	OptionEntryBoolean colorCycling;
	/** @brief Use alternate nest palette. */
	OptionEntryBoolean alternateNestArt;
#if SDL_VERSION_ATLEAST(2, 0, 0)
	/** @brief Use a hardware cursor (SDL2 only). */
	OptionEntryBoolean hardwareCursor;
	/** @brief Use a hardware cursor for items. */
	OptionEntryBoolean hardwareCursorForItems;
	/** @brief Maximum width / height for the hardware cursor. Larger cursors fall back to software. */
	OptionEntryInt<int> hardwareCursorMaxSize;
#endif
	/** @brief Show FPS, even without the -f command line flag. */
	OptionEntryBoolean showFPS;
};

struct GameplayOptions : OptionCategoryBase {
	GameplayOptions();
	std::vector<OptionEntryBase *> GetEntries() override;

	/** @brief Gameplay ticks per second. */
	OptionEntryInt<int> tickRate;
	/** DETH: Monster Strength (percent) used when hosting a multiplayer game. */
	OptionEntryInt<int> monsterStrength;
	/** @brief Enable double walk speed when in town. */
	OptionEntryBoolean runInTown;
	/** @brief Do not let the mouse leave the application window. */
	OptionEntryBoolean grabInput;
	/** @brief Pause the game when focus is lost. */
	OptionEntryBoolean pauseOnFocusLoss;
	/** @brief Enable the Theo quest. */
	OptionEntryBoolean theoQuest;
	/** @brief Enable the cow quest. */
	OptionEntryBoolean cowQuest;
	/** @brief Will players still damage other players in non-PvP mode. */
	OptionEntryBoolean friendlyFire;
	/** @brief Enables the full/uncut singleplayer version of quests. */
	OptionEntryBoolean multiplayerFullQuests;
	/** @brief Enable the bard hero class. */
	OptionEntryBoolean testBard;
	/** @brief Enable the babarian hero class. */
	OptionEntryBoolean testBarbarian;
	/** @brief Show the current level progress. */
	OptionEntryBoolean experienceBar;
	/** @brief Display current/max health values on health globe. */
	OptionEntryBoolean showHealthValues;
	/** @brief Display current/max mana values on mana globe. */
	OptionEntryBoolean showManaValues;
	/** @brief Enable the multiplayer party information display */
	OptionEntryEnum<PartyPortraits> showMultiplayerPartyInfo;
	/** @brief Show enemy health at the top of the screen. */
	OptionEntryBoolean enemyHealthBar;
	/** @brief Displays item info in a floating box when hovering over an ite. */
	OptionEntryBoolean floatingInfoBox;
	/** @brief DETH: bottom panel reduced to orbs and belt; the world shows through. */
	OptionEntryBoolean compactPanel;
	/** @brief DETH: with the compact panel, menu buttons in the top-right corner (desktop). */
	OptionEntryBoolean compactMenuButtons;
	/** @brief DETH: size of the menu buttons in the top-right corner, in percent. */
	OptionEntryInt<int> compactMenuSize;
	/** @brief DETH: how solid the menu buttons in the top-right corner are, in percent. */
	OptionEntryInt<int> compactMenuOpacity;
	/** @brief DETH: opacity of the full-screen map overlay (percent). */
	OptionEntryInt<int> mapOpacity;
	/** @brief DETH: opacity of the minimap (percent). */
	OptionEntryInt<int> minimapOpacity;
	/** @brief DETH: screen corner of the minimap. */
	OptionEntryEnum<MinimapCorner> minimapCorner;
	/** @brief Automatically pick up gold when walking over it. */
	OptionEntryBoolean autoGoldPickup;
	/** @brief Auto-pickup elixirs */
	OptionEntryBoolean autoElixirPickup;
	/** @brief Auto-pickup oils */
	OptionEntryBoolean autoOilPickup;
	/** @brief Enable or Disable auto-pickup in town */
	OptionEntryBoolean autoPickupInTown;
	/** @brief Automatically attempt to equip weapon-type items when picking them up. */
	OptionEntryBoolean autoEquipWeapons;
	/** @brief Automatically attempt to equip armor-type items when picking them up. */
	OptionEntryBoolean autoEquipArmor;
	/** @brief Automatically attempt to equip helm-type items when picking them up. */
	OptionEntryBoolean autoEquipHelms;
	/** @brief Automatically attempt to equip shield-type items when picking them up. */
	OptionEntryBoolean autoEquipShields;
	/** @brief Automatically attempt to equip jewelry-type items when picking them up. */
	OptionEntryBoolean autoEquipJewelry;
	/** @brief Only enable 2/3 quests in each game session */
	OptionEntryBoolean randomizeQuests;
	/** @brief Indicates whether or not monster type (Animal, Demon, Undead) is shown along with other monster information. */
	OptionEntryBoolean showMonsterType;
	/** @brief Displays item labels for items on the ground.  */
	OptionEntryBoolean showItemLabels;
	/** @brief DETH: no ground labels for plain (white) gear and small gold piles. */
	OptionEntryBoolean hideJunkLabels;
	/** @brief DETH: the item info box compares damage or armor with what is worn. */
	OptionEntryBoolean compareToEquipped;
	/** @brief DETH: gold you pick up in multiplayer is split evenly with everyone in the game. */
	OptionEntryBoolean sharePartyGold;
	/** @brief Refill belt from inventory, or rather, use potions/scrolls from inventory first when belt item is consumed.  */
	OptionEntryBoolean autoRefillBelt;
	/** @brief Locally disable clicking on shrines which permanently cripple character. */
	OptionEntryBoolean disableCripplingShrines;
	/** @brief Spell hotkeys instantly cast the spell. */
	OptionEntryBoolean quickCast;
	/** @brief Number of Healing potions to pick up automatically */
	OptionEntryInt<int> numHealPotionPickup;
	/** @brief Number of Full Healing potions to pick up automatically */
	OptionEntryInt<int> numFullHealPotionPickup;
	/** @brief Number of Mana potions to pick up automatically */
	OptionEntryInt<int> numManaPotionPickup;
	/** @brief Number of Full Mana potions to pick up automatically */
	OptionEntryInt<int> numFullManaPotionPickup;
	/** @brief Number of Rejuvenating potions to pick up automatically */
	OptionEntryInt<int> numRejuPotionPickup;
	/** @brief Number of Full Rejuvenating potions to pick up automatically */
	OptionEntryInt<int> numFullRejuPotionPickup;
	/** @brief Store user interface. */
	OptionEntryEnum<StoreUi> storeUi;

	/**
	 * @brief If loading takes less than this value, skips displaying the loading screen.
	 *
	 * Advanced option, not displayed in the UI.
	 */
	OptionEntryInt<int> skipLoadingScreenThresholdMs;
};

struct ControllerOptions : OptionCategoryBase {
	ControllerOptions();
	std::vector<OptionEntryBase *> GetEntries() override;

	/** @brief SDL Controller mapping, see SDL_GameControllerDB. */
	char szMapping[1024];
	/** @brief Configure gamepad joysticks deadzone */
	float fDeadzone;
	/** @brief DETH: right stick cursor speed in percent (on-screen speed, independent of UI scale). */
	OptionEntryScalePercent cursorSpeed;
	/** @brief DETH: show touch controls when the screen is touched (Android). Off: touch acts as a mouse. */
	OptionEntryBoolean onScreenControls;
	/** @brief DETH: size of the touch stick in percent. */
	OptionEntryScalePercent touchControlsSize;
	/** @brief DETH: size of the touch buttons (and the menu bar) in percent. */
	OptionEntryScalePercent touchButtonsSize;
	/** @brief DETH: stick dead zone in percent of full tilt (both sticks). */
	OptionEntryScalePercent stickDeadZone;
	/** @brief DETH: vibrate briefly on taps in the menus (Android). */
	OptionEntryBoolean touchHaptics;
	/** @brief DETH: how solid the touch controls are, in percent. */
	OptionEntryScalePercent touchControlsOpacity;
	/** @brief DETH: positions and sizes set in the touch layout editor (see layout_editor.cpp). */
	std::string touchLayout;
#ifdef __vita__
	/** @brief Enable input via rear touchpad */
	bool bRearTouch;
#endif
};

struct NetworkOptions : OptionCategoryBase {
	NetworkOptions();
	std::vector<OptionEntryBase *> GetEntries() override;

	/** @brief Optionally bind to a specific network interface. */
	char szBindAddress[129];
	/** @brief Most recently entered ZeroTier Game ID. */
	char szPreviousZTGame[129];
	/** @brief Most recently entered Hostname in join dialog. */
	char szPreviousHost[129];
	/** @brief What network port to use. */
	OptionEntryInt<uint16_t> port;
};

struct ChatOptions : OptionCategoryBase {
	ChatOptions();
	std::vector<OptionEntryBase *> GetEntries() override;

	/** @brief Quick chat messages. */
	std::vector<std::string> szHotKeyMsgs[QuickMessages.size()];
};

struct LanguageOptions : OptionCategoryBase {
	LanguageOptions();
	std::vector<OptionEntryBase *> GetEntries() override;

	OptionEntryLanguageCode code;
};

constexpr uint32_t KeymapperMouseButtonMask = 1 << 31;
constexpr uint32_t MouseScrollUpButton = 65536 | KeymapperMouseButtonMask;
constexpr uint32_t MouseScrollDownButton = 65537 | KeymapperMouseButtonMask;
constexpr uint32_t MouseScrollLeftButton = 65538 | KeymapperMouseButtonMask;
constexpr uint32_t MouseScrollRightButton = 65539 | KeymapperMouseButtonMask;

/** The Keymapper maps keys to actions. */
struct KeymapperOptions : OptionCategoryBase {
	/**
	 * Action represents an action that can be triggered using a keyboard
	 * shortcut.
	 */
	class Action final : public OptionEntryBase {
	public:
		// OptionEntryBase::key may be referencing Action::dynamicKey.
		// The implicit copy constructor would copy that reference instead of referencing the copy.
		Action(const Action &) = delete;

		Action(std::string_view key, const char *name, const char *description, uint32_t defaultKey, std::function<void()> actionPressed, std::function<void()> actionReleased, std::function<bool()> enable, unsigned index);

		[[nodiscard]] std::string_view GetName() const override;
		[[nodiscard]] OptionEntryType GetType() const override
		{
			return OptionEntryType::Key;
		}

		void LoadFromIni(std::string_view category) override;
		void SaveToIni(std::string_view category) const override;

		[[nodiscard]] std::string_view GetValueDescription() const override;

		bool SetValue(int value);

		[[nodiscard]] bool isEnabled() const { return !enable || enable(); }

		std::function<void()> actionPressed;
		std::function<void()> actionReleased;

	private:
		uint32_t defaultKey;
		std::function<bool()> enable;
		uint32_t boundKey = SDLK_UNKNOWN;
		unsigned dynamicIndex;
		std::string dynamicKey;
		mutable std::string dynamicName;

		friend struct KeymapperOptions;
	};

	KeymapperOptions();
	std::vector<OptionEntryBase *> GetEntries() override;

	void AddAction(
	    std::string_view key, const char *name, const char *description, uint32_t defaultKey,
	    std::function<void()> actionPressed,
	    std::function<void()> actionReleased = nullptr,
	    std::function<bool()> enable = nullptr,
	    unsigned index = 0);
	void CommitActions();

	[[nodiscard]] const Action *findAction(uint32_t key) const;

	std::string_view KeyNameForAction(std::string_view actionName) const;
	uint32_t KeyForAction(std::string_view actionName) const;
	/** @brief DETH: puts every action back on its default key. */
	void ResetToDefaults();

private:
	std::forward_list<Action> actions;
	ankerl::unordered_dense::segmented_map<uint32_t, std::reference_wrapper<Action>> keyIDToAction;
	ankerl::unordered_dense::segmented_map<uint32_t, std::string> keyIDToKeyName;
	ankerl::unordered_dense::segmented_map<std::string, uint32_t, StringViewHash, StringViewEquals> keyNameToKeyID;
};

/** The Padmapper maps gamepad buttons to actions. */
struct PadmapperOptions : OptionCategoryBase {
	/**
	 * Action represents an action that can be triggered using a gamepad
	 * button combo.
	 */
	class Action final : public OptionEntryBase {
	public:
		Action(std::string_view key, const char *name, const char *description, ControllerButtonCombo defaultInput, std::function<void()> actionPressed, std::function<void()> actionReleased, std::function<bool()> enable, unsigned index);

		// OptionEntryBase::key may be referencing Action::dynamicKey.
		// The implicit copy constructor would copy that reference instead of referencing the copy.
		Action(const Action &) = delete;

		[[nodiscard]] std::string_view GetName() const override;
		[[nodiscard]] OptionEntryType GetType() const override
		{
			return OptionEntryType::PadButton;
		}

		void LoadFromIni(std::string_view category) override;
		void SaveToIni(std::string_view category) const override;

		[[nodiscard]] std::string_view GetValueDescription() const override;
		[[nodiscard]] std::string_view GetValueDescription(bool useShortName) const;

		bool SetValue(ControllerButtonCombo value);
		void ResetToDefault() { SetValue(defaultInput); }

		[[nodiscard]] bool isEnabled() const { return !enable || enable(); }

		std::function<void()> actionPressed;
		std::function<void()> actionReleased;
		ControllerButtonCombo boundInput;

	private:
		ControllerButtonCombo defaultInput;
		std::function<bool()> enable;
		mutable GamepadLayout boundInputDescriptionType = GamepadLayout::Generic;
		mutable std::string boundInputDescription;
		mutable std::string boundInputShortDescription;
		unsigned dynamicIndex;
		std::string dynamicKey;
		mutable std::string dynamicName;

		void UpdateValueDescription() const;
		std::string_view Shorten(std::string_view buttonName) const;

		friend struct PadmapperOptions;
	};

	PadmapperOptions();
	std::vector<OptionEntryBase *> GetEntries() override;

	void AddAction(
	    std::string_view key, const char *name, const char *description, ControllerButtonCombo defaultInput,
	    std::function<void()> actionPressed,
	    std::function<void()> actionReleased = nullptr,
	    std::function<bool()> enable = nullptr,
	    unsigned index = 0);
	void CommitActions();
	std::string_view InputNameForAction(std::string_view actionName, bool useShortName = false) const;
	ControllerButtonCombo ButtonComboForAction(std::string_view actionName) const;

	[[nodiscard]] const Action *findAction(ControllerButton button, tl::function_ref<bool(ControllerButton)> isModifierPressed) const;

	std::forward_list<Action> actions;

private:
	std::array<std::string, enum_size<ControllerButton>::value> buttonToButtonName;
	ankerl::unordered_dense::segmented_map<std::string, ControllerButton, StringViewHash, StringViewEquals> buttonNameToButton;
	bool committed = false;
};

struct ModOptions;
/** @brief DETH: shorthand for GetOptions().Mods.IsModActive(name). */
bool IsModActive(std::string_view name);

struct ModOptions : OptionCategoryBase {
	ModOptions();
	std::vector<std::string_view> GetActiveModList();
	/** @brief DETH: whether the mod with this folder name is installed and switched on. */
	bool IsModActive(std::string_view name);
	std::vector<std::string_view> GetModList();
	std::vector<OptionEntryBase *> GetEntries() override;
	void AddModEntry(const std::string &modName);
	void RemoveModEntry(const std::string &modName);
	void SetHellfireEnabled(bool enableHellfire);

	/**
	 * @brief DETH: switches mods to match a host for one multiplayer game (Hellfire is left
	 * alone). The previous choice is kept and saved to the settings file in the meantime.
	 * @param missing receives the mods that are not installed here (nothing is changed then).
	 */
	bool ApplyTemporaryMods(const std::vector<std::string> &mods, std::vector<std::string> &missing);
	/** @brief DETH: puts the mods back as they were before ApplyTemporaryMods. */
	void RestoreTemporaryMods();
	[[nodiscard]] bool HasTemporaryMods() const { return temporaryOriginal_.has_value(); }
	/** @brief DETH: the saved choice while temporary mods are on (for the settings file). */
	[[nodiscard]] std::optional<bool> SavedModValue(std::string_view name) const;

private:
	std::optional<std::vector<std::pair<std::string, bool>>> temporaryOriginal_;

	struct ModEntry {
		// OptionEntryBase::key references ModEntry::name.
		// The implicit copy constructor would copy that reference instead of referencing the copy.
		ModEntry(const ModEntry &) = delete;

		ModEntry(std::string_view name);
		// `name` is the mod id (MPQ filename stem / INI key). `displayName` and `description`
		// come from the mod's `manifest.ini` (falling back to `name` and empty), and are what
		// the settings UI shows via `enabled`.
		std::string name;
		std::string displayName;
		std::string description;
		OptionEntryBoolean enabled;

	private:
		ModEntry(std::string_view name, const ModManifest &manifest);
	};

	std::forward_list<ModEntry> &GetModEntries();
	std::optional<std::forward_list<ModEntry>> modEntries;
};

struct Options {
	GameModeOptions GameMode;
	StartUpOptions StartUp;
	DiabloOptions Diablo;
	HellfireOptions Hellfire;
	AudioOptions Audio;
	GameplayOptions Gameplay;
	GraphicsOptions Graphics;
	ControllerOptions Controller;
	NetworkOptions Network;
	ChatOptions Chat;
	LanguageOptions Language;
	KeymapperOptions Keymapper;
	PadmapperOptions Padmapper;
	ModOptions Mods;

	[[nodiscard]] std::vector<OptionCategoryBase *> GetCategories()
	{
		return {
			&Language,
			&Mods,
			&GameMode,
			&StartUp,
			&Graphics,
			&Audio,
			&Diablo,
			&Hellfire,
			&Gameplay,
			&Controller,
			&Network,
			&Chat,
			&Keymapper,
			&Padmapper,
		};
	}
};

/**
 * @brief Get the Options singleton object
 */
[[nodiscard]] Options &GetOptions();

bool HardwareCursorSupported();

/**
 * @brief Save game configurations to ini file
 */
void SaveOptions();

/**
 * @brief Load game configurations from ini file
 */
void LoadOptions();

} // namespace devilution
