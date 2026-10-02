#include "pages/performance/disks/disks.h"
#include "pages/page_common.h"
#include "theme.h"
#include "widgets.h"

#include <cstdio>

void DrawDisksDetail(const Metrics& m, float width) {
    char pct[16]; std::snprintf(pct, sizeof(pct), "%.1f%%", m.diskActivePct * 100.0f);
    char subtitle[48]; std::snprintf(subtitle, sizeof(subtitle), "%d block device%s combined", m.diskCount, m.diskCount == 1 ? "" : "s");
    Widgets::DetailHeader("Disks", subtitle, m.diskActivePct, pct, Theme::kAccentDisk, width);
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::TextColored(Theme::kTextMuted, "%% Active time");
    PushPlotTheme();
    if (ImPlot::BeginPlot("##disk_active", ImVec2(width, 180), ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
        PlotFilledLine("active", m.diskActiveHistory, Theme::kAccentDisk);
        ImPlot::EndPlot();
    }
    PopPlotTheme();
    ImGui::TextColored(Theme::kTextMuted, "Scrolling history");

    ImGui::Dummy(ImVec2(0, 10));
    char xferLabel[48]; std::snprintf(xferLabel, sizeof(xferLabel), "Disk transfer rate  (R: %.0f KB/s  W: %.0f KB/s)", m.diskReadKBs, m.diskWriteKBs);
    ImGui::TextColored(Theme::kTextMuted, "%s", xferLabel);
    PushPlotTheme();
    if (ImPlot::BeginPlot("##disk_xfer", ImVec2(width, 150), ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
        PlotFilledLine("xfer", m.diskTransferHistory, Theme::kAccentTemp);
        ImPlot::EndPlot();
    }
    PopPlotTheme();
    ImGui::TextColored(Theme::kTextMuted, "Scrolling history");
}
