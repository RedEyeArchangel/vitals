#include "pages/disk_space/disk_space.h"
#include "theme.h"
#include "widgets.h"

#include <cstdio>

void DrawDiskSpacePage(const Metrics& m, ImVec2 size) {
    ImGui::TextColored(Theme::kTextPrimary, "Disk Space");
    ImGui::Dummy(ImVec2(0, 8));

    if (m.diskVolumes.empty()) {
        ImGui::TextColored(Theme::kTextMuted, "No real block-device volumes found in /proc/mounts.");
        return;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("disk_space", 5, flags, ImVec2(size.x, size.y))) {
        ImGui::TableSetupColumn("Mount point", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Device", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, Theme::Scale(70.0f));
        ImGui::TableSetupColumn("Used / Total", ImGuiTableColumnFlags_WidthFixed, Theme::Scale(150.0f));
        ImGui::TableSetupColumn("Usage", ImGuiTableColumnFlags_WidthFixed, Theme::Scale(160.0f));
        ImGui::TableHeadersRow();

        for (const DiskVolume& v : m.diskVolumes) {
            ImGui::TableNextRow(ImGuiTableRowFlags_None, Theme::Scale(34.0f));
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextPrimary, "%s", v.mountPoint.c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextMuted, "%s", v.device.c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextSecondary, "%s", v.fsType.c_str());
            ImGui::TableNextColumn();
            char buf[48]; std::snprintf(buf, sizeof(buf), "%.1f GB / %.1f GB", v.usedGB, v.totalGB);
            ImGui::TextColored(Theme::kTextSecondary, "%s", buf);
            ImGui::TableNextColumn();
            ImGui::PushID(v.mountPoint.c_str());
            Widgets::LedBarHorizontal("usage", v.usedPct / 100.0f, Theme::kAccentDisk,
                                       ImVec2(Theme::Scale(110.0f), Theme::Scale(16.0f)));
            ImGui::SameLine();
            ImGui::TextColored(Theme::kTextMuted, "%.0f%%", v.usedPct);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}
