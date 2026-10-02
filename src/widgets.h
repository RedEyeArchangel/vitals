#pragma once
#include "imgui.h"
#include "metrics.h"
#include "theme.h"
#include <vector>

namespace Widgets {

// The reference UI's signature control: a stack of small LED-style segments
// that fill from one end, glowing at the topmost/rightmost lit segment.
// Not a stock ImGui widget — built with ImDrawList (see skill notes on
// custom controls). `value01` is clamped to [0,1].

// Vertical bar, fills bottom-up (used for the CPU/Clock/Temp/GPU column
// meters and the tall Memory meter on the left of its card).
void LedBarVertical(const char* id, float value01, ImVec4 color,
                     ImVec2 size, int segments = 26);

// Horizontal bar, fills left-to-right (used for Disks/Network/Energy/GPU/
// NPU/Thermals cards, and the CPU-core grid in the Performance view).
void LedBarHorizontal(const char* id, float value01, ImVec4 color,
                       ImVec2 size, int segments = 30);

// A small "pill" status badge with a colored dot, e.g. "LIVE" or "HEALTHY".
void StatusPill(const char* text, ImVec4 dotColor);

// Begins a themed card: a rounded, bordered child region with the given
// size (0 on an axis = fill available space on that axis). `elevated` draws
// a soft drop shadow behind the card (size must be non-zero on both axes for
// this — see Theme::ShadowAlpha/ShadowUseAccent). `borderColor` overrides
// the card's border (used for accent-bordered preview boxes).
bool BeginCard(const char* id, ImVec2 size, bool elevated = false, ImVec4 borderColor = Theme::kBorder);
void EndCard();

// A detail-page header: big title, muted subtitle below it, and a
// horizontal LED meter + value right-aligned on the title's line.
// Used at the top of every Performance > <metric> detail panel.
void DetailHeader(const char* title, const char* subtitle, float value01,
                   const char* valueText, ImVec4 color, float width);

// One label/value pair for a stats grid (muted small label above a bold
// value) — lay several out with ImGui::Columns/Table and call once per cell.
void StatCell(const char* label, const char* value);

// A small "pill" box with a label above and a bold value below, used for
// e.g. "Thermal state / Nominal" boxes on the Energy/Thermals pages.
void InfoPill(const char* label, const char* value, ImVec2 size);

// A hand-drawn sparkline (polyline + soft fill, no axes/grid) for small
// preview boxes — nesting a full ImPlot plot inside a tiny (~90x46) child
// is fragile, so these draw directly with ImDrawList instead.
void Sparkline(const RingBuffer& history, ImVec4 color, ImVec2 size);

// Draggable curve editor (fan curve / VF curve): an ImPlot plot fixed to
// [xRange,yRange] with one draggable point per entry in `points` (x/y in
// whatever units the caller wants displayed — e.g. tempC/speed01 for a fan
// curve, clockOffsetMHz/voltageMV for a VF curve) connected by a line.
// Dragging a point clamps it to the axis range and keeps points ordered by
// x (swapping neighbors instead of letting the curve cross itself).
// Modifies `points` in place; returns true the frame anything moved, so
// callers only need to push an Apply-eligible config when this is true.
// Built on ImPlot::DragPoint (already an ImPlot 0.17 API, see CMakeLists.txt's
// implot FetchContent pin) rather than a hand-rolled drag/hit-test — no
// existing curve-editing widget in this file to build on otherwise.
// Optional: xLabel/yLabel name the axes, yTickFmt formats Y ticks (e.g.
// "%g%%"), pointLabelFmt draws a caption under each point, formatted with
// (y, x) — e.g. "%.0f%% at %.0f\xC2\xB0" "C".
bool CurveEditor(const char* id, std::vector<ImVec2>& points, ImVec2 size,
                  ImVec2 xRange, ImVec2 yRange, ImVec4 color,
                  const char* xLabel = nullptr, const char* yLabel = nullptr,
                  const char* yTickFmt = nullptr, const char* pointLabelFmt = nullptr);

// ---- LACT-style control-page widgets (GPU Overclock / Fan Control) ----

// Flat rounded progress bar filling `width` (value01 clamped to [0,1]).
void ValueBar(float value01, ImVec4 color, float width, float height);

// iOS/GNOME-style on/off switch. Returns true when toggled.
bool ToggleSwitch(const char* id, bool* value);

// Equal-width segmented buttons across `width`; returns true when *selected changed.
bool SegmentedTabs(const char* id, const char* const* labels, int count, int* selected, float width);

// One editable row: label | slider | number field with -/+ steps.
// When *value != baseline the row is marked modified (accent label with a
// leading dot, "was X" tooltip, and a reset button that restores baseline).
// Returns true when the value changed this frame.
bool EditRowInt(const char* label, int* value, int vmin, int vmax, int baseline, float width, const char* unit = "");

} // namespace Widgets
