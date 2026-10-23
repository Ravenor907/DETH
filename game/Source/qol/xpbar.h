/**
 * @file xpbar.h
 *
 * Adds XP bar QoL feature
 */
#pragma once

namespace devilution {

struct Surface;

void InitXPBar();
void FreeXPBar();

void DrawXPBar(const Surface &out);
bool CheckXPBarInfo();

/** @brief DETH: height of the XP bar on the compact HUD (0 when it is off). */
int GetCompactXPBarHeight();

} // namespace devilution
