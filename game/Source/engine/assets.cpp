#include "engine/assets.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#else
#include <SDL.h>
#endif

#include "appfat.h"
#include "game_mode.hpp"
#include "mods/mod_identity.h"
#include "utils/file_util.h"
#include "utils/format.hpp"
#include "utils/log.hpp"
#include "utils/paths.h"
#include "utils/sdl_compat.h"
#include "utils/str_case.hpp"
#include "utils/str_cat.hpp"
#include "utils/str_split.hpp"

#if defined(_WIN32) && !defined(__UWP__) && !defined(DEVILUTIONX_WINDOWS_NO_WCHAR)
#include <find_steam_game.h>
#endif

#ifndef UNPACKED_MPQS
#include "mpq/mpq_sdl_rwops.hpp"
#endif

namespace devilution {

std::vector<std::string> OverridePaths;
std::map<int, MpqArchiveT, std::greater<>> MpqArchives;
bool HasHellfireMpq;
bool HasDiabdatMpq;
bool IsAssetIntegrityViolated = false;

namespace {

#ifdef UNPACKED_MPQS
char *FindUnpackedMpqFile(char *relativePath)
{
	char *path = nullptr;
	for (const auto &[_, unpackedDir] : MpqArchives) {
		path = relativePath - unpackedDir.size();
		std::memcpy(path, unpackedDir.data(), unpackedDir.size());
		if (FileExists(path)) break;
		path = nullptr;
	}
	return path;
}
#else
bool IsDebugLogging()
{
	return IsLogLevel(LogCategory::Application, SDL_LOG_PRIORITY_DEBUG);
}

SDL_IOStream *OpenOptionalRWops(const std::string &path)
{
	// SDL always logs an error in Debug mode.
	// We check the file presence in Debug mode to avoid this.
#if defined(__ANDROID__) && !defined(TERMUX)
	// DETH: relative paths are APK assets, which FileExists cannot see.
	if (!path.empty() && path[0] != '/' && IsDebugLogging())
		return SDL_IOFromFile(path.c_str(), "rb");
#endif
	if (IsDebugLogging() && !FileExists(path.c_str()))
		return nullptr;
	return SDL_IOFromFile(path.c_str(), "rb");
};

bool FindMpqFile(std::string_view filename, MpqArchive **archive, uint32_t *hashIndex)
{
	for (auto &[_, mpqArchive] : MpqArchives) {
		uint32_t hash = mpqArchive.FindHash(filename);
		if (hash != UINT32_MAX) {
			*archive = &mpqArchive;
			*hashIndex = hash;
			return true;
		}
	}

	return false;
}

bool HasLogicAssetExtension(std::string_view filename)
{
	if (filename.size() < 4)
		return false;
	// Case-insensitive so that overrides on case-insensitive filesystems are caught.
	const std::string extension = AsciiStrToLower(filename.substr(filename.size() - 4));
	return extension == ".lua" || extension == ".tsv" || extension == ".sol";
}

/**
 * @brief Returns true if a logic-asset request could resolve to a loose file at `relativePath`.
 */
bool IsLoadableLogicAssetPath(std::string relativePath)
{
	// Asset requests use `\` as the separator.
	std::replace(relativePath.begin(), relativePath.end(), DirectorySeparator, '\\');
	AsciiStrToLower(relativePath);

	// Lua modules are requested by module name under `lua\` (see `LoadPackageData`), including
	// mod entry points such as `lua\mods\<name>\init.lua` that have no archive counterpart.
	if (relativePath.starts_with("lua\\") && relativePath.ends_with(".lua"))
		return true;

	// TSV and SOL requests use fixed canonical paths.
	MpqArchive *archive;
	uint32_t hashIndex;
	if (FindMpqFile(relativePath, &archive, &hashIndex))
		return true;

	std::string assetsDirPath = relativePath;
	std::replace(assetsDirPath.begin(), assetsDirPath.end(), '\\', DirectorySeparator);
	return FileExists((paths::AssetsPath() + assetsDirPath).c_str());
}

bool ContainsLogicAssets(const std::string &rootPath, const std::string &relativeDir, unsigned depth)
{
	constexpr unsigned MaxScanDepth = 16;
	if (depth > MaxScanDepth)
		return false;
	const std::string dirPath = rootPath + relativeDir;
	for (const std::string &filename : ListFiles(dirPath.c_str())) {
		if (HasLogicAssetExtension(filename) && IsLoadableLogicAssetPath(relativeDir + filename))
			return true;
	}
	for (const std::string &subdirName : ListDirectories(dirPath.c_str())) {
		if (ContainsLogicAssets(rootPath, StrCat(relativeDir, subdirName, DIRECTORY_SEPARATOR_STR), depth + 1))
			return true;
	}
	return false;
}

#endif

} // namespace

#ifdef UNPACKED_MPQS
AssetRef FindAsset(std::string_view filename)
{
	AssetRef result;
	if (filename.empty() || filename.back() == '\\')
		return result;
	result.path[0] = '\0';

	char pathBuf[AssetRef::PathBufSize];
	char *const pathEnd = pathBuf + AssetRef::PathBufSize;
	char *const relativePath = &pathBuf[AssetRef::PathBufSize - filename.size() - 1];
	*BufCopy(relativePath, filename) = '\0';

#if !defined(_WIN32) && !defined(__DJGPP__)
	std::replace(relativePath, pathEnd, '\\', '/');
#endif
	// Absolute path:
	if (relativePath[0] == '/') {
		if (FileExists(relativePath)) {
			*BufCopy(result.path, std::string_view(relativePath, filename.size())) = '\0';
		}
		return result;
	}

	// Unpacked MPQ file:
	char *const unpackedMpqPath = FindUnpackedMpqFile(relativePath);
	if (unpackedMpqPath != nullptr) {
		*BufCopy(result.path, std::string_view(unpackedMpqPath, pathEnd - unpackedMpqPath)) = '\0';
		return result;
	}

	// The `/assets` directory next to the devilutionx binary.
	const std::string &assetsPathPrefix = paths::AssetsPath();
	char *assetsPath = relativePath - assetsPathPrefix.size();
	std::memcpy(assetsPath, assetsPathPrefix.data(), assetsPathPrefix.size());
	if (FileExists(assetsPath)) {
		*BufCopy(result.path, std::string_view(assetsPath, pathEnd - assetsPath)) = '\0';
	}
	return result;
}
#else
AssetRef FindAsset(std::string_view filename)
{
	AssetRef result;
	if (filename.empty() || filename.back() == '\\')
		return result;

	std::string relativePath { filename };
#ifndef _WIN32
	std::replace(relativePath.begin(), relativePath.end(), '\\', '/');
#endif

	if (relativePath[0] == '/') {
		result.directHandle = SDL_IOFromFile(relativePath.c_str(), "rb");
		if (result.directHandle != nullptr) {
			return result;
		}
	}

	// Files in the `PrefPath()` directory can override MPQ contents.
	{
		for (const auto &overridePath : OverridePaths) {
			const std::string path = overridePath + relativePath;
			result.directHandle = OpenOptionalRWops(path);
			if (result.directHandle != nullptr) {
				LogVerbose("Loaded MPQ file override: {}", path);
				result.isOverridden = true;
				return result;
			}
		}
	}

	// Look for the file in all the MPQ archives:
	if (FindMpqFile(filename, &result.archive, &result.hashIndex)) {
		result.filename = filename;
		return result;
	}

	// Load from the `/assets` directory next to the devilutionx binary.
	result.directHandle = OpenOptionalRWops(paths::AssetsPath() + relativePath);
	if (result.directHandle != nullptr)
		return result;

#if (defined(__ANDROID__) && !defined(TERMUX)) || defined(__APPLE__)
	// Fall back to the bundled assets on supported systems.
	// This is handled by SDL when we pass a relative path.
	if (!paths::AssetsPath().empty()) {
		result.directHandle = SDL_IOFromFile(relativePath.c_str(), "rb");
		if (result.directHandle != nullptr)
			return result;
	}
#endif

	return result;
}
#endif

AssetHandle OpenAsset(AssetRef &&ref, bool threadsafe)
{
#ifdef UNPACKED_MPQS
	return AssetHandle { OpenFile(ref.path, "rb") };
#else
	if (ref.archive != nullptr)
		return AssetHandle { SDL_RWops_FromMpqFile(*ref.archive, ref.hashIndex, ref.filename, threadsafe) };
	if (ref.directHandle != nullptr) {
		// Transfer handle ownership:
		auto *handle = ref.directHandle;
		ref.directHandle = nullptr;
		return AssetHandle { handle };
	}
	return AssetHandle { nullptr };
#endif
}

AssetHandle OpenAsset(std::string_view filename, bool threadsafe)
{
	AssetRef ref = FindAsset(filename);
	if (!ref.ok())
		return AssetHandle {};
	return OpenAsset(std::move(ref), threadsafe);
}

AssetHandle OpenAsset(std::string_view filename, size_t &fileSize, bool threadsafe)
{
	AssetRef ref = FindAsset(filename);
	if (!ref.ok())
		return AssetHandle {};
	fileSize = ref.size();
	return OpenAsset(std::move(ref), threadsafe);
}

AssetHandle OpenIntegralAsset(AssetRef &&ref, bool threadsafe)
{
#ifndef UNPACKED_MPQS
	if (ref.isOverridden)
		IsAssetIntegrityViolated = true;
#endif
	return OpenAsset(std::move(ref), threadsafe);
}

AssetHandle OpenIntegralAsset(std::string_view filename, bool threadsafe)
{
	AssetRef ref = FindAsset(filename);
	if (!ref.ok())
		return AssetHandle {};
	return OpenIntegralAsset(std::move(ref), threadsafe);
}

AssetHandle OpenIntegralAsset(std::string_view filename, size_t &fileSize, bool threadsafe)
{
	AssetRef ref = FindAsset(filename);
	if (!ref.ok())
		return AssetHandle {};
	fileSize = ref.size();
	return OpenIntegralAsset(std::move(ref), threadsafe);
}

SDL_IOStream *OpenAssetAsSdlRwOps(std::string_view filename, bool threadsafe)
{
#ifdef UNPACKED_MPQS
	AssetRef ref = FindAsset(filename);
	if (!ref.ok())
		return nullptr;
	return SDL_IOFromFile(ref.path, "rb");
#else
	return OpenAsset(filename, threadsafe).release();
#endif
}

std::expected<AssetData, std::string> LoadAsset(std::string_view path)
{
	AssetRef ref = FindAsset(path);
	if (!ref.ok()) {
		return std::unexpected(StrCat("Asset not found: ", path));
	}

	const size_t size = ref.size();
	std::unique_ptr<char[]> data { new char[size] };

	AssetHandle handle = OpenAsset(std::move(ref));
	if (!handle.ok()) {
		return std::unexpected(StrCat("Failed to open asset: ", path, "\n", handle.error()));
	}

	if (size > 0 && !handle.read(data.get(), size)) {
		return std::unexpected(StrCat("Read failed: ", path, "\n", handle.error()));
	}

	return AssetData { std::move(data), size };
}

std::expected<AssetData, std::string> LoadIntegralAsset(std::string_view path)
{
	AssetRef ref = FindAsset(path);
	if (!ref.ok()) {
		return std::unexpected(StrCat("Asset not found: ", path));
	}

	const size_t size = ref.size();
	std::unique_ptr<char[]> data { new char[size] };

	AssetHandle handle = OpenIntegralAsset(std::move(ref));
	if (!handle.ok()) {
		return std::unexpected(StrCat("Failed to open asset: ", path, "\n", handle.error()));
	}

	if (size > 0 && !handle.read(data.get(), size)) {
		return std::unexpected(StrCat("Read failed: ", path, "\n", handle.error()));
	}

	return AssetData { std::move(data), size };
}

std::string FailedToOpenFileErrorMessage(std::string_view path, std::string_view error)
{
	return FormatRuntime(_("Failed to open file:\n{:s}\n\n{:s}\n\nThe MPQ file(s) might be damaged. Please check the file integrity."), path, error);
}

namespace {
#ifdef UNPACKED_MPQS
std::optional<std::string> FindUnpackedMpqData(std::span<const std::string> paths, std::string_view mpqName)
{
	std::string targetPath;
	for (const std::string &path : paths) {
		targetPath.clear();
		targetPath.reserve(path.size() + mpqName.size() + 1);
		targetPath.append(path).append(mpqName) += DirectorySeparator;
		if (FileExists(targetPath)) {
			LogVerbose("  Found unpacked MPQ directory: {}", targetPath);
			return targetPath;
		}
	}
	return std::nullopt;
}

bool FindMPQ(std::span<const std::string> paths, std::string_view mpqName)
{
	return FindUnpackedMpqData(paths, mpqName).has_value();
}

bool LoadMPQ(std::span<const std::string> paths, std::string_view mpqName, int priority)
{
	std::optional<std::string> mpqPath = FindUnpackedMpqData(paths, mpqName);
	if (!mpqPath.has_value()) {
		LogVerbose("Missing: {}", mpqName);
		return false;
	}
	MpqArchives[priority] = *std::move(mpqPath);
	return true;
}
#else
bool FindMPQ(std::span<const std::string> paths, std::string_view mpqName)
{
	std::string mpqAbsPath;
	for (const auto &path : paths) {
		mpqAbsPath = StrCat(path, mpqName, ".mpq");
		if (FileExists(mpqAbsPath)) {
			LogVerbose("  Found: {} in {}", mpqName, path);
			return true;
		}
	}

	return false;
}

bool LoadMPQ(std::span<const std::string> paths, std::string_view mpqName, int priority, std::string_view ext = ".mpq", std::string *loadedPath = nullptr)
{
	std::string mpqAbsPath;
	bool foundButFailed = false;
	for (const auto &path : paths) {
		mpqAbsPath = StrCat(path, mpqName, ext);
		if (!FileExists(mpqAbsPath))
			continue;
		std::expected<MpqArchive, std::string> archive = MpqArchive::Open(mpqAbsPath.c_str());
		if (!archive.has_value()) {
			foundButFailed = true;
			LogError("Error {}: {}", archive.error(), mpqAbsPath);
			continue;
		}
		LogVerbose("  Found: {} in {}", mpqName, path);
		auto [it, inserted] = MpqArchives.emplace(priority, *std::move(archive));
		if (!inserted) {
			LogError("MPQ with priority {} is already registered, skipping {}", priority, mpqName);
		}
		if (loadedPath != nullptr)
			*loadedPath = mpqAbsPath;
		return true;
	}
	if (!foundButFailed) {
		LogVerbose("Missing: {}", mpqName);
	}

	return false;
}
#endif

std::vector<std::string> GetMPQSearchPaths()
{
	std::vector<std::string> paths;
	// DETH: game files next to the AppImage (standalone use), then Downloads/DETH.
	if (const char *appImage = std::getenv("APPIMAGE"); appImage != nullptr && *appImage != '\0') {
		const std::string_view appImagePath = appImage;
		const size_t slash = appImagePath.rfind('/');
		if (slash != std::string_view::npos)
			paths.emplace_back(appImagePath.substr(0, slash + 1));
	}
	if (!paths::DropPath().empty())
		paths.push_back(paths::DropPath());
	paths.push_back(paths::BasePath());
	for (const std::string &p : { paths::PrefPath(), paths::ConfigPath() }) {
		if (std::find(paths.begin(), paths.end(), p) == paths.end())
			paths.push_back(p);
	}

#if (defined(__unix__) || defined(__APPLE__)) && !defined(__ANDROID__) && !defined(__DJGPP__)
	// `XDG_DATA_HOME` is usually the root path of `paths::PrefPath()`, so we only
	// add `XDG_DATA_DIRS`.
	const char *xdgDataDirs = std::getenv("XDG_DATA_DIRS");
	if (xdgDataDirs != nullptr) {
		for (const std::string_view path : SplitByChar(xdgDataDirs, ':')) {
			std::string fullPath(path);
			if (!path.empty() && path.back() != '/')
				fullPath += '/';
			fullPath.append("diasurgical/devilutionx/");
			paths.push_back(std::move(fullPath));
		}
	} else {
		paths.emplace_back("/usr/local/share/diasurgical/devilutionx/");
		paths.emplace_back("/usr/share/diasurgical/devilutionx/");
	}
#elif defined(TERMUX)
#ifdef CMAKE_INSTALL_PREFIX
	paths.emplace_back(CMAKE_INSTALL_PREFIX "/share/diasurgical/devilutionx/");
#else
	paths.emplace_back("/usr/share/diasurgical/devilutionx/");
#endif
#elif defined(__3DS__) || defined(__SWITCH__)
	paths.emplace_back("romfs:/");
#elif defined(NXDK)
	paths.emplace_back("D:\\");
#elif defined(_WIN32) && !defined(__UWP__) && !defined(DEVILUTIONX_WINDOWS_NO_WCHAR)
	char gogpath[_FSG_PATH_MAX];
	fsg_get_gog_game_path(gogpath, "1412601690");
	if (strlen(gogpath) > 0) {
		paths.emplace_back(std::string(gogpath) + "/");
		paths.emplace_back(std::string(gogpath) + "/hellfire/");
	}
#endif

	if (paths.empty() || !paths.back().empty()) {
		paths.emplace_back(); // PWD
	}

	if (IsLogLevel(LogCategory::Application, SDL_LOG_PRIORITY_VERBOSE)) {
		LogVerbose("Paths:\n    base: {}\n    pref: {}\n  config: {}\n  assets: {}",
		    paths::BasePath(), paths::PrefPath(), paths::ConfigPath(), paths::AssetsPath());

		std::string message;
		for (std::size_t i = 0; i < paths.size(); ++i) {
			message.append(StrCat("\n", LeftPad(i + 1, 6, ' '), ". '", paths[i], "'"));
		}
		LogVerbose("MPQ search paths:{}", message);
	}

	return paths;
}

} // namespace

void LoadCoreArchives()
{
	auto paths = GetMPQSearchPaths();

#if !(defined(__ANDROID__) && !defined(TERMUX)) && !defined(__APPLE__) && !defined(__3DS__) && !defined(__SWITCH__)
	// Load devilutionx.mpq first to get the font file for error messages
#ifdef __DJGPP__
	LoadMPQ(paths, "devx", DevilutionXMpqPriority);
#else
	LoadMPQ(paths, "devilutionx", DevilutionXMpqPriority);
#endif
#endif
	LoadMPQ(paths, "fonts", FontMpqPriority); // Extra fonts
	HasHellfireMpq = FindMPQ(paths, "hellfire");
}

void LoadLanguageArchive()
{
	MpqArchives.erase(LangMpqPriority);
	const std::string_view code = GetLanguageCode();
	if (code != "en") {
		LoadMPQ(GetMPQSearchPaths(), code, LangMpqPriority);
	}
}

static void EnsureDropFolder()
{
	const std::string &dropPath = paths::DropPath();
	if (dropPath.empty())
		return;
	if (!DirectoryExists(dropPath))
		RecursivelyCreateDir(dropPath.c_str());
	const std::string readme = dropPath + "PUT GAME FILES HERE.txt";
	if (!DirectoryExists(dropPath) || FileExists(readme))
		return;
	if (FILE *f = std::fopen(readme.c_str(), "w"); f != nullptr) {
		std::fputs(
		    "DETH game data folder\n"
		    "=====================\n\n"
		    "Copy your Diablo data files into this folder.\n\n"
		    "Diablo (required, one of):\n"
		    "  DIABDAT.MPQ  - full game, from the original CD, GOG or Battle.net\n"
		    "  spawn.mpq    - free shareware version\n\n"
		    "Hellfire expansion (optional, all four needed):\n"
		    "  hellfire.mpq, hfmonk.mpq, hfmusic.mpq, hfvoice.mpq\n"
		    "  (hfbard.mpq and hfbarb.mpq are optional extras)\n\n"
		    "File name case does not matter for diabdat.mpq.\n",
		    f);
		std::fclose(f);
	}
}

void LoadGameArchives()
{
	EnsureDropFolder();
	const std::vector<std::string> paths = GetMPQSearchPaths();
	bool haveDiabdat = false;
	bool haveSpawn = false;

#ifndef UNPACKED_MPQS
	// DIABDAT.MPQ is uppercase on the original CD and the GOG version.
	haveDiabdat = LoadMPQ(paths, "DIABDAT", MainMpqPriority, ".MPQ");
#endif

	if (!haveDiabdat) {
		haveDiabdat = LoadMPQ(paths, "diabdat", MainMpqPriority);
		if (!haveDiabdat) {
			gbIsSpawn = haveSpawn = LoadMPQ(paths, "spawn", MainMpqPriority);
		}
	}

	HasDiabdatMpq = haveDiabdat;

	if (!HeadlessMode) {
		if (!haveDiabdat && !haveSpawn) {
			LogError("{}", SDL_GetError());
			InsertCDDlg(_("diabdat.mpq or spawn.mpq"));
		}
	}

	if (forceHellfire && !HasHellfireMpq) {
#ifdef UNPACKED_MPQS
		InsertCDDlg("hellfire");
#else
		InsertCDDlg("hellfire.mpq");
#endif
	}

#ifndef UNPACKED_MPQS
	// In unpacked mode, all the hellfire data is in the hellfire directory.
	LoadMPQ(paths, "hfbard", 8110);
	LoadMPQ(paths, "hfbarb", 8120);
#endif
}

bool HaveMpqFile(std::string_view name)
{
#ifdef UNPACKED_MPQS
	return false;
#else
	const std::vector<std::string> paths = GetMPQSearchPaths();
	return FindMPQ(paths, name);
#endif
}

int CountHellfireMpqs()
{
#ifdef UNPACKED_MPQS
	return HasHellfireMpq ? 4 : 0;
#else
	const std::vector<std::string> paths = GetMPQSearchPaths();
	int count = 0;
	for (const std::string_view name : { "hellfire", "hfmonk", "hfmusic", "hfvoice" }) {
		if (FindMPQ(paths, name))
			++count;
	}
	return count;
#endif
}

void LoadHellfireArchives()
{
	const std::vector<std::string> paths = GetMPQSearchPaths();
	LoadMPQ(paths, "hellfire", 8000);

#ifdef UNPACKED_MPQS
	const std::string &hellfireDataPath = MpqArchives.at(8000);
	const bool hasMonk = FileExists(hellfireDataPath + "plrgfx/monk/mha/mhaas.clx");
	const bool hasMusic = FileExists(hellfireDataPath + "music/dlvlf.wav")
	    || FileExists(hellfireDataPath + "music/dlvlf.mp3");
	const bool hasVoice = FileExists(hellfireDataPath + "sfx/hellfire/cowsut1.wav")
	    || FileExists(hellfireDataPath + "sfx/hellfire/cowsut1.mp3");
#else
	const bool hasMonk = LoadMPQ(paths, "hfmonk", 8100);
	const bool hasMusic = LoadMPQ(paths, "hfmusic", 8200);
	const bool hasVoice = LoadMPQ(paths, "hfvoice", 8500);
#endif

	if (!hasMonk || !hasMusic || !hasVoice)
		DisplayFatalErrorAndExit(_("Some Hellfire MPQs are missing"), _("Not all Hellfire MPQs were found.\nPlease copy all the hf*.mpq files."));
}

void UnloadModArchives()
{
	OverridePaths.clear();

#ifndef UNPACKED_MPQS
	for (auto it = MpqArchives.begin(); it != MpqArchives.end();) {
		if ((it->first >= 8000 && it->first < 9000) || it->first >= 10000) {
			it = MpqArchives.erase(it); // erase returns the next valid iterator
		} else {
			++it;
		}
	}
#endif
}

#ifndef UNPACKED_MPQS
namespace {

std::expected<ModManifest, int32_t> ReadPackedModManifestFrom(MpqArchive &archive)
{
	constexpr std::string_view ManifestName = "manifest.ini";
	if (!archive.HasFile(ManifestName))
		return std::unexpected(0);

	size_t fileSize = 0;
	int32_t error = 0;
	std::unique_ptr<std::byte[]> data = archive.ReadFile(ManifestName, fileSize, error);
	if (data == nullptr)
		return std::unexpected(error);

	return ParseModManifest(std::string_view(reinterpret_cast<const char *>(data.get()), fileSize));
}

std::expected<ModManifest, int32_t> ReadPackedModManifest(std::span<const std::string> paths, std::string_view modname)
{
	const std::string mpqName = StrCat("mods" DIRECTORY_SEPARATOR_STR, modname);
	std::string mpqAbsPath;
	for (const std::string &path : paths) {
		mpqAbsPath = StrCat(path, mpqName, ".mpq");
		if (!FileExists(mpqAbsPath))
			continue;
		std::expected<MpqArchive, std::string> archive = MpqArchive::Open(mpqAbsPath.c_str());
		if (!archive.has_value())
			return std::unexpected(0);
		return ReadPackedModManifestFrom(*archive);
	}
	return std::unexpected(0);
}

// Reads the mod's `manifest.ini` from its own archive (registered at `priority`) and
// attaches the parsed metadata. Deliberately bypasses the override-capable `FindAsset`
// pipeline so the manifest is exactly the one covered by the mod's identifying hash.
void ReadLoadedModManifest(ModIdentifier &mod, int priority)
{
	auto it = MpqArchives.find(priority);
	if (it == MpqArchives.end())
		return;
	MpqArchive &archive = it->second;
	std::expected<ModManifest, int32_t> manifest = ReadPackedModManifestFrom(archive);
	if (manifest.has_value())
		mod.manifest = std::move(*manifest);
	else if (manifest.error() != 0)
		LogError("Failed to read manifest from mod {}: error {}", mod.name, manifest.error());
}

// Reads the `requires` list from a packed mod's manifest without registering the archive, so
// it can inform load ordering before the real load pass assigns priorities. Returns empty if
// the mod has no packed archive, no manifest, or an unreadable one.
std::vector<std::string> ReadPackedModRequiredMods(std::span<const std::string> paths, std::string_view modname)
{
	std::expected<ModManifest, int32_t> manifest = ReadPackedModManifest(paths, modname);
	if (!manifest.has_value())
		return {};
	return std::move(manifest->requiredMods);
}

} // namespace
#endif

ModManifest ReadModManifestByName(std::string_view name)
{
	constexpr std::string_view ManifestName = "manifest.ini";

	// Matches the OverridePaths registered per mod by LoadModArchives()
	const std::array<std::string, 2> unpackedSearchPaths {
		paths::PrefPath(),
		paths::BasePath(),
	};

	// Loose mod directory (also how UNPACKED_MPQS builds ship their mods).
	for (const std::string &path : unpackedSearchPaths) {
		const std::string manifestPath = StrCat(path, "mods" DIRECTORY_SEPARATOR_STR, name, DIRECTORY_SEPARATOR_STR, ManifestName);
		FILE *file = OpenFile(manifestPath.c_str(), "rb");
		if (file == nullptr)
			continue;
		uintmax_t size = 0;
		std::string contents;
		if (GetFileSize(manifestPath.c_str(), &size) && size > 0) {
			contents.resize(static_cast<size_t>(size));
			if (std::fread(contents.data(), 1, contents.size(), file) != contents.size())
				contents.clear();
		}
		std::fclose(file);
		if (!contents.empty())
			return ParseModManifest(contents);
	}

#ifndef UNPACKED_MPQS
	// Packed mod archive.
	const std::vector<std::string> packedSearchPaths = GetMPQSearchPaths();
	std::expected<ModManifest, int32_t> manifest = ReadPackedModManifest(packedSearchPaths, name);
	if (manifest.has_value())
		return std::move(*manifest);
#endif

	return {};
}

void LoadModArchives(std::span<const std::string_view> modnames)
{
	ClearModIdentifiers();

#ifdef UNPACKED_MPQS
	const std::vector<std::string_view> activeMods(modnames.begin(), modnames.end());
#else
	// Order the active mods so every mod's declared requiredMods load first: a dependency gets a
	// lower priority (loaded earlier, forms the base) and its dependents get higher priorities
	// (loaded later, override it). `requires` is read from each packed mod's manifest in a
	// pre-pass, because the real load below fixes priorities in iteration order.
	const std::vector<std::string> searchPaths = GetMPQSearchPaths();
	std::vector<ModDependency> deps;
	deps.reserve(modnames.size());
	for (const std::string_view modname : modnames)
		deps.push_back(ModDependency { std::string(modname), ReadPackedModRequiredMods(searchPaths, modname) });
	const std::vector<std::string> orderedNames = OrderModsByDependencies(deps);
	const std::vector<std::string_view> activeMods(orderedNames.begin(), orderedNames.end());

	// Tracks, per active mod, whether a loose override directory was found. Such mods carry no
	// identifier (they are gated by the loose-asset integrity check) and are not built-in.
	std::vector<bool> hasLooseOverride(activeMods.size(), false);
#endif

	std::string targetPath;
	for (size_t i = 0; i < activeMods.size(); ++i) {
		const std::string_view modname = activeMods[i];
		targetPath = StrCat(paths::PrefPath(), "mods" DIRECTORY_SEPARATOR_STR, modname, DIRECTORY_SEPARATOR_STR);
		if (DirectoryExists(targetPath)) {
			OverridePaths.emplace_back(targetPath);
#ifndef UNPACKED_MPQS
			hasLooseOverride[i] = true;
#endif
		}
		targetPath = StrCat(paths::BasePath(), "mods" DIRECTORY_SEPARATOR_STR, modname, DIRECTORY_SEPARATOR_STR);
		if (DirectoryExists(targetPath)) {
			OverridePaths.emplace_back(targetPath);
#ifndef UNPACKED_MPQS
			hasLooseOverride[i] = true;
#endif
		}
#if defined(__ANDROID__) && !defined(TERMUX)
		// DETH: mods bundled in the APK (the Hellfire mod) are read through SDL's asset loader,
		// which takes paths relative to the APK's assets folder. They count as built-in.
		targetPath = StrCat("mods/", modname, "/");
		if (SDL_IOStream *manifest = SDL_IOFromFile(StrCat(targetPath, "manifest.ini").c_str(), "rb"); manifest != nullptr) {
			SDL_CloseIO(manifest);
			OverridePaths.emplace_back(targetPath);
		}
#endif
	}
	OverridePaths.emplace_back(paths::PrefPath());

	int priority = 10000;
	auto paths = GetMPQSearchPaths();
	for (size_t i = 0; i < activeMods.size(); ++i) {
		const std::string_view modname = activeMods[i];
		const std::string mpqName = StrCat("mods" DIRECTORY_SEPARATOR_STR, modname);
#ifdef UNPACKED_MPQS
		LoadMPQ(paths, mpqName, priority);
#else
		std::string loadedPath;
		if (LoadMPQ(paths, mpqName, priority, ".mpq", &loadedPath)) {
			// A packed mod: identify it by the hash of its MPQ bytes. A local packed mod
			// deliberately shadows any built-in of the same name, so it is treated as external.
			ModIdentifier &mod = RegisterPackedModIdentifier(modname, loadedPath.c_str());
			ReadLoadedModManifest(mod, priority);
		} else if (!hasLooseOverride[i]) {
			// Neither a packed MPQ nor a loose override directory: this mod resolves purely
			// from core archives (a built-in), so it is provenance-whitelisted.
			RegisterBuiltinModIdentifier(modname);
		}
#endif
		priority++;
	}
}

namespace {

struct Fnv64 {
	uint64_t value = 14695981039346656037ULL;
	void Add(const void *data, size_t size)
	{
		const auto *bytes = static_cast<const uint8_t *>(data);
		for (size_t i = 0; i < size; ++i) {
			value ^= bytes[i];
			value *= 1099511628211ULL;
		}
	}
	void Add(std::string_view text)
	{
		Add(text.data(), text.size());
		const uint8_t separator = 0;
		Add(&separator, 1);
	}
};

#ifndef UNPACKED_MPQS
void AddLooseLogicAssets(Fnv64 &hash, const std::string &rootPath, const std::string &relativeDir, unsigned depth)
{
	if (depth > 16)
		return;
	const std::string dirPath = rootPath + relativeDir;
	std::vector<std::string> files = ListFiles(dirPath.c_str());
	std::sort(files.begin(), files.end());
	for (const std::string &filename : files) {
		if (!HasLogicAssetExtension(filename) || !IsLoadableLogicAssetPath(relativeDir + filename))
			continue;
		std::string name = relativeDir + filename;
		std::replace(name.begin(), name.end(), DirectorySeparator, '\\');
		AsciiStrToLower(name);
		hash.Add(name);
		const std::string path = dirPath + filename;
		if (std::FILE *file = std::fopen(path.c_str(), "rb"); file != nullptr) {
			char buffer[4096];
			size_t read;
			while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
				hash.Add(buffer, read);
			std::fclose(file);
		}
	}
	std::vector<std::string> dirs = ListDirectories(dirPath.c_str());
	std::sort(dirs.begin(), dirs.end());
	for (const std::string &subdir : dirs)
		AddLooseLogicAssets(hash, rootPath, StrCat(relativeDir, subdir, DIRECTORY_SEPARATOR_STR), depth + 1);
}
#endif

} // namespace

uint64_t GetLogicAssetsFingerprint()
{
	Fnv64 hash;
	for (const ModIdentifier &mod : ActiveModIdentifiers) {
		hash.Add(mod.name);
		hash.Add(mod.hash.data(), mod.hash.size());
	}
#ifndef UNPACKED_MPQS
	for (const std::string &overridePath : OverridePaths)
		AddLooseLogicAssets(hash, overridePath, {}, 0);
#endif
	return hash.value;
}

bool HasLooseLogicAssets()
{
#ifdef UNPACKED_MPQS
	// Unpacked builds do not consult `OverridePaths` and have no override integrity tracking.
	return false;
#else
	for (const std::string &overridePath : OverridePaths) {
		if (ContainsLogicAssets(overridePath, /*relativeDir=*/ {}, /*depth=*/0))
			return true;
	}
	return false;
#endif
}

} // namespace devilution
