#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace devilution {

/** DETH: hands control to the Android game-file importer (the activity restarts the game afterwards). */
void AndroidImportGameFiles();

/** DETH: opens the native text entry dialog for a menu edit field (kind: 0 text, 1 address, 2 password). */
void AndroidShowTextInput(std::string_view title, std::string_view value, std::size_t maxLength, int kind);

/** DETH: true while the native text entry dialog is up. */
bool AndroidTextInputOpen();

struct AndroidTextInputResult {
	bool ok;
	std::string text;
};

/** DETH: the dialog's answer, once, after it closes. */
std::optional<AndroidTextInputResult> AndroidTakeTextInputResult();

} // namespace devilution
