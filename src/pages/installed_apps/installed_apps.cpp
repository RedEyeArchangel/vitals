#include "pages/installed_apps/installed_apps.h"
#include "pages/page_common.h"
#include "theme.h"

#include <string>
#include <vector>

void DrawInstalledAppsPage(const Metrics& m, ImVec2 size) {
    ImGui::TextColored(Theme::kTextPrimary, "Installed Apps");
    ImGui::SameLine(size.x - Theme::Scale(200.0f));
    ImGui::SetNextItemWidth(Theme::Scale(200.0f));
    static char filterBuf[128] = "";
    ImGui::InputTextWithHint("##filter", "Filter by name", filterBuf, sizeof(filterBuf));

    ImGui::Dummy(ImVec2(0, 10));

    std::string filter = ToLower(filterBuf);
    std::vector<const DesktopApp*> shown;
    for (const DesktopApp& a : m.installedApps) {
        if (filter.empty() || ContainsCI(a.name, filter)) shown.push_back(&a);
    }

    ImGui::TextColored(Theme::kTextMuted, "Applications (%d)", (int)shown.size());

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("installed_apps", 3, flags, ImVec2(size.x, size.y - Theme::Scale(60.0f)))) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableHeadersRow();

        for (const DesktopApp* aPtr : shown) {
            const DesktopApp& a = *aPtr;
            ImGui::TableNextRow(ImGuiTableRowFlags_None, Theme::Scale(28.0f));
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextPrimary, "%s", a.name.c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextMuted, "%s", a.exec.c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextSecondary, "%s", a.comment.c_str());
        }
        ImGui::EndTable();
    }
}
