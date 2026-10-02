#include "app_shell.h"

#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include "theme.h"
#include "widgets.h"
#include "settings_persistence.h"
#include "logging.h"
#include "tray_icon.h"
#include "app_icon.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

static void GlfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
    Logging::Log("GLFW error %d: %s", error, description); // no-op before Logging::Init() has run
}

// Custom title bar's window-control glyphs (minimize/maximize/restore/
// close) — no icon font bundled, same hand-drawn approach as the gear.
static void DrawMinimizeIcon(ImDrawList* dl, ImVec2 center, float half, ImU32 color) {
    dl->AddLine(ImVec2(center.x - half, center.y), ImVec2(center.x + half, center.y), color, 1.5f);
}
static void DrawMaximizeIcon(ImDrawList* dl, ImVec2 center, float half, ImU32 color) {
    dl->AddRect(ImVec2(center.x - half, center.y - half), ImVec2(center.x + half, center.y + half), color, 0.0f, 0, 1.5f);
}
static void DrawRestoreIcon(ImDrawList* dl, ImVec2 center, float half, ImU32 color) {
    float o = half * 0.35f;
    dl->AddRect(ImVec2(center.x - half + o, center.y - half - o), ImVec2(center.x + half + o, center.y + half - o), color, 0.0f, 0, 1.5f);
    dl->AddRect(ImVec2(center.x - half - o, center.y - half + o), ImVec2(center.x + half - o, center.y + half + o), color, 0.0f, 0, 1.5f);
}
static void DrawCloseIcon(ImDrawList* dl, ImVec2 center, float half, ImU32 color) {
    dl->AddLine(ImVec2(center.x - half, center.y - half), ImVec2(center.x + half, center.y + half), color, 1.5f);
    dl->AddLine(ImVec2(center.x - half, center.y + half), ImVec2(center.x + half, center.y - half), color, 1.5f);
}

AppShell::~AppShell() {
    if (window_) Shutdown();
}

bool AppShell::Init() {
    glfwSetErrorCallback(GlfwErrorCallback);
    // Force X11: Wayland init fails hard on setups where XDG_RUNTIME_DIR
    // isn't visible to this process (e.g. launched outside a login shell).
    // Runs fine under XWayland on a Wayland session.
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    if (!glfwInit()) return false;

    // Load Theme::* settings + saved window geometry before creating the
    // window, so the window can be created at the right size/position
    // instead of jumping there after the fact.
    SettingsPersistence::WindowGeometry geo = SettingsPersistence::LoadSettings();
    Logging::Init(); // needs Theme::LoggingEnabled (just loaded) + ~/.config/vitals (LoadSettings ensured it exists)

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // positioned before first show — avoids a create-then-move flicker
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE); // borderless — DrawTitleBar() draws our own themed title bar instead

    window_ = glfwCreateWindow(geo.w, geo.h, "Vitals", nullptr, nullptr);
    if (!window_) { glfwTerminate(); return false; }

    // Sets _NET_WM_ICON directly on the window (X11/XWayland), which the
    // shell reads for alt-tab/dash without needing a matching .desktop file
    // — replaces the generic gear fallback shown for icon-less windows.
    int iconSizes[] = {16, 32, 48, 64};
    std::vector<std::vector<unsigned char>> iconPixels;
    std::vector<GLFWimage> icons;
    for (int s : iconSizes) iconPixels.push_back(AppIcon::Render(s));
    for (size_t i = 0; i < iconPixels.size(); ++i) {
        icons.push_back(GLFWimage{iconSizes[i], iconSizes[i], iconPixels[i].data()});
    }
    glfwSetWindowIcon(window_, (int)icons.size(), icons.data());

    if (geo.x >= 0) {
        // Clamp onto the primary monitor's work area so a saved position
        // from a since-removed second monitor doesn't leave the window
        // unreachable.
        int mx, my, mw, mh;
        glfwGetMonitorWorkarea(glfwGetPrimaryMonitor(), &mx, &my, &mw, &mh);
        glfwSetWindowPos(window_, std::clamp(geo.x, mx, mx + mw - 100), std::clamp(geo.y, my, my + mh - 100));
    }
    glfwShowWindow(window_);

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(Theme::VSyncEnabled ? 1 : 0);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    Theme::Apply();
    Theme::LoadFonts(Theme::kFontOversample); // Theme::LoadedFonts holds the results

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    if (const char* renderer = (const char*)glGetString(GL_RENDERER)) metrics_.gpuName = renderer;

    TrayIcon::Init(window_); // false/inert if GTK3+appindicator weren't available at build time
    return true;
}

// ---------------------------------------------------------------------
// Title bar: replaces the OS decoration (window is undecorated — see
// Init()) so it can be drawn in the current theme's colors.
// ---------------------------------------------------------------------
void AppShell::DrawTitleBar(float width) {
    float h = Theme::kTitleBarHeight;
    ImGui::BeginChild("TitleBar", ImVec2(width, h), false, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), ImGui::ColorConvertFloat4ToU32(Theme::kBgPanel));
    dl->AddLine(ImVec2(p.x, p.y + h - 1), ImVec2(p.x + width, p.y + h - 1), ImGui::ColorConvertFloat4ToU32(Theme::kBorder));

    float cy = p.y + h * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(p.x + Theme::Scale(12.0f), cy - ImGui::GetTextLineHeight() * 0.5f));
    ImGui::TextColored(Theme::kTextMuted, "Vitals");

    float btnW = Theme::Scale(40.0f);
    ImVec2 btnSize(btnW, h);
    ImU32 iconColor = ImGui::ColorConvertFloat4ToU32(Theme::kTextSecondary);
    ImU32 closeIconColor = ImGui::ColorConvertFloat4ToU32(Theme::kTextPrimary);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::kBgPanelAlt);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::kBgPanelAlt);

    bool maximized = glfwGetWindowAttrib(window_, GLFW_MAXIMIZED) != 0;

    ImGui::SetCursorScreenPos(ImVec2(p.x + width - btnW * 3, p.y));
    if (ImGui::Button("##tb_min", btnSize)) glfwIconifyWindow(window_);
    DrawMinimizeIcon(dl, ImVec2(p.x + width - btnW * 2.5f, cy), Theme::Scale(4.5f), iconColor);

    ImGui::SetCursorScreenPos(ImVec2(p.x + width - btnW * 2, p.y));
    if (ImGui::Button("##tb_max", btnSize)) {
        if (maximized) glfwRestoreWindow(window_); else glfwMaximizeWindow(window_);
    }
    ImVec2 maxCenter(p.x + width - btnW * 1.5f, cy);
    if (maximized) DrawRestoreIcon(dl, maxCenter, Theme::Scale(4.5f), iconColor);
    else DrawMaximizeIcon(dl, maxCenter, Theme::Scale(4.5f), iconColor);

    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::kAccentClock);
    ImGui::SetCursorScreenPos(ImVec2(p.x + width - btnW, p.y));
    bool closeHovered;
    if (ImGui::Button("##tb_close", btnSize)) glfwSetWindowShouldClose(window_, GLFW_TRUE);
    closeHovered = ImGui::IsItemHovered();
    ImGui::PopStyleColor();
    DrawCloseIcon(dl, ImVec2(p.x + width - btnW * 0.5f, cy), Theme::Scale(4.5f), closeHovered ? closeIconColor : iconColor);

    ImGui::PopStyleColor(3);

    // Drag-to-move region: everything left of the three buttons. Native
    // Wayland gives clients no way to set their own window position (same
    // limitation already hit restoring a saved window position — see
    // app_shell.h), so dragging there is a deliberate no-op instead of
    // spamming that GLFW error every frame of the drag.
    float dragW = std::max(0.0f, width - btnW * 3 - Theme::Scale(12.0f));
    ImGui::SetCursorScreenPos(ImVec2(p.x + Theme::Scale(12.0f), p.y));
    ImGui::InvisibleButton("##tb_drag", ImVec2(dragW, h));
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (maximized) glfwRestoreWindow(window_); else glfwMaximizeWindow(window_);
    } else if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND) {
        if (ImGui::IsItemActivated()) {
            titleBarDragging_ = true;
            double curX, curY;
            glfwGetCursorPos(window_, &curX, &curY);
            glfwGetWindowPos(window_, &dragStartWinX_, &dragStartWinY_);
            dragStartCursorX_ = dragStartWinX_ + curX;
            dragStartCursorY_ = dragStartWinY_ + curY;
        }
        if (titleBarDragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            double curX, curY;
            glfwGetCursorPos(window_, &curX, &curY);
            int winX, winY;
            glfwGetWindowPos(window_, &winX, &winY);
            double absX = winX + curX, absY = winY + curY;
            glfwSetWindowPos(window_,
                              dragStartWinX_ + (int)(absX - dragStartCursorX_),
                              dragStartWinY_ + (int)(absY - dragStartCursorY_));
        } else {
            titleBarDragging_ = false;
        }
    }

    ImGui::EndChild();
}

void AppShell::DrawResizeGrip(ImVec2 viewportOrigin, float width, float height) {
    float gripSize = Theme::Scale(14.0f);
    ImVec2 gripPos(viewportOrigin.x + width - gripSize, viewportOrigin.y + height - gripSize);
    ImGui::SetCursorScreenPos(gripPos);
    ImGui::InvisibleButton("##resize_grip", ImVec2(gripSize, gripSize));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        int w, h;
        glfwGetWindowSize(window_, &w, &h);
        glfwSetWindowSize(window_, std::max(400, w + (int)delta.x), std::max(300, h + (int)delta.y));
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 col = ImGui::ColorConvertFloat4ToU32(Theme::kTextMuted);
    for (int i = 1; i <= 3; ++i) {
        float o = (float)i * 4.0f;
        dl->AddLine(ImVec2(gripPos.x + gripSize - o, gripPos.y + gripSize),
                    ImVec2(gripPos.x + gripSize, gripPos.y + gripSize - o), col, 1.5f);
    }
}

// ---------------------------------------------------------------------
// Header bar: breadcrumb, Default/Expert mode toggle
// ---------------------------------------------------------------------
void AppShell::DrawHeader(float width) {
    ImGui::BeginChild("Header", ImVec2(width, Theme::kHeaderHeight), false,
                       ImGuiWindowFlags_NoScrollbar);

    ImVec2 p = ImGui::GetCursorScreenPos();
    float cy = p.y + Theme::kHeaderHeight * 0.5f;

    const char* pageLabel =
        currentPage_ == Page::Summary ? "SYSTEM SUMMARY" :
        currentPage_ == Page::Performance ? "PERFORMANCE" :
        currentPage_ == Page::Processes ? "PROCESSES" :
        currentPage_ == Page::SystemInfo ? "SYSTEM INFO" :
        currentPage_ == Page::StartupApps ? "STARTUP APPS" :
        currentPage_ == Page::Users ? "USERS" :
        currentPage_ == Page::Services ? "SERVICES" :
        currentPage_ == Page::Benchmarks ? "BENCHMARKS" :
        currentPage_ == Page::InstalledApps ? "INSTALLED APPS" :
        currentPage_ == Page::DiskSpace ? "DISK SPACE" :
        currentPage_ == Page::Settings ? "SETTINGS" : "";

    // Brand mark: bold, in Theme::kAccentBrand (a dedicated token, not
    // reused from a metric accent, so it can be a clearly distinct hue per
    // StyleDirection instead of blending into a near-identical blue), with
    // a soft glow behind it on the directions that opt into one
    // (Theme::ChartGlow — Vivid Monitoring, Cyberpunk) so the header itself
    // reads as part of the chosen theme, not just body content.
    ImGui::SetCursorScreenPos(ImVec2(p.x + Theme::Scale(20.0f), cy - ImGui::GetTextLineHeight() * 0.5f));
    ImGui::PushFont(Theme::LoadedFonts.bodyBold);
    if (Theme::ChartGlow) {
        ImVec2 brandPos = ImGui::GetCursorScreenPos();
        ImVec2 brandSize = ImGui::CalcTextSize("VITALS");
        ImVec4 glow = Theme::kAccentBrand;
        glow.w = Theme::ShadowAlpha;
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(brandPos.x - 8.0f, brandPos.y - 4.0f),
            ImVec2(brandPos.x + brandSize.x + 8.0f, brandPos.y + brandSize.y + 4.0f),
            ImGui::ColorConvertFloat4ToU32(glow), 6.0f);
    }
    ImGui::TextColored(Theme::kAccentBrand, "VITALS");
    ImGui::PopFont();
    ImGui::SameLine(0, 0);
    ImGui::TextColored(Theme::kTextSecondary, " / %s", pageLabel);

    ImGui::EndChild();
}

// ---------------------------------------------------------------------
// Sidebar: nav list + logo footer
// ---------------------------------------------------------------------
void AppShell::DrawSidebar(float height) {
    ImGui::BeginChild("Sidebar", ImVec2(Theme::kSidebarWidth, height), false);
    ImGui::Dummy(ImVec2(0, 8));

    struct NavItem { const char* label; Page page; };
    static const NavItem kNav[] = {
        {"Summary", Page::Summary}, {"Performance", Page::Performance}, {"Processes", Page::Processes},
        {"System Info", Page::SystemInfo}, {"Startup apps", Page::StartupApps}, {"Users", Page::Users},
        {"Services", Page::Services}, {"Benchmarks", Page::Benchmarks}, {"Installed Apps", Page::InstalledApps},
        {"Disk Space", Page::DiskSpace},
    };
    ImVec2 rowSize(Theme::kSidebarWidth - Theme::Scale(30.0f), Theme::Scale(34.0f));
    // Highlight follows currentPage_ itself, so it can never disagree with the page shown.
    auto navRow = [&](const char* label, Page page) {
        bool isSelected = currentPage_ == page;
        if (isSelected) ImGui::PushStyleColor(ImGuiCol_Header, Theme::kBgSidebarSel);
        if (ImGui::Selectable(label, isSelected, 0, rowSize)) currentPage_ = page;
        if (isSelected) ImGui::PopStyleColor();
    };

    ImGui::Indent(10.0f);
    for (const NavItem& item : kNav) navRow(item.label, item.page);

    // Settings pinned to the bottom of the sidebar, separated from the pages above.
    float bottomY = height - rowSize.y - Theme::Scale(12.0f);
    if (ImGui::GetCursorPosY() < bottomY) ImGui::SetCursorPosY(bottomY);
    ImGui::Separator();
    navRow("Settings", Page::Settings);
    ImGui::Unindent(10.0f);

    ImGui::EndChild();
}

int AppShell::Run() {
    if (!window_) return 1;

    auto lastTime = std::chrono::steady_clock::now();
    double sinceRefresh = 0.0;

    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        TrayIcon::Poll();
        if (TrayIcon::QuitRequested()) {
            // "Quit" from the tray menu always means a real exit, even
            // though the window may currently be hidden (shouldClose is
            // otherwise only ever set by the OS close button).
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        } else if (glfwWindowShouldClose(window_) && TrayIcon::Available()) {
            // Closing the window hides to tray instead of exiting, so
            // vitals keeps running in the background — only "Quit" (above)
            // ends the process. When no tray is available this branch
            // never runs, so the close button exits exactly as before.
            glfwSetWindowShouldClose(window_, GLFW_FALSE);
            glfwHideWindow(window_);
        }

        // Cheap enough to just re-apply every frame instead of tracking a
        // dirty flag for this rarely-changed Settings-page toggle.
        glfwSwapInterval(Theme::VSyncEnabled ? 1 : 0);
        // GLFW_FLOATING isn't supported under Wayland and logs a GLFW error
        // on every call (not just when it'd actually change anything) — only
        // call it when the value has actually changed, so leaving the
        // toggle off (the default) never spams that error at frame rate.
        if (Theme::AlwaysOnTop != appliedAlwaysOnTop_) {
            glfwSetWindowAttrib(window_, GLFW_FLOATING, Theme::AlwaysOnTop ? GLFW_TRUE : GLFW_FALSE);
            appliedAlwaysOnTop_ = Theme::AlwaysOnTop;
        }

        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - lastTime).count();
        lastTime = now;
        sinceRefresh += dt;
        if (sinceRefresh >= Theme::RefreshRateSec) {
            metrics_.Update(sinceRefresh);
            sinceRefresh = 0.0;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                      ImGuiWindowFlags_NoScrollbar;
        ImGui::Begin("Root", nullptr, rootFlags);

        float fullWidth = viewport->WorkSize.x;
        float fullHeight = viewport->WorkSize.y;
        Theme::SetUiScale(fullWidth, fullHeight);
        Theme::HistoryWindowSamples = std::max(1, (int)std::lround(Theme::HistoryWindowSec / std::max(0.05f, Theme::RefreshRateSec)));

        DrawTitleBar(fullWidth);
        DrawHeader(fullWidth);
        ImGui::Separator();

        float bodyHeight = fullHeight - Theme::kTitleBarHeight - Theme::kHeaderHeight - 8.0f;
        DrawSidebar(bodyHeight);
        ImGui::SameLine();

        ImGui::BeginChild("Content", ImVec2(fullWidth - Theme::kSidebarWidth, bodyHeight), false);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Theme::kGap, Theme::kGap));
        ImGui::Indent(Theme::kGap);
        ImGui::Dummy(ImVec2(0, Theme::kGap));

        float contentWidth = fullWidth - Theme::kSidebarWidth - Theme::kGap * 3;

        switch (currentPage_) {
            case Page::Summary:
                DrawSummaryPage(metrics_, contentWidth);
                break;
            case Page::Performance:
                DrawPerformancePage(metrics_, perfSelected_, ImVec2(contentWidth, bodyHeight - Theme::kGap * 2));
                break;
            case Page::Processes:
                DrawProcessesPage(metrics_, ImVec2(contentWidth, bodyHeight - Theme::kGap * 2));
                break;
            case Page::SystemInfo:
                DrawSystemInfoPage(metrics_, ImVec2(contentWidth, bodyHeight - Theme::kGap * 2));
                break;
            case Page::StartupApps:
                DrawStartupAppsPage(metrics_, ImVec2(contentWidth, bodyHeight - Theme::kGap * 2));
                break;
            case Page::Users:
                DrawUsersPage(metrics_, ImVec2(contentWidth, bodyHeight - Theme::kGap * 2));
                break;
            case Page::Services:
                DrawServicesPage(metrics_, ImVec2(contentWidth, bodyHeight - Theme::kGap * 2));
                break;
            case Page::Benchmarks:
                DrawBenchmarksPage(metrics_, ImVec2(contentWidth, bodyHeight - Theme::kGap * 2));
                break;
            case Page::InstalledApps:
                DrawInstalledAppsPage(metrics_, ImVec2(contentWidth, bodyHeight - Theme::kGap * 2));
                break;
            case Page::DiskSpace:
                DrawDiskSpacePage(metrics_, ImVec2(contentWidth, bodyHeight - Theme::kGap * 2));
                break;
            case Page::Settings:
                DrawSettingsPage(metrics_, ImVec2(contentWidth, bodyHeight - Theme::kGap * 2));
                break;
        }

        ImGui::Unindent(Theme::kGap);
        ImGui::PopStyleVar();
        ImGui::EndChild();

        DrawResizeGrip(viewport->WorkPos, fullWidth, fullHeight);

        ImGui::End();
        ImGui::PopStyleVar(2);

        // GPU stress test (Benchmarks page): no compute-shader plumbing —
        // ImGui's own draw calls already exercise the GPU, so loading it is
        // just drawing a lot more of them for the configured duration. Watch
        // the GPU panel's live gpuPct/temp while this runs.
        if (metrics_.bench.gpuStressUntilT > metrics_.t) {
            // Confined to a small on-screen swatch instead of tiled across
            // the whole viewport: same quad count redrawing far fewer
            // pixels means *more* fill-rate stress (true overdraw), and it
            // reads as a visible "GPU load" indicator instead of a
            // translucent haze blanketing the entire UI.
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            float boxSize = Theme::Scale(160.0f);
            float pad = Theme::Scale(24.0f);
            ImVec2 boxMin(fullWidth - boxSize - pad, fullHeight - boxSize - pad);
            int rectCount = 3000 * std::clamp(metrics_.bench.gpuStressLoadPct, 1, 100) / 100;
            for (int i = 0; i < rectCount; ++i) {
                float x = boxMin.x + (float)((i * 2654435761u) % (unsigned)boxSize);
                float y = boxMin.y + (float)((i * 40503u) % (unsigned)boxSize);
                dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 24.0f, y + 24.0f), IM_COL32(80, 160, 255, 30));
            }
            dl->AddRect(boxMin, ImVec2(boxMin.x + boxSize, boxMin.y + boxSize), IM_COL32(80, 160, 255, 140), 4.0f);
        }

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window_, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window_);
    }

    Shutdown();
    return 0;
}

void AppShell::Shutdown() {
    SettingsPersistence::WindowGeometry geo;
    glfwGetWindowPos(window_, &geo.x, &geo.y);
    glfwGetWindowSize(window_, &geo.w, &geo.h);
    SettingsPersistence::SaveSettings(geo);
    Logging::Shutdown();
    TrayIcon::Shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window_);
    glfwTerminate();
    window_ = nullptr;
}
