#include "pages/performance/performance.h"
#include "pages/performance/shared.h"
#include "pages/performance/cpu/cpu.h"
#include "pages/performance/memory/memory.h"
#include "pages/performance/disks/disks.h"
#include "pages/performance/network/network.h"
#include "pages/performance/energy/energy.h"
#include "pages/performance/thermals/thermals.h"
#include "pages/performance/gpu/gpu.h"
#include "theme.h"
#include "widgets.h"

#include <cstdio>

// One row in the left-hand metric list: label, two summary lines, and a
// live sparkline preview. Selecting a row picks which DrawXDetail panel
// (see pages/performance/*.cpp) renders on the right.
static bool DrawMetricRow(const char* label, const char* line1, const char* line2,
                          ImVec4 color, const RingBuffer& hist, bool selected, float width) {
    ImGui::PushID(label);
    float rowHeight = Theme::Scale(78.0f);
    ImVec2 rowPos = ImGui::GetCursorScreenPos();

    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.06f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1, 1, 1, 0.10f));
    bool clicked = ImGui::Selectable("##row", selected, 0, ImVec2(width, rowHeight));
    ImGui::PopStyleColor(2);

    if (selected) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(rowPos, ImVec2(rowPos.x + width, rowPos.y + rowHeight),
                           ImGui::ColorConvertFloat4ToU32(ImVec4(Theme::kBgSidebarSel.x, Theme::kBgSidebarSel.y, Theme::kBgSidebarSel.z, 0.5f)), 6.0f);
    }

    ImGui::SetCursorScreenPos(rowPos);
    ImGui::BeginGroup();
    ImGui::TextColored(Theme::kTextPrimary, "%s", label);
    ImGui::TextColored(Theme::kTextMuted, "%s", line1);
    if (line2 && line2[0]) ImGui::TextColored(Theme::kTextMuted, "%s", line2);
    ImGui::EndGroup();

    ImGui::SameLine(width - Theme::Scale(100.0f));
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Border, color);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::kBgPanelAlt);
    ImGui::BeginChild("preview", ImVec2(Theme::Scale(90.0f), Theme::Scale(46.0f)), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    Widgets::Sparkline(hist, color, ImGui::GetContentRegionAvail());
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::EndGroup();

    ImGui::SetCursorScreenPos(ImVec2(rowPos.x, rowPos.y + rowHeight));
    ImGui::Dummy(ImVec2(0, 0));

    ImGui::PopID();
    return clicked;
}

void DrawPerformancePage(const Metrics& m, PerfMetric& selected, ImVec2 size) {
    ImGui::TextColored(Theme::kTextPrimary, "Performance");
    ImGui::Dummy(ImVec2(0, 8));

    float leftWidth = Theme::Scale(320.0f);
    float rightWidth = size.x - leftWidth - Theme::kGap;

    ImGui::BeginChild("PerfLeft", ImVec2(leftWidth, size.y - Theme::Scale(40.0f)), false);
    char cpuLine1[32]; std::snprintf(cpuLine1, sizeof(cpuLine1), "%.1f%%", m.cpuPct * 100.0f);
    char cpuLine2[32]; std::snprintf(cpuLine2, sizeof(cpuLine2), "%d logical processors", m.logicalProcessors);
    if (DrawMetricRow("CPU", cpuLine1, cpuLine2, Theme::kAccentCpu, m.cpuHistory, selected == PerfMetric::CPU, leftWidth))
        selected = PerfMetric::CPU;

    char gpuLine2[32]; std::snprintf(gpuLine2, sizeof(gpuLine2), "%.1f%%", m.gpuPct * 100.0f);
    if (DrawMetricRow("GPU", GpuSubtitle(m), gpuLine2, Theme::kAccentGpu, m.gpuHistory, selected == PerfMetric::Gpu, leftWidth))
        selected = PerfMetric::Gpu;

    char memLine1[32]; std::snprintf(memLine1, sizeof(memLine1), "%.2f GB / %.0f GB", m.memUsedGB, m.memTotalGB);
    char memLine2[32]; std::snprintf(memLine2, sizeof(memLine2), "%.1f%%", 100.0f * m.memUsedGB / m.memTotalGB);
    if (DrawMetricRow("Memory", memLine1, memLine2, Theme::kAccentMemory, m.memHistory, selected == PerfMetric::Memory, leftWidth))
        selected = PerfMetric::Memory;

    char diskLine2[32]; std::snprintf(diskLine2, sizeof(diskLine2), "%.1f%% active", m.diskActivePct * 100.0f);
    char diskLine1[48]; std::snprintf(diskLine1, sizeof(diskLine1), "%d block device%s combined", m.diskCount, m.diskCount == 1 ? "" : "s");
    if (DrawMetricRow("Disks", diskLine1, diskLine2, Theme::kAccentDisk, m.diskActiveHistory, selected == PerfMetric::Disks, leftWidth))
        selected = PerfMetric::Disks;

    char netLine1[48]; std::snprintf(netLine1, sizeof(netLine1), "%s", m.netInterfaceCount.c_str());
    char netLine2[32]; std::snprintf(netLine2, sizeof(netLine2), "S: %.0f KB/s  R: %.0f KB/s", m.netSendKBs, m.netReceiveKBs);
    if (DrawMetricRow("Network", netLine1, netLine2, Theme::kAccentNetwork, m.netHistory, selected == PerfMetric::Network, leftWidth))
        selected = PerfMetric::Network;

    char enLine1[48]; char enLine2[16];
    if (m.hasBattery) {
        std::snprintf(enLine1, sizeof(enLine1), "%s", m.batteryStatus.c_str());
        std::snprintf(enLine2, sizeof(enLine2), "%.0f%%", m.batteryPct * 100.0f);
    } else {
        std::snprintf(enLine1, sizeof(enLine1), "%s", m.thermalState.c_str());
        std::snprintf(enLine2, sizeof(enLine2), "%.0f W", m.energyW);
    }
    if (DrawMetricRow(m.hasBattery ? "Battery" : "Energy", enLine1, enLine2, Theme::kAccentEnergy,
                       m.hasBattery ? m.batteryHistory : m.energyHistory, selected == PerfMetric::Energy, leftWidth))
        selected = PerfMetric::Energy;

    char thLine1[32]; std::snprintf(thLine1, sizeof(thLine1), "%s thermal pressure", m.thermalPressureText.c_str());
    char thLine2[16]; std::snprintf(thLine2, sizeof(thLine2), "%.1f %s", Theme::TempForDisplay(m.tempC), Theme::TempUnitSuffix());
    if (DrawMetricRow("Thermals", thLine1, thLine2, Theme::kAccentTemp, m.hotspotHistory, selected == PerfMetric::Thermals, leftWidth))
        selected = PerfMetric::Thermals;

    ImGui::EndChild();

    ImGui::SameLine(0, Theme::kGap);
    ImGui::BeginChild("PerfRight", ImVec2(rightWidth, size.y - Theme::Scale(40.0f)), false);
    switch (selected) {
        case PerfMetric::CPU:      DrawCpuDetail(m, rightWidth); break;
        case PerfMetric::Memory:   DrawMemoryDetail(m, rightWidth); break;
        case PerfMetric::Disks:    DrawDisksDetail(m, rightWidth); break;
        case PerfMetric::Network:  DrawNetworkDetail(m, rightWidth); break;
        case PerfMetric::Energy:   DrawEnergyDetail(m, rightWidth); break;
        case PerfMetric::Thermals: DrawThermalsDetail(m, rightWidth); break;
        case PerfMetric::Gpu:      DrawGpuDetail(m, rightWidth); break;
    }
    ImGui::EndChild();
}
