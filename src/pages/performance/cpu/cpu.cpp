#include "pages/performance/cpu/cpu.h"
#include "pages/performance/shared.h"
#include "pages/page_common.h"
#include "theme.h"
#include "widgets.h"

#include <algorithm>
#include <cstdio>
#include <vector>

static void DrawCoreCard(const char* label, const CoreMetric& core, ImVec2 size) {
    ImGui::PushID(label);
    BeginAltCardStyle();
    ImGui::BeginChild("core", size, true, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextColored(Theme::kTextSecondary, "%s", label);
    PushPlotTheme();
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0, 0));
    if (ImPlot::BeginPlot("##core", ImVec2(-1, size.y - Theme::Scale(48.0f)), ImPlotFlags_NoTitle | ImPlotFlags_NoMenus |
                          ImPlotFlags_NoBoxSelect | ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText | ImPlotFlags_NoInputs)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
        PlotFilledLine("##u", core.utilHistory, Theme::kAccentCpu);
        ImPlot::SetNextLineStyle(Theme::kAccentClock, 1.2f);
        std::vector<float> kx, ky;
        WindowedSamples(core.kernelHistory, kx, ky);
        if (!kx.empty()) ImPlot::PlotLine("##k", kx.data(), ky.data(), (int)kx.size());
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar();
    PopPlotTheme();
    ImGui::EndChild();
    EndAltCardStyle();
    ImGui::PopID();
}

void DrawCpuDetail(const Metrics& m, float width) {
    char pct[16]; std::snprintf(pct, sizeof(pct), "%.1f%%", m.cpuPct * 100.0f);
    Widgets::DetailHeader("CPU", m.cpuModel.c_str(), m.cpuPct, pct, Theme::kAccentCpu, width);

    ImGui::TextColored(Theme::kTextMuted, "%d logical processors", m.logicalProcessors);
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextColored(Theme::kTextMuted, "%% Utilization by logical processor");
    ImGui::Dummy(ImVec2(0, 4));

    float gap = Theme::Scale(8.0f);
    float desiredCardW = Theme::Scale(150.0f);
    int cols = std::max(1, (int)((width + gap) / (desiredCardW + gap)));
    float cardW = (width - gap * (cols - 1)) / cols;
    float cardH = Theme::Scale(100.0f);
    for (int i = 0; i < (int)m.cores.size(); ++i) {
        if (i % cols != 0) ImGui::SameLine(0, gap);
        char label[16]; std::snprintf(label, sizeof(label), "CPU %d", i);
        DrawCoreCard(label, m.cores[i], ImVec2(cardW, cardH));
        if ((i + 1) % cols == 0) ImGui::Dummy(ImVec2(0, gap));
    }

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextColored(Theme::kTextMuted, "Scrolling history");
    ImGui::Dummy(ImVec2(0, 6));

    char procBuf[16]; std::snprintf(procBuf, sizeof(procBuf), "%d", m.processCount);
    char thrBuf[16]; std::snprintf(thrBuf, sizeof(thrBuf), "%d", m.threadCount);
    char speedBuf[16]; std::snprintf(speedBuf, sizeof(speedBuf), "%.0f MHz", m.avgMHz);
    const char* labels[] = {"Utilization", "Speed", "Processes", "Threads"};
    char utilBuf[16]; std::snprintf(utilBuf, sizeof(utilBuf), "%.1f%%", m.cpuPct * 100.0f);
    const char* values[] = {utilBuf, speedBuf, procBuf, thrBuf};
    StatGrid4(labels, values, 4);
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::TextColored(Theme::kTextMuted, "Up time");
    ImGui::TextColored(Theme::kTextPrimary, "%s", m.upTime.c_str());
    ImGui::Dummy(ImVec2(0, 8));

    char sockBuf[8]; std::snprintf(sockBuf, sizeof(sockBuf), "%d", m.sockets);
    char physBuf[8]; std::snprintf(physBuf, sizeof(physBuf), "%d", m.physicalCores);
    const char* labels2[] = {
        "Base speed", "Sockets", "Physical cores", "Virtualization", "Virtual machine",
        "L1 cache (d)", "L2 cache", "L3 cache", "Frequency driver", "Frequency governor", "Power preference"
    };
    const char* values2[] = {
        m.baseSpeed.c_str(), sockBuf, physBuf, m.virtualization.c_str(), m.virtualMachine.c_str(),
        m.l1Cache.c_str(), m.l2Cache.c_str(), m.l3Cache.c_str(), m.freqDriver.c_str(),
        m.freqGovernor.c_str(), m.powerPreference.c_str()
    };
    StatGrid4(labels2, values2, 11);

    // VID/core-voltage/temp/fan telemetry via ryzen_smu — see memory.cpp's
    // DrawDramOcDetail for the Memory-page counterpart. Same source, needs root.
    if (m.dramOcSupported && Theme::ExpertMode) {
        const DramOcSmu& sm = m.dramOc.metrics;
        float boxWidth = BeginExpertBox(width);
        ImGui::TextColored(Theme::kTextMuted, "SMU telemetry");
        ImGui::Dummy(ImVec2(0, 6));
        float gap2 = Theme::Scale(8.0f);
        float halfW = (boxWidth - gap2) / 2.0f;
        float cardH2 = Theme::Scale(340.0f);

        char vidBuf[16];
        FmtVolt(vidBuf, sizeof(vidBuf), sm.vid);
        int coreVoltN = (int)sm.coreVoltages.size();
        std::vector<std::string> coreLabelBufs(coreVoltN), coreVoltBufs(coreVoltN);
        std::vector<const char*> voltLabels(coreVoltN + 1), voltValues(coreVoltN + 1);
        voltLabels[0] = "VID"; voltValues[0] = vidBuf;
        for (int i = 0; i < coreVoltN; ++i) {
            char lbuf[16]; std::snprintf(lbuf, sizeof(lbuf), "C%d", i);
            coreLabelBufs[i] = lbuf;
            char vbuf[16]; FmtVolt(vbuf, sizeof(vbuf), sm.coreVoltages[i]);
            coreVoltBufs[i] = vbuf;
            voltLabels[i + 1] = coreLabelBufs[i].c_str();
            voltValues[i + 1] = coreVoltBufs[i].c_str();
        }
        DrawKVCard("vidcard", "VID & Core Voltages", voltLabels.data(), voltValues.data(), coreVoltN + 1, ImVec2(halfW, cardH2));
        ImGui::SameLine(0, gap2);

        ImGui::PushID("tempfans");
        BeginAltCardStyle();
        ImGui::BeginChild("card", ImVec2(halfW, cardH2), true);
        ImGui::TextColored(Theme::kTextPrimary, "Temp & Fans");
        ImGui::Dummy(ImVec2(0, 6));

        char fanBuf[160];
        if (!m.dramOc.fans.empty()) {
            int off = 0;
            for (size_t i = 0; i < m.dramOc.fans.size() && off < (int)sizeof(fanBuf) - 32; ++i)
                off += std::snprintf(fanBuf + off, sizeof(fanBuf) - off, "%s%s: %d RPM",
                                      i ? "  " : "", m.dramOc.fans[i].label.c_str(), m.dramOc.fans[i].rpm);
        } else {
            std::snprintf(fanBuf, sizeof(fanBuf), "-");
        }
        ImGui::TextColored(Theme::kTextMuted, "Fans:");
        ImGui::SameLine();
        ImGui::TextColored(Theme::kAccentCpu, "%s", fanBuf);

        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextColored(Theme::kTextMuted, "Temps:");
        char tempBuf[192]; int toff = 0;
        const char* tu = Theme::TempUnitSuffix();
        if (sm.hasTdie)       toff += std::snprintf(tempBuf + toff, sizeof(tempBuf) - toff, "Tdie: %.1f %s  ", Theme::TempForDisplay(sm.tdieC), tu);
        if (sm.hasTctl)       toff += std::snprintf(tempBuf + toff, sizeof(tempBuf) - toff, "Tctl: %.1f %s  ", Theme::TempForDisplay(sm.tctlC), tu);
        if (sm.hasTccd1)      toff += std::snprintf(tempBuf + toff, sizeof(tempBuf) - toff, "Tccd1: %.1f %s  ", Theme::TempForDisplay(sm.tccd1C), tu);
        if (sm.hasTccd2)      toff += std::snprintf(tempBuf + toff, sizeof(tempBuf) - toff, "Tccd2: %.1f %s  ", Theme::TempForDisplay(sm.tccd2C), tu);
        if (sm.hasIodHotspot) toff += std::snprintf(tempBuf + toff, sizeof(tempBuf) - toff, "IOD: %.1f %s", Theme::TempForDisplay(sm.iodHotspotC), tu);
        if (toff == 0) std::snprintf(tempBuf, sizeof(tempBuf), "Unavailable");
        ImGui::TextWrapped("%s", tempBuf);

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextColored(Theme::kTextMuted, "Core temps / load / freq:");
        size_t coreN = std::max({sm.coreTempsC.size(), sm.coreUsagePct.size(), sm.coreFreqMhz.size()});
        for (size_t i = 0; i < coreN; ++i) {
            float ct = i < sm.coreTempsC.size() ? sm.coreTempsC[i] : 0.0f;
            float cu = i < sm.coreUsagePct.size() ? sm.coreUsagePct[i] : 0.0f;
            float cf = i < sm.coreFreqMhz.size() ? sm.coreFreqMhz[i] : 0.0f;
            ImGui::TextColored(Theme::kAccentCpu, "C%d: %.1f %s  %.0f%%  %.0fMHz", (int)i, Theme::TempForDisplay(ct), Theme::TempUnitSuffix(), cu, cf);
        }
        ImGui::EndChild();
        EndAltCardStyle();
        ImGui::PopID();
        EndExpertBox();
    }
}
