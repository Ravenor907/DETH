#pragma once

namespace devilution {

/** DETH test aid: pushes scripted finger/mouse events (see touch_script.cpp). No-op unless DETH_TOUCH_SCRIPT is set. */
void PumpScriptedTouch();

} // namespace devilution
