// Updates of HeartGram Desktop from the releases of the public repository
// whyheartone/HeartGramDesktop (tags "v<build>", a suffix like "-beta" makes
// a pre-release, the asset HeartGram.exe).
// A running exe can be renamed on Windows: the new file is downloaded next
// to it, checked against SHA-256, swapped in and the application restarts.
#pragma once

namespace Extera::Update {

enum class CheckResult {
	Available,
	Latest,
	Failed,
};

// A check shortly after the start and then every few hours.
void Start();

// Asks GitHub for the releases. Pre-releases count only when the
// "update_prerelease" switch is on.
void CheckNow(Fn<void(CheckResult)> done);

[[nodiscard]] rpl::producer<bool> AvailableValue();
[[nodiscard]] QString AvailableVersion();
[[nodiscard]] QString AvailableNotes();
[[nodiscard]] rpl::producer<QString> StatusValue();

// Downloads the available release, replaces the exe and restarts.
void Install();

// Opens the release page in the browser.
void OpenRelease();

} // namespace Extera::Update
