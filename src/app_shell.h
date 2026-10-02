#pragma once

#include "metrics.h"
#include "pages.h"

struct GLFWwindow;

// Owns the GLFW/OpenGL window, ImGui/ImPlot context, and the per-frame
// render loop — everything main.cpp used to do directly, so main.cpp is
// just Init() + Run(). See app_shell.cpp for the frame loop itself.
class AppShell {
public:
    ~AppShell();

    // GLFW/GL window+context, ImGui/ImPlot context, font loading. false on
    // any fatal setup failure — don't call Run() in that case.
    bool Init();

    // The frame loop: polls input, refreshes Metrics on the configured
    // interval, draws the current page, presents. Returns once the window
    // is closed (0), or immediately (1) if Init() was never called/failed.
    int Run();

private:
    void Shutdown();

    // UI chrome — the header bar and page-nav sidebar framing every page.
    // Kept as members (not free functions) since both read/write currentPage_.
    void DrawHeader(float width);
    void DrawSidebar(float height);

    // The window is created undecorated (see Init()) so this can draw a
    // themed title bar instead of the OS's — includes its own drag-to-move
    // and minimize/maximize/close buttons.
    void DrawTitleBar(float width);

    // Going undecorated also loses the OS's edge-resize handles. ponytail:
    // only a bottom-right corner grip, not full per-edge resize — upgrade
    // path is adding hit regions on the other 3 edges/corners if the corner
    // alone turns out to be too fiddly in practice.
    void DrawResizeGrip(ImVec2 viewportOrigin, float width, float height);

    GLFWwindow* window_ = nullptr;
    Metrics metrics_;
    Page currentPage_ = Page::Summary;
    PerfMetric perfSelected_ = PerfMetric::CPU;

    // Drag-to-move state for the custom title bar. X11-only: GLFW/Wayland
    // gives clients no way to reposition their own window (confirmed via a
    // GLFW error earlier — restoring a saved window position already hits
    // this), so a drag on native Wayland is a deliberate no-op rather than
    // spamming that error every frame. ponytail: no native Wayland
    // interactive-move; upgrade path is the xdg-toplevel move protocol via
    // GLFW's native-access headers if this becomes a real complaint.
    bool titleBarDragging_ = false;
    double dragStartCursorX_ = 0.0, dragStartCursorY_ = 0.0;
    int dragStartWinX_ = 0, dragStartWinY_ = 0;

    // Applied to the window only when Theme::AlwaysOnTop actually changes
    // (not every frame) — GLFW_FLOATING isn't supported under Wayland and
    // logs a GLFW error on every call, unconditional/every-frame would
    // spam that error at frame rate for anyone not using the toggle.
    bool appliedAlwaysOnTop_ = false;
};
