#include "pages/page_common.h"
#include "theme.h"
#include <algorithm>

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

bool ContainsCI(const std::string& haystack, const std::string& needleLower) {
    return ToLower(haystack).find(needleLower) != std::string::npos;
}

void PushPlotTheme() {
    ImPlot::PushStyleColor(ImPlotCol_FrameBg, Theme::kBgPanelAlt);
    ImPlot::PushStyleColor(ImPlotCol_PlotBg, Theme::kBgPanelAlt);
    ImPlot::PushStyleColor(ImPlotCol_AxisGrid, ImVec4(1, 1, 1, 0.05f));
}

void PopPlotTheme() {
    ImPlot::PopStyleColor(3);
}

// Right-aligns rb's last Theme::HistoryWindowSamples samples into outX/outY
// against the same fixed x-axis window PlotFilledLine plots on (same fixed
// 30s scrolling window as the Summary page's CPU Overview graph), instead of
// the x-axis growing until the ring buffer fills. Shared with any caller
// overlaying a second/multi-series line on that same axis (see
// DrawCoreCard's kernel-time overlay, the GPU page's per-sensor temp plot).
void WindowedSamples(const RingBuffer& rb, std::vector<float>& outX, std::vector<float>& outY) {
    int windowSamples = std::max(1, Theme::HistoryWindowSamples);
    int n = (int)rb.data.size();
    int shown = std::min(n, windowSamples);
    outX.clear();
    outY.clear();
    if (shown <= 0) return;
    outY.assign(rb.data.begin() + (n - shown), rb.data.end());
    outX.resize(shown);
    int xOffset = windowSamples - shown;
    for (int i = 0; i < shown; ++i) outX[i] = (float)(xOffset + i);
}

void PlotFilledLine(const char* id, const RingBuffer& rb, ImVec4 color, float yMax) {
    std::vector<float> x, y;
    WindowedSamples(rb, x, y);
    if (x.empty()) return;
    int windowSamples = std::max(1, Theme::HistoryWindowSamples);

    ImPlot::SetupAxisLimits(ImAxis_X1, 0, (double)std::max(1, windowSamples - 1), ImGuiCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, yMax, ImGuiCond_Always);
    ImPlot::SetNextFillStyle(color, Theme::ChartFillAlpha);
    ImPlot::SetNextLineStyle(color, Theme::ChartLineThickness);
    ImPlot::PlotShaded(id, x.data(), y.data(), (int)x.size(), 0.0);
    if (Theme::ChartGlow) {
        std::string glowId = std::string(id) + "_glow";
        ImVec4 glowColor = color;
        glowColor.w = 0.25f;
        ImPlot::SetNextLineStyle(glowColor, Theme::ChartLineThickness * 3.0f);
        ImPlot::PlotLine(glowId.c_str(), x.data(), y.data(), (int)x.size());
    }
    ImPlot::SetNextLineStyle(color, Theme::ChartLineThickness);
    ImPlot::PlotLine(id, x.data(), y.data(), (int)x.size());
}
