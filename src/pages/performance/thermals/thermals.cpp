#include "pages/performance/thermals/thermals.h"
#include "pages/performance/shared.h"
#include "pages/page_common.h"
#include "theme.h"
#include "widgets.h"

#include <cstdio>
#include <vector>

void DrawThermalsDetail(const Metrics& m, float width) {
    char cText[16]; std::snprintf(cText, sizeof(cText), "%.1f %s", Theme::TempForDisplay(m.tempC), Theme::TempUnitSuffix());
    Widgets::DetailHeader("Thermals", m.thermalPressureText.c_str(), m.tempC / 110.0f, cText, Theme::kAccentTemp, width);
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::TextColored(Theme::kTextMuted, "CPU hotspot");
    PushPlotTheme();
    if (ImPlot::BeginPlot("##hotspot", ImVec2(width, 180), ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
        PlotFilledLine("hotspot", m.hotspotHistory, Theme::kAccentTemp);
        ImPlot::EndPlot();
    }
    PopPlotTheme();
    ImGui::TextColored(Theme::kTextMuted, "Scrolling history");
    ImGui::Dummy(ImVec2(0, 10));

    float pillW = (width - 8.0f) / 2.0f;
    Widgets::InfoPill("Thermal pressure", m.thermalPressureText.c_str(), ImVec2(pillW, Theme::Scale(54.0f)));
    ImGui::SameLine();
    char hotspotBuf[16]; std::snprintf(hotspotBuf, sizeof(hotspotBuf), "%.1f %s", Theme::TempForDisplay(m.tempC), Theme::TempUnitSuffix());
    Widgets::InfoPill("Hotspot", hotspotBuf, ImVec2(pillW, Theme::Scale(54.0f)));

    // Every sensor this machine exposes (hwmon + ACPI thermal_zone, see
    // ReadThermalSensors) lives in the expert box — this is deliberately
    // exhaustive, not curated, so nothing available goes missing from it.
    if (Theme::ExpertMode) {
        float boxWidth = BeginExpertBox(width);
        ImGui::TextColored(Theme::kTextPrimary, "Sensors");
        ImGui::Dummy(ImVec2(0, 4));

        struct SensorCard { const char* label; float tempC; const RingBuffer* hist; bool available; };
        std::vector<SensorCard> sensors;
        sensors.push_back({"CPU", m.tempC, &m.hotspotHistory, m.tempC > 0.0f});
        sensors.push_back({"GPU", m.gpuTempC, &m.gpuTempHistory, m.gpuTempC > 0.0f});
        sensors.push_back({"Motherboard", m.boardTempC, &m.boardTempHistory, m.boardTempC > 0.0f});
        for (const ThermalSensor& s : m.thermalSensors) {
            auto it = m.thermalSensorHistory.find(s.label);
            const RingBuffer* hist = it != m.thermalSensorHistory.end() ? &it->second : nullptr;
            sensors.push_back({s.label.c_str(), s.tempC, hist, s.available});
        }

        float cardW = (boxWidth - 8.0f) / 2.0f;
        for (size_t i = 0; i < sensors.size(); ++i) {
            if (i % 2 != 0) ImGui::SameLine(0, 8.0f);
            ImGui::PushID((int)i);
            BeginAltCardStyle();
            ImGui::BeginChild("sensor", ImVec2(cardW, Theme::Scale(90.0f)), true);
            ImGui::TextColored(Theme::kTextSecondary, "%s", sensors[i].label);
            char tbuf[24];
            if (sensors[i].available) std::snprintf(tbuf, sizeof(tbuf), "%.1f %s", Theme::TempForDisplay(sensors[i].tempC), Theme::TempUnitSuffix());
            else std::snprintf(tbuf, sizeof(tbuf), "Unavailable");
            ImGui::SameLine(cardW - ImGui::CalcTextSize(tbuf).x - 12.0f);
            ImGui::TextColored(Theme::kAccentTemp, "%s", tbuf);
            if (sensors[i].available && sensors[i].hist) {
                PushPlotTheme();
                if (ImPlot::BeginPlot("##sensor_plot", ImVec2(-1, 46), ImPlotFlags_NoTitle | ImPlotFlags_NoMenus |
                                      ImPlotFlags_NoBoxSelect | ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText | ImPlotFlags_NoInputs)) {
                    ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
                    PlotFilledLine("s", *sensors[i].hist, Theme::kAccentTemp);
                    ImPlot::EndPlot();
                }
                PopPlotTheme();
            } else if (!sensors[i].available) {
                ImGui::TextColored(Theme::kTextMuted, "No hwmon sensor found");
            }
            ImGui::EndChild();
            EndAltCardStyle();
            ImGui::PopID();
            if (i % 2 == 1) ImGui::Dummy(ImVec2(0, 8));
        }
        EndExpertBox();
    }
}
