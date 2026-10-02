#pragma once

#include "imgui.h"
#include "metrics.h"

// Helpers shared by two or more Performance detail panels (cpu.cpp,
// memory.cpp, gpu.cpp, thermals.cpp) plus performance.cpp's own left-hand
// metric list. Panel-local helpers (DrawCoreCard, DrawDimmConfigCard, ...)
// stay static in their own .cpp instead of living here.

void StatGrid4(const char* labels[], const char* values[], int count);

// Formats v with fmt (a single-%f printf format) if v > 0, else "Unavailable"
// — the shape behind every optional hardware readout that may not exist on
// this machine (voltage/clock/power/temp/speed/...).
void FmtOrNA(char* buf, size_t n, const char* fmt, float v);

void FmtVolt(char* buf, size_t n, float v);
void FmtMHz(char* buf, size_t n, float v);
void FmtU32(char* buf, size_t n, uint32_t v);

// Pushes the bordered/rounded "alt panel" card styling shared by every card
// on the Performance pages (DrawKVCard, BeginExpertBox, DrawCoreCard,
// DrawDimmConfigCard, ...). Call ImGui::BeginChild(...) right after this and
// ImGui::EndChild() right before EndAltCardStyle() — centralized so a future
// style tweak, or an added Push*, can't silently desync from a copy-pasted
// PopStyleVar/PopStyleColor count at some other call site.
void BeginAltCardStyle();
void EndAltCardStyle();

// Wraps all expert-mode-only content in one consistently labeled, auto-sized
// bordered box, so it always reads as "the advanced stuff" regardless of
// which page it's on. Returns the box's inner content width.
float BeginExpertBox(float width);
void EndExpertBox();

// A titled card of label/value rows — mirrors TuxTimings' per-category RAM/CPU panels.
void DrawKVCard(const char* id, const char* title, const char* const* labels,
                 const char* const* values, int count, ImVec2 size);

const char* GpuSubtitle(const Metrics& m);
