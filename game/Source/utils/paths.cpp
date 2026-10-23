#include "utils/paths.h"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#ifdef USE_SDL3
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#else
#include <SDL.h>
#endif

#include "appfat.h"
#include "utils/file_util.h"
#include "utils/log.hpp"
#include "utils/sdl_ptrs.h"

#ifdef __IPHONEOS__
#include "platform/ios/ios_paths.h"
#endif

#ifdef NXDK
#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(_WIN32) && !defined(NXDK)
#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#endif

#ifdef __ANDROID__
#include <jni.h>
#ifdef USE_SDL3
#include <SDL3/SDL_system.h>
#else
#include <SDL_system.h>
#endif
#endif

#ifdef USE_SDL1
#include "utils/sdl2_to_1_2_backports.h"
#endif

namespace devilution {

namespace paths {

namespace {

std::optional<std::string> basePath;
std::optional<std::string> prefPath;
std::optional<std::string> configPath;
std::optional<std::string> assetsPath;

void AddTrailingSlash(std::string &path)
{
	if (!path.empty() && path.back() != DirectorySeparator)
		path += DirectorySeparator;
}

[[maybe_unused]] std::string FromSDL(char *s)
{
	const SDLUniquePtr<char> pinned(s);
	std::string result = (s != nullptr ? s : "");
	if (s == nullptr) {
		Log("{}", SDL_GetError());
		SDL_ClearError();
	}
	return result;
}

#ifdef NXDK
const std::string &NxdkGetPrefPath()
{
	static const std::string Path = []() {
		const char *path = "E:\\UDATA\\devilutionx\\";
		if (CreateDirectoryA(path, nullptr) == FALSE && ::GetLastError() != ERROR_ALREADY_EXISTS) {
			DirErrorDlg(path);
		}
		return path;
	}();
	return Path;
}
#endif

std::string GetSdlBasePath()
{
	std::string result;
#if defined(__DJGPP__)
	// In DOS, use an empty base path.
#elif defined(USE_SDL3)
	const char *s = SDL_GetBasePath();
	if (s == nullptr) {
		LogError("{}", SDL_GetError());
		SDL_ClearError();
	} else {
		result = s;
	}
#else
	result = FromSDL(SDL_GetBasePath());
#endif
	return result;
}

} // namespace

const std::string &BasePath()
{
	if (!basePath) {
		basePath = GetSdlBasePath();
	}
	return *basePath;
}

const std::string &PrefPath()
{
	if (!prefPath) {
#if defined(__DJGPP__)
		prefPath = std::string();
#elif defined(__IPHONEOS__)
		prefPath = FromSDL(IOSGetPrefPath());
#elif defined(NXDK)
		prefPath = NxdkGetPrefPath();
#else
		prefPath = FromSDL(SDL_GetPrefPath("diasurgical", "devilution"));
#if !defined(__amigaos__) && !defined(__DJGPP__)
		if (FileExistsAndIsWriteable("diablo.ini")) {
			prefPath = std::string();
		}
		// DETH: a `portable.txt` next to the program keeps settings and saves in its folder.
		if (FileExists((BasePath() + "portable.txt").c_str())) {
			prefPath = BasePath();
		}
#endif
#endif
	}
	return *prefPath;
}

const std::string &ConfigPath()
{
	if (!configPath) {
#if defined(__DJGPP__)
		configPath = std::string();
#elif defined(__IPHONEOS__)
		configPath = FromSDL(IOSGetPrefPath());
#elif defined(NXDK)
		configPath = NxdkGetPrefPath();
#else
		configPath = FromSDL(SDL_GetPrefPath("diasurgical", "devilution"));
#if !defined(__amigaos__) && !defined(__DJGPP__)
		if (FileExistsAndIsWriteable("diablo.ini")) {
			configPath = std::string();
		}
		// DETH: a `portable.txt` next to the program keeps settings and saves in its folder.
		if (FileExists((BasePath() + "portable.txt").c_str())) {
			configPath = BasePath();
		}
#endif
#endif
	}
	return *configPath;
}

namespace {

#if defined(__linux__) && !defined(__ANDROID__)
// Resolves the user's Downloads folder the same way desktop file managers do:
// $XDG_DOWNLOAD_DIR, then ~/.config/user-dirs.dirs, then ~/Downloads.
std::string GetDownloadsDir()
{
	const char *home = std::getenv("HOME");
	if (home == nullptr || *home == '\0')
		return {};
	const std::string homeDir = home;

	const auto expand = [&](std::string value) {
		if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
			value = value.substr(1, value.size() - 2);
		if (value.rfind("$HOME", 0) == 0)
			value = homeDir + value.substr(5);
		return value;
	};

	if (const char *env = std::getenv("XDG_DOWNLOAD_DIR"); env != nullptr && *env != '\0')
		return expand(env);

	const char *configHome = std::getenv("XDG_CONFIG_HOME");
	const std::string userDirs = (configHome != nullptr && *configHome != '\0' ? std::string(configHome) : homeDir + "/.config") + "/user-dirs.dirs";
	if (FILE *f = std::fopen(userDirs.c_str(), "r"); f != nullptr) {
		char line[1024];
		std::string result;
		constexpr std::string_view Key = "XDG_DOWNLOAD_DIR=";
		while (std::fgets(line, sizeof(line), f) != nullptr) {
			std::string_view l = line;
			while (!l.empty() && (l.back() == '\n' || l.back() == '\r'))
				l.remove_suffix(1);
			if (l.substr(0, Key.size()) == Key) {
				result = expand(std::string(l.substr(Key.size())));
				break;
			}
		}
		std::fclose(f);
		if (!result.empty())
			return result;
	}

	return homeDir + "/Downloads";
}
#endif

} // namespace

namespace {

bool prefPathExplicit = false;

#if defined(_WIN32) && !defined(NXDK)
std::string GetWindowsDownloadsDir()
{
	PWSTR wide = nullptr;
	std::string result;
	// FOLDERID_Downloads, spelled out so no extra GUID library is needed.
	static const GUID DownloadsFolder = { 0x374DE290, 0x123F, 0x4565, { 0x91, 0x64, 0x39, 0xC4, 0x92, 0x5E, 0x46, 0x7B } };
	if (SUCCEEDED(SHGetKnownFolderPath(DownloadsFolder, 0, nullptr, &wide)) && wide != nullptr) {
		const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
		if (size > 1) {
			result.resize(static_cast<size_t>(size - 1));
			WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), size, nullptr, nullptr);
		}
	}
	CoTaskMemFree(wide);
	return result;
}
#endif

#ifdef __ANDROID__
/** Downloads/DETH/saves when the app may write there (asks Java; empty otherwise). */
std::string AndroidSharedSavesDir()
{
#ifdef USE_SDL3
	JNIEnv *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
#else
	JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
	jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
#endif
	if (env == nullptr || activity == nullptr)
		return {};
	std::string dir;
	jclass cls = env->GetObjectClass(activity);
	jmethodID method = env->GetStaticMethodID(cls, "dethSharedSavesDir", "()Ljava/lang/String;");
	if (method != nullptr) {
		auto result = static_cast<jstring>(env->CallStaticObjectMethod(cls, method));
		if (!env->ExceptionCheck() && result != nullptr) {
			const char *chars = env->GetStringUTFChars(result, nullptr);
			if (chars != nullptr)
				dir = chars;
			env->ReleaseStringUTFChars(result, chars);
			env->DeleteLocalRef(result);
		}
	}
	if (env->ExceptionCheck())
		env->ExceptionClear();
	env->DeleteLocalRef(cls);
	env->DeleteLocalRef(activity);
	return dir;
}
#endif

/** The shared saves folder for this platform, or empty when there is none. */
std::string SharedSavesDir()
{
	std::string dir;
#if defined(__ANDROID__)
	dir = AndroidSharedSavesDir();
#elif defined(_WIN32) && !defined(NXDK)
	if (prefPathExplicit)
		return {};
	dir = GetWindowsDownloadsDir();
	if (!dir.empty())
		dir += "\\DETH\\saves";
#elif defined(__linux__)
	if (prefPathExplicit)
		return {};
	dir = GetDownloadsDir();
	if (!dir.empty())
		dir += "/DETH/saves";
#endif
	if (dir.empty())
		return {};
	RecursivelyCreateDir(dir.c_str());
	AddTrailingSlash(dir);
	// Usable only if a file can be written there.
	const std::string probe = dir + ".deth-write-test";
	if (FILE *f = std::fopen(probe.c_str(), "wb"); f != nullptr) {
		std::fclose(f);
		RemoveFile(probe.c_str());
		return dir;
	}
	return {};
}

bool IsSaveFileName(std::string_view name)
{
	const bool saveName = name.starts_with("single_") || name.starts_with("multi_") || name.starts_with("spawn_")
	    || name.starts_with("share_") || name.starts_with("stash");
	return saveName && (name.ends_with(".sv") || name.ends_with(".hsv"));
}

/** Copies saves from the old folder into the new one, once; files already there are kept. */
void CopyExistingSaves(const std::string &from, const std::string &to)
{
	if (from.empty() || from == to)
		return;
	const std::string marker = to + ".deth-copied";
	if (FileExists(marker.c_str()))
		return;
	for (const std::string &name : ListFiles(from.c_str())) {
		if (!IsSaveFileName(name))
			continue;
		const std::string target = to + name;
		if (!FileExists(target.c_str()))
			CopyFileOverwrite((from + name).c_str(), target.c_str());
	}
	if (FILE *f = std::fopen(marker.c_str(), "wb"); f != nullptr)
		std::fclose(f);
}

} // namespace

std::string SavePath()
{
	static std::optional<std::string> shared;
#ifndef __ANDROID__
	// An explicit --save-dir wins (Android always passes its private folder, which is the fallback).
	if (prefPathExplicit)
		return PrefPath();
#endif
	if (const char *env = std::getenv("DETH_SAVE_DIR"); env != nullptr && *env != '\0') {
		std::string dir = env;
		AddTrailingSlash(dir);
		return dir;
	}
	if (!shared) {
		std::string dir = SharedSavesDir();
		if (!dir.empty()) {
			CopyExistingSaves(PrefPath(), dir);
			LogVerbose("Saves folder: {}", dir);
			shared = dir;
		}
	}
	return shared ? *shared : PrefPath();
}

const std::string &DropPath()
{
	static const std::string dropPath = []() -> std::string {
#if defined(__linux__) && !defined(__ANDROID__)
		std::string downloads = GetDownloadsDir();
		if (downloads.empty())
			return {};
		if (downloads.back() != '/')
			downloads += '/';
		return downloads + "DETH/";
#else
		return {};
#endif
	}();
	return dropPath;
}

const std::string &AssetsPath()
{
	if (!assetsPath) {
#if __EMSCRIPTEN__ || defined(__DJGPP__)
		assetsPath.emplace("assets" DIRECTORY_SEPARATOR_STR);
#elif defined(NXDK)
		assetsPath.emplace("D:\\assets\\");
#elif defined(__3DS__) || defined(__SWITCH__)
		assetsPath.emplace("romfs:/assets/");
#elif defined(__APPLE__) && (defined(USE_SDL1) || defined(USE_SDL3))
		// In `Info.plist` we have
		//
		//    <key>SDL_FILESYSTEM_BASE_DIR_TYPE</key>
		//    <string>resource</string>
		//
		// This means `SDL_GetBasePath()` returns exedir for non-bundled
		// and the `app_dir.app/Resources/` for bundles.
		//
		// Our built-in resources are directly in the `devilutionx.app/Resources` directory
		// and are normally looked up via a relative lookup in `FindAsset`.
		// In SDL2, this is implemented by calling `SDL_OpenFPFromBundleOrFallback`
		// from `SDL_RWFromFile` but SDL1 doesn't do it, so we set the directory explicitly.
		//
		// Note that SDL3 reverts to SDL1 behaviour!
		// https://github.com/libsdl-org/SDL/blob/962268ca21ed10b9cee31198c22681099293f20a/docs/README-migration.md?plain=1#L1623
		assetsPath.emplace(GetSdlBasePath());
#else
		assetsPath.emplace(GetSdlBasePath() + ("assets" DIRECTORY_SEPARATOR_STR));
#endif
	}
	return *assetsPath;
}

void SetBasePath(const std::string &path)
{
	basePath = path;
	AddTrailingSlash(*basePath);
}

void SetPrefPath(const std::string &path)
{
	prefPathExplicit = true;
	prefPath = path;
	AddTrailingSlash(*prefPath);
}

void SetConfigPath(const std::string &path)
{
	configPath = path;
	AddTrailingSlash(*configPath);
}

void SetAssetsPath(const std::string &path)
{
	assetsPath = path;
	AddTrailingSlash(*assetsPath);
}

} // namespace paths

} // namespace devilution
