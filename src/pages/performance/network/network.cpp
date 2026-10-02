#include "pages/performance/network/network.h"
#include "pages/performance/shared.h"
#include "pages/page_common.h"
#include "theme.h"
#include "widgets.h"

#include <algorithm>
#include <cstdio>

void DrawNetworkDetail(const Metrics& m, float width) {
    char kbs[16]; std::snprintf(kbs, sizeof(kbs), "%.0f KB/s", m.netReceiveKBs + m.netSendKBs);
    Widgets::DetailHeader("Network", m.netInterfaceCount.c_str(), std::min(1.0f, m.netReceiveKBs / 2000.0f), kbs, Theme::kAccentNetwork, width);
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::TextColored(Theme::kTextMuted, "Throughput");
    PushPlotTheme();
    if (ImPlot::BeginPlot("##net", ImVec2(width, 220), ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
        PlotFilledLine("net", m.netHistory, Theme::kAccentNetwork);
        ImPlot::EndPlot();
    }
    PopPlotTheme();
    ImGui::TextColored(Theme::kTextMuted, "Scrolling history");
    ImGui::Dummy(ImVec2(0, 8));

    char recv[16], send[16], totRecv[16], totSend[16];
    std::snprintf(recv, sizeof(recv), "%.0f KB/s", m.netReceiveKBs);
    std::snprintf(send, sizeof(send), "%.0f KB/s", m.netSendKBs);
    std::snprintf(totRecv, sizeof(totRecv), "%.2f GB", m.netTotalReceivedGB);
    std::snprintf(totSend, sizeof(totSend), "%.2f GB", m.netTotalSentGB);
    const char* labels[] = {"Receive", "Send", "Total received", "Total sent",
                             "Interface name", "Connection type", "Hardware address", "IPv6 address"};
    const char* values[] = {recv, send, totRecv, totSend,
                             "All interfaces", "Combined", "Unavailable", "Unavailable"};
    StatGrid4(labels, values, 8);
}
