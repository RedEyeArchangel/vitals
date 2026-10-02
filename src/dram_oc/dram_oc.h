#pragma once

// DRAM/CPU telemetry. Memory speed/type, DIMM/SPD info, per-core temp/usage/
// freq, and package power (RAPL) are read generically and work on any CPU
// vendor. Voltages, PPT, FCLK/UCLK/MCLK, and JEDEC DRAM sub-timings come from
// the ryzen_smu/aod_voltages kernel modules and are AMD-only, needs root —
// see DramOcSummary::smuSupported below.
//
// The actual reading happens in a separate GPLv3 process (ram_oc_daemon,
// built from the vendored ram_oc/ sources — see ram_oc_helper/daemon_main.c)
// so that GPL code never gets compiled into this binary. ReadDramOc() reads
// a lock-free snapshot from the POSIX shared-memory frame the daemon keeps
// refreshed (see include/ipc/ram_oc_shm.h for the wire contract), spawning
// the daemon on first use if it isn't already running. These struct field
// names mirror ram_oc's grouping (same names as the physical properties
// they represent — vcore, tCL, etc. — not copyrightable expression) so
// callers read naturally, but the types themselves, and the IPC that fills
// them, are original to vitals.
//
// This file is domain-only: the telemetry model plus the two functions that
// fill it. The daemon lifecycle/socket/shm-attach mechanics live in
// dram_oc/daemon_ipc.h — ReadDramOc()/RunRemoteBenchmark() are implemented
// in terms of that, not the other way around, so the trickiest, most
// Linux-specific part of this codebase (SO_PEERCRED, root-vs-nonroot daemon
// replacement, mmap lifetime) can be read/reviewed without the DRAM
// register-decoding noise around it.
#include <cstdint>
#include <string>
#include <vector>

struct DramOcModule {
    std::string capacityDisplay, manufacturer, partNumber, serialNumber, slotDisplay, rank;
};

struct DramOcFan {
    std::string label;
    int rpm = 0;
};

struct DramOcSmu {
    float pptW = 0.0f, vcore = 0.0f, vsoc = 0.0f, vddp = 0.0f, vddgCcd = 0.0f, vddgIod = 0.0f, vddMisc = 0.0f;
    float cpuVddio = 0.0f, memVdd = 0.0f, memVddq = 0.0f, memVpp = 0.0f, vid = 0.0f;
    float bclkMhz = 0.0f, fclkMhz = 0.0f, uclkMhz = 0.0f, mclkMhz = 0.0f;
    bool hasTdie = false;        float tdieC = 0.0f;
    bool hasTctl = false;        float tctlC = 0.0f;
    bool hasTccd1 = false;       float tccd1C = 0.0f;
    bool hasTccd2 = false;       float tccd2C = 0.0f;
    bool hasIodHotspot = false;  float iodHotspotC = 0.0f;
    std::vector<float> coreVoltages, coreTempsC, coreUsagePct, coreFreqMhz, spdTempsC;
};

struct DramOcTimings {
    uint32_t tcl = 0, trcdRd = 0, trcdWr = 0, trp = 0, tras = 0, trc = 0;
    uint32_t trrds = 0, trrdl = 0, tfaw = 0, twr = 0, tcwl = 0;
    uint32_t rtp = 0, wtrs = 0, wtrl = 0, rdwr = 0, wrrd = 0;
    uint32_t rdrdScl = 0, wrwrScl = 0;
    uint32_t rdrdSc = 0, rdrdSd = 0, rdrdDd = 0;
    uint32_t wrwrSc = 0, wrwrSd = 0, wrwrDd = 0;
    uint32_t refi = 0, wrpre = 0, rdpre = 0;
    uint32_t trcPage = 0, mod = 0, modPda = 0, mrd = 0, mrdPda = 0;
    uint32_t stag = 0, stagSb = 0, cke = 0, xp = 0;
    uint32_t phyWrd = 0, phyWrl = 0, phyRdl = 0;
    uint32_t rfc = 0, rfc2 = 0, rfcsb = 0;
    float trefiNs = 0.0f, trfcNs = 0.0f;
    bool gdmEnabled = false, powerDownEnabled = false;
    std::string cmd2t;
};

struct DramOcSummary {
    // true only when ryzen_smu (AMD) is available: gates cpuCodename, smuVersion,
    // pmTableVersion, metrics (voltages/PPT/clocks), and dram (JEDEC sub-timings).
    // Everything else here (cpuModel, board/DIMM info, memoryFrequency/Type,
    // per-core temp/usage/freq inside metrics, packagePowerW, fans) is real on
    // any CPU vendor whenever ReadDramOc() returns true.
    bool smuSupported = false;
    std::string cpuCodename, cpuModel, smuVersion, pmTableVersion, boardDisplayLine;
    float memoryFrequency = 0.0f;
    float packagePowerW = 0.0f; // AMD: PM table PPT. Intel/other: RAPL. 0 if neither.
    std::string memoryType;
    DramOcSmu metrics;
    DramOcTimings dram;
    std::vector<DramOcModule> modules;
    std::vector<DramOcFan> fans;
};

// Reads a lock-free snapshot of the shared-memory telemetry frame (see
// include/ipc/ram_oc_shm.h) into `out`, spawning/attaching ram_oc_daemon on
// first call if needed. Returns false (leaving `out` default) only if the
// daemon/segment can't be reached at all. When it returns true, the
// vendor-neutral fields (see DramOcSummary above) are always real; check
// out.smuSupported before reading the AMD-only ones.
bool ReadDramOc(DramOcSummary& out);

// Sends CMD_RUN_BENCHMARK to ram_oc_daemon over its control socket and
// blocks (on the caller's thread — call this from a background thread, same
// as BenchmarkController::RunMemory() already does) until the daemon's
// worker thread finishes bench_run() and replies. Fields match
// VitalsBenchResultPOD (include/ipc/ram_oc_shm.h): latL1Ns/latL2Ns/latL3Ns/
// latDramNs (ns), bwReadMBs/bwWriteMBs/bwCopyMBs (MB/s). Returns false on
// any IPC failure.
struct RemoteBenchResult {
    double latL1Ns = 0.0, latL2Ns = 0.0, latL3Ns = 0.0, latDramNs = 0.0;
    double bwReadMBs = 0.0, bwWriteMBs = 0.0, bwCopyMBs = 0.0;
};
bool RunRemoteBenchmark(RemoteBenchResult& out);
