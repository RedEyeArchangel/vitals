#include "pages/summary/summary.h"
#include "pages/page_common.h"
#include "theme.h"
#include "widgets.h"
#include "implot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static void DrawMeterStrip(const Metrics& m, ImVec2 size) {
    if (!Widgets::BeginCard("MeterStrip", size)) { Widgets::EndCard(); return; }

    struct Col { const char* label; float value01; ImVec4 color; char valueText[16]; };
    Col cols[4] = {
        {"CPU",   m.cpuPct,          Theme::kAccentCpu,   ""},
        {"Clock", m.avgMHz / m.maxMHz, Theme::kAccentClock, ""},
        {"Temp",  m.tempC / m.maxTempC, Theme::kAccentTemp,  ""},
        {"GPU",   m.gpuPct,          Theme::kAccentGpu,   ""},
    };
    std::snprintf(cols[0].valueText, sizeof(cols[0].valueText), "%.1f%%", m.cpuPct * 100.0f);
    std::snprintf(cols[1].valueText, sizeof(cols[1].valueText), "%.0f MHz", m.avgMHz);
    std::snprintf(cols[2].valueText, sizeof(cols[2].valueText), "%.1f %s", Theme::TempForDisplay(m.tempC), Theme::TempUnitSuffix());
    std::snprintf(cols[3].valueText, sizeof(cols[3].valueText), "%.1f%%", m.gpuPct * 100.0f);

    float gap = Theme::Scale(40.0f);
    float available = size.x - Theme::kPanelPadding * 2;
    float colWidth = std::min(Theme::Scale(40.0f), std::max((available - gap * 3) / 4.0f, 4.0f));
    float barHeight = size.y - Theme::Scale(90.0f);

    float totalW = colWidth * 4 + gap * 3;
    float baseX = ImGui::GetCursorPosX() + std::max(0.0f, (available - totalW) / 2.0f);
    float baseY = ImGui::GetCursorPosY();

    // Fixed positions, not SameLine: a wide label/value ("3049 MHz") must not
    // push its neighbor's column, or the bars drift apart unevenly.
    for (int i = 0; i < 4; ++i) {
        ImGui::SetCursorPos(ImVec2(baseX + i * (colWidth + gap), baseY));
        ImGui::BeginGroup();
        ImGui::TextColored(cols[i].color, "%s", cols[i].label);
        ImGui::SetCursorPosX(baseX + i * (colWidth + gap));
        Widgets::LedBarVertical(cols[i].label, cols[i].value01, cols[i].color,
                                 ImVec2(colWidth, barHeight));
        ImGui::SetCursorPosX(baseX + i * (colWidth + gap));
        ImGui::TextColored(cols[i].color, "%s", cols[i].valueText);
        ImGui::EndGroup();
    }

    Widgets::EndCard();
}

static void DrawCpuOverview(const Metrics& m, ImVec2 size) {
    if (!Widgets::BeginCard("CpuOverview", size)) { Widgets::EndCard(); return; }

    ImGui::TextColored(Theme::kTextPrimary, "CPU Overview");

    struct LegendEntry { const char* label; ImVec4 color; };
    ImDrawList* headDl = ImGui::GetWindowDrawList();
    auto legendItemWidth = [](const LegendEntry& e) {
        return 14.0f + 4.0f + ImGui::CalcTextSize(e.label).x;
    };
    auto drawLegendItem = [&](const LegendEntry& e) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float lineH = ImGui::GetTextLineHeight();
        headDl->AddCircleFilled(ImVec2(p.x + 5.0f, p.y + lineH * 0.5f), 5.0f,
                                 ImGui::ColorConvertFloat4ToU32(e.color));
        ImGui::Dummy(ImVec2(14.0f, lineH));
        ImGui::SameLine(0, 4.0f);
        ImGui::TextColored(Theme::kTextSecondary, "%s", e.label);
    };
    const LegendEntry allLegend[] = {
        {"Utilization", Theme::kAccentCpu},
        {"Kernel", Theme::kAccentTemp},
        {"Temperature", Theme::kAccentClock},
    };

    char pctBuf[16]; std::snprintf(pctBuf, sizeof(pctBuf), "%.1f%%", m.cpuPct * 100.0f);
    ImGui::SameLine(size.x - Theme::kPanelPadding * 2 - ImGui::CalcTextSize(pctBuf).x);
    ImGui::TextColored(Theme::kAccentCpu, "%s", pctBuf);

    PushPlotTheme();
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(4, 4));

    // Fixed 30s rolling window: the x-axis is always 30s wide from the first frame,
    // the newest sample always lands at the right edge, and the window slides left
    // as more history arrives. Never shrinks/grows the axis itself to fit the data.
    // Theme::HistoryWindowSamples is refreshed from the configured refresh
    // interval every frame (see AppShell::Run) — same window
    // WindowedSamples()/PlotFilledLine use.

    ImVec2 plotSize(size.x - Theme::kPanelPadding * 2, size.y - Theme::Scale(60.0f));
    if (ImPlot::BeginPlot("##cpu_overview", plotSize,
                           ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect |
                           ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, (double)std::max(1, Theme::HistoryWindowSamples - 1), ImGuiCond_Always);

        // Temperature (C) is the plot's Y1 - the plain, unambiguous left-hand axis.
        // Utilization/Kernel (%) go on Y2 with Opposite so they render on the right,
        // instead of leaving two same-side axes to stack on top of one another.
        bool fahrenheit = Theme::CurrentTempUnit == Theme::TempUnit::Fahrenheit;
        ImPlot::PushStyleColor(ImPlotCol_AxisText, Theme::kAccentClock);
        ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_None);
        ImPlot::PopStyleColor();
        ImPlot::SetupAxisLimits(ImAxis_Y1, fahrenheit ? 32.0 : 0.0, fahrenheit ? 212.0 : 100.0, ImGuiCond_Always);
        ImPlot::SetupAxisFormat(ImAxis_Y1, fahrenheit ? "%.0f F" : "%.0f C");

        ImPlot::PushStyleColor(ImPlotCol_AxisText, Theme::kAccentCpu);
        ImPlot::SetupAxis(ImAxis_Y2, nullptr, ImPlotAxisFlags_Opposite);
        ImPlot::PopStyleColor();
        ImPlot::SetupAxisLimits(ImAxis_Y2, 0.0, 100.0, ImGuiCond_Always);
        ImPlot::SetupAxisFormat(ImAxis_Y2, "%.0f%%");

        auto plotLine = [](const char* label, const RingBuffer& rb, ImVec4 color, float scale) {
            std::vector<float> x, y;
            WindowedSamples(rb, x, y);
            if (x.empty()) return;
            if (scale != 1.0f) for (float& v : y) v *= scale;
            ImPlot::SetNextLineStyle(color, Theme::ChartLineThickness);
            ImPlot::PlotLine(label, x.data(), y.data(), (int)x.size());
        };
        auto plotTempLine = [](const char* label, const RingBuffer& rb, ImVec4 color) {
            std::vector<float> x, y;
            WindowedSamples(rb, x, y);
            if (x.empty()) return;
            for (float& v : y) v = Theme::TempForDisplay(v); // rb stores raw C; convert per Settings unit
            ImPlot::SetNextLineStyle(color, Theme::ChartLineThickness);
            ImPlot::PlotLine(label, x.data(), y.data(), (int)x.size());
        };
        // Temperature -> Y1 (left), in the display unit. Utilization/Kernel are
        // 0..1 fractions -> Y2 (right, %), so the two series don't share a bogus 0-1 scale.
        plotTempLine("Temperature", m.tempHistory, Theme::kAccentClock);
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
        plotLine("Utilization", m.cpuHistory, Theme::kAccentCpu, 100.0f);
        plotLine("Kernel", m.kernelHistory, Theme::kAccentTemp, 100.0f);
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);

        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar();
    PopPlotTheme();

    float legendW = 0.0f;
    for (int i = 0; i < 3; ++i) legendW += legendItemWidth(allLegend[i]) + (i > 0 ? 14.0f : 0.0f);

    ImGui::TextColored(Theme::kTextMuted, "%d logical processors", m.logicalProcessors);
    ImGui::SameLine(size.x - Theme::kPanelPadding * 2 - legendW);
    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine(0, 14.0f);
        drawLegendItem(allLegend[i]);
    }

    Widgets::EndCard();
}

static void DrawProcessList(const Metrics& m, ImVec2 size) {
    if (!Widgets::BeginCard("ProcessList", size)) { Widgets::EndCard(); return; }

    ImGui::TextColored(Theme::kTextPrimary, "Top CPU processes (%d)", (int)m.topProcesses.size());

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg;
    if (ImGui::BeginTable("procs", 4, flags)) {
        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, Theme::Scale(50.0f));
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthFixed, Theme::Scale(50.0f));
        ImGui::TableSetupColumn("Memory", ImGuiTableColumnFlags_WidthFixed, Theme::Scale(70.0f));

        for (int i = 0; i < (int)m.topProcesses.size(); ++i) {
            const ProcessRow& row = m.topProcesses[i];
            ImGui::TableNextRow();
            bool highlight = (i < 2);
            if (highlight) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                        ImGui::ColorConvertFloat4ToU32(ImVec4(Theme::kAccentCpu.x, Theme::kAccentCpu.y, Theme::kAccentCpu.z, 0.15f)));
            }
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextMuted, "%d", row.pid);
            ImGui::TableNextColumn(); ImGui::TextColored(highlight ? Theme::kAccentCpu : Theme::kTextPrimary, "%s", row.name.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%.1f%%", row.cpuPct);
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextSecondary, "%s", row.memory.c_str());
        }
        ImGui::EndTable();
    }

    Widgets::EndCard();
}

static void DrawMemoryCard(const Metrics& m, ImVec2 size) {
    if (!Widgets::BeginCard("Memory", size)) { Widgets::EndCard(); return; }

    ImGui::TextColored(Theme::kTextPrimary, "Memory Utilization");
    char memText[32];
    std::snprintf(memText, sizeof(memText), "%.2f GB / %.0f GB", m.memUsedGB, m.memTotalGB);
    ImGui::SameLine(size.x - Theme::kPanelPadding * 2 - ImGui::CalcTextSize(memText).x);
    ImGui::TextColored(Theme::kAccentMemory, "%s", memText);

    float barWidth = Theme::Scale(60.0f);
    float rowHeight = size.y - Theme::Scale(70.0f);

    ImGui::BeginGroup();
    Widgets::LedBarVertical("mem_bar", m.memUsedGB / m.memTotalGB, Theme::kAccentMemory,
                             ImVec2(barWidth, rowHeight));
    ImGui::TextColored(Theme::kAccentMemory, "%.1f%%", 100.0f * m.memUsedGB / m.memTotalGB);
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::BeginGroup();
    PushPlotTheme();
    if (ImPlot::BeginPlot("##mem_plot", ImVec2(size.x - barWidth - Theme::kPanelPadding * 2 - 20.0f, rowHeight),
                           ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect |
                           ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 100.0, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, std::max(1.0, (double)m.memHistory.data.size() - 1.0), ImGuiCond_Always);
        std::vector<float> y(m.memHistory.data.begin(), m.memHistory.data.end());
        for (float& v : y) v *= 100.0f;
        if (!y.empty()) {
            ImPlot::SetNextLineStyle(Theme::kAccentMemory, Theme::ChartLineThickness);
            ImPlot::SetNextFillStyle(Theme::kAccentMemory, Theme::ChartFillAlpha);
            ImPlot::PlotLine("mem", y.data(), (int)y.size());
        }
        ImPlot::EndPlot();
    }
    PopPlotTheme();
    ImGui::EndGroup();

    Widgets::EndCard();
}

static void DrawStatCard(const char* title, const char* subtitle, float pct,
                          const char* valueText, ImVec4 color, ImVec2 size) {
    ImGui::PushID(title);
    if (!Widgets::BeginCard("StatCard", size)) { Widgets::EndCard(); ImGui::PopID(); return; }

    ImGui::TextColored(color, "%s", title);
    ImGui::SameLine(size.x - Theme::kPanelPadding * 2 - ImGui::CalcTextSize(valueText).x);
    ImGui::TextColored(color, "%s", valueText);

    ImGui::TextColored(Theme::kTextMuted, "%s", subtitle);
    ImGui::Dummy(ImVec2(0, 4));
    Widgets::LedBarHorizontal("meter", pct, color, ImVec2(size.x - Theme::kPanelPadding * 2, 14.0f));

    Widgets::EndCard();
    ImGui::PopID();
}

void DrawSummaryPage(const Metrics& metrics, float contentWidth) {
    float row1Height = Theme::Scale(320.0f);
    float meterW = contentWidth * 0.22f;
    float cpuW = contentWidth * 0.52f;
    float procW = contentWidth - meterW - cpuW - Theme::kGap * 2;

    DrawMeterStrip(metrics, ImVec2(meterW, row1Height));
    ImGui::SameLine(0, Theme::kGap);
    DrawCpuOverview(metrics, ImVec2(cpuW, row1Height));
    ImGui::SameLine(0, Theme::kGap);
    DrawProcessList(metrics, ImVec2(procW, row1Height));

    ImGui::Dummy(ImVec2(0, Theme::kGap));

    DrawMemoryCard(metrics, ImVec2(contentWidth, Theme::Scale(220.0f)));
    ImGui::Dummy(ImVec2(0, Theme::kGap));

    float thirdW = (contentWidth - Theme::kGap * 2) / 3.0f;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f%%", metrics.diskPct * 100.0f);
    char diskSub[48]; std::snprintf(diskSub, sizeof(diskSub), "R: %.0f KB/s  W: %.0f KB/s", metrics.diskReadKBs, metrics.diskWriteKBs);
    DrawStatCard("Disks", diskSub, metrics.diskPct, buf, Theme::kAccentDisk, ImVec2(thirdW, Theme::Scale(110.0f)));
    ImGui::SameLine(0, Theme::kGap);
    std::snprintf(buf, sizeof(buf), "%.0f KB/s", metrics.networkKBs);
    DrawStatCard("Network", metrics.netInterfaceCount.c_str(), metrics.networkKBs / 100.0f, buf, Theme::kAccentNetwork, ImVec2(thirdW, Theme::Scale(110.0f)));
    ImGui::SameLine(0, Theme::kGap);
    if (metrics.hasBattery) {
        std::snprintf(buf, sizeof(buf), "%.0f%%", metrics.batteryPct * 100.0f);
        DrawStatCard("Battery", metrics.batteryStatus.c_str(), metrics.batteryPct, buf, Theme::kAccentEnergy, ImVec2(thirdW, Theme::Scale(110.0f)));
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f W", metrics.energyW);
        DrawStatCard("Energy", "", metrics.energyW / 200.0f, buf, Theme::kAccentEnergy, ImVec2(thirdW, Theme::Scale(110.0f)));
    }

    ImGui::Dummy(ImVec2(0, Theme::kGap));

    char thermSub[32]; std::snprintf(thermSub, sizeof(thermSub), "%s thermal pressure", metrics.thermalPressureText.c_str());
#ifdef __APPLE__
    std::snprintf(buf, sizeof(buf), "%.1f%%", metrics.gpuPct * 100.0f);
    DrawStatCard(metrics.gpuName.c_str(), metrics.gpuSource == "unavailable" ? "unavailable" : metrics.gpuSource.c_str(), metrics.gpuPct, buf, Theme::kAccentGpu, ImVec2(thirdW, Theme::Scale(110.0f)));
    ImGui::SameLine(0, Theme::kGap);
    std::snprintf(buf, sizeof(buf), "%.1f%%", metrics.npuPct * 100.0f);
    DrawStatCard("NPU 0", "Apple Neural Engine", metrics.npuPct, buf, Theme::kAccentClock, ImVec2(thirdW, Theme::Scale(110.0f)));
    ImGui::SameLine(0, Theme::kGap);
    std::snprintf(buf, sizeof(buf), "%.1f %s", Theme::TempForDisplay(metrics.tempC), Theme::TempUnitSuffix());
    DrawStatCard("Thermals", thermSub, metrics.tempC / metrics.maxTempC, buf, Theme::kAccentTemp, ImVec2(thirdW, Theme::Scale(110.0f)));
#else
    float halfW = (contentWidth - Theme::kGap) / 2.0f;
    std::snprintf(buf, sizeof(buf), "%.1f%%", metrics.gpuPct * 100.0f);
    DrawStatCard(metrics.gpuName.c_str(), metrics.gpuSource == "unavailable" ? "unavailable" : metrics.gpuSource.c_str(), metrics.gpuPct, buf, Theme::kAccentGpu, ImVec2(halfW, Theme::Scale(110.0f)));
    ImGui::SameLine(0, Theme::kGap);
    std::snprintf(buf, sizeof(buf), "%.1f %s", Theme::TempForDisplay(metrics.tempC), Theme::TempUnitSuffix());
    DrawStatCard("Thermals", thermSub, metrics.tempC / metrics.maxTempC, buf, Theme::kAccentTemp, ImVec2(halfW, Theme::Scale(110.0f)));
#endif
}
