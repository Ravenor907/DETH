#include "DiabloUI/selimport.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#else
#include <unistd.h>
#endif

#include "DiabloUI/deth_screen.hpp"
#include "DiabloUI/deth_theme.hpp"
#include "DiabloUI/diabloui.h"
#include "DiabloUI/scrollbar.h"
#include "DiabloUI/selok.h"
#include "DiabloUI/selyesno.h"
#include "DiabloUI/ui_flags.hpp"
#include "DiabloUI/ui_item.h"
#include "engine/render/text_render.hpp"
#include "diablo.h"
#include "game_import.hpp"
#include "options.h"
#include "utils/language.h"
#include "utils/log.hpp"
#include "utils/str_cat.hpp"
#include "utils/utf8.hpp"

namespace devilution {

namespace {

constexpr int BrowseFileValue = 100000;
constexpr int BrowseFolderValue = 100001;
constexpr int LookAgainValue = 100002;

std::vector<std::unique_ptr<UiListItem>> vecItems;
std::vector<std::unique_ptr<UiItemBase>> vecDialog;
bool endMenu;
std::optional<int> pickedValue;

void FreeScreen()
{
	vecDialog.clear();
	vecItems.clear();
}

void ItemSelected(size_t index)
{
	pickedValue = vecItems[index]->m_value;
	endMenu = true;
}

void EscPressed()
{
	pickedValue = std::nullopt;
	endMenu = true;
}

void RunScreen(void (*select)(size_t), void (*esc)(), size_t selected = 0)
{
	UiInitList(nullptr, select, esc, vecDialog, true, nullptr, nullptr, selected);
	endMenu = false;
	while (!endMenu) {
		UiClearScreen();
		UiRenderItems(vecDialog);
		UiPollAndRender();
	}
	UiInitList_clear();
}

std::string SizeText(std::uintmax_t size)
{
	if (size >= 1024 * 1024)
		return StrCat(std::to_string(size / (1024 * 1024)), " MB");
	return StrCat(std::to_string((size + 1023) / 1024), " KB");
}

/** One frame with a message in a panel, shown while the import works. */
void ShowBusy(std::string_view message)
{
	std::vector<std::unique_ptr<UiItemBase>> busy;
	const theme::Layout layout = theme::GetLayout();
	const int width = std::min(layout.column.size.width, gnScreenWidth - 2 * layout.margin);
	const int height = 120;
	const int x = (gnScreenWidth - width) / 2;
	const int y = (gnScreenHeight - height) / 2;
	busy.push_back(std::make_unique<UiPanel>(MakeSdlRect(x, y, width, height)));
	static std::string text;
	text = StrCat(message, "...");
	busy.push_back(std::make_unique<UiArtText>(text.c_str(), MakeSdlRect(x + 16, y + 30, width - 32, 30), UiFlags::FontSize24 | UiFlags::ColorUiGold | UiFlags::AlignCenter));
	busy.push_back(std::make_unique<UiArtText>(_("This can take a minute for the full game.").data(), MakeSdlRect(x + 16, y + 70, width - 32, 24), UiFlags::FontSize12 | UiFlags::ColorUiSilverDark | UiFlags::AlignCenter));
	UiClearScreen();
	UiRenderItems(busy);
	UiPollAndRender();
}

// ---- password ----

char passwordBuffer[128];
bool passwordOk;

void PasswordSelect(size_t /*value*/)
{
	passwordOk = true;
	endMenu = true;
}

void PasswordEsc()
{
	passwordOk = false;
	endMenu = true;
}

std::optional<std::string> AskPassword(std::string_view archiveName, bool retry)
{
	FreeScreen();
	std::memset(passwordBuffer, 0, sizeof(passwordBuffer));
	const theme::ScreenFrame frame = theme::MakeScreenFrame(/*wide=*/false, /*sidePane=*/false, /*buttons=*/true);
	theme::AddTitle(vecDialog, _("Password").data(), frame);
	static std::string heading;
	heading = retry ? std::string(_("Wrong password, try again")) : StrCat(_("Password for "), archiveName);
	int y = frame.content.position.y;
	y += theme::AddHeading(vecDialog, heading.c_str(), frame, y);
	y += 8;
	{
		const SDL_Rect editRect = MakeSdlRect(frame.content.position.x, y, frame.content.size.width, frame.layout.rowHeight);
		auto edit = std::make_unique<UiEdit>(_("Password"), passwordBuffer, sizeof(passwordBuffer) - 1, true, editRect, UiFlags::FontSize24 | UiFlags::ColorUiGold);
		edit->themed = true;
		edit->inputKind = UiEdit::InputKind::Password;
		vecDialog.push_back(std::move(edit));
	}
	theme::AddButtons(vecDialog, frame, { { _("OK"), &UiFocusNavigationSelect }, { _("Skip"), &UiFocusNavigationEsc } });
	passwordOk = false;
	RunScreen(PasswordSelect, PasswordEsc);
	FreeScreen();
	if (!passwordOk)
		return std::nullopt;
	return std::string(passwordBuffer);
}

// ---- picking a file or folder (Windows) ----

#ifdef _WIN32
std::string Narrow(const wchar_t *text)
{
	const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if (n <= 1)
		return {};
	std::string out(static_cast<size_t>(n - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), n, nullptr, nullptr);
	return out;
}

std::string BrowseForFile()
{
	wchar_t path[4096] {};
	OPENFILENAMEW ofn {};
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFilter = L"Diablo files and archives\0*.mpq;*.zip;*.7z;*.rar;*.tar;*.gz;*.tgz;*.xz;*.txz;*.bz2;*.exe\0All files\0*.*\0";
	ofn.lpstrFile = path;
	ofn.nMaxFile = sizeof(path) / sizeof(path[0]);
	ofn.lpstrTitle = L"Import Game Files";
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameW(&ofn))
		return {};
	return Narrow(path);
}

std::string BrowseForFolder()
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	BROWSEINFOW info {};
	info.lpszTitle = L"Choose the folder with your Diablo files";
	info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	PIDLIST_ABSOLUTE list = SHBrowseForFolderW(&info);
	if (list == nullptr)
		return {};
	wchar_t path[MAX_PATH] {};
	const bool ok = SHGetPathFromIDListW(list, path) != FALSE;
	CoTaskMemFree(list);
	return ok ? Narrow(path) : std::string {};
}
#endif

// ---- restart ----

/** Starts DETH again so the new game files are loaded. */
void RestartGame()
{
	SaveOptions();
#ifdef _WIN32
	wchar_t exe[MAX_PATH] {};
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::wstring commandLine = GetCommandLineW();
	STARTUPINFOW si {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi {};
	if (CreateProcessW(exe, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}
	diablo_quit(0);
#else
	std::vector<std::string> args;
	if (FILE *f = std::fopen("/proc/self/cmdline", "rb"); f != nullptr) {
		std::string current;
		int c;
		while ((c = std::fgetc(f)) != EOF) {
			if (c == '\0') {
				args.push_back(current);
				current.clear();
			} else {
				current += static_cast<char>(c);
			}
		}
		std::fclose(f);
	}
	if (args.empty())
		return;
	std::vector<char *> argv;
	for (std::string &arg : args)
		argv.push_back(arg.data());
	argv.push_back(nullptr);
	execv("/proc/self/exe", argv.data());
	LogError("Restart failed; please start DETH again");
#endif
}

std::string JoinNames(const std::vector<std::string> &names)
{
	std::string out;
	for (const std::string &name : names) {
		if (!out.empty())
			out += ", ";
		out += name;
	}
	return out;
}

void ImportFrom(const std::string &path)
{
	FreeScreen();
	const ImportResult result = ImportGameFiles(path, AskPassword, ShowBusy);
	std::string message;
	if (!result.imported.empty())
		message = StrCat(_("Imported: "), JoinNames(result.imported));
	if (!result.alreadyThere.empty())
		message = StrCat(message, message.empty() ? "" : "\n", _("Already there: "), JoinNames(result.alreadyThere));
	for (const std::string &problem : result.problems)
		message = StrCat(message, message.empty() ? "" : "\n", problem);
	if (message.empty())
		message = std::string(_("No Diablo or Hellfire game files were found there."));

	if (!result.imported.empty()) {
		const std::string question = StrCat(message, "\n\n", _("Restart DETH now to load them?"));
		if (UiSelHeroYesNoDialog(_("Game files imported").data(), question.c_str()))
			RestartGame();
		return;
	}
	UiSelOkDialog(_("Import Game Files").data(), message.c_str(), false);
}

/** The list of things to import. Returns the picked value, or nullopt for Back. */
std::optional<int> ChooseSource(const std::vector<ImportCandidate> &candidates)
{
	FreeScreen();
	const theme::ScreenFrame frame = theme::MakeScreenFrame(/*wide=*/true, /*sidePane=*/false, /*buttons=*/!theme::TouchMode());
	theme::AddTitle(vecDialog, _("Import Game Files").data(), frame);
	int y = frame.content.position.y;
	const char *heading = candidates.empty()
	    ? _("Put DIABDAT.MPQ, the Hellfire files or an archive (zip, 7z, rar, tar.gz) in your Downloads folder").data()
	    : _("Found in your Downloads").data();
	y += theme::AddHeading(vecDialog, heading, frame, y);

	for (size_t i = 0; i < candidates.size(); ++i) {
		auto item = std::make_unique<UiListItem>(std::string(candidates[i].name), static_cast<int>(i));
		item->subText = StrCat(candidates[i].where, "  ", SizeText(candidates[i].size));
		vecItems.push_back(std::move(item));
	}
#ifdef _WIN32
	vecItems.push_back(std::make_unique<UiListItem>(_("Browse for a file..."), BrowseFileValue));
	vecItems.push_back(std::make_unique<UiListItem>(_("Browse for a folder..."), BrowseFolderValue));
#endif
	{
		auto item = std::make_unique<UiListItem>(_("Look again"), LookAgainValue);
		item->subText = std::string(_("After copying files into Downloads"));
		vecItems.push_back(std::move(item));
	}
	auto list = theme::MakeList(vecItems, frame, y, vecItems.size(), UiFlags::FontSize24 | UiFlags::AlignCenter, theme::SubTextHeight());
	vecDialog.push_back(std::move(list));
	if (!theme::TouchMode())
		theme::AddButtons(vecDialog, frame, { { _("OK"), &UiFocusNavigationSelect }, { _("Back"), &UiFocusNavigationEsc } });
	pickedValue = std::nullopt;
	RunScreen(ItemSelected, EscPressed);
	FreeScreen();
	return pickedValue;
}

} // namespace

void UiImportGameFiles()
{
	UiLoadBlackBackground();
	const bool themeWas = UiThemeBackground;
	UiThemeBackground = true;
	LoadScrollBar();
	while (true) {
		const std::vector<ImportCandidate> candidates = FindImportCandidates();
		const std::optional<int> picked = ChooseSource(candidates);
		if (!picked)
			break;
		if (*picked == LookAgainValue)
			continue;
		std::string path;
#ifdef _WIN32
		if (*picked == BrowseFileValue)
			path = BrowseForFile();
		else if (*picked == BrowseFolderValue)
			path = BrowseForFolder();
		else
#endif
		    if (*picked >= 0 && static_cast<size_t>(*picked) < candidates.size())
			path = candidates[static_cast<size_t>(*picked)].path;
		if (path.empty())
			continue;
		ImportFrom(path);
		UiLoadBlackBackground();
		UiThemeBackground = true;
	}
	UnloadScrollBar();
	UiThemeBackground = themeWas;
	ArtBackground = std::nullopt;
	ArtBackgroundWidescreen = std::nullopt;
}

} // namespace devilution
