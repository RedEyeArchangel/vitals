#include "theme.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <unistd.h>

namespace Theme {

// Finds assets/fonts/ next to the running binary via /proc/self/exe, same
// trick as FindDaemonPath() in dram_oc/daemon_ipc.cpp — a relative
// "assets/fonts/..." path only resolves when cwd happens to be the repo
// root, which isn't true once vitals is installed as a system package and
// launched from a desktop entry (cwd is then $HOME or /). Falls back to the
// bare relative path (dev build run as ./build/vitals from the repo root)
// and to the packaged system data dir.
static std::string FindAssetDir() {
    if (std::FILE* f = std::fopen("assets/fonts/Inter-Regular.ttf", "rb")) {
        std::fclose(f);
        return "assets";
    }
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        std::string path(exe);
        size_t slash = path.find_last_of('/');
        if (slash != std::string::npos) {
            std::string candidate = path.substr(0, slash + 1) + "assets";
            std::string probe = candidate + "/fonts/Inter-Regular.ttf";
            if (std::FILE* f = std::fopen(probe.c_str(), "rb")) {
                std::fclose(f);
                return candidate;
            }
        }
    }
    return "/usr/share/vitals"; // installed location (see CMakeLists.txt install(DIRECTORY assets/fonts ...))
}

// Design baseline: the window size main() creates at startup. UiScale is
// how much bigger/smaller the actual window is than this, so a maximized
// window on a large/dense display (e.g. 4K run at OS scale 1.0, where the
// OS gives the app no HiDPI hint at all) still reads at a sane size instead
// of the fixed few-dozen pixels the original design assumed.
namespace {
constexpr float kBaseW = 1600.0f, kBaseH = 1000.0f;
constexpr float kBaseSidebarWidth = 250.0f, kBaseTitleBarHeight = 32.0f, kBaseHeaderHeight = 46.0f;
constexpr float kBasePanelPadding = 14.0f, kBaseGap = 14.0f;
// Not constexpr: SetStyleDirection() overwrites this per-direction.
float gBasePanelRounding = 10.0f;
}

void SetUiScale(float windowWidth, float windowHeight) {
    float s = std::min(windowWidth / kBaseW, windowHeight / kBaseH);
    s = std::clamp(s, 0.85f, 3.0f); // ponytail: window-ratio heuristic, not a real DPI query — bump the clamp if a display needs more
    UiScale = s;

    kSidebarWidth  = kBaseSidebarWidth * s;
    kTitleBarHeight = kBaseTitleBarHeight * s;
    kHeaderHeight  = kBaseHeaderHeight * s;
    kPanelRounding = gBasePanelRounding * s;
    kPanelPadding  = kBasePanelPadding * s;
    kGap           = kBaseGap * s;

    ImGui::GetIO().FontGlobalScale = s / Theme::kFontOversample;
}

void SetStyleDirection(StyleDirection d) {
    switch (d) {
    case StyleDirection::RefinedDark:
        kBgWindow      = ImVec4(0.0431f, 0.0471f, 0.0627f, 1.00f);
        kBgPanel       = ImVec4(0.0784f, 0.0863f, 0.1098f, 1.00f);
        kBgPanelAlt    = ImVec4(0.0627f, 0.0706f, 0.0941f, 1.00f);
        kBorder        = ImVec4(0.1490f, 0.1647f, 0.2000f, 1.00f);
        kBgSidebarSel  = ImVec4(0.1176f, 0.1412f, 0.1922f, 1.00f);
        kTextPrimary   = ImVec4(0.9294f, 0.9373f, 0.9490f, 1.00f);
        kTextSecondary = ImVec4(0.6039f, 0.6275f, 0.6745f, 1.00f);
        kTextMuted     = ImVec4(0.3608f, 0.3843f, 0.4392f, 1.00f);
        kAccentCpu     = ImVec4(0.4353f, 0.8118f, 0.5569f, 1.00f);
        kAccentClock   = ImVec4(0.8784f, 0.4627f, 0.4353f, 1.00f);
        kAccentTemp    = ImVec4(0.9098f, 0.7686f, 0.4078f, 1.00f);
        kAccentGpu     = ImVec4(0.4353f, 0.6588f, 0.8627f, 1.00f);
        kAccentMemory  = ImVec4(0.7059f, 0.5569f, 0.8784f, 1.00f);
        kAccentEnergy  = ImVec4(0.8784f, 0.6588f, 0.3686f, 1.00f);
        kAccentNetwork = ImVec4(0.3725f, 0.7804f, 0.7804f, 1.00f);
        kAccentDisk    = ImVec4(0.6078f, 0.7882f, 0.4784f, 1.00f);
        kAccentOk      = ImVec4(0.4000f, 0.8510f, 0.4780f, 1.00f);
        kAccentBrand   = ImVec4(0.4353f, 0.6588f, 0.8627f, 1.00f); // blue
        gBasePanelRounding = 10.0f;
        ShadowAlpha = 0.14f; ShadowUseAccent = false;
        ChartLineThickness = 2.0f; ChartFillAlpha = 0.16f; ChartGlow = false;
        break;
    case StyleDirection::ModernSaaS:
        kBgWindow      = ImVec4(0.0706f, 0.0745f, 0.0863f, 1.00f);
        kBgPanel       = ImVec4(0.1020f, 0.1059f, 0.1216f, 1.00f);
        kBgPanelAlt    = ImVec4(0.1255f, 0.1294f, 0.1490f, 1.00f);
        kBorder        = ImVec4(0.1725f, 0.1804f, 0.2000f, 1.00f);
        kBgSidebarSel  = ImVec4(0.1490f, 0.1570f, 0.1880f, 1.00f);
        kTextPrimary   = ImVec4(0.9490f, 0.9490f, 0.9529f, 1.00f);
        kTextSecondary = ImVec4(0.5451f, 0.5529f, 0.5765f, 1.00f);
        kTextMuted     = ImVec4(0.3333f, 0.3412f, 0.3647f, 1.00f);
        kAccentCpu     = ImVec4(0.4941f, 0.8784f, 0.6588f, 1.00f);
        kAccentClock   = ImVec4(0.9098f, 0.5412f, 0.5137f, 1.00f);
        kAccentTemp    = ImVec4(0.8784f, 0.7529f, 0.4667f, 1.00f);
        kAccentGpu     = ImVec4(0.4980f, 0.6627f, 0.9412f, 1.00f);
        kAccentMemory  = ImVec4(0.7020f, 0.6157f, 0.8588f, 1.00f);
        kAccentEnergy  = ImVec4(0.8784f, 0.6588f, 0.3686f, 1.00f);
        kAccentNetwork = ImVec4(0.3922f, 0.7686f, 0.7686f, 1.00f);
        kAccentDisk    = ImVec4(0.6196f, 0.8196f, 0.4941f, 1.00f);
        kAccentOk      = ImVec4(0.4200f, 0.8200f, 0.5000f, 1.00f);
        kAccentBrand   = ImVec4(0.4863f, 0.4196f, 0.9373f, 1.00f); // indigo/violet
        gBasePanelRounding = 7.0f;
        ShadowAlpha = 0.05f; ShadowUseAccent = false;
        ChartLineThickness = 1.3f; ChartFillAlpha = 0.08f; ChartGlow = false;
        break;
    case StyleDirection::VividMonitoring:
        kBgWindow      = ImVec4(0.0314f, 0.0353f, 0.0471f, 1.00f);
        kBgPanel       = ImVec4(0.0706f, 0.0745f, 0.0980f, 1.00f);
        kBgPanelAlt    = ImVec4(0.0941f, 0.1020f, 0.1333f, 1.00f);
        kBorder        = ImVec4(0.1490f, 0.1647f, 0.2196f, 1.00f);
        kBgSidebarSel  = ImVec4(0.1059f, 0.1255f, 0.1882f, 1.00f);
        kTextPrimary   = ImVec4(1.0000f, 1.0000f, 1.0000f, 1.00f);
        kTextSecondary = ImVec4(0.6549f, 0.6745f, 0.7412f, 1.00f);
        kTextMuted     = ImVec4(0.3608f, 0.3804f, 0.4706f, 1.00f);
        kAccentCpu     = ImVec4(0.2902f, 0.8706f, 0.5020f, 1.00f);
        kAccentClock   = ImVec4(1.0000f, 0.3608f, 0.3608f, 1.00f);
        kAccentTemp    = ImVec4(1.0000f, 0.8235f, 0.2471f, 1.00f);
        kAccentGpu     = ImVec4(0.2902f, 0.6235f, 1.0000f, 1.00f);
        kAccentMemory  = ImVec4(0.7529f, 0.5176f, 0.9882f, 1.00f);
        kAccentEnergy  = ImVec4(1.0000f, 0.6392f, 0.2471f, 1.00f);
        kAccentNetwork = ImVec4(0.1765f, 0.8314f, 0.8314f, 1.00f);
        kAccentDisk    = ImVec4(0.4941f, 0.8784f, 0.5059f, 1.00f);
        kAccentOk      = ImVec4(0.4000f, 0.8510f, 0.4780f, 1.00f);
        kAccentBrand   = ImVec4(1.0000f, 0.2000f, 0.5490f, 1.00f); // hot pink
        gBasePanelRounding = 12.0f;
        ShadowAlpha = 0.30f; ShadowUseAccent = true;
        ChartLineThickness = 2.2f; ChartFillAlpha = 0.22f; ChartGlow = true;
        break;
    case StyleDirection::Cyberpunk:
        kBgWindow      = ImVec4(0.0392f, 0.0235f, 0.0706f, 1.00f); // deep violet-black
        kBgPanel       = ImVec4(0.0824f, 0.0588f, 0.1412f, 1.00f);
        kBgPanelAlt    = ImVec4(0.1098f, 0.0745f, 0.1882f, 1.00f);
        kBorder        = ImVec4(0.3373f, 0.1725f, 0.4784f, 1.00f); // neon-violet border
        kBgSidebarSel  = ImVec4(0.2510f, 0.0784f, 0.3529f, 1.00f);
        kTextPrimary   = ImVec4(0.9216f, 0.9608f, 1.0000f, 1.00f); // faint cyan-white
        kTextSecondary = ImVec4(0.6039f, 0.5804f, 0.7216f, 1.00f);
        kTextMuted     = ImVec4(0.3804f, 0.3608f, 0.4784f, 1.00f);
        kAccentCpu     = ImVec4(0.2000f, 1.0000f, 0.6000f, 1.00f); // matrix green
        kAccentClock   = ImVec4(1.0000f, 0.1490f, 0.6000f, 1.00f); // hot magenta
        kAccentTemp    = ImVec4(1.0000f, 0.9490f, 0.2000f, 1.00f); // neon yellow
        kAccentGpu     = ImVec4(0.1020f, 0.9490f, 1.0000f, 1.00f); // electric cyan
        kAccentMemory  = ImVec4(0.7529f, 0.3020f, 1.0000f, 1.00f); // neon purple
        kAccentEnergy  = ImVec4(1.0000f, 0.5490f, 0.1490f, 1.00f); // neon orange
        kAccentNetwork = ImVec4(0.3020f, 0.6000f, 1.0000f, 1.00f); // electric blue
        kAccentDisk    = ImVec4(0.5490f, 1.0000f, 0.3490f, 1.00f); // acid green
        kAccentOk      = ImVec4(0.3020f, 1.0000f, 0.5490f, 1.00f);
        kAccentBrand   = ImVec4(1.0000f, 0.1490f, 0.8510f, 1.00f); // neon magenta
        gBasePanelRounding = 4.0f; // angular, low-rounding cyberpunk HUD look
        ShadowAlpha = 0.38f; ShadowUseAccent = true; // strongest neon glow of any direction
        ChartLineThickness = 2.4f; ChartFillAlpha = 0.25f; ChartGlow = true;
        break;
    }
    CurrentStyleDirection = d;
}

void Apply() {
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding    = 10.0f;
    s.ChildRounding      = kPanelRounding;
    s.FrameRounding      = 6.0f;
    s.PopupRounding      = 8.0f;
    s.ScrollbarRounding  = 8.0f;
    s.GrabRounding        = 6.0f;
    s.TabRounding         = 6.0f;

    s.WindowPadding  = ImVec2(0, 0);
    s.FramePadding   = ImVec2(8, 5);
    s.ItemSpacing    = ImVec2(8, 8);
    s.ItemInnerSpacing = ImVec2(6, 6);
    s.ScrollbarSize  = 12.0f;
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize  = 1.0f;
    s.FrameBorderSize  = 0.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]        = kBgWindow;
    c[ImGuiCol_ChildBg]         = kBgPanel;
    c[ImGuiCol_PopupBg]         = kBgPanel;
    c[ImGuiCol_Border]          = kBorder;
    c[ImGuiCol_Text]            = kTextPrimary;
    c[ImGuiCol_TextDisabled]    = kTextMuted;

    c[ImGuiCol_Header]          = kBgSidebarSel;
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.20f, 0.24f, 0.38f, 1.0f);
    c[ImGuiCol_HeaderActive]    = kBgSidebarSel;

    c[ImGuiCol_FrameBg]         = ImVec4(0.13f, 0.13f, 0.15f, 1.0f);
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.17f, 0.17f, 0.19f, 1.0f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.20f, 0.20f, 0.23f, 1.0f);

    c[ImGuiCol_Button]          = ImVec4(0.15f, 0.15f, 0.17f, 1.0f);
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.20f, 0.20f, 0.23f, 1.0f);
    c[ImGuiCol_ButtonActive]    = kAccentGpu;

    c[ImGuiCol_ScrollbarBg]     = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]   = ImVec4(0.25f, 0.25f, 0.28f, 1.0f);

    c[ImGuiCol_Separator]       = kBorder;
    c[ImGuiCol_TableBorderLight] = kBorder;
    c[ImGuiCol_TableRowBg]      = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]   = ImVec4(1, 1, 1, 0.02f);
}

static ImFont* TryLoadFont(ImGuiIO& io, const char* path, float pxSize) {
    if (FILE* f = std::fopen(path, "rb")) {
        std::fclose(f);
        ImFontConfig cfg;
        cfg.OversampleH = 3;
        cfg.OversampleV = 3;
        // Latin-1 plus the few UI symbols the pages use: — (U+2014),
        // → (U+2192), ↺ (U+21BA, reset), ● (U+25CF, "modified" marker).
        static const ImWchar kRanges[] = {0x0020, 0x00FF, 0x2014, 0x2014, 0x2192, 0x2192,
                                          0x21BA, 0x21BA, 0x25CF, 0x25CF, 0};
        return io.Fonts->AddFontFromFileTTF(path, pxSize, &cfg, kRanges);
    }
    return nullptr;
}

void LoadFonts(float dpiScale) {
    ImGuiIO& io = ImGui::GetIO();
    Fonts& out = LoadedFonts;

    // Expected bundled assets (see assets/README.md) — swap paths for
    // whatever font family matches your target design (Inter, Segoe UI,
    // SF Pro, etc). Falls back to ImGui's built-in font if not found.
    std::string dir = FindAssetDir();
    out.body     = TryLoadFont(io, (dir + "/fonts/Inter-Regular.ttf").c_str(), 16.0f * dpiScale);
    out.bodyBold = TryLoadFont(io, (dir + "/fonts/Inter-SemiBold.ttf").c_str(), 16.0f * dpiScale);
    out.heading  = TryLoadFont(io, (dir + "/fonts/Inter-Bold.ttf").c_str(), 26.0f * dpiScale);
    out.mono     = TryLoadFont(io, (dir + "/fonts/JetBrainsMono-Regular.ttf").c_str(), 14.0f * dpiScale);

    if (!out.body) {
        out.body = io.Fonts->AddFontDefault();
    }
    if (!out.bodyBold) out.bodyBold = out.body;
    if (!out.heading) out.heading = out.body;
    if (!out.mono) out.mono = out.body;
    io.FontGlobalScale = 1.0f / dpiScale;
    // Do NOT call io.Fonts->Build() here: on current ImGui (docking branch,
    // new texture-streaming backend) the atlas is built automatically once
    // the renderer backend is initialized. Calling Build() before that is
    // set up just spams "RendererHasTextures" warnings every frame.
}

} // namespace Theme
