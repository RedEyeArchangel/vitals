#pragma once

struct GLFWwindow;

// Thin wrapper around zserge/tray (github.com/zserge/tray) for the system
// tray icon + minimize-to-tray feature. Public surface has NO #ifdefs so
// callers (app_shell.cpp, settings.cpp) never conditionally compile —
// internally, everything becomes a no-op stub when VITALS_HAVE_TRAY isn't
// defined (GTK3/appindicator dev packages weren't found at configure time;
// see CMakeLists.txt), so the rest of the app is unaffected either way.
namespace TrayIcon {

// Creates the tray icon. Safe to call unconditionally; returns false (and
// leaves the feature inert) if unavailable. `window` is used by the
// click/"Show" menu callbacks to restore a hidden window.
bool Init(GLFWwindow* window);

// Pumps the tray's own event loop (non-blocking). Call once per frame
// alongside glfwPollEvents(); no-op if Init() wasn't called or failed.
void Poll();

void Shutdown();

// True once the tray menu's "Quit" item has been clicked — checked once
// per frame by the caller to actually end the run loop.
bool QuitRequested();

// Compile-time (bundled+buildable) AND runtime (a tray host is actually
// present) capability check — gates whether app_shell.cpp intercepts the
// window close button, and whether settings.cpp shows the feature as live
// vs. "not available".
bool Available();

} // namespace TrayIcon
