#pragma once

#include "imgui.h"
#include "implot.h"
#include "metrics.h"
#include <string>
#include <vector>

void PushPlotTheme();
void PopPlotTheme();

// See page_common.cpp — shared right-aligned windowing math behind
// PlotFilledLine, also usable directly for a second/multi-series overlay
// sharing PlotFilledLine's x-axis.
void WindowedSamples(const RingBuffer& rb, std::vector<float>& outX, std::vector<float>& outY);
void PlotFilledLine(const char* id, const RingBuffer& rb, ImVec4 color, float yMax = 1.0f);

// True if haystack contains needleLower as a case-insensitive substring.
// needleLower must already be lowercase — build it once per frame with
// ToLower() rather than re-lowering the filter text for every row/field.
bool ContainsCI(const std::string& haystack, const std::string& needleLower);

std::string ToLower(std::string s);
