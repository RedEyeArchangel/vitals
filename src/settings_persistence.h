#pragma once

// Persists Theme:: settings + window geometry to a plain key=value file
// under ~/.config/vitals/settings.conf, so Settings-page toggles survive a
// restart. Unknown/missing keys are ignored on load — an old file loads
// fine under a newer build with more settings, and vice versa.
namespace SettingsPersistence {

struct WindowGeometry {
    int x = -1, y = -1;   // -1,-1 = "let the OS place it" (first launch)
    int w = 1600, h = 1000;
};

// Reads the config file (if any) into Theme::* globals; returns the saved
// window geometry (defaults if none saved yet). Also ensures
// ~/.config/vitals exists so callers relying on that directory (e.g.
// Logging::Init()) can assume it's there afterward. Safe to call even if
// $HOME can't be resolved — everything just stays at its compiled-in default.
WindowGeometry LoadSettings();

// Writes every persisted Theme:: field plus the given window geometry back
// to the config file. Best-effort; failures are silently ignored — nothing
// the user can act on at shutdown.
void SaveSettings(const WindowGeometry& geo);

} // namespace SettingsPersistence
