#include "pages/startup_apps/startup_apps.h"
#include "pages/page_common.h"
#include "metrics/desktop_apps.h"
#include "theme.h"

#include <algorithm>
#include <cstdlib>
#include <string>

static bool IsUserOwned(const DesktopApp& a) {
    const char* home = getenv("HOME");
    if (!home) return false;
    std::string prefix = std::string(home) + "/.config/autostart/";
    return a.path.compare(0, prefix.size(), prefix) == 0;
}

static void DrawAddPopup(Metrics& m) {
    if (!ImGui::BeginPopup("Add startup app")) return;
    ImGui::SetNextItemWidth(Theme::Scale(260.0f));
    static char filterBuf[128] = "";
    ImGui::InputTextWithHint("##addfilter", "Filter installed apps", filterBuf, sizeof(filterBuf));
    std::string filter = ToLower(filterBuf);

    // Note: don't rescan (which reallocates m.installedApps) while this
    // range-for is iterating over it — defer to after the loop, and stop
    // iterating once an entry's been added.
    bool rescanNeeded = false;
    ImGui::BeginChild("##addlist", ImVec2(Theme::Scale(360.0f), Theme::Scale(260.0f)));
    for (const DesktopApp& a : m.installedApps) {
        bool alreadyStartup = std::any_of(m.startupApps.begin(), m.startupApps.end(),
            [&](const DesktopApp& s) { return s.name == a.name; });
        if (alreadyStartup) continue;
        if (!filter.empty() && !ContainsCI(a.name, filter)) continue;
        if (ImGui::Selectable(a.name.c_str())) {
            rescanNeeded = AddStartupApp(a);
            ImGui::CloseCurrentPopup();
            break;
        }
    }
    ImGui::EndChild();
    ImGui::EndPopup();
    if (rescanNeeded) m.RescanApps();
}

void DrawStartupAppsPage(Metrics& m, ImVec2 size) {
    ImGui::TextColored(Theme::kTextPrimary, "Startup apps");
    ImGui::SameLine(size.x - Theme::Scale(90.0f));
    if (ImGui::Button("+ Add", ImVec2(Theme::Scale(80.0f), 0))) ImGui::OpenPopup("Add startup app");
    DrawAddPopup(m);

    ImGui::Dummy(ImVec2(0, 8));

    if (m.startupApps.empty()) {
        ImGui::TextColored(Theme::kTextMuted, "No XDG autostart entries found "
            "(/etc/xdg/autostart, ~/.config/autostart).");
        return;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("startup_apps", 4, flags, ImVec2(size.x, size.y))) {
        ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, Theme::Scale(36.0f));
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, Theme::Scale(70.0f));
        ImGui::TableHeadersRow();

        // Note: don't rescan (which reallocates m.startupApps) while this
        // range-for is iterating over it — defer to after the table closes.
        bool rescanNeeded = false;
        int row = 0;
        for (DesktopApp& a : m.startupApps) {
            ImGui::PushID(row++);
            ImGui::TableNextRow(ImGuiTableRowFlags_None, Theme::Scale(30.0f));
            ImGui::TableNextColumn();
            bool enabled = a.enabled;
            if (ImGui::Checkbox("##on", &enabled)) {
                if (SetStartupAppEnabled(a, enabled)) rescanNeeded = true;
            }
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextPrimary, "%s", a.name.c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextMuted, "%s", a.exec.c_str());
            ImGui::TableNextColumn();
            if (IsUserOwned(a)) {
                if (ImGui::SmallButton("Remove")) {
                    if (RemoveStartupApp(a)) rescanNeeded = true;
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (rescanNeeded) m.RescanApps();
    }
}
