#include "pages/performance/gpu/gpu.h"
#include "pages/performance/shared.h"
#include "pages/page_common.h"
#include "theme.h"
#include "widgets.h"
#include "gpu_ctl/gpu_ctl_ipc.h"
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

// Layout follows LACT's OC / Thermals pages: a live stats header, then
// titled sections. Non-expert users get a plain task-manager view (stats +
// graphs); Expert mode adds the Overclock and Fan Control tabs, which talk
// to gpu_ctl_daemon. Every editable value is compared against the card's
// *live* state (read straight from sysfs, see ReadAmdOdState) so a modified
// value is always visibly marked and listed in the bottom changes bar.

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
    if (ImPlot::BeginPlot("##gpu", ImVec2(width, 180), ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoLegend)) {
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
    if (ImPlot::BeginPlot("##gpu_temps", ImVec2(width, 160), ImPlotFlags_NoTitle | ImPlotFlags_NoMenus |
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
        char t[32]; std::snprintf(t, sizeof(t), "%.1f %s", Theme::TempForDisplay(temps[i].second), Theme::TempUnitSuffix());
        ImVec4 color = palette[i % paletteSize];
        ImGui::TextColored(color, "%s", temps[i].first.c_str());
        ImGui::SameLine(width - ImGui::CalcTextSize(t).x - 8.0f);
        ImGui::TextColored(color, "%s", t);
    }
}

// ---------------------------------------------------------------------------
// Control state: baseline (live hardware) vs working copy (user edits)
// ---------------------------------------------------------------------------

enum FanTab { kFanAuto = 0, kFanCurve = 1, kFanStatic = 2 };

// pp_od_clk_voltage / pp_dpm_mclk report VRAM at half the effective DDR rate;
// the UI shows the effective rate (as the stats card and LACT do) and halves
// it again when writing.
constexpr int kMemRate = 2;

struct Knobs {
    int powerCapW = 0;
    int perfLevel = GPUCTL_PERF_AUTO;
    int profile = -1;
    uint32_t sclkMask = 0, mclkMask = 0;
    int sclkMin = 0, sclkMax = 0, voltOffset = 0;
    int mclkMin = 0, mclkMax = 0; // effective (DDR) MHz = 2x sysfs, like the stats card and LACT

    int nvGpuOffset = 0, nvMemOffset = 0, nvVoltBoost = 0; // Nvidia
    int fanMode = kFanAuto;
    int fanStaticPct = 50;
    std::vector<ImVec2> curve;        // x = tempC, y = percent
    int targetTemp = 0, acousticLimit = 0, acousticTarget = 0, minPwm = 0, zeroRpmStop = 0;
    bool zeroRpm = false;
};

struct UiState {
    int gpu = -1;
    AmdOdState od;
    Knobs base, work;
    double lastBaselineRead = -1000.0;
    int tab = 0;                       // 0 Overview, 1 Overclock, 2 Fan Control
    bool pending = false;
    double pendingDeadline = 0.0;
    std::string lastError;
};
UiState g;

int PerfLevelFromSysfs(const std::string& s) {
    if (s == "low") return GPUCTL_PERF_LOW;
    if (s == "high") return GPUCTL_PERF_HIGH;
    if (s == "manual") return GPUCTL_PERF_MANUAL;
    return GPUCTL_PERF_AUTO;
}

std::vector<ImVec2> DefaultCurve(int points) {
    // LACT's default curve (30%@40C .. 100%@80C), resampled to `points`.
    const ImVec2 lact[5] = {{40, 30}, {50, 35}, {60, 50}, {70, 75}, {80, 100}};
    std::vector<ImVec2> out;
    for (int i = 0; i < points; ++i) out.push_back(lact[std::min(4, i * 4 / std::max(1, points - 1))]);
    return out;
}

// Baseline = what the card is doing right now (+ the daemon's confirmed
// fan mode, since "static vs curve" isn't recoverable from sysfs alone).
Knobs ReadBaseline(int gpu, const GpuCtlCapsPOD& caps, AmdOdState* odOut) {
    Knobs k;
    AmdOdState od = ReadAmdOdState(caps.pciSlot);
    GpuCtlConfigPOD confirmed;
    if (!GpuCtlIpc::GetConfig(gpu, confirmed)) gpuctl_config_init_unset(&confirmed);

    if (od.valid) {
        k.powerCapW = od.hasPowerCap ? od.powerCapW : caps.powerCapDefaultW;
        k.perfLevel = PerfLevelFromSysfs(od.perfLevel);
        k.profile = od.activeProfile;
        k.sclkMin = od.sclkMin; k.sclkMax = od.sclkMax;
        k.mclkMin = od.mclkMin * kMemRate; k.mclkMax = od.mclkMax * kMemRate;
        k.voltOffset = od.voltOffsetMv;
        auto allMask = [](size_t n) { return n == 0 ? 0u : (n >= 32 ? 0xffffffffu : (1u << n) - 1); };
        k.sclkMask = confirmed.corePstatesEnabledMask ? confirmed.corePstatesEnabledMask : allMask(od.sclkLevelsMhz.size());
        k.mclkMask = confirmed.memPstatesEnabledMask ? confirmed.memPstatesEnabledMask : allMask(od.mclkLevelsMhz.size());
        k.targetTemp = od.targetTempC; k.acousticLimit = od.acousticLimitRpm; k.acousticTarget = od.acousticTargetRpm;
        k.minPwm = od.minPwmPct; k.zeroRpm = od.zeroRpm; k.zeroRpmStop = od.zeroRpmStopC;

        // PMFW reports an all-zero curve while its stock (automatic) curve is active.
        bool customCurve = false;
        for (int i = 0; i < od.fanCurvePoints; ++i) if (od.fanCurvePct[i] > 0) customCurve = true;
        if (od.hasFanCurve && customCurve) {
            for (int i = 0; i < od.fanCurvePoints; ++i) k.curve.emplace_back((float)od.fanCurveTemp[i], (float)od.fanCurvePct[i]);
            bool flat = true;
            for (int i = 1; i < od.fanCurvePoints; ++i) if (od.fanCurvePct[i] != od.fanCurvePct[0]) flat = false;
            k.fanMode = flat && confirmed.fan.mode == GPUCTL_FAN_MODE_STATIC ? kFanStatic : kFanCurve;
            k.fanStaticPct = od.fanCurvePct[0];
        } else if (!od.hasFanCurve && od.pwmEnable == 1) {
            k.fanMode = confirmed.fan.mode == GPUCTL_FAN_MODE_CURVE ? kFanCurve : kFanStatic;
            k.fanStaticPct = (int)std::lround(std::max(0.0f, od.fanPct));
        }
        if (k.curve.empty()) {
            if (confirmed.fan.curveCount > 0 && !od.hasFanCurve)
                for (uint32_t i = 0; i < confirmed.fan.curveCount; ++i) k.curve.emplace_back(confirmed.fan.curve[i].tempC, confirmed.fan.curve[i].speed01 * 100.0f);
            else k.curve = DefaultCurve(od.hasFanCurve ? std::max(2, od.fanCurvePoints) : 5);
        }
    } else {
        // Nvidia (or anything without amdgpu sysfs): the daemon's confirmed config is the best baseline we have.
        k.powerCapW = confirmed.powerCapWatts != GPUCTL_UNSET ? confirmed.powerCapWatts : caps.powerCapDefaultW;
        k.nvGpuOffset = confirmed.clocks.gpuClockOffsetCount ? confirmed.clocks.gpuClockOffsets[0].offsetMhz : 0;
        k.nvMemOffset = confirmed.clocks.memClockOffsetCount ? confirmed.clocks.memClockOffsets[0].offsetMhz : 0;
        k.nvVoltBoost = confirmed.clocks.voltageBoostPct != GPUCTL_UNSET ? confirmed.clocks.voltageBoostPct : 0;
        k.fanMode = confirmed.fan.mode != GPUCTL_UNSET && confirmed.fan.enabled ? kFanStatic : kFanAuto;
        k.fanStaticPct = (int)std::lround(confirmed.fan.staticSpeed01 * 100.0f);
        if (k.fanStaticPct <= 0) k.fanStaticPct = 50;
        k.curve = DefaultCurve(5);
    }
    if (odOut) *odOut = od;
    return k;
}

bool CurveEq(const std::vector<ImVec2>& a, const std::vector<ImVec2>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::lround(a[i].x) != std::lround(b[i].x) || std::lround(a[i].y) != std::lround(b[i].y)) return false;
    return true;
}

bool FanDirty(const Knobs& w, const Knobs& b) {
    if (w.fanMode != b.fanMode) return true;
    if (w.fanMode == kFanCurve) return !CurveEq(w.curve, b.curve);
    if (w.fanMode == kFanStatic) return w.fanStaticPct != b.fanStaticPct;
    return false;
}
bool PmfwDirty(const Knobs& w, const Knobs& b) {
    return w.targetTemp != b.targetTemp || w.acousticLimit != b.acousticLimit || w.acousticTarget != b.acousticTarget ||
           w.minPwm != b.minPwm || w.zeroRpm != b.zeroRpm || w.zeroRpmStop != b.zeroRpmStop;
}

const char* kPerfLabels[] = {"Automatic", "Lowest Clocks", "Highest Clocks", "Manual"};
const char* kPerfHelp[] = {"Automatically adjust GPU and VRAM clocks. (Default)",
                           "Always use the lowest clockspeeds for GPU and VRAM.",
                           "Always use the highest clockspeeds for GPU and VRAM.",
                           "Manual performance control (power states, profile mode)."};
const char* kFanLabels[] = {"Automatic", "Curve", "Static"};

struct Change { int tab; std::string text; };

// Human-readable list of every modified value — drives the changes bar,
// the per-tab counters and the section markers.
std::vector<Change> Changes(const Knobs& w, const Knobs& b, bool isAmd) {
    std::vector<Change> c;
    char s[96];
    auto add = [&](int tab, const char* name, int from, int to, const char* unit) {
        if (from == to) return;
        std::snprintf(s, sizeof(s), "%s %d \xE2\x86\x92 %d%s", name, from, to, unit);
        c.push_back({tab, s});
    };
    add(1, "Power limit", b.powerCapW, w.powerCapW, " W");
    if (w.perfLevel != b.perfLevel) c.push_back({1, std::string("Performance \xE2\x86\x92 ") + kPerfLabels[w.perfLevel]});
    if (w.profile != b.profile) c.push_back({1, "Power profile mode"});
    if (w.sclkMask != b.sclkMask || w.mclkMask != b.mclkMask) c.push_back({1, "Power states"});
    if (isAmd) {
        add(1, "Max GPU clock", b.sclkMax, w.sclkMax, " MHz");
        add(1, "Min GPU clock", b.sclkMin, w.sclkMin, " MHz");
        add(1, "Max VRAM clock", b.mclkMax, w.mclkMax, " MHz");
        add(1, "Min VRAM clock", b.mclkMin, w.mclkMin, " MHz");
        add(1, "Voltage offset", b.voltOffset, w.voltOffset, " mV");
    } else {
        add(1, "GPU clock offset", b.nvGpuOffset, w.nvGpuOffset, " MHz");
        add(1, "Memory clock offset", b.nvMemOffset, w.nvMemOffset, " MHz");
        add(1, "Voltage boost", b.nvVoltBoost, w.nvVoltBoost, "%");
    }
    if (w.fanMode != b.fanMode) c.push_back({2, std::string("Fan \xE2\x86\x92 ") + kFanLabels[w.fanMode]});
    else if (w.fanMode == kFanCurve && !CurveEq(w.curve, b.curve)) c.push_back({2, "Fan curve"});
    else if (w.fanMode == kFanStatic) add(2, "Static fan speed", b.fanStaticPct, w.fanStaticPct, "%");
    add(2, "Target temperature", b.targetTemp, w.targetTemp, "\xC2\xB0" "C");
    add(2, "Acoustic limit", b.acousticLimit, w.acousticLimit, " RPM");
    add(2, "Acoustic target", b.acousticTarget, w.acousticTarget, " RPM");
    add(2, "Minimum fan speed", b.minPwm, w.minPwm, "%");
    if (w.zeroRpm != b.zeroRpm) c.push_back({2, w.zeroRpm ? "Zero RPM on" : "Zero RPM off"});
    add(2, "Zero RPM stop temp", b.zeroRpmStop, w.zeroRpmStop, "\xC2\xB0" "C");
    return c;
}

// Builds the config to send: the daemon's already-confirmed config with
// only the modified fields overlaid — so confirming accumulates settings
// instead of each Apply forgetting the previous one.
GpuCtlConfigPOD BuildConfig(int gpu, const Knobs& w, const Knobs& b, bool isAmd) {
    GpuCtlConfigPOD cfg;
    if (!GpuCtlIpc::GetConfig(gpu, cfg)) gpuctl_config_init_unset(&cfg);
    if (w.powerCapW != b.powerCapW) cfg.powerCapWatts = w.powerCapW;
    bool manualBits = w.profile != b.profile || w.sclkMask != b.sclkMask || w.mclkMask != b.mclkMask;
    if (w.perfLevel != b.perfLevel || manualBits) cfg.performanceLevel = w.perfLevel;
    if (w.profile != b.profile) cfg.powerProfileModeIndex = w.profile;
    if (w.sclkMask != b.sclkMask) cfg.corePstatesEnabledMask = w.sclkMask;
    if (w.mclkMask != b.mclkMask) cfg.memPstatesEnabledMask = w.mclkMask;
    GpuCtlClocksPOD& ck = cfg.clocks;
    if (isAmd) {
        if (w.sclkMin != b.sclkMin) ck.minCoreClockMhz = w.sclkMin;
        if (w.sclkMax != b.sclkMax) ck.maxCoreClockMhz = w.sclkMax;
        if (w.mclkMin != b.mclkMin) ck.minMemClockMhz = w.mclkMin / kMemRate; // back to the sysfs (half) rate
        if (w.mclkMax != b.mclkMax) ck.maxMemClockMhz = w.mclkMax / kMemRate;
        if (w.voltOffset != b.voltOffset) ck.voltageOffsetMv = w.voltOffset;
    } else {
        if (w.nvGpuOffset != b.nvGpuOffset) { ck.gpuClockOffsetCount = 1; ck.gpuClockOffsets[0] = {0, w.nvGpuOffset}; }
        if (w.nvMemOffset != b.nvMemOffset) { ck.memClockOffsetCount = 1; ck.memClockOffsets[0] = {0, w.nvMemOffset}; }
        if (w.nvVoltBoost != b.nvVoltBoost) ck.voltageBoostPct = w.nvVoltBoost;
    }
    if (FanDirty(w, b)) {
        cfg.fan.enabled = w.fanMode != kFanAuto;
        cfg.fan.mode = w.fanMode == kFanStatic ? GPUCTL_FAN_MODE_STATIC : GPUCTL_FAN_MODE_CURVE;
        cfg.fan.staticSpeed01 = w.fanStaticPct / 100.0f;
        cfg.fan.curveCount = (uint32_t)std::min(w.curve.size(), (size_t)GPUCTL_MAX_FAN_POINTS);
        for (uint32_t i = 0; i < cfg.fan.curveCount; ++i) cfg.fan.curve[i] = {w.curve[i].x, w.curve[i].y / 100.0f};
    } else if (cfg.fan.mode != GPUCTL_UNSET && !cfg.fan.enabled) {
        cfg.fan.mode = GPUCTL_UNSET; // confirmed "auto" fan: don't re-reset the curve on every apply
    }
    if (PmfwDirty(w, b)) {
        GpuCtlPmfwPOD& p = cfg.pmfw;
        p.set = true;
        if (w.targetTemp != b.targetTemp) p.targetTempC = w.targetTemp;
        if (w.acousticLimit != b.acousticLimit) p.acousticLimitRpm = w.acousticLimit;
        if (w.acousticTarget != b.acousticTarget) p.acousticTargetRpm = w.acousticTarget;
        if (w.minPwm != b.minPwm) p.minimumPwmPct = w.minPwm;
        if (w.zeroRpm != b.zeroRpm || w.zeroRpmStop != b.zeroRpmStop) { p.zeroRpmSet = true; p.zeroRpm = w.zeroRpm; }
        if (w.zeroRpmStop != b.zeroRpmStop) p.zeroRpmThresholdC = w.zeroRpmStop;
    }
    return cfg;
}

void Reload(int gpu, const GpuCtlCapsPOD& caps) {
    g.gpu = gpu;
    g.base = ReadBaseline(gpu, caps, &g.od);
    g.work = g.base;
    g.lastBaselineRead = ImGui::GetTime();
}

// ---------------------------------------------------------------------------
// Section chrome
// ---------------------------------------------------------------------------

bool DangerButton(const char* label, float width) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(Theme::kAccentClock.x, Theme::kAccentClock.y, Theme::kAccentClock.z, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::kAccentClock);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    bool clicked = ImGui::Button(label, ImVec2(width, 0));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    return clicked;
}

// Bold section title (LACT's "Power Usage Limit"), with a dot when modified,
// and an optional right-aligned button. Returns true if the button was clicked.
bool SectionTitle(const char* title, bool modified, float width, const char* button = nullptr, bool danger = false) {
    ImGui::Dummy(ImVec2(0, Theme::Scale(12.0f)));
    ImGui::PushFont(Theme::LoadedFonts.bodyBold);
    ImGui::SetWindowFontScale(1.2f);
    ImGui::AlignTextToFramePadding();
    if (modified) ImGui::TextColored(Theme::kAccentTemp, "\xE2\x97\x8F %s", title);
    else ImGui::TextColored(Theme::kTextPrimary, "%s", title);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();
    bool clicked = false;
    if (button) {
        float bw = ImGui::CalcTextSize(button).x + Theme::Scale(28.0f);
        ImGui::SameLine(width - bw);
        clicked = danger ? DangerButton(button, bw) : ImGui::Button(button, ImVec2(bw, 0));
    }
    ImGui::Dummy(ImVec2(0, Theme::Scale(4.0f)));
    return clicked;
}

void NotActiveHint(const char* what) {
    ImGui::Dummy(ImVec2(0, Theme::Scale(10.0f)));
    Widgets::StatusPill("NOT ACTIVE", Theme::kAccentClock);
    ImGui::TextColored(Theme::kTextSecondary, "%s needs vitals-gpud (the GPU control daemon), which isn't running.", what);
    ImGui::TextColored(Theme::kTextSecondary, "Activate it in Settings \xE2\x86\x92 Daemons.");
}

void LactWarning() {
    struct stat st;
    if (stat("/run/lactd.sock", &st) != 0) return;
    ImGui::Dummy(ImVec2(0, Theme::Scale(6.0f)));
    Widgets::StatusPill("LACT RUNNING", Theme::kAccentTemp);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(Theme::kAccentTemp, "lactd controls the same settings \xE2\x80\x94 whichever applies last wins. Stop one: sudo systemctl disable --now lactd");
}

void ModLabel(const char* label, bool modified) {
    ImGui::AlignTextToFramePadding();
    if (modified) ImGui::TextColored(Theme::kAccentTemp, "\xE2\x97\x8F %s", label);
    else ImGui::TextColored(Theme::kTextPrimary, "%s", label);
}

// ---------------------------------------------------------------------------
// Overclock tab
// ---------------------------------------------------------------------------

void DrawOverclock(const GpuCtlCapsPOD& caps, bool isAmd, float width) {
    Knobs& w = g.work;
    const Knobs& b = g.base;
    const AmdOdState& od = g.od;

    // ---- Power Usage Limit ----
    int capMin = od.hasPowerCap ? od.powerCapRange.min : caps.powerCapMinW;
    int capMax = od.hasPowerCap ? od.powerCapRange.max : caps.powerCapMaxW;
    int capDef = od.hasPowerCap ? od.powerCapDefaultW : caps.powerCapDefaultW;
    if (capMax > 0) {
        if (SectionTitle("Power Usage Limit", w.powerCapW != b.powerCapW, width, "Default")) w.powerCapW = capDef;
        BeginPanel("##power", width);
        char lbl[48]; std::snprintf(lbl, sizeof(lbl), "%d/%d W###powerrow", w.powerCapW, capMax);
        Widgets::EditRowInt(lbl, &w.powerCapW, capMin, capMax, b.powerCapW, ImGui::GetContentRegionAvail().x, " W");
        EndPanel();
    }

    // ---- Performance ----
    if (isAmd) {
        SectionTitle("Performance", w.perfLevel != b.perfLevel || w.profile != b.profile, width);
        BeginPanel("##perf", width);
        float avail = ImGui::GetContentRegionAvail().x;
        float comboW = Theme::Scale(200.0f);
        ModLabel("Performance Level", w.perfLevel != b.perfLevel);
        const char* help = kPerfHelp[w.perfLevel];
        ImGui::SameLine(avail - comboW - ImGui::CalcTextSize(help).x - Theme::Scale(12.0f));
        ImGui::TextColored(Theme::kTextSecondary, "%s", help);
        ImGui::SameLine(avail - comboW);
        ImGui::SetNextItemWidth(comboW);
        if (ImGui::BeginCombo("##perflevel", kPerfLabels[w.perfLevel])) {
            for (int i = 0; i < 4; ++i) if (ImGui::Selectable(kPerfLabels[i], w.perfLevel == i)) w.perfLevel = i;
            ImGui::EndCombo();
        }

        if (caps.profileModeCount > 0) {
            bool manual = w.perfLevel == GPUCTL_PERF_MANUAL;
            ModLabel("Power Profile Mode", w.profile != b.profile);
            if (!manual) {
                const char* note = "Requires the Manual performance level";
                ImGui::SameLine(avail - comboW - ImGui::CalcTextSize(note).x - Theme::Scale(12.0f));
                ImGui::TextColored(Theme::kTextMuted, "%s", note);
            }
            ImGui::SameLine(avail - comboW);
            ImGui::SetNextItemWidth(comboW);
            ImGui::BeginDisabled(!manual);
            const char* preview = w.profile >= 0 && w.profile < (int)caps.profileModeCount ? caps.profileModeNames[w.profile] : "-";
            if (ImGui::BeginCombo("##profile", preview)) {
                for (uint32_t i = 0; i < caps.profileModeCount; ++i)
                    if (caps.profileModeNames[i][0] && ImGui::Selectable(caps.profileModeNames[i], w.profile == (int)i)) w.profile = (int)i;
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
        }
        EndPanel();

        // ---- Power States ----
        if (!od.sclkLevelsMhz.empty() || !od.mclkLevelsMhz.empty()) {
            ImGui::Dummy(ImVec2(0, Theme::Scale(8.0f)));
            bool modified = w.sclkMask != b.sclkMask || w.mclkMask != b.mclkMask;
            if (modified) ImGui::PushStyleColor(ImGuiCol_Text, Theme::kAccentTemp);
            bool open = ImGui::CollapsingHeader(modified ? "\xE2\x97\x8F Power States###pstates" : "Power States###pstates");
            if (modified) ImGui::PopStyleColor();
            if (open) {
                bool manual = w.perfLevel == GPUCTL_PERF_MANUAL;
                if (!manual) ImGui::TextColored(Theme::kTextMuted, "Set the performance level to Manual to restrict power states.");
                ImGui::BeginDisabled(!manual);
                if (ImGui::BeginTable("pstates", 2, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextRow();
                    auto column = [](const char* title, const std::vector<int>& levels, uint32_t* mask) {
                        ImGui::TableNextColumn();
                        ImGui::TextColored(Theme::kTextSecondary, "%s", title);
                        for (size_t i = 0; i < levels.size() && i < 32; ++i) {
                            bool on = (*mask >> i) & 1u;
                            char l[64]; std::snprintf(l, sizeof(l), "%zu: %d MHz##%s%zu", i, levels[i], title, i);
                            if (ImGui::Checkbox(l, &on)) *mask = on ? (*mask | (1u << i)) : (*mask & ~(1u << i));
                        }
                    };
                    column("GPU", od.sclkLevelsMhz, &w.sclkMask);
                    std::vector<int> vramLevels = od.mclkLevelsMhz;
                    for (int& l : vramLevels) l *= kMemRate;
                    column("VRAM", vramLevels, &w.mclkMask);
                    ImGui::EndTable();
                }
                ImGui::EndDisabled();
            }
        }
    }

    // ---- Clockspeed and Voltage ----
    bool clocksModified = isAmd ? (w.sclkMin != b.sclkMin || w.sclkMax != b.sclkMax || w.mclkMin != b.mclkMin || w.mclkMax != b.mclkMax || w.voltOffset != b.voltOffset)
                                : (w.nvGpuOffset != b.nvGpuOffset || w.nvMemOffset != b.nvMemOffset || w.nvVoltBoost != b.nvVoltBoost);
    if (SectionTitle("Clockspeed and Voltage", clocksModified, width, "Reset Now", true)) {
        GpuCtlIpc::ResetToDefault(g.gpu);
        g.pending = false;
        Reload(g.gpu, caps);
    }
    ImGui::TextColored(Theme::kAccentEnergy, "Changing these values may lead to system instability and can potentially damage your hardware!");
    ImGui::Dummy(ImVec2(0, Theme::Scale(4.0f)));

    float gap = Theme::Scale(10.0f);
    float half = (width - gap) / 2.0f;
    if (isAmd) {
        if (od.hasSclk) {
            BeginPanel("##gpuclk", half);
            float a = ImGui::GetContentRegionAvail().x;
            Widgets::EditRowInt("Maximum GPU Clock (MHz)", &w.sclkMax, od.sclkRange.min, od.sclkRange.max, b.sclkMax, a, " MHz");
            Widgets::EditRowInt("Minimum GPU Clock (MHz)", &w.sclkMin, od.sclkRange.min, od.sclkRange.max, b.sclkMin, a, " MHz");
            EndPanel();
        }
        if (od.hasMclk) {
            if (od.hasSclk) ImGui::SameLine(0, gap);
            BeginPanel("##memclk", half);
            float a = ImGui::GetContentRegionAvail().x;
            int memLo = od.mclkRange.min * kMemRate, memHi = od.mclkRange.max * kMemRate;
            Widgets::EditRowInt("Maximum VRAM Clock (MHz)", &w.mclkMax, memLo, memHi, b.mclkMax, a, " MHz");
            Widgets::EditRowInt("Minimum VRAM Clock (MHz)", &w.mclkMin, memLo, memHi, b.mclkMin, a, " MHz");
            EndPanel();
        }
        if (od.hasVoltOffset) {
            ImGui::Dummy(ImVec2(0, Theme::Scale(4.0f)));
            BeginPanel("##volt", half);
            Widgets::EditRowInt("GPU voltage offset (mV)", &w.voltOffset, od.voltOffsetRange.min, od.voltOffsetRange.max, b.voltOffset, ImGui::GetContentRegionAvail().x, " mV");
            EndPanel();
        }
        if (!od.hasSclk && !od.hasMclk)
            ImGui::TextColored(Theme::kTextMuted, "Overdrive isn't enabled for this GPU (amdgpu.ppfeaturemask kernel parameter).");
    } else {
        BeginPanel("##nvclk", width);
        float a = ImGui::GetContentRegionAvail().x;
        Widgets::EditRowInt("GPU clock offset (MHz)", &w.nvGpuOffset, caps.coreClockMinMhz, caps.coreClockMaxMhz, b.nvGpuOffset, a, " MHz");
        Widgets::EditRowInt("Memory clock offset (MHz)", &w.nvMemOffset, caps.memClockMinMhz, caps.memClockMaxMhz, b.nvMemOffset, a, " MHz");
        Widgets::EditRowInt("Voltage boost (%)", &w.nvVoltBoost, 0, 100, b.nvVoltBoost, a, "%");
        ImGui::TextColored(Theme::kTextMuted, "Voltage boost is experimental \xE2\x80\x94 routed through an undocumented NvAPI call.");
        EndPanel();
    }
}

// ---------------------------------------------------------------------------
// Fan Control tab
// ---------------------------------------------------------------------------

void ZeroRpmRows(float avail) {
    Knobs& w = g.work;
    const Knobs& b = g.base;
    const AmdOdState& od = g.od;
    if (od.hasZeroRpm) {
        ModLabel("Zero RPM", w.zeroRpm != b.zeroRpm);
        ImGui::SameLine(avail - ImGui::GetFrameHeight() * 1.9f);
        Widgets::ToggleSwitch("##zerorpm", &w.zeroRpm);
    }
    if (od.hasZeroRpmStop)
        Widgets::EditRowInt("Zero RPM stop temperature (\xC2\xB0" "C)", &w.zeroRpmStop, od.zeroRpmStopRange.min, od.zeroRpmStopRange.max, b.zeroRpmStop, avail, "\xC2\xB0" "C");
}

void DrawFanControl(const GpuCtlCapsPOD& caps, bool isAmd, float width) {
    Knobs& w = g.work;
    const Knobs& b = g.base;
    const AmdOdState& od = g.od;

    SectionTitle("Fan Control", FanDirty(w, b) || PmfwDirty(w, b), width);
    BeginPanel("##fan", width);
    float avail = ImGui::GetContentRegionAvail().x;

    // Nvidia's backend has no curve (daemon rejects it) — offer Automatic/Static only.
    const char* nvLabels[] = {"Automatic", "Static"};
    int sel = isAmd ? w.fanMode : (w.fanMode == kFanStatic ? 1 : 0);
    if (Widgets::SegmentedTabs("fanmode", isAmd ? kFanLabels : nvLabels, isAmd ? 3 : 2, &sel, avail))
        w.fanMode = isAmd ? sel : (sel == 1 ? kFanStatic : kFanAuto);
    ImGui::Dummy(ImVec2(0, Theme::Scale(6.0f)));

    if (w.fanMode == kFanAuto) {
        bool any = false;
        if (od.hasTargetTemp) { any = true; Widgets::EditRowInt("Target temperature (\xC2\xB0" "C)", &w.targetTemp, od.targetTempRange.min, od.targetTempRange.max, b.targetTemp, avail, "\xC2\xB0" "C"); }
        if (od.hasAcousticLimit) { any = true; Widgets::EditRowInt("Acoustic Limit (RPM)", &w.acousticLimit, od.acousticLimitRange.min, od.acousticLimitRange.max, b.acousticLimit, avail, " RPM"); }
        if (od.hasAcousticTarget) { any = true; Widgets::EditRowInt("Acoustic Target (RPM)", &w.acousticTarget, od.acousticTargetRange.min, od.acousticTargetRange.max, b.acousticTarget, avail, " RPM"); }
        if (od.hasMinPwm) { any = true; Widgets::EditRowInt("Minimum Fan Speed (%)", &w.minPwm, od.minPwmRange.min, od.minPwmRange.max, b.minPwm, avail, "%"); }
        ZeroRpmRows(avail);
        if (!any && !od.hasZeroRpm) ImGui::TextColored(Theme::kTextSecondary, "The GPU firmware controls the fan automatically.");
        if (isAmd) {
            ImGui::Dummy(ImVec2(0, Theme::Scale(4.0f)));
            float bw = ImGui::CalcTextSize("Reset Now").x + Theme::Scale(28.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - bw);
            if (DangerButton("Reset Now", bw)) {
                GpuCtlIpc::ResetToDefault(g.gpu);
                g.pending = false;
                Reload(g.gpu, caps);
            }
        }
    } else if (w.fanMode == kFanCurve) {
        ImVec2 tRange = od.hasFanCurve ? ImVec2((float)od.fanCurveTempRange.min, (float)od.fanCurveTempRange.max) : ImVec2(20, 100);
        ImVec2 sRange = od.hasFanCurve ? ImVec2((float)od.fanCurvePctRange.min, 100.0f) : ImVec2(0, 100);
        Widgets::CurveEditor("##fancurve", w.curve, ImVec2(avail, Theme::Scale(320.0f)), tRange, sRange, Theme::kAccentGpu,
                             "Temperature (\xC2\xB0" "C)", "Speed", "%g%%", "%.0f%% at %.0f\xC2\xB0" "C");
        for (ImVec2& p : w.curve) { p.x = std::round(p.x); p.y = std::round(p.y); } // whole degrees / percent, like the driver
        if (od.hasFanCurve) ImGui::TextColored(Theme::kTextMuted, "This GPU's firmware uses the junction (hotspot) temperature and exactly %d curve points.", od.fanCurvePoints);
        float bw = ImGui::CalcTextSize("Default").x + Theme::Scale(28.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - bw);
        if (ImGui::Button("Default", ImVec2(bw, 0))) w.curve = DefaultCurve((int)w.curve.size());
        ZeroRpmRows(avail);
    } else {
        Widgets::EditRowInt("Static Speed (%)", &w.fanStaticPct, 0, 100, b.fanStaticPct, avail, "%");
    }
    EndPanel();
}

// ---------------------------------------------------------------------------
// Changes / confirm bar — pinned to the bottom of the page's window
// ---------------------------------------------------------------------------

void DrawChangesBar(const std::vector<Change>& changes, const GpuCtlCapsPOD& caps, bool isAmd) {
    if (changes.empty() && !g.pending && g.lastError.empty()) return;
    ImVec2 hostPos = ImGui::GetWindowPos(), hostSize = ImGui::GetWindowSize();
    float h = ImGui::GetFrameHeight() + Theme::Scale(24.0f);
    ImGui::SetNextWindowPos(ImVec2(hostPos.x, hostPos.y + hostSize.y - h));
    ImGui::SetNextWindowSize(ImVec2(hostSize.x, h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::kBgPanel);
    ImGui::PushStyleColor(ImGuiCol_Border, g.pending ? Theme::kAccentTemp : Theme::kAccentGpu);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Theme::Scale(16.0f), Theme::Scale(12.0f)));
    ImGui::Begin("##gpu_changes_bar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
    float avail = ImGui::GetContentRegionAvail().x;
    float bw = Theme::Scale(110.0f);
    float buttonsX = avail - bw * 2 - ImGui::GetStyle().ItemSpacing.x;
    ImGui::AlignTextToFramePadding();

    if (g.pending) {
        double remaining = g.pendingDeadline - ImGui::GetTime();
        ImGui::TextColored(Theme::kAccentTemp, "Applied \xE2\x80\x94 keep these settings within %.0f s or they revert automatically", std::max(0.0, remaining));
        ImGui::SameLine(buttonsX);
        if (ImGui::Button("Revert", ImVec2(bw, 0))) {
            GpuCtlIpc::Revert(g.gpu);
            g.pending = false;
            Reload(g.gpu, caps);
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(Theme::kAccentOk.x, Theme::kAccentOk.y, Theme::kAccentOk.z, 0.35f));
        if (ImGui::Button("Keep", ImVec2(bw, 0))) {
            GpuCtlConfigPOD unused; gpuctl_config_init_unset(&unused); // daemon persists its own armed config
            if (!GpuCtlIpc::Confirm(g.gpu, unused)) g.lastError = "Keep failed \xE2\x80\x94 the daemon had already reverted.";
            g.pending = false;
            Reload(g.gpu, caps);
        }
        ImGui::PopStyleColor();
    } else if (!changes.empty()) {
        std::string list;
        for (const Change& c : changes) list += (list.empty() ? "" : "   \xC2\xB7   ") + c.text;
        ImGui::TextColored(Theme::kAccentTemp, "\xE2\x97\x8F %zu change%s:", changes.size(), changes.size() == 1 ? "" : "s");
        ImGui::SameLine();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float listW = buttonsX - ImGui::GetCursorPosX() - Theme::Scale(12.0f);
        ImGui::PushClipRect(p, ImVec2(p.x + listW, p.y + ImGui::GetFrameHeight()), true);
        ImGui::TextColored(Theme::kTextPrimary, "%s", list.c_str());
        ImGui::PopClipRect();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", list.c_str());
        ImGui::SameLine(buttonsX);
        if (ImGui::Button("Discard", ImVec2(bw, 0))) { g.work = g.base; g.lastError.clear(); }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(Theme::kAccentGpu.x, Theme::kAccentGpu.y, Theme::kAccentGpu.z, 0.55f));
        if (ImGui::Button("Apply", ImVec2(bw, 0))) {
            uint32_t deadline = 0;
            g.lastError.clear();
            if (GpuCtlIpc::SetConfig(g.gpu, BuildConfig(g.gpu, g.work, g.base, isAmd), &deadline, &g.lastError)) {
                g.pending = true;
                g.pendingDeadline = ImGui::GetTime() + (double)deadline;
                g.base = g.work; // these values are now live; the countdown bar takes over
            }
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::TextColored(Theme::kAccentClock, "%s", g.lastError.c_str());
        ImGui::SameLine(avail - bw);
        if (ImGui::Button("Dismiss", ImVec2(bw, 0))) g.lastError.clear();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// Finds the amdgpu card's PCI slot without the daemon (for the read-only
// stats header in task-manager mode), via the existing busy-% probe.
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

    bool daemon = m.gpuCtl.daemonReachable && m.gpuCtl.gpuCount > 0;
    int gpuIndex = daemon ? std::clamp(g.gpu < 0 ? 0 : g.gpu, 0, (int)m.gpuCtl.gpuCount - 1) : -1;

    // Read-only overdrive state for the stats header (fan %, GTT) when the
    // daemon isn't available to tell us the card's slot.
    static AmdOdState s_localOd;
    static double s_localOdT = -1000.0;
    if (!daemon && ImGui::GetTime() - s_localOdT > 2.0) {
        static std::string slot = LocalAmdPciSlot();
        s_localOd = ReadAmdOdState(slot);
        s_localOdT = ImGui::GetTime();
    }

    if (daemon) {
        const GpuCtlCapsPOD& caps = m.gpuCtl.gpus[gpuIndex];
        if (gpuIndex != g.gpu) Reload(gpuIndex, caps);
        bool dirty = !Changes(g.work, g.base, caps.vendor == GPUCTL_VENDOR_AMD).empty();
        // Refresh the baseline from hardware every ~2s while nothing is edited or pending.
        if (!dirty && !g.pending && ImGui::GetTime() - g.lastBaselineRead > 2.0) Reload(gpuIndex, caps);
        if (g.pending && ImGui::GetTime() > g.pendingDeadline) { // daemon reverted on its own
            g.pending = false;
            Reload(gpuIndex, caps);
        }
    }
    const AmdOdState& od = daemon ? g.od : s_localOd;

    // Non-expert: plain task-manager view.
    if (!Theme::ExpertMode) {
        DrawStatsHeader(m, od, width);
        DrawOverviewGraphs(m, width);
        return;
    }

    // Expert: Overview | Overclock | Fan Control
    bool isAmd = daemon && m.gpuCtl.gpus[gpuIndex].vendor == GPUCTL_VENDOR_AMD;
    std::vector<Change> changes = daemon ? Changes(g.work, g.base, isAmd) : std::vector<Change>{};

    if (daemon && m.gpuCtl.gpuCount > 1) {
        ImGui::SetNextItemWidth(Theme::Scale(260.0f));
        if (ImGui::BeginCombo("##gpuctl_select", m.gpuCtl.gpus[gpuIndex].name)) {
            for (uint32_t i = 0; i < m.gpuCtl.gpuCount; ++i)
                if (ImGui::Selectable(m.gpuCtl.gpus[i].name, (int)i == gpuIndex) && (int)i != gpuIndex) Reload((int)i, m.gpuCtl.gpus[i]);
            ImGui::EndCombo();
        }
    }

    int counts[3] = {0, 0, 0};
    for (const Change& c : changes) counts[c.tab]++;
    char l1[32], l2[32];
    if (counts[1]) std::snprintf(l1, sizeof(l1), "Overclock (%d)", counts[1]); else std::snprintf(l1, sizeof(l1), "Overclock");
    if (counts[2]) std::snprintf(l2, sizeof(l2), "Fan Control (%d)", counts[2]); else std::snprintf(l2, sizeof(l2), "Fan Control");
    const char* tabLabels[] = {"Overview", l1, l2};
    Widgets::SegmentedTabs("gputabs", tabLabels, 3, &g.tab, width);
    ImGui::Dummy(ImVec2(0, Theme::Scale(8.0f)));

    DrawStatsHeader(m, od, width);

    if (g.tab == 0) {
        DrawOverviewGraphs(m, width);
    } else if (!daemon) {
        NotActiveHint(g.tab == 1 ? "Overclocking" : "Fan control");
    } else {
        LactWarning();
        const GpuCtlCapsPOD& c = m.gpuCtl.gpus[gpuIndex];
        if (g.tab == 1) DrawOverclock(c, isAmd, width);
        else DrawFanControl(c, isAmd, width);
        changes = Changes(g.work, g.base, isAmd); // include this frame's edits
    }

    if (daemon) {
        // Room so the pinned bar never covers the last row.
        ImGui::Dummy(ImVec2(0, ImGui::GetFrameHeight() + Theme::Scale(40.0f)));
        DrawChangesBar(changes, m.gpuCtl.gpus[gpuIndex], isAmd);
    }
}

void DrawGpuControlsSection(const Metrics&, float) {
    // Superseded by the Overclock / Fan Control tabs in DrawGpuDetail.
}
