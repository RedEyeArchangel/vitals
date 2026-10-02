#include "pages/processes/processes.h"
#include "pages/page_common.h"
#include "metrics/process_detail.h"
#include "theme.h"

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <string>
#include <vector>

// Reads fresh detail every call (see ReadProcessDetail — on-demand, not part
// of the per-tick Update() loop), so the window live-updates while open,
// same as GNOME System Monitor's Properties dialog.
static void DrawProcessProperties(const Metrics& m, int pid, bool* open) {
    float liveCpuPct = 0.0f;
    for (const ProcessRow& r : m.allProcesses) {
        if (r.pid == pid) { liveCpuPct = r.cpuPct; break; }
    }
    ProcessDetail d = ReadProcessDetail(pid, liveCpuPct);

    char title[80];
    std::snprintf(title, sizeof(title), "%s (PID %d)###process_properties",
                  d.found ? d.name.c_str() : "Process", pid);
    ImGui::SetNextWindowSize(ImVec2(Theme::Scale(480.0f), Theme::Scale(500.0f)), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(title, open)) {
        if (!d.found) {
            ImGui::TextColored(Theme::kTextMuted, "Process no longer running.");
        } else {
            char cpuBuf[16]; std::snprintf(cpuBuf, sizeof(cpuBuf), "%.2f%%", d.cpuPct);
            char pidBuf[16]; std::snprintf(pidBuf, sizeof(pidBuf), "%d", d.pid);
            char niceBuf[16]; std::snprintf(niceBuf, sizeof(niceBuf), "%d", d.nice);

            const char* labels[] = {
                "Process Name", "User", "Status", "Memory", "Virtual Memory", "Resident Memory",
                "Writable Memory", "Shared Memory", "CPU", "CPU Time", "Started", "Nice",
                "Priority", "ID", "Security Context", "Command Line", "Waiting Channel", "Control Group"
            };
            const char* values[] = {
                d.name.c_str(), d.user.c_str(), d.status.c_str(), d.memoryText.c_str(),
                d.virtualText.c_str(), d.residentText.c_str(), d.writableText.c_str(), d.sharedText.c_str(),
                cpuBuf, d.cpuTimeText.c_str(), d.startedText.c_str(), niceBuf, d.priorityText.c_str(),
                pidBuf, d.securityContext.c_str(), d.commandLine.c_str(), d.waitingChannel.c_str(),
                d.controlGroup.c_str()
            };

            if (ImGui::BeginTable("proc_props", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, Theme::Scale(160.0f));
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextSecondary, "%s", labels[i]);
                    ImGui::TableNextColumn(); ImGui::TextWrapped("%s", values[i]);
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::End();
}

void DrawProcessesPage(const Metrics& m, ImVec2 size) {
    ImGui::TextColored(Theme::kTextPrimary, "Processes");
    ImGui::SameLine(size.x - Theme::Scale(260.0f));
    ImGui::SetNextItemWidth(Theme::Scale(140.0f));
    static char filterBuf[128] = "";
    ImGui::InputTextWithHint("##filter", "Filter by name, user, or PID", filterBuf, sizeof(filterBuf));

    ImGui::Dummy(ImVec2(0, 10));

    std::string filter = ToLower(filterBuf);
    std::vector<const ProcessRow*> shown;
    for (const ProcessRow& row : m.allProcesses) {
        if (filter.empty() || ContainsCI(row.name, filter) || ContainsCI(row.user, filter) ||
            ContainsCI(std::to_string(row.pid), filter)) {
            shown.push_back(&row);
        }
    }

    ImGui::TextColored(Theme::kTextMuted, "Processes (%d) - right-click a row for Properties/Continue/Stop/Terminate/Kill", (int)shown.size());

    static bool showProps = false;
    static int propsPid = -1;

    // NoSavedSettings: without it, ImGui persists this table's column order
    // and active sort spec to imgui.ini across runs — after any column-list
    // edit, a stale saved sort spec pointed at the wrong column and produced
    // rows that only looked sorted (mismatched against what's on screen).
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                             ImGuiTableFlags_Sortable | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable("processes_full", 8, flags, ImVec2(size.x, size.y - Theme::Scale(60.0f)))) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.5f);
        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, Theme::Scale(80.0f));
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, Theme::Scale(90.0f));
        ImGui::TableSetupColumn("User name", ImGuiTableColumnFlags_WidthFixed, Theme::Scale(100.0f));
        ImGui::TableSetupColumn("CPU usage", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort |
                                 ImGuiTableColumnFlags_PreferSortDescending, Theme::Scale(140.0f));
        ImGui::TableSetupColumn("Memory usage", ImGuiTableColumnFlags_WidthFixed |
                                 ImGuiTableColumnFlags_PreferSortDescending, Theme::Scale(110.0f));
        ImGui::TableSetupColumn("GPU usage", ImGuiTableColumnFlags_WidthFixed |
                                 ImGuiTableColumnFlags_PreferSortDescending, Theme::Scale(140.0f));
        ImGui::TableSetupColumn("VRAM usage", ImGuiTableColumnFlags_WidthFixed |
                                 ImGuiTableColumnFlags_PreferSortDescending, Theme::Scale(110.0f));
        ImGui::TableHeadersRow();

        // shown is rebuilt from scratch every frame (in the backend's default
        // CPU-descending order), so the sort must be reapplied every frame —
        // gating it on SpecsDirty (ImGui's "just changed" one-shot flag) only
        // sorted the single frame the user clicked a header, then the very
        // next frame's fresh rebuild silently reverted it.
        if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
            if (sortSpecs->SpecsCount > 0) {
                const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
                bool ascending = spec.SortDirection == ImGuiSortDirection_Ascending;
                std::sort(shown.begin(), shown.end(), [&](const ProcessRow* a, const ProcessRow* b) {
                    int delta = 0;
                    switch (spec.ColumnIndex) {
                        case 0: delta = a->name.compare(b->name); break;
                        case 1: delta = (a->pid < b->pid) ? -1 : (a->pid > b->pid) ? 1 : 0; break;
                        case 2: delta = a->status.compare(b->status); break;
                        case 3: delta = a->user.compare(b->user); break;
                        case 4: delta = (a->cpuPct < b->cpuPct) ? -1 : (a->cpuPct > b->cpuPct) ? 1 : 0; break;
                        case 5: delta = (a->memoryKb < b->memoryKb) ? -1 : (a->memoryKb > b->memoryKb) ? 1 : 0; break;
                        case 6: delta = (a->gpuPct < b->gpuPct) ? -1 : (a->gpuPct > b->gpuPct) ? 1 : 0; break;
                        case 7: delta = (a->vramKb < b->vramKb) ? -1 : (a->vramKb > b->vramKb) ? 1 : 0; break;
                        default: break;
                    }
                    if (delta == 0) delta = a->pid - b->pid; // stable tie-break
                    return ascending ? delta < 0 : delta > 0;
                });
            }
            sortSpecs->SpecsDirty = false;
        }

        for (const ProcessRow* rowPtr : shown) {
            const ProcessRow& row = *rowPtr;
            ImGui::TableNextRow(ImGuiTableRowFlags_None, Theme::Scale(32.0f));
            ImGui::TableNextColumn();
            ImGui::PushID(row.pid);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextPrimary);
            ImGui::Selectable(row.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
            ImGui::PopStyleColor();
            if (row.pid > 0 && ImGui::BeginPopupContextItem("proc_ctx")) {
                ImGui::TextColored(Theme::kTextMuted, "%s (PID %d)", row.name.c_str(), row.pid);
                ImGui::Separator();
                if (ImGui::MenuItem("Properties")) { propsPid = row.pid; showProps = true; }
                ImGui::Separator();
                if (ImGui::MenuItem("Continue")) kill(row.pid, SIGCONT);
                if (ImGui::MenuItem("Stop")) kill(row.pid, SIGSTOP);
                if (ImGui::MenuItem("Terminate")) kill(row.pid, SIGTERM);
                ImGui::Separator();
                if (ImGui::BeginMenu("Kill")) {
                    if (ImGui::MenuItem("Confirm kill (SIGKILL)")) kill(row.pid, SIGKILL);
                    ImGui::EndMenu();
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextMuted, "%d", row.pid);
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kAccentOk, "%s", row.status.c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextSecondary, "%s", row.user.c_str());
            ImGui::TableNextColumn();
            if (row.cpuPct > 0.05f) {
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Theme::kAccentDisk);
                char buf[16]; std::snprintf(buf, sizeof(buf), "%.1f%%", row.cpuPct);
                ImGui::ProgressBar(std::min(1.0f, row.cpuPct / 5.0f), ImVec2(Theme::Scale(90.0f), Theme::Scale(16.0f)), buf);
                ImGui::PopStyleColor();
            } else {
                ImGui::TextColored(Theme::kTextMuted, "%.1f%%", row.cpuPct);
            }
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextSecondary, "%s", row.memory.c_str());
            ImGui::TableNextColumn();
            if (row.gpuPct > 0.05f) {
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Theme::kAccentGpu);
                char buf[16]; std::snprintf(buf, sizeof(buf), "%.1f%%", row.gpuPct);
                ImGui::ProgressBar(std::min(1.0f, row.gpuPct / 100.0f), ImVec2(Theme::Scale(90.0f), Theme::Scale(16.0f)), buf);
                ImGui::PopStyleColor();
            } else {
                ImGui::TextColored(Theme::kTextMuted, "%.1f%%", row.gpuPct);
            }
            ImGui::TableNextColumn(); ImGui::TextColored(Theme::kTextSecondary, "%s", row.vram.c_str());
        }
        ImGui::EndTable();
    }

    if (showProps) DrawProcessProperties(m, propsPid, &showProps);
}
