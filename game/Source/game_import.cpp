#include "game_import.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "utils/file_util.h"
#include "utils/log.hpp"
#include "utils/paths.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

constexpr int MaxNesting = 5;
/** Never a real password; stops 7-Zip from waiting for one on unencrypted archives. */
constexpr std::string_view NoPassword = "DETH-NO-PASSWORD";

const std::string_view GameMpqNames[] = {
	"diabdat.mpq", "spawn.mpq", "hellfire.mpq", "hfmonk.mpq", "hfmusic.mpq", "hfvoice.mpq", "hfbard.mpq", "hfbarb.mpq"
};

std::string Lower(std::string_view text)
{
	std::string out(text);
	for (char &c : out)
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	return out;
}

bool EndsWith(std::string_view text, std::string_view suffix)
{
	return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

std::string_view BaseName(std::string_view path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

std::string Join(std::string dir, std::string_view name)
{
	if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
		dir += DIRECTORY_SEPARATOR_STR;
	return StrCat(dir, name);
}

/** The name the game loads a file under. */
std::string CanonicalMpqName(std::string_view fileName)
{
	const std::string lower = Lower(fileName);
	return lower == "diabdat.mpq" ? std::string("DIABDAT.MPQ") : lower;
}

bool IsGogInstallerName(std::string_view fileName)
{
	const std::string lower = Lower(fileName);
	return EndsWith(lower, ".exe") && (lower.rfind("setup_diablo", 0) == 0 || lower.find("hellfire") != std::string::npos);
}

bool IsFolder(const std::string &path)
{
	return DirectoryExists(path.c_str());
}

std::uintmax_t SizeOf(const std::string &path)
{
	std::uintmax_t size = 0;
	GetFileSize(path.c_str(), &size);
	return size;
}

void RemoveTree(const std::string &dir, int depth = 0)
{
	if (depth > 12 || !IsFolder(dir))
		return;
	for (const std::string &file : ListFiles(dir.c_str()))
		RemoveFile(Join(dir, file).c_str());
	for (const std::string &sub : ListDirectories(dir.c_str())) {
		if (sub == "." || sub == "..")
			continue;
		RemoveTree(Join(dir, sub), depth + 1);
	}
#ifdef _WIN32
	if (auto wide = ToWideChar(dir); wide != nullptr)
		RemoveDirectoryW(wide.get());
#else
	rmdir(dir.c_str());
#endif
}

std::string MakeTempDir(const std::string &parent)
{
	std::random_device rd;
	const std::string dir = Join(parent, StrCat(".deth-import-", std::to_string(rd() % 1000000)));
	RecursivelyCreateDir(dir.c_str());
	return dir;
}

// ---- running the bundled tools ----

std::string ToolsDir()
{
	if (const char *env = std::getenv("DETH_TOOLS_DIR"); env != nullptr && *env != '\0')
		return env;
#ifdef _WIN32
	return Join(paths::BasePath(), "tools");
#else
	if (FileExists("/.flatpak-info"))
		return "/app/bin";
	return paths::BasePath();
#endif
}

std::string FindTool(std::initializer_list<std::string_view> names)
{
	for (std::string_view name : names) {
		const std::string path = Join(ToolsDir(), name);
		if (FileExists(path.c_str()))
			return path;
	}
#ifndef _WIN32
	// A system copy (outside the Flatpak) is fine too.
	for (std::string_view name : names) {
		for (std::string_view dir : { "/usr/bin/", "/usr/local/bin/", "/bin/" }) {
			const std::string path = StrCat(dir, name);
			if (FileExists(path.c_str()))
				return path;
		}
	}
#endif
	return {};
}

std::string SevenZip()
{
#ifdef _WIN32
	return FindTool({ "7z.exe" });
#else
	return FindTool({ "7zz", "7zzs", "7z", "7za" });
#endif
}

std::string InnoExtract()
{
#ifdef _WIN32
	return FindTool({ "innoextract.exe" });
#else
	return FindTool({ "innoextract" });
#endif
}

struct RunOutput {
	int exitCode = -1;
	std::string text;
};

std::function<void()> ImportTick;

void Tick()
{
	if (ImportTick)
		ImportTick();
}

#ifdef _WIN32
std::wstring Widen(std::string_view text)
{
	if (text.empty())
		return {};
	const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring out(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
	return out;
}

/** Quotes one argument the way CommandLineToArgvW reads it back. */
std::wstring QuoteArg(const std::wstring &arg)
{
	if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos)
		return arg;
	std::wstring out = L"\"";
	size_t backslashes = 0;
	for (wchar_t c : arg) {
		if (c == L'\\') {
			++backslashes;
			continue;
		}
		if (c == L'"') {
			out.append(backslashes * 2 + 1, L'\\');
			out += L'"';
		} else {
			out.append(backslashes, L'\\');
			out += c;
		}
		backslashes = 0;
	}
	out.append(backslashes * 2, L'\\');
	out += L'"';
	return out;
}

RunOutput Run(const std::vector<std::string> &args)
{
	RunOutput result;
	std::wstring commandLine;
	for (const std::string &arg : args) {
		if (!commandLine.empty())
			commandLine += L' ';
		commandLine += QuoteArg(Widen(arg));
	}
	wchar_t tempDir[MAX_PATH + 1] {};
	GetTempPathW(MAX_PATH, tempDir);
	const std::wstring outPath = std::wstring(tempDir) + L"deth-import-output.txt";
	SECURITY_ATTRIBUTES sa { sizeof(sa), nullptr, TRUE };
	HANDLE out = CreateFileW(outPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	HANDLE in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
	STARTUPINFOW si {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = out;
	si.hStdError = out;
	si.hStdInput = in;
	PROCESS_INFORMATION pi {};
	const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
	if (out != INVALID_HANDLE_VALUE)
		CloseHandle(out);
	if (in != INVALID_HANDLE_VALUE)
		CloseHandle(in);
	if (!started)
		return result;
	while (WaitForSingleObject(pi.hProcess, 50) == WAIT_TIMEOUT)
		Tick();
	DWORD code = 1;
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	result.exitCode = static_cast<int>(code);
	if (FILE *f = _wfopen(outPath.c_str(), L"rb"); f != nullptr) {
		char buf[4096];
		size_t n;
		while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0 && result.text.size() < 1 << 20)
			result.text.append(buf, n);
		std::fclose(f);
	}
	DeleteFileW(outPath.c_str());
	return result;
}
#else
RunOutput Run(const std::vector<std::string> &args)
{
	RunOutput result;
	int pipeFds[2];
	if (pipe(pipeFds) != 0)
		return result;
	const pid_t pid = fork();
	if (pid < 0) {
		close(pipeFds[0]);
		close(pipeFds[1]);
		return result;
	}
	if (pid == 0) {
		const int devNull = open("/dev/null", O_RDONLY);
		if (devNull >= 0)
			dup2(devNull, STDIN_FILENO);
		dup2(pipeFds[1], STDOUT_FILENO);
		dup2(pipeFds[1], STDERR_FILENO);
		close(pipeFds[0]);
		close(pipeFds[1]);
		std::vector<char *> argv;
		for (const std::string &arg : args)
			argv.push_back(const_cast<char *>(arg.c_str()));
		argv.push_back(nullptr);
		execv(argv[0], argv.data());
		_exit(127);
	}
	close(pipeFds[1]);
	char buf[4096];
	while (true) {
		pollfd pfd { pipeFds[0], POLLIN, 0 };
		const int ready = poll(&pfd, 1, 50);
		if (ready == 0) {
			Tick();
			continue;
		}
		if (ready < 0 && errno == EINTR)
			continue;
		const ssize_t n = read(pipeFds[0], buf, sizeof(buf));
		if (n <= 0)
			break;
		if (result.text.size() < 1 << 20)
			result.text.append(buf, static_cast<size_t>(n));
	}
	close(pipeFds[0]);
	int status = 0;
	waitpid(pid, &status, 0);
	result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	return result;
}
#endif

bool LooksLikeWrongPassword(const std::string &text)
{
	const std::string lower = Lower(text);
	return lower.find("wrong password") != std::string::npos || lower.find("encrypted archive") != std::string::npos
	    || lower.find("data error in encrypted file") != std::string::npos;
}

// ---- the import ----

class Importer {
public:
	Importer(const PasswordPrompt &askPassword, const ImportProgress &progress)
	    : askPassword_(askPassword)
	    , progress_(progress)
	    , destination_(ImportDestination())
	{
	}

	ImportResult Run(const std::string &path)
	{
		RecursivelyCreateDir(destination_.c_str());
		tempRoot_ = MakeTempDir(destination_);
		if (IsFolder(path))
			ImportFolder(path, 0, 0);
		else
			ImportFile(path, 0);
		RemoveTree(tempRoot_);
		return std::move(result_);
	}

private:
	void Note(std::string_view message)
	{
		if (progress_)
			progress_(message);
	}

	void ImportFolder(const std::string &dir, int nesting, int depth)
	{
		if (depth > 6)
			return;
		for (const std::string &file : ListFiles(dir.c_str()))
			ImportFile(Join(dir, file), nesting);
		for (const std::string &sub : ListDirectories(dir.c_str())) {
			if (sub == "." || sub == ".." || sub.rfind(".deth-import-", 0) == 0)
				continue;
			ImportFolder(Join(dir, sub), nesting, depth + 1);
		}
	}

	void ImportFile(const std::string &path, int nesting)
	{
		const std::string_view name = BaseName(path);
		if (IsGameMpqName(name)) {
			Place(path, name, /*move=*/nesting > 0);
			return;
		}
		if (nesting >= MaxNesting || !IsImportableArchiveName(name))
			return;
		const std::string lower = Lower(name);
		if (EndsWith(lower, ".exe")) {
			if (ExtractInstaller(path, nesting))
				return;
			// Not an Inno Setup installer: 7-Zip opens many other kinds.
		}
		ExtractArchive(path, nesting);
	}

	/** Puts one game file into the data folder. Files from temporary folders are moved, the player's own are copied. */
	void Place(const std::string &path, std::string_view fileName, bool move)
	{
		const std::string canonical = CanonicalMpqName(fileName);
		const std::string target = Join(destination_, canonical);
		if (Lower(path) == Lower(target))
			return; // already in place
		const std::uintmax_t size = SizeOf(path);
		if (FileExists(target.c_str()) && SizeOf(target) == size) {
			result_.alreadyThere.push_back(canonical);
			if (move)
				RemoveFile(path.c_str());
			return;
		}
		Note(StrCat("Copying ", canonical));
		RemoveFile(target.c_str());
		if (move)
			RenameFile(path.c_str(), target.c_str());
		if (!FileExists(target.c_str()))
			CopyFileOverwrite(path.c_str(), target.c_str());
		if (FileExists(target.c_str()) && SizeOf(target) == size) {
			result_.imported.push_back(canonical);
			LogInfo("Imported {} from {}", canonical, path);
		} else {
			result_.problems.push_back(StrCat(canonical, ": could not be copied (is the disk full?)"));
		}
	}

	bool ExtractInstaller(const std::string &path, int nesting)
	{
		const std::string tool = InnoExtract();
		if (tool.empty())
			return false;
		Note(StrCat("Unpacking ", BaseName(path)));
		const std::string out = MakeTempDir(tempRoot_);
		const RunOutput run = devilution::Run({ tool, "--silent", "--output-dir", out, path });
		if (run.exitCode != 0) {
			LogVerbose("innoextract failed on {}: {}", path, run.text);
			RemoveTree(out);
			return false;
		}
		ImportFolder(out, nesting + 1, 0);
		RemoveTree(out);
		return true;
	}

	void ExtractArchive(const std::string &path, int nesting)
	{
		const std::string tool = SevenZip();
		const std::string name(BaseName(path));
		if (tool.empty()) {
			result_.problems.push_back(StrCat(name, ": the 7-Zip tool is missing from this copy of DETH"));
			return;
		}
		std::string password(NoPassword);
		bool retry = false;
		for (int attempt = 0; attempt < 10; ++attempt) {
			Note(StrCat("Opening ", name));
			const std::string out = MakeTempDir(tempRoot_);
			std::vector<std::string> args = { tool, "e", "-y", "-bd", "-ssc-", StrCat("-o", out), StrCat("-p", password), path };
			for (std::string_view pattern : { "*.mpq", "*.zip", "*.7z", "*.rar", "*.tar", "*.gz", "*.tgz", "*.xz", "*.txz", "*.bz2", "*.tbz2", "*.exe", "*.bin" })
				args.push_back(StrCat("-ir!", pattern));
			const RunOutput run = devilution::Run(args);
			if (LooksLikeWrongPassword(run.text)) {
				RemoveTree(out);
				const std::optional<std::string> entered = askPassword_ ? askPassword_(name, retry) : std::nullopt;
				if (!entered) {
					result_.problems.push_back(StrCat(name, ": skipped (password needed)"));
					return;
				}
				password = *entered;
				retry = true;
				continue;
			}
			if (run.exitCode > 1) {
				LogVerbose("7-Zip failed on {} ({}): {}", path, run.exitCode, run.text);
				RemoveTree(out);
				if (nesting == 0)
					result_.problems.push_back(StrCat(name, ": could not be opened"));
				return;
			}
			ImportFolder(out, nesting + 1, 0);
			RemoveTree(out);
			return;
		}
	}

	const PasswordPrompt &askPassword_;
	const ImportProgress &progress_;
	std::string destination_;
	std::string tempRoot_;
	ImportResult result_;
};

void AddCandidates(std::vector<ImportCandidate> &out, const std::string &dir, std::string_view where, bool skipGameMpqs)
{
	if (dir.empty() || !IsFolder(dir))
		return;
	for (const std::string &file : ListFiles(dir.c_str())) {
		const bool mpq = IsGameMpqName(file);
		if (mpq && skipGameMpqs)
			continue;
		if (!mpq && !IsImportableArchiveName(file))
			continue;
		const std::string lower = Lower(file);
		// Only installers that look like GOG's Diablo ones; any other .exe in Downloads is noise.
		if (EndsWith(lower, ".exe") && !IsGogInstallerName(file))
			continue;
		ImportCandidate candidate;
		candidate.path = Join(dir, file);
		candidate.name = file;
		candidate.where = std::string(where);
		candidate.size = SizeOf(candidate.path);
		out.push_back(std::move(candidate));
	}
}

} // namespace

bool IsGameMpqName(std::string_view fileName)
{
	const std::string lower = Lower(fileName);
	return std::find(std::begin(GameMpqNames), std::end(GameMpqNames), lower) != std::end(GameMpqNames);
}

bool IsImportableArchiveName(std::string_view fileName)
{
	const std::string lower = Lower(fileName);
	for (std::string_view ext : { ".zip", ".7z", ".rar", ".tar", ".gz", ".tgz", ".xz", ".txz", ".bz2", ".tbz2", ".exe" })
		if (EndsWith(lower, ext))
			return true;
	return false;
}

std::string ImportDestination()
{
	if (const char *env = std::getenv("DETH_IMPORT_DEST"); env != nullptr && *env != '\0')
		return Join(env, "");
#ifdef _WIN32
	return paths::BasePath();
#else
	if (!paths::DropPath().empty())
		return paths::DropPath();
	return paths::PrefPath();
#endif
}

bool GameImportSupported()
{
#if defined(__ANDROID__)
	return false;
#else
	return !SevenZip().empty();
#endif
}

std::vector<ImportCandidate> FindImportCandidates()
{
	std::vector<ImportCandidate> out;
	const std::string downloads = paths::DownloadsDir();
	const std::string destination = ImportDestination();
	AddCandidates(out, downloads, "Downloads", Lower(downloads) == Lower(destination));
#ifdef _WIN32
	// Next to DETH.exe: archives dropped into the game's own folder.
	if (Lower(paths::BasePath()) != Lower(downloads))
		AddCandidates(out, paths::BasePath(), "DETH folder", Lower(paths::BasePath()) == Lower(destination));
#endif
	if (!paths::DropPath().empty() && Lower(paths::DropPath()) != Lower(downloads))
		AddCandidates(out, paths::DropPath(), "Downloads/DETH", Lower(paths::DropPath()) == Lower(destination));
	// Biggest first: the full game archive is usually the one wanted.
	std::stable_sort(out.begin(), out.end(), [](const ImportCandidate &a, const ImportCandidate &b) { return a.size > b.size; });
	return out;
}

ImportResult ImportGameFiles(const std::string &path, const PasswordPrompt &askPassword, const ImportProgress &progress)
{
	Importer importer(askPassword, progress);
	return importer.Run(path);
}

void SetImportTick(std::function<void()> tick)
{
	ImportTick = std::move(tick);
}

} // namespace devilution
