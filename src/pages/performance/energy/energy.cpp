#include "pages/performance/energy/energy.h"
#include "pages/page_common.h"
#include "theme.h"
#include "widgets.h"

#include <cstdio>

void DrawEnergyDetail(const Metrics& m, float width) {
    char valueText[16];
    char subtitle[64];
    float headerPct;
    if (m.hasBattery) {
        std::snprintf(valueText, sizeof(valueText), "%.0f%%", m.batteryPct * 100.0f);
        std::snprintf(subtitle, sizeof(subtitle), "%s - %s", m.batteryStatus.c_str(), m.powerSource.c_str());
        headerPct = m.batteryPct;
    } else {
        std::snprintf(valueText, sizeof(valueText), "%.0f W", m.energyW);
        std::snprintf(subtitle, sizeof(subtitle), "%s - %s", m.thermalState.c_str(), m.powerSource.c_str());
        headerPct = m.energyW / 200.0f;
    }
    Widgets::DetailHeader(m.hasBattery ? "Battery" : "Energy", subtitle, headerPct, valueText, Theme::kAccentEnergy, width);
    ImGui::Dummy(ImVec2(0, 8));
    if (m.hasBattery) {
        ImGui::TextColored(Theme::kTextMuted, "Battery charge over time");
    } else {
        ImGui::TextColored(Theme::kTextMuted, "Power consumption (real when the GPU exposes hwmon power; otherwise this stays 0)");
    }
    PushPlotTheme();
    if (ImPlot::BeginPlot("##energy", ImVec2(width, 180), ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
        if (m.hasBattery) PlotFilledLine("battery", m.batteryHistory, Theme::kAccentEnergy);
        else PlotFilledLine("energy", m.energyHistory, Theme::kAccentEnergy);
        ImPlot::EndPlot();
    }
    PopPlotTheme();
    ImGui::Dummy(ImVec2(0, 10));

    float pillW = (width - 16.0f) / 3.0f;
    Widgets::InfoPill("Thermal state", m.thermalState.c_str(), ImVec2(pillW, Theme::Scale(54.0f)));
    ImGui::SameLine();
    Widgets::InfoPill("Power mode", m.powerMode.c_str(), ImVec2(pillW, Theme::Scale(54.0f)));
    ImGui::SameLine();
    Widgets::InfoPill("Power source", m.powerSource.c_str(), ImVec2(pillW, Theme::Scale(54.0f)));

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextColored(Theme::kTextPrimary, "GPU fan / sensor telemetry");
    if (m.gpuFanRpm > 0.0f) {
        char fanBuf[32]; std::snprintf(fanBuf, sizeof(fanBuf), "%.0f RPM", m.gpuFanRpm);
        Widgets::InfoPill("GPU fan", fanBuf, ImVec2(width * 0.45f, Theme::Scale(54.0f)));
        ImGui::SameLine();
    }
    if (m.gpuTempC > 0.0f) {
        char gpuTemp[32]; std::snprintf(gpuTemp, sizeof(gpuTemp), "%.1f %s", Theme::TempForDisplay(m.gpuTempC), Theme::TempUnitSuffix());
        Widgets::InfoPill("GPU temp", gpuTemp, ImVec2(width * 0.45f, Theme::Scale(54.0f)));
    }

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextColored(Theme::kTextPrimary, "Processes with the highest estimated energy demand");
    ImGui::Dummy(ImVec2(0, 4));
    if (m.topEnergyProcesses.empty()) {
        ImGui::TextColored(Theme::kTextMuted, "Unavailable — no generic Linux energy-accounting source");
    }
    for (const EnergyProcessRow& p : m.topEnergyProcesses) {
        ImGui::TextColored(Theme::kTextSecondary, "%s", p.name.c_str());
        ImGui::SameLine(width - 40.0f);
        ImGui::TextColored(Theme::kTextMuted, "%d", p.score);
        float frac = p.score / 100.0f;
        ImGui::ProgressBar(frac, ImVec2(width - 50.0f, 6.0f), "");
    }
}
