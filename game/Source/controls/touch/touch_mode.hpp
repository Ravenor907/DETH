/**
 * @file touch_mode.hpp
 *
 * DETH: touch-first interaction rules. Everything here is a no-op on devices without a
 * touch screen, so desktop keeps its mouse, keyboard and controller behaviour unchanged.
 */
#pragma once

namespace devilution::touch {

/** True when the device has a touch screen (always on Android; on desktop only if SDL reports one). */
bool IsTouchDevice();

/**
 * True when the pre-game screens should use touch rules: single tap activates, drag scrolls,
 * a Back button is shown and the gamepad overlay is hidden. Always on for Android, off on desktop.
 */
bool IsTouchMode();

/** Debug aid: `DETH_TOUCH_MODE=1` forces touch mode on desktop for screenshots and the test rig. */
bool IsTouchModeForced();

/** Short vibration on a tap when the option is on; does nothing where unsupported. */
void TapFeedback();

} // namespace devilution::touch
