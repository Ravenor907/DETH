#include "deth_update.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <filesystem>

#include <windows.h>
#include <urlmon.h>
#endif

#ifdef __ANDROID__
#include <jni.h>
#ifdef USE_SDL3
#include <SDL3/SDL_system.h>
#else
#include <SDL_system.h>
#endif
#endif

#include "deth_version.hpp"
#include "options.h"
#include "utils/file_util.h"
#include "utils/log.hpp"
#include "utils/paths.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

constexpr std::string_view Repo = "Ravenor907/DETH";
constexpr std::string_view FlatpakAppId = "io.github.ravenor907.DETH";

// --- Version comparison -----------------------------------------------------------------

struct Version {
	int parts[3] = { 0, 0, 0 };
	/**
	 * Where the version sits around its release: 0 for the release itself, N for beta
	 * "M.m.b.N" (betas follow the M.m.0 release), N - 1000000 for the older "M.m.p-betaN"
	 * style (betas before their release).
	 */
	int stage = 0;
};

Version ParseVersion(std::string_view text)
{
	Version v;
	if (!text.empty() && (text.front() == 'v' || text.front() == 'V'))
		text.remove_prefix(1);
	auto readNumber = [&](size_t &i) {
		int n = 0;
		while (i < text.size() && text[i] >= '0' && text[i] <= '9')
			n = n * 10 + (text[i++] - '0');
		return n;
	};
	size_t i = 0;
	for (int p = 0; p < 3; ++p) {
		if (i < text.size() && (text[i] == 'b' || text[i] == 'B')) {
			// "1.0.b.1": beta 1 after 1.0.0.
			++i;
			if (i < text.size() && text[i] == '.')
				++i;
			v.stage = readNumber(i);
			return v;
		}
		v.parts[p] = readNumber(i);
		if (i < text.size() && text[i] == '.')
			++i;
		else
			break;
	}
	if (i < text.size() && text[i] == '-') {
		while (i < text.size() && (text[i] < '0' || text[i] > '9'))
			++i;
		v.stage = readNumber(i) - 1000000;
	}
	return v;
}

// --- Minimal JSON reading (enough for the GitHub releases API) -----------------------

/** Reads the JSON string starting at `pos` (just after the opening quote). */
std::string ReadJsonString(std::string_view json, size_t pos)
{
	std::string out;
	for (; pos < json.size() && json[pos] != '"'; ++pos) {
		char c = json[pos];
		if (c != '\\') {
			out += c;
			continue;
		}
		if (++pos >= json.size())
			break;
		switch (json[pos]) {
		case 'n': out += '\n'; break;
		case 'r': break;
		case 't': out += ' '; break;
		case 'u': {
			// Keep ASCII, drop the rest (release notes are plain text).
			if (pos + 4 < json.size()) {
				const int code = std::strtol(std::string(json.substr(pos + 1, 4)).c_str(), nullptr, 16);
				if (code > 0 && code < 128)
					out += static_cast<char>(code);
				pos += 4;
			}
		} break;
		default: out += json[pos]; break;
		}
	}
	return out;
}

/** Finds "key": and returns its string value, searching json[from, to). */
std::optional<std::string> FindJsonString(std::string_view json, std::string_view key, size_t from, size_t to, size_t *foundAt = nullptr)
{
	const std::string needle = StrCat("\"", key, "\"");
	size_t at = json.find(needle, from);
	while (at != std::string_view::npos && at < to) {
		size_t p = at + needle.size();
		while (p < json.size() && (json[p] == ' ' || json[p] == ':'))
			++p;
		if (p < json.size() && json[p] == '"') {
			if (foundAt != nullptr)
				*foundAt = at;
			return ReadJsonString(json, p + 1);
		}
		at = json.find(needle, at + 1);
	}
	return std::nullopt;
}

bool FindJsonBool(std::string_view json, std::string_view key, size_t from, size_t to)
{
	const std::string needle = StrCat("\"", key, "\"");
	const size_t at = json.find(needle, from);
	if (at == std::string_view::npos || at >= to)
		return false;
	size_t p = at + needle.size();
	while (p < json.size() && (json[p] == ' ' || json[p] == ':'))
		++p;
	return json.substr(p, 4) == "true";
}

// --- Platform -------------------------------------------------------------------------

bool IsFlatpak()
{
#if defined(__linux__) && !defined(__ANDROID__)
	return FileExists("/.flatpak-info");
#else
	return false;
#endif
}

std::string_view AssetSuffix()
{
#if defined(_WIN32)
	return "-windows.zip";
#elif defined(__ANDROID__)
	return ".apk";
#else
	return ".flatpak";
#endif
}

#ifdef __ANDROID__
struct Jni {
	JNIEnv *env = nullptr;
	jclass cls = nullptr;
	jobject activity = nullptr;
	Jni()
	{
#ifdef USE_SDL3
		env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
		activity = static_cast<jobject>(SDL_GetAndroidActivity());
#else
		env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
		activity = static_cast<jobject>(SDL_AndroidGetActivity());
#endif
		if (env != nullptr && activity != nullptr)
			cls = env->GetObjectClass(activity);
	}
	~Jni()
	{
		if (env == nullptr)
			return;
		if (env->ExceptionCheck())
			env->ExceptionClear();
		if (cls != nullptr)
			env->DeleteLocalRef(cls);
		if (activity != nullptr)
			env->DeleteLocalRef(activity);
	}
};

std::optional<std::string> AndroidHttpGet(const std::string &url)
{
	Jni jni;
	if (jni.cls == nullptr)
		return std::nullopt;
	jmethodID method = jni.env->GetStaticMethodID(jni.cls, "dethHttpGet", "(Ljava/lang/String;)Ljava/lang/String;");
	if (method == nullptr)
		return std::nullopt;
	jstring jurl = jni.env->NewStringUTF(url.c_str());
	auto result = static_cast<jstring>(jni.env->CallStaticObjectMethod(jni.cls, method, jurl));
	jni.env->DeleteLocalRef(jurl);
	if (jni.env->ExceptionCheck() || result == nullptr)
		return std::nullopt;
	const char *chars = jni.env->GetStringUTFChars(result, nullptr);
	std::string body = chars != nullptr ? chars : "";
	jni.env->ReleaseStringUTFChars(result, chars);
	jni.env->DeleteLocalRef(result);
	return body;
}

bool AndroidCallBool(const char *name, const std::string &a, const std::string &b)
{
	Jni jni;
	if (jni.cls == nullptr)
		return false;
	jmethodID method = jni.env->GetStaticMethodID(jni.cls, name, "(Ljava/lang/String;Ljava/lang/String;)Z");
	if (method == nullptr)
		return false;
	jstring ja = jni.env->NewStringUTF(a.c_str());
	jstring jb = jni.env->NewStringUTF(b.c_str());
	const jboolean ok = jni.env->CallStaticBooleanMethod(jni.cls, method, ja, jb);
	jni.env->DeleteLocalRef(ja);
	jni.env->DeleteLocalRef(jb);
	return !jni.env->ExceptionCheck() && ok == JNI_TRUE;
}
#endif

#ifdef _WIN32
/** Runs a command line without a console window; stdout goes to `outputPath` if given. */
int RunHidden(const std::string &commandLine, const std::string &outputPath = {})
{
	SECURITY_ATTRIBUTES sa { sizeof(sa), nullptr, TRUE };
	HANDLE out = INVALID_HANDLE_VALUE;
	if (!outputPath.empty())
		out = CreateFileA(outputPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	STARTUPINFOA si {};
	si.cb = sizeof(si);
	if (out != INVALID_HANDLE_VALUE) {
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdOutput = out;
		si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
		si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	}
	PROCESS_INFORMATION pi {};
	std::string mutableLine = commandLine;
	const BOOL started = CreateProcessA(nullptr, mutableLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
	if (out != INVALID_HANDLE_VALUE)
		CloseHandle(out);
	if (!started)
		return -1;
	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD code = 1;
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return static_cast<int>(code);
}

std::string TempDir()
{
	char buf[MAX_PATH] {};
	const DWORD n = GetTempPathA(MAX_PATH, buf);
	return std::string(buf, n);
}
#elif !defined(__ANDROID__)
/** Runs a shell command (through the host on Flatpak); returns its exit status and output. */
int RunShell(const std::string &command, std::string *output = nullptr)
{
	const std::string full = IsFlatpak() ? StrCat("flatpak-spawn --host ", command) : command;
	FILE *pipe = popen(full.c_str(), "r");
	if (pipe == nullptr)
		return -1;
	char buf[4096];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof(buf), pipe)) > 0) {
		if (output != nullptr)
			output->append(buf, n);
	}
	const int status = pclose(pipe);
	return status;
}

std::string ShellQuote(std::string_view text)
{
	std::string out = "'";
	for (const char c : text) {
		if (c == '\'')
			out += "'\\''";
		else
			out += c;
	}
	out += "'";
	return out;
}
#endif

std::optional<std::string> HttpGet(const std::string &url)
{
#if defined(__ANDROID__)
	return AndroidHttpGet(url);
#elif defined(_WIN32)
	// DETH: Windows' own downloader (also in Wine/Proton), not curl.exe, which older systems lack.
	const std::string outPath = TempDir() + "deth-update.json";
	DeleteFileA(outPath.c_str());
	if (URLDownloadToFileA(nullptr, url.c_str(), outPath.c_str(), 0, nullptr) != S_OK)
		return std::nullopt;
	std::ifstream in(outPath, std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
#else
	std::string body;
	if (RunShell(StrCat("curl -fsSL --max-time 20 -A DETH -H 'Accept: application/vnd.github+json' ", ShellQuote(url)), &body) != 0)
		return std::nullopt;
	return body;
#endif
}

bool DownloadFile(const std::string &url, const std::string &path)
{
#if defined(__ANDROID__)
	return AndroidCallBool("dethDownload", url, path);
#elif defined(_WIN32)
	DeleteFileA(path.c_str());
	return URLDownloadToFileA(nullptr, url.c_str(), path.c_str(), 0, nullptr) == S_OK;
#else
	return RunShell(StrCat("curl -fsSL --max-time 900 -A DETH -o ", ShellQuote(path), " ", ShellQuote(url))) == 0;
#endif
}

/** Keeps the bullet points of the release notes (the change list), at most `maxLines`. */
std::string SummarizeNotes(std::string_view body, size_t maxLines)
{
	std::string out;
	size_t lines = 0;
	size_t start = 0;
	while (start < body.size() && lines < maxLines) {
		size_t end = body.find('\n', start);
		if (end == std::string_view::npos)
			end = body.size();
		std::string_view line = body.substr(start, end - start);
		if (line.starts_with("- ")) {
			// One short line per change, so the dialog fits a phone screen.
			std::string_view text = line;
			if (text.size() > 64) {
				text = text.substr(0, 61);
				text = text.substr(0, text.find_last_of(' ') == std::string_view::npos ? text.size() : text.find_last_of(' '));
				out += StrCat(text, "...\n");
			} else {
				out += StrCat(text, "\n");
			}
			++lines;
		}
		start = end + 1;
	}
	return out;
}

std::mutex backgroundMutex;
std::optional<UpdateInfo> backgroundResult;
bool backgroundStarted = false;

} // namespace

bool IsDethReleaseVersion(std::string_view text)
{
	// Exactly M.m.p (stable) or M.m.b.N (beta); anything else (e.g. "1.0.1-rc1") is ignored.
	auto digits = [&](size_t &i) {
		const size_t from = i;
		while (i < text.size() && text[i] >= '0' && text[i] <= '9')
			++i;
		return i > from && i - from <= 6;
	};
	size_t i = 0;
	if (!digits(i) || i >= text.size() || text[i++] != '.')
		return false;
	if (!digits(i) || i >= text.size() || text[i++] != '.')
		return false;
	if (i < text.size() && text[i] == 'b') {
		++i;
		if (i >= text.size() || text[i++] != '.')
			return false;
	}
	return digits(i) && i == text.size();
}

int CompareDethVersions(std::string_view a, std::string_view b)
{
	const Version va = ParseVersion(a);
	const Version vb = ParseVersion(b);
	for (int i = 0; i < 3; ++i) {
		if (va.parts[i] != vb.parts[i])
			return va.parts[i] < vb.parts[i] ? -1 : 1;
	}
	if (va.stage != vb.stage)
		return va.stage < vb.stage ? -1 : 1;
	return 0;
}

namespace {
/** Test aid: DETH_UPDATE_TEST=<version> enables the updater anywhere and pretends to be that version. */
const char *TestVersion()
{
	static const char *version = std::getenv("DETH_UPDATE_TEST");
	return version;
}
std::string_view CurrentVersion()
{
	return TestVersion() != nullptr ? std::string_view(TestVersion()) : std::string_view(DETH_VERSION);
}
} // namespace

bool UpdaterSupported()
{
	if (TestVersion() != nullptr)
		return true;
#if defined(_WIN32) || defined(__ANDROID__)
	return true;
#elif defined(__linux__)
	return IsFlatpak();
#else
	return false;
#endif
}

bool CheckForUpdate(std::optional<UpdateInfo> &update, std::string &error)
{
	update = std::nullopt;
	const bool beta = *GetOptions().StartUp.updateChannel == UpdateChannel::Beta;
	// The list is newest first and includes pre-releases; "latest" is the newest full release.
	const std::string url = beta ? StrCat("https://api.github.com/repos/", Repo, "/releases?per_page=30")
	                             : StrCat("https://api.github.com/repos/", Repo, "/releases/latest");
	const std::optional<std::string> body = HttpGet(url);
	if (!body) {
		error = beta ? "Could not reach GitHub. Check the internet connection." : "No stable release yet (or GitHub could not be reached). Try the Beta channel.";
		return false;
	}
	const std::string_view json = *body;
	// Look at every release in the list and take the highest proper version (M.m.p or M.m.b.N)
	// that is newer than this build and has a file for this platform. The list order (by date)
	// and odd tags (test or helper releases) are never trusted.
	size_t tagAt = json.find("\"tag_name\"");
	if (tagAt == std::string_view::npos) {
		error = "No releases were found.";
		return false;
	}
	std::optional<UpdateInfo> best;
	while (tagAt != std::string_view::npos) {
		const size_t nextTag = json.find("\"tag_name\"", tagAt + 1);
		const size_t releaseEnd = nextTag == std::string_view::npos ? json.size() : nextTag;
		std::string version = FindJsonString(json, "tag_name", tagAt, releaseEnd).value_or("");
		if (!version.empty() && (version.front() == 'v' || version.front() == 'V'))
			version.erase(0, 1);
		const bool candidate = IsDethReleaseVersion(version)
		    && CompareDethVersions(version, CurrentVersion()) > 0
		    && (!best || CompareDethVersions(version, best->version) > 0)
		    && !FindJsonBool(json, "draft", tagAt, releaseEnd);
		if (candidate) {
			UpdateInfo info;
			info.version = version;
			info.prerelease = FindJsonBool(json, "prerelease", tagAt, releaseEnd);
			if (const std::optional<std::string> notes = FindJsonString(json, "body", tagAt, releaseEnd))
				info.notes = SummarizeNotes(*notes, 3);
			size_t searchFrom = tagAt;
			while (true) {
				size_t at = 0;
				const std::optional<std::string> asset = FindJsonString(json, "browser_download_url", searchFrom, releaseEnd, &at);
				if (!asset)
					break;
				if (asset->ends_with(AssetSuffix())) {
					info.assetUrl = *asset;
					info.assetName = asset->substr(asset->find_last_of('/') + 1);
					break;
				}
				searchFrom = at + 1;
			}
			if (!info.assetUrl.empty())
				best = info;
			else
				LogVerbose("Update check: {} has no file for this platform", version);
		}
		tagAt = nextTag;
	}
	update = best;
	return true;
}

void StartBackgroundUpdateCheck()
{
	{
		const std::lock_guard<std::mutex> lock(backgroundMutex);
		if (backgroundStarted || !UpdaterSupported() || !*GetOptions().StartUp.checkForUpdates)
			return;
		backgroundStarted = true;
	}
	std::thread([] {
		std::optional<UpdateInfo> update;
		std::string error;
		if (CheckForUpdate(update, error) && update) {
			const std::lock_guard<std::mutex> lock(backgroundMutex);
			backgroundResult = update;
		} else if (!error.empty()) {
			LogVerbose("Update check: {}", error);
		}
	}).detach();
}

std::optional<UpdateInfo> BackgroundUpdateResult()
{
	const std::lock_guard<std::mutex> lock(backgroundMutex);
	return backgroundResult;
}

bool DownloadAndInstallUpdate(const UpdateInfo &update, std::string &error)
{
#if defined(__ANDROID__)
	const std::string path = StrCat(paths::PrefPath(), "update.apk");
	if (!DownloadFile(update.assetUrl, path)) {
		error = "The download failed. Check the internet connection and try again.";
		return false;
	}
	if (!AndroidCallBool("dethInstallApk", path, update.version)) {
		error = "Android did not start the installer. Allow DETH to install apps (Settings > Apps > DETH > Install unknown apps) and try again.";
		return false;
	}
	return true;
#elif defined(_WIN32)
	char exePath[MAX_PATH] {};
	GetModuleFileNameA(nullptr, exePath, MAX_PATH);
	std::string gameDir = exePath;
	gameDir.erase(gameDir.find_last_of("\\/"));
	const std::string temp = TempDir();
	const std::string zip = temp + "deth-update.zip";
	if (!DownloadFile(update.assetUrl, zip)) {
		error = "The download failed. Check the internet connection and try again.";
		return false;
	}
	// DETH: unpack with the 7-Zip that ships in the DETH folder, then put the new files in place.
	// A running file cannot be overwritten but can be renamed, so each old file is moved aside
	// first (as *.old, removed by the next update). Saves and settings are not in the zip.
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path unpacked = fs::path(temp) / "deth-update";
	fs::remove_all(unpacked, ec);
	fs::create_directories(unpacked, ec);
	const std::string sevenZip = gameDir + "\\tools\\7z.exe";
	if (RunHidden(StrCat("\"", sevenZip, "\" x -y \"-o", unpacked.string(), "\" \"", zip, "\"")) != 0) {
		error = "Could not unpack the update.";
		return false;
	}
	const fs::path source = unpacked / "DETH";
	if (!fs::exists(source / "DETH.exe", ec)) {
		error = "The update is incomplete.";
		return false;
	}
	const fs::path target = fs::path(gameDir);
	for (const fs::directory_entry &entry : fs::recursive_directory_iterator(target, ec)) {
		if (entry.is_regular_file(ec) && entry.path().extension() == ".old")
			fs::remove(entry.path(), ec);
	}
	for (const fs::directory_entry &entry : fs::recursive_directory_iterator(source, ec)) {
		const fs::path destination = target / fs::relative(entry.path(), source, ec);
		if (entry.is_directory(ec)) {
			fs::create_directories(destination, ec);
			continue;
		}
		if (fs::exists(destination, ec)) {
			fs::path aside = destination;
			aside += ".old";
			fs::remove(aside, ec);
			fs::rename(destination, aside, ec);
		}
		ec.clear();
		fs::copy_file(entry.path(), destination, fs::copy_options::overwrite_existing, ec);
		if (ec) {
			error = StrCat("Could not write ", destination.filename().string(), ".");
			return false;
		}
	}
	STARTUPINFOA si {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi {};
	std::string cmd = StrCat("\"", (target / "DETH.exe").string(), "\"");
	if (CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, gameDir.c_str(), &si, &pi)) {
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}
	SaveOptions();
	std::exit(0);
#else
	if (!IsFlatpak()) {
		error = "This build cannot update itself. Download the new version from GitHub.";
		return false;
	}
	std::string dir = paths::DropPath();
	if (dir.empty())
		dir = paths::PrefPath();
	const std::string file = StrCat(dir, "DETH-update.flatpak");
	if (!DownloadFile(update.assetUrl, file)) {
		error = "The download failed. Check the internet connection and try again.";
		return false;
	}
	// Install into the same installation (per-user or system) the game runs from.
	std::string info;
	{
		std::ifstream in("/.flatpak-info");
		std::stringstream ss;
		ss << in.rdbuf();
		info = ss.str();
	}
	const bool userInstall = info.find("/.local/share/flatpak") != std::string::npos;
	const std::string install = StrCat("flatpak install ", userInstall ? "--user" : "--system", " -y --noninteractive --reinstall ", ShellQuote(file));
	if (RunShell(install) != 0) {
		error = "Installing the update failed. You can install DETH-update.flatpak from your Downloads/DETH folder by hand.";
		return false;
	}
	// No automatic relaunch: a copy started from here runs outside Steam, which in Game Mode is an
	// invisible game playing in the background (it then shows up in Desktop Mode). The player
	// starts DETH again from Steam or the app menu.
	SaveOptions();
	return true;
#endif
}

} // namespace devilution
