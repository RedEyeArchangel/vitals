#include "pages/performance/gpu/gpu.h"
#include "pages/performance/shared.h"
#include "pages/page_common.h"
#include "theme.h"
#include "widgets.h"
#include "metrics/gpu_sysfs.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

// Layout follows LACT's stats page: a live stats header, then utilization
// and temperature graphs. Read-only — all values come straight from sysfs.

namespace {

// ---------------------------------------------------------------------------
// Overview pieces (stats header, utilization + temperature graphs)
// ---------------------------------------------------------------------------

bool IsGpuTempLabel(const std::string& label) {
    std::string lower = ToLower(label);
    for (const char* k : {"edge", "junction", "hotspot", "mem", "vram", "soc", "gpu", "therm", "core", "pwr", "vrgfx", "vrmem", "vrsoc"})
        if (lower.find(k) != std::string::npos) return true;
    return false;
}

int TempSortKey(const std::string& s) {
    std::string lower = ToLower(s);
    if (lower.find("edge") != std::string::npos) return 0;
    if (lower.find("junction") != std::string::npos) return 1;
    if (lower.find("mem") != std::string::npos && lower.find("vrmem") == std::string::npos) return 2;
    if (lower.find("vrgfx") != std::string::npos) return 3;
    if (lower.find("vrmem") != std::string::npos) return 4;
    if (lower.find("vrsoc") != std::string::npos) return 5;
    return 6;
}

std::vector<std::pair<std::string, float>> GpuTemps(const Metrics& m) {
    std::vector<std::pair<std::string, float>> temps;
    for (const ThermalSensor& s : m.thermalSensors)
        if (s.available && IsGpuTempLabel(s.label)) temps.emplace_back(s.label, s.tempC);
    if (temps.empty() && m.gpuTempC > 0.0f) temps.emplace_back("GPU", m.gpuTempC);
    std::sort(temps.begin(), temps.end(), [](const auto& a, const auto& b) { return TempSortKey(a.first) < TempSortKey(b.first); });
    return temps;
}

// Rounded panel matching LACT's stat cards.
void BeginPanel(const char* id, float width) {
    BeginAltCardStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Theme::Scale(16.0f), Theme::Scale(12.0f)));
    ImGui::BeginChild(id, ImVec2(width, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
}
void EndPanel() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    EndAltCardStyle();
}

void LabeledValue(const char* label, const char* value) {
    ImGui::TextColored(Theme::kTextSecondary, "%s", label);
    ImGui::PushFont(Theme::LoadedFonts.bodyBold);
    ImGui::TextColored(Theme::kTextPrimary, "%s", value);
    ImGui::PopFont();
}

// "label / value  [=====bar=====]" cell, as in LACT's second stats card.
void BarCell(const char* label, const char* value, float value01, float width) {
    ImGui::TextColored(Theme::kTextSecondary, "%s", label);
    ImGui::PushFont(Theme::LoadedFonts.bodyBold);
    ImGui::TextColored(Theme::kTextPrimary, "%s", value);
    float textW = ImGui::CalcTextSize(value).x;
    ImGui::PopFont();
    // Relative spacing, not SameLine(x): an absolute x is window-relative
    // and would stack every table column's bar on top of the first one.
    float valueW = Theme::Scale(130.0f);
    ImGui::SameLine(0, std::max(Theme::Scale(8.0f), valueW - textW));
    float h = ImGui::GetTextLineHeight();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + h * 0.1f);
    Widgets::ValueBar(value01, Theme::kAccentGpu, std::max(Theme::Scale(40.0f), width - valueW - Theme::Scale(8.0f)), h * 0.8f);
}

void DrawStatsHeader(const Metrics& m, const AmdOdState& od, float width) {
    // Card 1: temperatures / clocks / voltage
    BeginPanel("##stats1", width);
    if (ImGui::BeginTable("stats1", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        std::string temps;
        for (const auto& [label, c] : GpuTemps(m)) {
            char b[64]; std::snprintf(b, sizeof(b), "%s%s: %.0f\xC2\xB0%s", temps.empty() ? "" : ", ", label.c_str(), Theme::TempForDisplay(c), Theme::TempUnitSuffix());
            temps += b;
        }
        LabeledValue("Temperature", temps.empty() ? "Unavailable" : temps.c_str());
        ImGui::TableNextColumn();
        char buf[32]; FmtMHz(buf, sizeof(buf), m.gpuCoreClockMHz);
        LabeledValue("GPU Core Clock", buf);
        ImGui::TableNextColumn();
        FmtOrNA(buf, sizeof(buf), "%.3f V", m.gpuVoltageV);
        LabeledValue("GPU Voltage", buf);
        ImGui::EndTable();
    }
    EndPanel();
    ImGui::Dummy(ImVec2(0, Theme::Scale(6.0f)));

    // Card 2: bars
    BeginPanel("##stats2", width);
    if (ImGui::BeginTable("stats2", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        float colW = ImGui::GetContentRegionAvail().x;
        char b[48];
        float sclkMax = od.sclkMax > 0 ? (float)od.sclkMax : 3000.0f;
        // Scaled against the card's hardware VRAM ceiling (OD_RANGE), not the
        // configured max: RDNA3 often pins VRAM at its top level (e.g. two 4K
        // monitors), so "of configured max" would read 100% forever.
        // x2: hwmon/the label show the effective (DDR) rate, see ReadGpuSysfsClocks.
        float mclkMax = od.mclkRange.max > 0 ? (float)od.mclkRange.max * 2.0f : (od.mclkMax > 0 ? (float)od.mclkMax * 2.0f : 2500.0f);

        FmtMHz(b, sizeof(b), m.gpuCoreClockMHz); BarCell("GPU Core Clock", b, m.gpuCoreClockMHz / sclkMax, colW);
        ImGui::TableNextColumn();
        FmtMHz(b, sizeof(b), m.gpuVramClockMHz);
        char vramLabel[48];
        if (od.mclkActiveLevel >= 0 && !od.mclkLevelsMhz.empty())
            std::snprintf(vramLabel, sizeof(vramLabel), "VRAM Clock (level %d/%d)", od.mclkActiveLevel, (int)od.mclkLevelsMhz.size() - 1);
        else std::snprintf(vramLabel, sizeof(vramLabel), "VRAM Clock");
        ImGui::BeginGroup();
        BarCell(vramLabel, b, m.gpuVramClockMHz / mclkMax, colW);
        ImGui::EndGroup();
        if (ImGui::IsItemHovered() && od.mclkActiveLevel >= 0 && od.mclkActiveLevel == (int)od.mclkLevelsMhz.size() - 1)
            ImGui::SetTooltip("VRAM is at its highest power level. amdgpu keeps it there whenever the\n"
                              "display setup can't tolerate memory reclocking (e.g. several high-res or\n"
                              "high-refresh monitors) \xE2\x80\x94 expected, not a vitals reading error.");
        ImGui::TableNextColumn(); std::snprintf(b, sizeof(b), "%.0f%%", m.gpuPct * 100.0f); BarCell("GPU Usage", b, m.gpuPct, colW);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (m.gpuVramTotalGB > 0.0f) std::snprintf(b, sizeof(b), "%.1f GiB", m.gpuVramUsedGB); else std::snprintf(b, sizeof(b), "N/A");
        BarCell("VRAM Usage", b, m.gpuVramTotalGB > 0.0f ? m.gpuVramUsedGB / m.gpuVramTotalGB : 0.0f, colW);
        ImGui::TableNextColumn();
        if (od.gttTotalGB > 0.0f) std::snprintf(b, sizeof(b), "%.1f GiB", od.gttUsedGB); else std::snprintf(b, sizeof(b), "N/A");
        BarCell("GTT Usage", b, od.gttTotalGB > 0.0f ? od.gttUsedGB / od.gttTotalGB : 0.0f, colW);
        ImGui::TableNextColumn();
        float capW = od.hasPowerCap ? (float)od.powerCapW : m.gpuPowerLimitW;
        FmtOrNA(b, sizeof(b), "%.1f W", m.energyW);
        BarCell("Power Usage", b, capW > 0.0f ? m.energyW / capW : 0.0f, colW);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        float fanPct = od.fanPct >= 0.0f ? od.fanPct : 0.0f;
        std::snprintf(b, sizeof(b), "%.0f RPM (%.0f%%)", m.gpuFanRpm, fanPct);
        BarCell("Fan Speed", b, fanPct / 100.0f, colW);
        ImGui::EndTable();
    }
    EndPanel();
}

void DrawOverviewGraphs(const Metrics& m, float width) {
    ImGui::Dummy(ImVec2(0, Theme::Scale(10.0f)));
    ImGui::TextColored(Theme::kTextMuted, "Utilization");
    PushPlotTheme();
    if (ImPlot::BeginPlot("##gpu", ImVec2(width, Theme::Scale(180.0f)),ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
        PlotFilledLine("gpu", m.gpuHistory, Theme::kAccentGpu);
        ImPlot::EndPlot();
    }
    PopPlotTheme();

    auto temps = GpuTemps(m);
    if (temps.empty()) return;
    ImGui::Dummy(ImVec2(0, Theme::Scale(10.0f)));
    ImGui::TextColored(Theme::kTextMuted, "Temperature sensors");
    const ImVec4 palette[] = {Theme::kAccentTemp, Theme::kAccentCpu, Theme::kAccentGpu,
                              Theme::kAccentMemory, Theme::kAccentEnergy, Theme::kAccentNetwork, Theme::kAccentClock};
    const int paletteSize = (int)(sizeof(palette) / sizeof(palette[0]));
    PushPlotTheme();
    if (ImPlot::BeginPlot("##gpu_temps", ImVec2(width, Theme::Scale(160.0f)),ImPlotFlags_NoTitle | ImPlotFlags_NoMenus |
                          ImPlotFlags_NoBoxSelect | ImPlotFlags_NoLegend)) {
        int windowSamples = std::max(1, Theme::HistoryWindowSamples);
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_None);
        ImPlot::SetupAxisFormat(ImAxis_Y1, "%g%%"); // history is normalized 0..1 -> shown as 0..100%
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 100.0, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, (double)std::max(1, windowSamples - 1), ImGuiCond_Always);
        for (size_t i = 0; i < temps.size(); ++i) {
            auto it = m.thermalSensorHistory.find(temps[i].first);
            if (it == m.thermalSensorHistory.end()) continue;
            std::vector<float> xs, ys;
            WindowedSamples(it->second, xs, ys);
            for (float& v : ys) v *= 100.0f;
            if (ys.empty()) continue;
            ImPlot::SetNextLineStyle(palette[i % paletteSize], 1.8f);
            ImPlot::PlotLine(temps[i].first.c_str(), xs.data(), ys.data(), (int)ys.size());
        }
        ImPlot::EndPlot();
    }
    PopPlotTheme();
    for (size_t i = 0; i < temps.size(); ++i) {
        char t[32]; std::snprintf(t, sizeof(t), "%.1f\xC2\xB0%s", Theme::TempForDisplay(temps[i].second), Theme::TempUnitSuffix());
        ImVec4 color = palette[i % paletteSize];
        ImGui::TextColored(color, "%s", temps[i].first.c_str());
        ImGui::SameLine(ImGui::GetCursorStartPos().x + width - ImGui::CalcTextSize(t).x - Theme::Scale(8.0f));
        ImGui::TextColored(color, "%s", t);
    }
}

// Finds the amdgpu card's PCI slot (for the read-only stats header)
// via the existing busy-% probe.
std::string LocalAmdPciSlot() {
    std::string devPath;
    if (ReadAmdGpuBusyPercent(&devPath) < 0.0f) return "";
    char real[4096];
    if (!realpath(devPath.c_str(), real)) return "";
    std::string r = real;
    return r.substr(r.find_last_of('/') + 1);
}

} // namespace

void DrawGpuDetail(const Metrics& m, float width) {
    width = std::min(width, ImGui::GetContentRegionAvail().x); // stay clear of the scrollbar
    char pct[16]; std::snprintf(pct, sizeof(pct), "%.1f%%", m.gpuPct * 100.0f);
    Widgets::DetailHeader(m.gpuName.c_str(), GpuSubtitle(m), m.gpuPct, pct, Theme::kAccentGpu, width);
    ImGui::Dummy(ImVec2(0, 8));

    // Read-only overdrive state for the stats header (fan %, GTT, clock ranges).
    static AmdOdState s_od;
    static double s_odT = -1000.0;
    if (ImGui::GetTime() - s_odT > 2.0) {
        static std::string slot = LocalAmdPciSlot();
        s_od = ReadAmdOdState(slot);
        s_odT = ImGui::GetTime();
    }

    DrawStatsHeader(m, s_od, width);
    DrawOverviewGraphs(m, width);
}
