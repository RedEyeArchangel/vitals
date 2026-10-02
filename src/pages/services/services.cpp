#include "pages/services/services.h"
#include "pages/page_common.h"
#include "theme.h"

#include <cstdio>
#include <string>
#include <vector>

void DrawServicesPage(const Metrics& m, ImVec2 size) {
    ImGui::TextColored(Theme::kTextPrimary, "Services");
    ImGui::SameLine(size.x - Theme::Scale(200.0f));
    ImGui::SetNextItemWidth(Theme::Scale(200.0f));
    static char filterBuf[128] = "";
    ImGui::InputTextWithHint("##filter", "Filter by name", filterBuf, sizeof(filterBuf));

    ImGui::Dummy(ImVec2(0, 10));

    std::string filter = ToLower(filterBuf);
    std::vector<const ServiceUnit*> shown;
    for (const ServiceUnit& s : m.services) {
        if (filter.empty() || ContainsCI(s.name, filter)) shown.push_back(&s);
    }

    ImGui::TextColored(Theme::kTextMuted, "Services (%d)", (int)shown.size());

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("services", 4, flags, ImVec2(size.x, size.y - Theme::Scale(60.0f)))) {
        ImGui::TableSetupColumn("Unit", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Active", ImGuiTableColumnFlags_WidthFixed, Theme::Scale(90.0f));
        ImGui::TableSetupColumn("Sub", ImGuiTableColumnFlags_WidthFixed, Theme::Scale(90.0f));
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 2.2f);
        ImGui::TableHeadersRow();

        for (const ServiceUnit* sPtr : shown) {
            const ServiceUnit& s = *sPtr;
            ImGui::TableNextRow(ImGuiTableRowFlags_None, Theme::Scale(28.0f));
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextPrimary, "%s", s.name.c_str());
            ImGui::TableNextColumn();
            ImVec4 activeColor = s.active == "active" ? Theme::kAccentOk :
                                  s.active == "failed" ? Theme::kAccentClock : Theme::kTextMuted;
            ImGui::TextColored(activeColor, "%s", s.active.c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextMuted, "%s", s.sub.c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextSecondary, "%s", s.description.c_str());
        }
        ImGui::EndTable();
    }
}
