#include "pages/performance/shared.h"
#include "theme.h"
#include "widgets.h"

#include <cstdio>

void StatGrid4(const char* labels[], const char* values[], int count) {
    if (ImGui::BeginTable("stats", 4, ImGuiTableFlags_None)) {
        for (int i = 0; i < count; ++i) {
            if (i % 4 == 0) ImGui::TableNextRow(ImGuiTableRowFlags_None, Theme::Scale(34.0f));
            ImGui::TableNextColumn();
            Widgets::StatCell(labels[i], values[i]);
        }
        ImGui::EndTable();
    }
}

void FmtOrNA(char* buf, size_t n, const char* fmt, float v) {
    if (v > 0.0f) std::snprintf(buf, n, fmt, v);
    else std::snprintf(buf, n, "Unavailable");
}
void FmtVolt(char* buf, size_t n, float v) { FmtOrNA(buf, n, "%.4f V", v); }
void FmtMHz(char* buf, size_t n, float v) { FmtOrNA(buf, n, "%.0f MHz", v); }
void FmtU32(char* buf, size_t n, uint32_t v) {
    // 0 isn't a real DRAM timing value — same "honest note, not a zero" rule
    // FmtOrNA already applies to voltages/clocks (a failed/unmapped SMU read
    // leaves these fields at their zero-initialized default). "N/A" (not
    // "Unavailable") because every timing field's buffer in memory.cpp is
    // char[8], sized for a numeric string — "Unavailable" truncated to
    // "Unavail" there.
    if (v > 0) std::snprintf(buf, n, "%u", v);
    else std::snprintf(buf, n, "N/A");
}

void BeginAltCardStyle() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::kBgPanelAlt);
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::kBorder);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Theme::kPanelRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
}
void EndAltCardStyle() {
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

float BeginExpertBox(float width) {
    ImGui::Dummy(ImVec2(0, 14));
    BeginAltCardStyle();
    ImGui::BeginChild("expert_box", ImVec2(width, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::TextColored(Theme::kTextPrimary, "Expert informations");
    ImGui::Dummy(ImVec2(0, 8));
    return ImGui::GetContentRegionAvail().x;
}
void EndExpertBox() {
    ImGui::EndChild();
    EndAltCardStyle();
}

void DrawKVCard(const char* id, const char* title, const char* const* labels,
                 const char* const* values, int count, ImVec2 size) {
    ImGui::PushID(id);
    BeginAltCardStyle();
    ImGui::BeginChild("card", size, true);
    ImGui::TextColored(Theme::kTextPrimary, "%s", title);
    ImGui::Dummy(ImVec2(0, 6));
    float valueX = size.x * 0.5f;
    for (int i = 0; i < count; ++i) {
        ImGui::TextColored(Theme::kTextMuted, "%s", labels[i]);
        ImGui::SameLine(valueX);
        ImGui::PushFont(Theme::LoadedFonts.mono);
        ImGui::TextColored(Theme::kAccentCpu, "%s", values[i]);
        ImGui::PopFont();
    }
    ImGui::EndChild();
    EndAltCardStyle();
    ImGui::PopID();
}

const char* GpuSubtitle(const Metrics& m) {
    if (m.gpuSource == "amdgpu") return "live: amdgpu gpu_busy_percent";
    if (m.gpuSource == "nvidia-smi") return "live: nvidia-smi";
    if (m.gpuSource == "i915") return "live: i915/xe PMU (rcs0-busy)";
    return "unavailable: no amdgpu/nvidia-smi/i915 source found";
}
