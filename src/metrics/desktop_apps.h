#pragma once

#include "metrics.h" // DesktopApp

#include <string>
#include <vector>

// Parses the [Desktop Entry] group of one .desktop file. For the Installed
// Apps list (isAutostartFile == false) entries marked NoDisplay are skipped
// (launcher-only helper entries, not real applications).
bool ParseDesktopFile(const std::string& path, DesktopApp* out, bool isAutostartFile);

// Scans dir for *.desktop files and appends each successfully-parsed one to out.
// Backs the Startup apps and Installed Apps pages.
void ScanDesktopDir(const std::string& dir, bool isAutostart, std::vector<DesktopApp>& out);

// The invoking user's real home directory. vitals is commonly run as
// `sudo ./vitals` for SMU/DRAM telemetry, which resets $HOME to root's — so
// this resolves via SUDO_USER first (falling back to $HOME) to make sure
// startup-app scans/edits land in the actual login session's config, not root's.
std::string RealHomeDir();

// Shared file-write plumbing for anything vitals writes under the real
// user's ~/.config, whether running unprivileged or via sudo:
//   - ChownToRealUser: hands a root-created file/dir back to the real
//     uid/gid (via $SUDO_UID/$SUDO_GID) so the user can edit/delete it later.
//   - EnsureConfigDir: mkdir (+ chown-back) `dir` and its ~/.config parent.
//   - WriteFile: truncate-write `content` to `path`, then chown-back.
// Originally private to this file's autostart-app read/write logic; now
// also used by settings_persistence.cpp and IsVitalsAutostartEnabled/
// SetVitalsAutostartEnabled below — kept together here rather than
// duplicated, since the root-vs-real-user handling is security-sensitive.
void ChownToRealUser(const std::string& path);
void EnsureConfigDir(const std::string& dir);
bool WriteFile(const std::string& path, const std::string& content);

// Whether vitals itself is registered to launch at login
// (~/.config/autostart/vitals.desktop exists).
bool IsVitalsAutostartEnabled();

// Writes/removes that .desktop entry, pointing Exec= at the currently
// running vitals binary (via /proc/self/exe). Returns false on any I/O
// failure (e.g. $HOME unresolvable).
bool SetVitalsAutostartEnabled(bool enabled);

// Enables/disables a startup entry. If app.path already lives under
// ~/.config/autostart it's edited in place; otherwise (a system entry under
// /etc/xdg/autostart) a per-user override copy is written there instead,
// same as GNOME Tweaks' "mask" behavior. Updates app.enabled/app.path on success.
bool SetStartupAppEnabled(DesktopApp& app, bool enabled);

// Copies an Installed Apps entry's .desktop file into ~/.config/autostart so
// it launches at login. False if it's already there.
bool AddStartupApp(const DesktopApp& app);

// Deletes a startup entry's file. Only works for entries under
// ~/.config/autostart (user-added, or a disable override) — system entries
// under /etc/xdg/autostart can only be disabled, not removed.
bool RemoveStartupApp(const DesktopApp& app);
