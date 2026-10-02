#pragma once
#include <string>

// Minimal on/off log-to-file for the Settings page's "Enable logging"
// toggle — no severity levels/categories, nothing in the app needs more
// than that. File is opened/closed once, gated on Theme::LoggingEnabled's
// value at Init() time: flipping the checkbox mid-session stops/starts
// writes, but a session that started with logging off won't retroactively
// create the file if it's flipped on mid-run.
namespace Logging {

// Opens ~/.config/vitals/vitals.log iff Theme::LoggingEnabled. No-op
// (silently) if $HOME can't be resolved or the flag is off.
void Init();
void Shutdown();

// printf-style; no-op if logging isn't enabled/initialized.
void Log(const char* fmt, ...);

// The log file's path, for the Settings page to show/offer to open —
// resolved (even if logging is currently disabled) as soon as Init() has
// run, so the field isn't blank while the checkbox is off.
std::string LogPath();

} // namespace Logging
