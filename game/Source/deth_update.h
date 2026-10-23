#pragma once

#include <optional>
#include <string>

namespace devilution {

/** DETH: a release newer than the running version, for this platform. */
struct UpdateInfo {
	/** Version without the leading "v", e.g. "1.1.0-beta7". */
	std::string version;
	/** Whether GitHub marks it as a pre-release (beta). */
	bool prerelease = false;
	/** The bullet points of the release notes, one per line. */
	std::string notes;
	/** Download URL and file name of this platform's file. */
	std::string assetUrl;
	std::string assetName;
};

/** Whether this build can update itself (Windows, Android, Flatpak). */
[[nodiscard]] bool UpdaterSupported();

/**
 * Asks GitHub for the newest release on the chosen channel (Stable or Beta).
 * @return true if the check worked; `update` is set only when a newer version exists.
 */
bool CheckForUpdate(std::optional<UpdateInfo> &update, std::string &error);

/** Starts CheckForUpdate in the background (startup check). Safe to call once. */
void StartBackgroundUpdateCheck();
/** The result of the background check, once it has finished and found something. */
[[nodiscard]] std::optional<UpdateInfo> BackgroundUpdateResult();

/**
 * Downloads and installs the update. On success the game restarts into the new
 * version (or, on Android, the system installer is shown) and this does not return
 * normally on Windows/Flatpak. Returns false with a message on failure.
 */
bool DownloadAndInstallUpdate(const UpdateInfo &update, std::string &error);

/** Compares DETH versions ("1.1.0-beta6" < "1.1.0-beta7" < "1.1.0"). Exposed for tests. */
[[nodiscard]] /** @brief DETH: true for a proper release version: "M.m.p" or "M.m.b.N" (no "v", no suffix). */
bool IsDethReleaseVersion(std::string_view text);

int CompareDethVersions(std::string_view a, std::string_view b);

} // namespace devilution
