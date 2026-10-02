#include "dram_oc/dram_oc.h"
#include "dram_oc/daemon_ipc.h"
#include "ipc/ram_oc_shm.h"

bool ReadDramOc(DramOcSummary& out) {
    out = DramOcSummary{};

    const volatile VitalsTelemetryFrame* frame = GetDaemonTelemetryFrame();
    if (!frame) return false;

    VitalsTelemetryFrame local;
    bool ok = false;
    for (int attempt = 0; attempt < 5 && !ok; ++attempt) ok = vitals_shm_try_read(frame, &local);
    if (!ok || !local.supported) return false;

    out.smuSupported = local.smu_supported;
    out.cpuCodename = local.cpu_codename;
    out.cpuModel = local.cpu_model;
    out.smuVersion = local.smu_version;
    out.pmTableVersion = local.pm_table_version;
    out.boardDisplayLine = local.board_display_line;
    out.memoryFrequency = local.memory_frequency;
    out.memoryType = local.memory_type;
    out.packagePowerW = local.package_power_w;

    DramOcSmu& sm = out.metrics;
    sm.pptW = local.ppt_w; sm.vcore = local.vcore; sm.vsoc = local.vsoc;
    sm.vddp = local.vddp; sm.vddgCcd = local.vddg_ccd; sm.vddgIod = local.vddg_iod;
    sm.vddMisc = local.vdd_misc; sm.cpuVddio = local.cpu_vddio;
    sm.memVdd = local.mem_vdd; sm.memVddq = local.mem_vddq; sm.memVpp = local.mem_vpp;
    sm.vid = local.vid;
    sm.bclkMhz = local.bclk_mhz; sm.fclkMhz = local.fclk_mhz;
    sm.uclkMhz = local.uclk_mhz; sm.mclkMhz = local.mclk_mhz;
    sm.hasTdie = local.has_tdie; sm.tdieC = local.tdie_c;
    sm.hasTctl = local.has_tctl; sm.tctlC = local.tctl_c;
    sm.hasTccd1 = local.has_tccd1; sm.tccd1C = local.tccd1_c;
    sm.hasTccd2 = local.has_tccd2; sm.tccd2C = local.tccd2_c;
    sm.hasIodHotspot = local.has_iod_hotspot; sm.iodHotspotC = local.iod_hotspot_c;

    sm.coreVoltages.assign(local.core_voltages, local.core_voltages + local.core_voltage_count);
    sm.coreTempsC.assign(local.core_temps_c, local.core_temps_c + local.core_temp_count);
    sm.coreUsagePct.assign(local.core_usage_pct, local.core_usage_pct + local.core_usage_count);
    sm.coreFreqMhz.assign(local.core_freq_mhz, local.core_freq_mhz + local.core_freq_count);
    sm.spdTempsC.assign(local.spd_temps_c, local.spd_temps_c + local.spd_temp_count);

    out.modules.resize(local.module_count);
    for (uint32_t i = 0; i < local.module_count; ++i) {
        const VitalsDramModulePOD& src = local.modules[i];
        DramOcModule& dst = out.modules[i];
        dst.manufacturer = src.manufacturer;
        dst.partNumber = src.part_number;
        dst.serialNumber = src.serial_number;
        dst.capacityDisplay = src.capacity_display;
        dst.slotDisplay = src.slot_display;
        dst.rank = src.rank;
    }

    out.fans.resize(local.fan_count);
    for (uint32_t i = 0; i < local.fan_count; ++i) {
        out.fans[i].label = local.fans[i].label;
        out.fans[i].rpm = local.fans[i].rpm;
    }

    DramOcTimings& d = out.dram;
    d.tcl = local.tcl; d.trcdRd = local.trcd_rd; d.trcdWr = local.trcd_wr;
    d.trp = local.trp; d.tras = local.tras; d.trc = local.trc;
    d.trrds = local.trrds; d.trrdl = local.trrdl; d.tfaw = local.tfaw;
    d.twr = local.twr; d.tcwl = local.tcwl;
    d.rtp = local.rtp; d.wtrs = local.wtrs; d.wtrl = local.wtrl;
    d.rdwr = local.rdwr; d.wrrd = local.wrrd;
    d.rdrdScl = local.rdrd_scl; d.wrwrScl = local.wrwr_scl;
    d.rdrdSc = local.rdrd_sc; d.rdrdSd = local.rdrd_sd; d.rdrdDd = local.rdrd_dd;
    d.wrwrSc = local.wrwr_sc; d.wrwrSd = local.wrwr_sd; d.wrwrDd = local.wrwr_dd;
    d.refi = local.refi; d.wrpre = local.wrpre; d.rdpre = local.rdpre;
    d.trcPage = local.trc_page; d.mod = local.mod; d.modPda = local.mod_pda;
    d.mrd = local.mrd; d.mrdPda = local.mrd_pda;
    d.stag = local.stag; d.stagSb = local.stag_sb; d.cke = local.cke; d.xp = local.xp;
    d.phyWrd = local.phy_wrd; d.phyWrl = local.phy_wrl; d.phyRdl = local.phy_rdl;
    d.rfc = local.rfc; d.rfc2 = local.rfc2; d.rfcsb = local.rfcsb;
    d.trefiNs = local.trefi_ns; d.trfcNs = local.trfc_ns;
    d.gdmEnabled = local.gdm_enabled; d.powerDownEnabled = local.power_down_enabled;
    d.cmd2t = local.cmd2t;

    return true;
}

bool RunRemoteBenchmark(RemoteBenchResult& out) {
    VitalsBenchResultPOD pod{};
    if (!RunDaemonBenchmark(pod)) return false;

    out.latL1Ns = pod.lat_l1_ns;
    out.latL2Ns = pod.lat_l2_ns;
    out.latL3Ns = pod.lat_l3_ns;
    out.latDramNs = pod.lat_dram_ns;
    out.bwReadMBs = pod.bw_read_mbs;
    out.bwWriteMBs = pod.bw_write_mbs;
    out.bwCopyMBs = pod.bw_copy_mbs;
    return true;
}
