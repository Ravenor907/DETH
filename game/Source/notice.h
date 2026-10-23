/**
 * @file notice.h
 *
 * DETH: a short notice ("Autosaved") that fades in and out in the top-right corner, under the clock,
 * with no bar behind it. Unlike EventPlrMsg it never goes to the chat log.
 */
#pragma once

#include <string_view>

#include "engine/surface.hpp"

namespace devilution {

/** Shows `text` for two seconds, replacing any notice still on screen. */
void ShowNotice(std::string_view text);

void DrawNotice(const Surface &out);

} // namespace devilution
