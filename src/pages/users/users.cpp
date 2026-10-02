#include "pages/users/users.h"
#include "theme.h"

#include <cstdio>

void DrawUsersPage(const Metrics& m, ImVec2 size) {
    ImGui::TextColored(Theme::kTextPrimary, "Users");
    ImGui::Dummy(ImVec2(0, 8));

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("users", 5, flags, ImVec2(size.x, size.y))) {
        ImGui::TableSetupColumn("User", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("UID", ImGuiTableColumnFlags_WidthFixed, Theme::Scale(70.0f));
        ImGui::TableSetupColumn("Home", ImGuiTableColumnFlags_WidthStretch, 1.8f);
        ImGui::TableSetupColumn("Shell", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, Theme::Scale(100.0f));
        ImGui::TableHeadersRow();

        for (const SystemUser& u : m.systemUsers) {
            ImGui::TableNextRow(ImGuiTableRowFlags_None, Theme::Scale(32.0f));
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextPrimary, "%s", u.name.c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextMuted, "%d", u.uid);
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextSecondary, "%s", u.homeDir.c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextSecondary, "%s", u.shell.c_str());
            ImGui::TableNextColumn();
            if (u.loggedIn) ImGui::TextColored(Theme::kAccentOk, "Logged in");
            else ImGui::TextColored(Theme::kTextMuted, "Offline");
        }
        ImGui::EndTable();
    }
}
