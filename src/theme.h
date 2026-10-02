#pragma once
#include "imgui.h"

// Centralized design tokens extracted from the reference screenshot.
// Every color used anywhere in the app should come from here, not a literal,
// so a palette tweak is a one-place change (see replica-workflow guidance).
namespace Theme {

// --- base surfaces ---
// NOT `const`: these are the runtime-switchable style-direction tokens (see
// StyleDirection/SetStyleDirection below) — values here are the compiled-in
// defaults, overwritten by SetStyleDirection() before the first Apply().
inline ImVec4 kBgWindow      = ImVec4(0.055f, 0.055f, 0.063f, 1.00f); // ~#0e0e10
inline ImVec4 kBgPanel       = ImVec4(0.086f, 0.086f, 0.098f, 1.00f); // ~#161619
inline ImVec4 kBgPanelAlt    = ImVec4(0.071f, 0.071f, 0.082f, 1.00f); // slightly darker card
inline ImVec4 kBorder        = ImVec4(0.180f, 0.180f, 0.196f, 1.00f); // ~#2e2e32
inline ImVec4 kBgSidebarSel  = ImVec4(0.129f, 0.180f, 0.325f, 1.00f); // selected nav row

// --- text ---
inline ImVec4 kTextPrimary   = ImVec4(0.910f, 0.910f, 0.925f, 1.00f);
inline ImVec4 kTextSecondary = ImVec4(0.560f, 0.560f, 0.590f, 1.00f);
inline ImVec4 kTextMuted     = ImVec4(0.380f, 0.380f, 0.410f, 1.00f);

// --- per-metric accent colors (the app's signature: every metric owns a hue,
//     reused consistently across its meter, graph line, and label) ---
inline ImVec4 kAccentCpu     = ImVec4(0.494f, 0.851f, 0.341f, 1.00f); // green
inline ImVec4 kAccentClock   = ImVec4(0.851f, 0.325f, 0.310f, 1.00f); // red
inline ImVec4 kAccentTemp    = ImVec4(0.941f, 0.769f, 0.098f, 1.00f); // yellow
inline ImVec4 kAccentGpu     = ImVec4(0.290f, 0.565f, 0.851f, 1.00f); // blue
inline ImVec4 kAccentMemory  = ImVec4(0.753f, 0.376f, 0.878f, 1.00f); // purple/magenta
inline ImVec4 kAccentEnergy  = ImVec4(0.878f, 0.627f, 0.188f, 1.00f); // amber
inline ImVec4 kAccentNetwork = ImVec4(0.290f, 0.565f, 0.851f, 1.00f); // cyan-blue
inline ImVec4 kAccentDisk    = ImVec4(0.494f, 0.851f, 0.341f, 1.00f); // green
inline ImVec4 kAccentOk      = ImVec4(0.400f, 0.851f, 0.478f, 1.00f);

// The header's "VITALS" brand mark color — deliberately its own token (not
// reused from an existing metric accent) so it can be a clearly distinct
// hue per StyleDirection instead of the near-identical blues several
// directions' kAccentGpu happen to share.
inline ImVec4 kAccentBrand   = ImVec4(0.435f, 0.659f, 0.863f, 1.00f);

// --- card elevation + chart style: the other per-direction differentiators
// (surfaces/text/accents above cover palette; these cover "how much shadow
// a card casts" and "how a chart line/fill reads") — see BeginCard()'s
// `elevated` param in widgets.cpp and PlotFilledLine() in page_common.cpp. ---
inline float ShadowAlpha     = 0.14f; // 0 = no visible shadow under elevated cards
inline bool  ShadowUseAccent = false; // true: shadow tints toward the card's own accent color instead of black
inline float ChartLineThickness = 2.0f;
inline float ChartFillAlpha     = 0.16f;
inline bool  ChartGlow          = false; // true: an extra soft wide pass is drawn under the crisp line

// The three visual directions from the mockup review, switchable on the
// Settings page. RefinedDark is the shipped default.
enum class StyleDirection { RefinedDark, ModernSaaS, VividMonitoring, Cyberpunk };
inline StyleDirection CurrentStyleDirection = StyleDirection::RefinedDark;

// Pure data: overwrites every token above (colors, elevation, chart, panel
// rounding) from the chosen direction's preset. Does NOT touch ImGui state,
// so it's safe to call before ImGui::CreateContext() exists (settings load
// at startup) as well as at runtime — call Apply() afterward to push the
// new colors into ImGuiStyle when a context does exist (startup already
// does this; a live Settings-page switch must do it explicitly too).
void SetStyleDirection(StyleDirection d);

// --- layout constants ---
// These are rescaled at runtime by SetUiScale() (not compile-time constants
// like the colors above) — the app was designed around a ~1600x1000 window,
// and needs to stay legible when the window is much larger (e.g. maximized
// on a 4K display running at OS scale 1.0, where fixed-pixel UI reads tiny).
inline float kSidebarWidth   = 250.0f;
inline float kTitleBarHeight = 32.0f; // custom-drawn title bar (window is created undecorated — see AppShell::DrawTitleBar)
inline float kHeaderHeight   = 46.0f;
inline float kPanelRounding  = 10.0f;
inline float kPanelPadding   = 14.0f;
inline float kGap            = 14.0f;

// Current runtime UI scale (1.0 == designed 1600x1000 window). Multiply any
// layout literal that isn't already one of the constants above by this via
// Scale() so it grows/shrinks along with the rest of the UI.
inline float UiScale = 1.0f;
inline float Scale(float v) { return v * UiScale; }

// Set on the Settings page. Gates advanced telemetry (SMU voltages, DRAM
// timings, GPU/VRAM clocks) that's noise for most users.
inline bool ExpertMode = false;

// Also set on the Settings page: how often Metrics::Update() runs, in
// seconds. Drives both the live refresh cadence and (via HistoryWindowSamples
// below) how many samples make up the graphs' fixed 30s window.
inline float RefreshRateSec = 0.5f;

// How many trailing seconds of history graphs plot against — a Settings
// preset (30s / 2min / 10min), not a slider (RingBuffer capacity in
// metrics.h is sized for the longest preset at the fastest refresh rate,
// so raising this never runs out of real samples to show).
inline float HistoryWindowSec = 30.0f;

// Fixed-width scrolling window (in samples) all history graphs plot against,
// recomputed each frame from HistoryWindowSec/RefreshRateSec so every graph
// always spans the configured window, right-aligned, instead of growing
// until the ring buffer fills. Set once per frame in app_shell.cpp; see
// PlotFilledLine().
inline int HistoryWindowSamples = 60;

// Settings-page toggles.
inline bool VSyncEnabled = true;
inline bool AlwaysOnTop = false;
inline bool LoggingEnabled = false;

enum class TempUnit { Celsius, Fahrenheit };
inline TempUnit CurrentTempUnit = TempUnit::Celsius;

// Every temperature display (text or plotted) should read through these
// two instead of assuming Celsius, so the Settings unit toggle covers the
// whole app from one place.
inline float TempForDisplay(float celsiusValue) {
    return CurrentTempUnit == TempUnit::Fahrenheit ? celsiusValue * 9.0f / 5.0f + 32.0f : celsiusValue;
}
inline const char* TempUnitSuffix() {
    return CurrentTempUnit == TempUnit::Fahrenheit ? "F" : "C";
}

// Recomputes UiScale (and the layout constants + ImGui font scale) from the
// current window size. Cheap — call once per frame.
void SetUiScale(float windowWidth, float windowHeight);

// Applies the base ImGuiStyle (rounding/spacing/colors) once at startup.
void Apply();

// Loads body + heading fonts at the given pixel sizes, oversampled for
// high-DPI. Call after ImGui::CreateContext(), before the first NewFrame.
// Falls back to ImGui's default font if the TTF paths aren't found, so the
// app still runs (with a visibly different look) if fonts aren't bundled.
struct Fonts {
    ImFont* body     = nullptr;
    ImFont* bodyBold = nullptr;
    ImFont* heading  = nullptr;
    ImFont* mono     = nullptr;
};
inline Fonts LoadedFonts;

// Fonts are baked at this multiple of their nominal pixel size, and
// FontGlobalScale divides it back out — keeps glyphs sharp when UiScale
// exceeds 1.0 instead of upscaling a small baked atlas.
inline float kFontOversample = 1.75f;

void LoadFonts(float dpiScale);

} // namespace Theme
