#include "controls/touch/touch_mode.hpp"

#include <cstdlib>

#include <SDL.h>

#include "options.h"
#include "utils/sdl_compat.h"

#ifdef __ANDROID__
#include <jni.h>
#endif

namespace devilution::touch {

namespace {

int forcedMode = -1;

} // namespace

bool IsTouchModeForced()
{
	if (forcedMode < 0) {
		const char *env = std::getenv("DETH_TOUCH_MODE");
		if (env == nullptr)
			env = std::getenv("DETH_FORCE_TOUCH");
		forcedMode = (env != nullptr && env[0] != '0') ? 1 : 0;
	}
	return forcedMode == 1;
}

bool IsTouchDevice()
{
#ifdef __ANDROID__
	return true;
#elif defined(USE_SDL3)
	if (IsTouchModeForced())
		return true;
	int count = 0;
	SDL_TouchID *ids = SDL_GetTouchDevices(&count);
	SDL_free(ids);
	return count > 0;
#elif !defined(USE_SDL1)
	return IsTouchModeForced() || SDL_GetNumTouchDevices() > 0;
#else
	return false;
#endif
}

bool IsTouchMode()
{
#ifdef __ANDROID__
	return true;
#else
	// Desktop keeps its mouse, keyboard and controller UI; the debug override exists for the test rig.
	return IsTouchModeForced();
#endif
}



void TapFeedback()
{
	if (!*GetOptions().Controller.touchHaptics)
		return;
#ifdef __ANDROID__
#ifdef USE_SDL3
	auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
#else
	auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
	auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
#endif
	if (env == nullptr || activity == nullptr)
		return;
	jclass cls = env->GetObjectClass(activity);
	jmethodID method = env->GetStaticMethodID(cls, "tapFeedback", "()V");
	if (method != nullptr)
		env->CallStaticVoidMethod(cls, method);
	if (env->ExceptionCheck())
		env->ExceptionClear();
	env->DeleteLocalRef(cls);
	env->DeleteLocalRef(activity);
#endif
}

} // namespace devilution::touch
