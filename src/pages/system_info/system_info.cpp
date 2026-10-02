#include "pages/system_info/system_info.h"
#include "theme.h"
#include "widgets.h"

#include <cstdio>

void DrawSystemInfoPage(const Metrics& m, ImVec2 size) {
    ImGui::TextColored(Theme::kTextPrimary, "System Info");
    ImGui::Dummy(ImVec2(0, 8));

    if (!Widgets::BeginCard("SystemInfo", size)) {
        Widgets::EndCard();
        return;
    }

    char logicalBuf[16]; std::snprintf(logicalBuf, sizeof(logicalBuf), "%d", m.logicalProcessors);
    char threadsBuf[16]; std::snprintf(threadsBuf, sizeof(threadsBuf), "%d", m.threadCount);
    char physCoresBuf[16]; std::snprintf(physCoresBuf, sizeof(physCoresBuf), "%d", m.physicalCores);
    char socketsBuf[16]; std::snprintf(socketsBuf, sizeof(socketsBuf), "%d", m.sockets);

    const char* labels[] = {
        "Kernel", "Hostname", "User", "OS",
        "Platform", "GPU", "CPU model", "Uptime",
        "Logical CPUs", "Threads", "Physical cores", "Sockets"
    };

    const char* values[] = {
        m.kernelVersion.c_str(), m.hostname.c_str(), m.userName.c_str(), m.osName.c_str(),
        m.platform.c_str(), m.gpuName.c_str(), m.cpuModel.c_str(), m.upTime.c_str(),
        logicalBuf, threadsBuf, physCoresBuf, socketsBuf
    };

    if (ImGui::BeginTable("system_info_grid", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, Theme::Scale(220.0f));
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = 0; i < 12; ++i) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(Theme::kTextSecondary, "%s", labels[i]);
            ImGui::TableNextColumn();
            ImGui::TextColored(Theme::kTextPrimary, "%s", values[i]);
        }

        ImGui::EndTable();
    }

    Widgets::EndCard();
}
