#include "init.hpp"
#include "mpq/mpq_reader.hpp"

#include <jni.h>
#include <SDL.h>

#include <atomic>
#include <mutex>

#include "platform/android/android.hpp"

namespace devilution {
namespace {

bool AreExtraFontsOutOfDateForMpqPath(const char *mpqPath)
{
	std::expected<MpqArchive, std::string> archive = MpqArchive::Open(mpqPath);
	return archive.has_value() && AreExtraFontsOutOfDate(*archive);
}

std::mutex TextInputMutex;
std::optional<AndroidTextInputResult> PendingTextInput;
std::atomic<bool> TextInputOpen { false };

} // namespace

void AndroidShowTextInput(std::string_view title, std::string_view value, std::size_t maxLength, int kind)
{
	if (TextInputOpen.exchange(true))
		return; // already showing
	{
		const std::lock_guard<std::mutex> lock(TextInputMutex);
		PendingTextInput = std::nullopt;
	}
	JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
	jobject activity = (jobject)SDL_AndroidGetActivity();
	jclass clazz = env->GetObjectClass(activity);
	jmethodID method = env->GetMethodID(clazz, "showTextInput", "(Ljava/lang/String;Ljava/lang/String;II)V");
	if (method != nullptr) {
		jstring jTitle = env->NewStringUTF(std::string(title).c_str());
		jstring jValue = env->NewStringUTF(std::string(value).c_str());
		env->CallVoidMethod(activity, method, jTitle, jValue, static_cast<jint>(maxLength), static_cast<jint>(kind));
		env->DeleteLocalRef(jTitle);
		env->DeleteLocalRef(jValue);
	} else {
		env->ExceptionClear();
		TextInputOpen = false;
	}
	env->DeleteLocalRef(activity);
	env->DeleteLocalRef(clazz);
}

bool AndroidTextInputOpen()
{
	return TextInputOpen;
}

std::optional<AndroidTextInputResult> AndroidTakeTextInputResult()
{
	const std::lock_guard<std::mutex> lock(TextInputMutex);
	std::optional<AndroidTextInputResult> result = std::move(PendingTextInput);
	PendingTextInput = std::nullopt;
	return result;
}

void AndroidImportGameFiles()
{
	JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
	jobject activity = (jobject)SDL_AndroidGetActivity();
	jclass clazz = env->GetObjectClass(activity);
	jmethodID method = env->GetMethodID(clazz, "importGameFiles", "()V");
	if (method != nullptr)
		env->CallVoidMethod(activity, method);
	env->DeleteLocalRef(activity);
	env->DeleteLocalRef(clazz);
}

} // namespace devilution

extern "C" {
JNIEXPORT jboolean JNICALL Java_org_diasurgical_devilutionx_DevilutionXSDLActivity_areFontsOutOfDate(JNIEnv *env, jclass cls, jstring fonts_mpq)
{
	const char *mpqPath = env->GetStringUTFChars(fonts_mpq, nullptr);
	bool outOfDate = devilution::AreExtraFontsOutOfDateForMpqPath(mpqPath);
	env->ReleaseStringUTFChars(fonts_mpq, mpqPath);
	return outOfDate;
}

JNIEXPORT void JNICALL Java_org_diasurgical_devilutionx_DevilutionXSDLActivity_textInputDone(JNIEnv *env, jclass cls, jstring text, jboolean ok)
{
	const char *chars = env->GetStringUTFChars(text, nullptr);
	{
		const std::lock_guard<std::mutex> lock(devilution::TextInputMutex);
		devilution::PendingTextInput = devilution::AndroidTextInputResult { ok == JNI_TRUE, chars != nullptr ? chars : "" };
	}
	if (chars != nullptr)
		env->ReleaseStringUTFChars(text, chars);
	devilution::TextInputOpen = false;
}
}
