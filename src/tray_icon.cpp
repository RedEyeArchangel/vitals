#include "tray_icon.h"
#include "app_icon.h"
#include "tray.h" // vendored via FetchContent; single header, all `static` — see CMakeLists.txt
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

// No #ifdef here: TRAY_APPINDICATOR is defined by CMake only when GTK3 +
// an AppIndicator implementation were found via pkg-config. When it isn't,
// tray.h's own unconditional fallback branch compiles tray_init()/
// tray_loop() as no-ops that just return -1 — Init() below already treats
// that as "unavailable", so this file behaves correctly either way without
// its own conditional compilation.

namespace {

GLFWwindow* gWindow = nullptr;
bool gAvailable = false;
bool gQuitRequested = false;

void OnShow(struct tray_menu*) {
    if (gWindow) {
        glfwShowWindow(gWindow);
        glfwFocusWindow(gWindow);
    }
}

void OnQuit(struct tray_menu*) { gQuitRequested = true; }

// Installs the app's own mark into the user's icon theme (~/.local/share/
// icons/hicolor/scalable/apps/), so the tray can reference it by name like
// any theme icon — AppIndicator/StatusNotifier want a theme name, not a raw
// path. Falls back to the generic "utilities-system-monitor" icon (present
// on essentially any desktop) if $HOME is unset or the write fails.
const char* InstallTrayIconName() {
    const char* home = getenv("HOME");
    if (!home) return "utilities-system-monitor";
    std::filesystem::path dir = std::filesystem::path(home) / ".local/share/icons/hicolor/scalable/apps";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return "utilities-system-monitor";
    std::ofstream f(dir / "vitals.svg");
    if (!f) return "utilities-system-monitor";
    f << AppIcon::RenderSVG();
    return f ? "vitals" : "utilities-system-monitor";
}

tray gTray{(char*)InstallTrayIconName(), nullptr};
tray_menu gMenu[] = {
    {(char*)"Show", 0, 0, OnShow, nullptr, nullptr},
    {(char*)"Quit", 0, 0, OnQuit, nullptr, nullptr},
    {nullptr, 0, 0, nullptr, nullptr, nullptr},
};

} // namespace

namespace TrayIcon {

bool Init(GLFWwindow* window) {
    gWindow = window;
    gTray.menu = gMenu;
    gAvailable = tray_init(&gTray) == 0;
    return gAvailable;
}

void Poll() {
    if (!gAvailable) return;
    tray_loop(0); // non-blocking; pumps GTK's own event loop for the icon/menu
}

void Shutdown() { gAvailable = false; }

bool QuitRequested() { return gQuitRequested; }
bool Available() { return gAvailable; }

} // namespace TrayIcon
