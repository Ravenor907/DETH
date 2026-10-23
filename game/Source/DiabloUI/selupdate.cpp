#include "DiabloUI/selupdate.h"

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "DiabloUI/diabloui.h"
#include "DiabloUI/selok.h"
#include "DiabloUI/selyesno.h"
#include "DiabloUI/ui_flags.hpp"
#include "DiabloUI/ui_item.h"
#include "deth_update.h"
#include "deth_version.hpp"
#include "options.h"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/sdl_geometry.h"

namespace devilution {

namespace {

/** Shows a one-line message while the next blocking step runs. */
void ShowBusy(const char *text)
{
	std::vector<std::unique_ptr<UiItemBase>> items;
	UiLoadBlackBackground();
	UiThemeBackground = true;
	items.push_back(std::make_unique<UiArtText>(text, MakeSdlRect(0, gnScreenHeight / 2 - 20, gnScreenWidth, 40), UiFlags::FontSize30 | UiFlags::ColorUiGold | UiFlags::AlignCenter | UiFlags::VerticalCenter));
	UiInitList(nullptr, nullptr, nullptr, items);
	for (int i = 0; i < 2; ++i) { // the first frame fades in
		UiClearScreen();
		UiRenderItems(items);
		UiPollAndRender();
	}
	UiInitList_clear();
	UiThemeBackground = false;
}

} // namespace

void UiCheckForUpdates()
{
	const char *title = _("Updates").data();
	if (!UpdaterSupported()) {
		UiSelOkDialog(title, _("This build cannot update itself. Download new versions from github.com/Ravenor907/DETH.").data(), false);
		return;
	}
	const bool beta = *GetOptions().StartUp.updateChannel == UpdateChannel::Beta;
	const std::string channel = std::string(beta ? _("Beta") : _("Stable"));

	ShowBusy(_("Checking for updates...").data());
	std::optional<UpdateInfo> update;
	std::string error;
	if (!CheckForUpdate(update, error)) {
		UiSelOkDialog(title, error.c_str(), false);
		return;
	}
	if (!update) {
		const std::string text = FormatRuntime(_("You have the newest {:s} version ({:s}).\n\nChange the channel under Settings > Start Up > Update Channel."), channel, DETH_VERSION);
		UiSelOkDialog(title, text.c_str(), false);
		return;
	}

	const std::string question = FormatRuntime(_("Version {:s} ({:s}) is available. You have {:s}.\n\n{:s}\nDownload and install it now?"),
	    update->version, update->prerelease ? std::string(_("beta")) : std::string(_("stable")), DETH_VERSION, update->notes);
	if (!UiSelHeroYesNoDialog(title, question.c_str()))
		return;

	ShowBusy(_("Downloading the update...").data());
	if (!DownloadAndInstallUpdate(*update, error)) {
		UiSelOkDialog(title, error.c_str(), false);
		return;
	}
#ifdef __ANDROID__
	// The system now asks the player to confirm the install.
	UiSelOkDialog(title, _("Android will now ask you to install the update. DETH restarts into the new version afterwards.").data(), false);
#else
	// Flatpak: installed; DETH closes and is started again by the player (from Steam or the menu).
	UiSelOkDialog(title, FormatRuntime(_("DETH {:s} is installed. DETH will close now; start it again from Steam or the app menu."), update->version).c_str(), false);
	std::exit(0);
#endif
}

} // namespace devilution
