#include "pages/performance/memory/memory.h"
#include "pages/performance/shared.h"
#include "pages/page_common.h"
#include "theme.h"
#include "widgets.h"

#include <cstdio>

// The DIMM config card (speed/clocks + GDM/PowerDown/SPD-temp sub-row) doesn't fit
// DrawKVCard's flat label:value list, so it gets its own layout.
static void DrawDimmConfigCard(const DramOcSummary& s, int moduleIdx, ImVec2 size) {
    const DramOcSmu& sm = s.metrics;
    const DramOcTimings& d = s.dram;
    BeginAltCardStyle();
    ImGui::BeginChild("dimmcfg", size, true);
    ImGui::TextColored(Theme::kTextPrimary, "DIMM");
    ImGui::Dummy(ImVec2(0, 6));

    char speedBuf[24], mclkBuf[24], fclkBuf[24], uclkBuf[24], bclkBuf[24];
    FmtOrNA(speedBuf, sizeof(speedBuf), "%.0f MT/s", s.memoryFrequency);
    FmtMHz(mclkBuf, sizeof(mclkBuf), sm.mclkMhz);
    FmtMHz(fclkBuf, sizeof(fclkBuf), sm.fclkMhz);
    FmtMHz(uclkBuf, sizeof(uclkBuf), sm.uclkMhz);
    FmtMHz(bclkBuf, sizeof(bclkBuf), sm.bclkMhz);
    const char* rowLabels[] = {"Speed", "MCLK", "FCLK", "UCLK", "BCLK"};
    const char* rowValues[] = {speedBuf, mclkBuf, fclkBuf, uclkBuf, bclkBuf};
    for (int i = 0; i < 5; ++i) {
        ImGui::TextColored(Theme::kTextMuted, "%s", rowLabels[i]);
        ImGui::SameLine(size.x * 0.5f);
        ImGui::TextColored(Theme::kAccentCpu, "%s", rowValues[i]);
    }

    ImGui::Dummy(ImVec2(0, 10));
    float col = size.x / 3.0f;
    ImGui::TextColored(Theme::kTextMuted, "GDM");
    ImGui::SameLine(col);
    ImGui::TextColored(Theme::kTextMuted, "PowerDown");
    ImGui::SameLine(col * 2.0f);
    ImGui::TextColored(Theme::kTextMuted, "Temp");

    char spdBuf[16];
    if (moduleIdx >= 0 && (size_t)moduleIdx < sm.spdTempsC.size()) std::snprintf(spdBuf, sizeof(spdBuf), "%.1f %s", Theme::TempForDisplay(sm.spdTempsC[moduleIdx]), Theme::TempUnitSuffix());
    else std::snprintf(spdBuf, sizeof(spdBuf), "-");
    ImGui::TextColored(Theme::kAccentCpu, "%s", d.gdmEnabled ? "True" : "False");
    ImGui::SameLine(col);
    ImGui::TextColored(Theme::kAccentCpu, "%s", d.powerDownEnabled ? "True" : "False");
    ImGui::SameLine(col * 2.0f);
    ImGui::TextColored(Theme::kAccentCpu, "%s", spdBuf);

    ImGui::EndChild();
    EndAltCardStyle();
}

static void DrawDimmInfoCard(const DramOcModule& mod, const DramOcTimings& d, int idx, bool multiple, ImVec2 size) {
    const char* labels[] = {"Capacity", "Manufacturer", "Part Number", "Serial", "Rank", "Cmd2T"};
    const char* values[] = {
        mod.capacityDisplay.empty() ? "Unavailable" : mod.capacityDisplay.c_str(),
        mod.manufacturer.empty() ? "Unavailable" : mod.manufacturer.c_str(),
        mod.partNumber.empty() ? "Unavailable" : mod.partNumber.c_str(),
        mod.serialNumber.empty() ? "Unavailable" : mod.serialNumber.c_str(),
        mod.rank.c_str(), d.cmd2t.empty() ? "-" : d.cmd2t.c_str(),
    };
    char id[24]; std::snprintf(id, sizeof(id), "dimminfo%d", idx);
    char title[96];
    if (multiple) std::snprintf(title, sizeof(title), "DIMM (%s)", mod.slotDisplay.empty() ? "?" : mod.slotDisplay.c_str());
    else std::snprintf(title, sizeof(title), "DIMM");
    DrawKVCard(id, title, labels, values, 6, size);
}

// DRAM/CPU telemetry — see dram_oc.h. Memory speed/type, DIMM/SPD info,
// per-core temp/usage/freq, and package power (RAPL) are real on any CPU
// vendor; voltages/PPT/clocks and JEDEC sub-timings need ryzen_smu (AMD
// only, root) and are swapped for an honest note when s.smuSupported is
// false rather than showing zeros. Card layout mirrors TuxTimings' RAM tab
// rather than the flat StatGrid4 used elsewhere on this page, since these
// fields are naturally grouped categories.
static void DrawDramOcDetail(const Metrics& m, float width) {
    width = BeginExpertBox(width);
    ImGui::TextColored(Theme::kTextMuted, "DRAM / CPU telemetry");
    ImGui::Dummy(ImVec2(0, 6));

    if (!m.dramOcSupported) {
        ImGui::TextColored(Theme::kTextMuted,
            "Not active \xE2\x80\x94 needs ram_oc_daemon. Start it in Settings \xE2\x86\x92 Daemons.");
        EndExpertBox();
        return;
    }

    const DramOcSummary& s = m.dramOc;
    const DramOcSmu& sm = s.metrics;
    const DramOcTimings& d = s.dram;
    bool smu = s.smuSupported;

    char headerLine[1536];
    if (smu) {
        std::snprintf(headerLine, sizeof(headerLine), "%s  |  SMU %s  |  %s  |  %s",
                      s.cpuCodename.c_str(), s.smuVersion.c_str(), s.pmTableVersion.c_str(), s.boardDisplayLine.c_str());
    } else {
        std::snprintf(headerLine, sizeof(headerLine), "%s  |  %s",
                      s.cpuModel.empty() ? "Unknown CPU" : s.cpuModel.c_str(), s.boardDisplayLine.c_str());
    }
    ImGui::TextColored(Theme::kTextMuted, "%s", headerLine);
    ImGui::Dummy(ImVec2(0, 8));

    float gap = Theme::Scale(8.0f);
    float cardW = (width - gap * 2.0f) / 3.0f;
    float rowAH = Theme::Scale(300.0f);
    float rowBH = Theme::Scale(370.0f);

    // Row 1: DIMM config | Voltages (AMD) or Power (any vendor)
    DrawDimmConfigCard(s, 0, ImVec2(cardW, rowAH));
    ImGui::SameLine(0, gap);

    if (smu) {
        char vsoc[16], vddp[16], vddgCcd[16], vddgIod[16], vddMisc[16], memVdd[16], memVddq[16], cpuVddio[16], memVpp[16], vcore[16], ppt[16];
        FmtVolt(vsoc, sizeof(vsoc), sm.vsoc);
        FmtVolt(vddp, sizeof(vddp), sm.vddp);
        FmtVolt(vddgCcd, sizeof(vddgCcd), sm.vddgCcd);
        FmtVolt(vddgIod, sizeof(vddgIod), sm.vddgIod);
        FmtVolt(vddMisc, sizeof(vddMisc), sm.vddMisc);
        FmtVolt(memVdd, sizeof(memVdd), sm.memVdd);
        FmtVolt(memVddq, sizeof(memVddq), sm.memVddq);
        FmtVolt(cpuVddio, sizeof(cpuVddio), sm.cpuVddio);
        FmtVolt(memVpp, sizeof(memVpp), sm.memVpp);
        FmtVolt(vcore, sizeof(vcore), sm.vcore);
        std::snprintf(ppt, sizeof(ppt), "%.1f W", sm.pptW);
        const char* voltLabels[] = {"VSOC", "CLDO VDDP", "VDDG CCD", "VDDG IOD", "VDD MISC",
            "VDram", "MEM VDDQ", "CPU VDDIO", "MEM VPP", "VCORE", "PPT"};
        const char* voltValues[] = {vsoc, vddp, vddgCcd, vddgIod, vddMisc, memVdd, memVddq, cpuVddio, memVpp, vcore, ppt};
        DrawKVCard("volts", "Voltages", voltLabels, voltValues, 11, ImVec2(cardW, rowAH));
    } else {
        char pkgPower[24]; FmtOrNA(pkgPower, sizeof(pkgPower), "%.1f W", s.packagePowerW);
        const char* powLabels[] = {"Package Power (RAPL)", "Voltages"};
        const char* powValues[] = {pkgPower, "AMD only (ryzen_smu)"};
        DrawKVCard("power", "Power", powLabels, powValues, 2, ImVec2(cardW, rowAH));
    }

    // All DIMM info cards together in their own row, wrapping every 3, so a
    // system with more than 2 sticks still lines them up side by side
    // instead of splitting module 0 off into the row above.
    ImGui::Dummy(ImVec2(0, gap));
    if (!s.modules.empty()) {
        for (int i = 0; i < (int)s.modules.size(); ++i) {
            if (i % 3 != 0) ImGui::SameLine(0, gap);
            DrawDimmInfoCard(s.modules[i], d, i, s.modules.size() > 1, ImVec2(cardW, rowAH));
        }
    } else {
        DrawKVCard("dimminfo_none", "DIMM", nullptr, nullptr, 0, ImVec2(cardW, rowAH));
    }

    // Row 2: Primary | Secondary | Tertiary timings (AMD SMN only — no
    // portable equivalent, so this stays an honest note on other vendors
    // instead of a wall of misleading zeros).
    ImGui::Dummy(ImVec2(0, gap));
    if (smu) {
        char tcl[8], trcdrd[8], trcdwr[8], trp[8], tras[8], trc[8], trrds[8], trrdl[8], tfaw[8], twr[8], tcwl[8], trfcns[16], rfc[8], rfc2[8], rfcsb[8];
        FmtU32(tcl, sizeof(tcl), d.tcl); FmtU32(trcdrd, sizeof(trcdrd), d.trcdRd); FmtU32(trcdwr, sizeof(trcdwr), d.trcdWr);
        FmtU32(trp, sizeof(trp), d.trp); FmtU32(tras, sizeof(tras), d.tras); FmtU32(trc, sizeof(trc), d.trc);
        FmtU32(trrds, sizeof(trrds), d.trrds); FmtU32(trrdl, sizeof(trrdl), d.trrdl); FmtU32(tfaw, sizeof(tfaw), d.tfaw);
        FmtU32(twr, sizeof(twr), d.twr); FmtU32(tcwl, sizeof(tcwl), d.tcwl);
        std::snprintf(trfcns, sizeof(trfcns), "%.0f", d.trfcNs);
        FmtU32(rfc, sizeof(rfc), d.rfc); FmtU32(rfc2, sizeof(rfc2), d.rfc2); FmtU32(rfcsb, sizeof(rfcsb), d.rfcsb);
        const char* primLabels[] = {"tCL", "tRCDRD", "tRCDWR", "tRP", "tRAS", "tRC", "tRRDS", "tRRDL",
            "tFAW", "tWR", "tCWL", "tRFC (ns)", "tRFC", "tRFC2", "tRFCsb"};
        const char* primValues[] = {tcl, trcdrd, trcdwr, trp, tras, trc, trrds, trrdl, tfaw, twr, tcwl, trfcns, rfc, rfc2, rfcsb};
        DrawKVCard("prim", "Primary Timings", primLabels, primValues, 15, ImVec2(cardW, rowBH));
        ImGui::SameLine(0, gap);

        char rtp[8], wtrs[8], wtrl[8], rdwr[8], wrrd[8], rdrdsc[8], rdrdsd[8], rdrddd[8], wrwrsc[8], wrwrsd[8], wrwrdd[8], refi[8], trefins[16], wrpre[8], rdpre[8];
        FmtU32(rtp, sizeof(rtp), d.rtp); FmtU32(wtrs, sizeof(wtrs), d.wtrs); FmtU32(wtrl, sizeof(wtrl), d.wtrl);
        FmtU32(rdwr, sizeof(rdwr), d.rdwr); FmtU32(wrrd, sizeof(wrrd), d.wrrd);
        FmtU32(rdrdsc, sizeof(rdrdsc), d.rdrdSc); FmtU32(rdrdsd, sizeof(rdrdsd), d.rdrdSd); FmtU32(rdrddd, sizeof(rdrddd), d.rdrdDd);
        FmtU32(wrwrsc, sizeof(wrwrsc), d.wrwrSc); FmtU32(wrwrsd, sizeof(wrwrsd), d.wrwrSd); FmtU32(wrwrdd, sizeof(wrwrdd), d.wrwrDd);
        FmtU32(refi, sizeof(refi), d.refi);
        std::snprintf(trefins, sizeof(trefins), "%.0f", d.trefiNs);
        FmtU32(wrpre, sizeof(wrpre), d.wrpre); FmtU32(rdpre, sizeof(rdpre), d.rdpre);
        const char* secLabels[] = {"tRTP", "tWTRS", "tWTRL", "tRDWR", "tWRRD", "tRDRDSC", "tRDRDSD",
            "tRDRDDD", "tWRWRSC", "tWRWRSD", "tWRWRDD", "tREFI", "tREFI (ns)", "tWRPRE", "tRDPRE"};
        const char* secValues[] = {rtp, wtrs, wtrl, rdwr, wrrd, rdrdsc, rdrdsd, rdrddd, wrwrsc, wrwrsd, wrwrdd, refi, trefins, wrpre, rdpre};
        DrawKVCard("sec", "Secondary Timings", secLabels, secValues, 15, ImVec2(cardW, rowBH));
        ImGui::SameLine(0, gap);

        char rdrdscl[8], wrwrscl[8], cke[8], xp[8], trcpage[8], mod[8], modpda[8], mrd[8], mrdpda[8], stag[8], stagsb[8], phywrl[8], phyrdl[8], phywrd[8];
        FmtU32(rdrdscl, sizeof(rdrdscl), d.rdrdScl); FmtU32(wrwrscl, sizeof(wrwrscl), d.wrwrScl);
        FmtU32(cke, sizeof(cke), d.cke); FmtU32(xp, sizeof(xp), d.xp); FmtU32(trcpage, sizeof(trcpage), d.trcPage);
        FmtU32(mod, sizeof(mod), d.mod); FmtU32(modpda, sizeof(modpda), d.modPda);
        FmtU32(mrd, sizeof(mrd), d.mrd); FmtU32(mrdpda, sizeof(mrdpda), d.mrdPda);
        FmtU32(stag, sizeof(stag), d.stag); FmtU32(stagsb, sizeof(stagsb), d.stagSb);
        FmtU32(phywrl, sizeof(phywrl), d.phyWrl); FmtU32(phyrdl, sizeof(phyrdl), d.phyRdl); FmtU32(phywrd, sizeof(phywrd), d.phyWrd);
        const char* tertLabels[] = {"tRDRDSCL", "tWRWRSCL", "tCKE", "tXP", "tRCPAGE", "tMOD", "tMODPDA",
            "tMRD", "tMRDPDA", "tSTAG", "tSTAGsb", "tPHYWRL", "tPHYRDL", "tPHYWRD"};
        const char* tertValues[] = {rdrdscl, wrwrscl, cke, xp, trcpage, mod, modpda, mrd, mrdpda, stag, stagsb, phywrl, phyrdl, phywrd};
        DrawKVCard("tert", "Tertiary Timings", tertLabels, tertValues, 14, ImVec2(cardW, rowBH));
    } else {
        const char* noteLabels[] = {"Status"};
        const char* noteValues[] = {"AMD only (ryzen_smu/SMN) - needs root, or unsupported CPU"};
        DrawKVCard("timings_note", "DRAM Sub-Timings", noteLabels, noteValues, 1, ImVec2(width, Theme::Scale(80.0f)));
    }

    // Footer: source note left, memory type badge right (matches TuxTimings).
    ImGui::Dummy(ImVec2(0, 6));
    const char* memType = s.memoryType.empty() ? "Unknown" : s.memoryType.c_str();
    if (smu) ImGui::TextColored(Theme::kTextMuted, "DRAM timings & MCLK/UCLK: SMN. Voltages & FCLK: PM table.");
    else     ImGui::TextColored(Theme::kTextMuted, "DIMM/SPD & memory speed: dmidecode. Package power: RAPL.");
    ImGui::SameLine(width - ImGui::CalcTextSize(memType).x - 4.0f);
    ImGui::TextColored(Theme::kAccentCpu, "%s", memType);

    EndExpertBox();
}

void DrawMemoryDetail(const Metrics& m, float width) {
    char valueText[16]; std::snprintf(valueText, sizeof(valueText), "%.1f%%", 100.0f * m.memUsedGB / m.memTotalGB);
    Widgets::DetailHeader("Memory", nullptr, m.memUsedGB / m.memTotalGB, valueText, Theme::kAccentMemory, width);
    ImGui::TextColored(Theme::kTextMuted, "%.0f GB total", m.memTotalGB);
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextColored(Theme::kTextMuted, "Memory utilization");

    PushPlotTheme();
    if (ImPlot::BeginPlot("##mem_big", ImVec2(width, 220), ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
        PlotFilledLine("mem", m.memHistory, Theme::kAccentMemory);
        ImPlot::EndPlot();
    }
    PopPlotTheme();

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextColored(Theme::kTextMuted, "Scrolling history");
    ImGui::Dummy(ImVec2(0, 6));

    char inUse[16], avail[16], committed[16], cached[16], buffers[16];
    std::snprintf(inUse, sizeof(inUse), "%.2f GB", m.memUsedGB);
    std::snprintf(avail, sizeof(avail), "%.2f GB", m.memAvailableGB);
    std::snprintf(committed, sizeof(committed), "%.2f GB", m.memCommittedGB);
    std::snprintf(cached, sizeof(cached), "%.2f GB", m.memCachedGB);
    std::snprintf(buffers, sizeof(buffers), "%.2f GB", m.memBuffersGB);
    char swapUsed[16], swapTotal[16], pressureLoad[16];
    std::snprintf(swapUsed, sizeof(swapUsed), "%.2f GB", m.memSwapUsedGB);
    std::snprintf(swapTotal, sizeof(swapTotal), "%.2f GB", m.memSwapTotalGB);
    std::snprintf(pressureLoad, sizeof(pressureLoad), "%.1f%%", m.memPressurePct);
    char pageIns[24], pageOuts[24];
    std::snprintf(pageIns, sizeof(pageIns), "%ld", m.memPageIns);
    std::snprintf(pageOuts, sizeof(pageOuts), "%ld", m.memPageOuts);

    const char* labels[] = {"In use", "Available", "Cached", "Buffers",
        "Committed", "Swap used", "Swap total", "Pressure",
        "Pressure load", "Page ins", "Page outs"};
    const char* values[] = {inUse, avail, cached, buffers,
        committed, swapUsed, swapTotal, m.memPressureText.c_str(),
        pressureLoad, pageIns, pageOuts};
    StatGrid4(labels, values, 11);

    if (Theme::ExpertMode) DrawDramOcDetail(m, width);
}
