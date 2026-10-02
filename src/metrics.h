#pragma once
#include <atomic>
#include <vector>
#include <deque>
#include <string>
#include <unordered_map>
#include <cstdint>
#include "dram_oc/dram_oc.h" // DramOcSummary — read via the ram_oc_helper subprocess, see dram_oc/dram_oc.h
#include "gpu_ctl/gpu_ctl.h" // GpuCtlState — gpu_ctl_daemon reachability + caps, see gpu_ctl/gpu_ctl.h
#include "benchmark_controller.h" // BenchmarkController — Benchmarks page orchestration, see Metrics::bench
#include "metrics/process_detail.h" // ProcessDetail — Processes page "Properties" popup, on-demand only

// Holds current + historical system metrics for the whole app (Summary,
// Performance detail pages, Processes). Real reads ported from
// vantage/src/SysStats.cpp (Linux /proc + /sys), minus the Qt dependency.
// CPU/GPU/motherboard temps come from /sys/class/hwmon; GPU usage from
// amdgpu's gpu_busy_percent or `nvidia-smi`. NPU/energy-watts genuinely
// have no generic Linux source, so those stay simulated/placeholder —
// see Metrics::Update().
// Capacity all history buffers are sized to, regardless of what's actually
// plotted (see Theme::HistoryWindowSamples) — 10 minutes of samples at the
// fastest allowed refresh rate (0.1s), so the longest history-length preset
// in Settings always has real data to show instead of being silently capped
// by buffer size.
inline constexpr size_t kMaxHistorySamples = 6000;

struct RingBuffer {
    std::deque<float> data;
    size_t maxSize;
    explicit RingBuffer(size_t n = kMaxHistorySamples) : maxSize(n) {}
    void Push(float v) {
        data.push_back(v);
        if (data.size() > maxSize) data.pop_front();
    }
    std::vector<float> ToVector() const { return std::vector<float>(data.begin(), data.end()); }
};

struct ProcessRow {
    int pid = 0;
    std::string name;
    float cpuPct = 0.0f;
    std::string memory;   // formatted for display, e.g. "12.3 MB"
    uint64_t memoryKb = 0; // raw value memory is formatted from, for numeric sorting
    std::string user;
    std::string status;
    float gpuPct = 0.0f;    // from /proc/<pid>/fdinfo drm-engine-* busy time; 0 if no DRM fd or driver doesn't report it
    std::string vram;       // formatted, e.g. "128.0 MB"
    uint64_t vramKb = 0;    // raw value vram is formatted from, for numeric sorting
};

struct CoreMetric {
    float utilization = 0.0f;
    RingBuffer utilHistory;
    RingBuffer kernelHistory;
};

struct EnergyProcessRow { std::string name; int score; };

struct ThermalSensor {
    std::string label;
    float tempC = 0.0f;
    bool available = false;
};

struct DiskVolume {
    std::string device;
    std::string mountPoint;
    std::string fsType;
    double totalGB = 0.0;
    double usedGB = 0.0;
    double freeGB = 0.0;
    float usedPct = 0.0f;
};

struct SystemUser {
    std::string name;
    int uid = 0;
    std::string homeDir;
    std::string shell;
    bool loggedIn = false;
};

struct ServiceUnit {
    std::string name;
    std::string load;
    std::string active;
    std::string sub;
    std::string description;
};

// One parsed XDG .desktop entry — shared by the Startup apps and Installed
// apps pages (same file format, different search directories).
struct DesktopApp {
    std::string name;
    std::string exec;
    std::string comment;
    std::string path; // source .desktop file, so it can be toggled/added/removed
    bool enabled = true; // startup apps only: false if Hidden/X-GNOME-Autostart-enabled=false
};

struct Metrics {
    // ---- headline values (Summary page) ----
    float cpuPct = 0.0f;
    float cpuKernelPct = 0.0f;
    float avgMHz = 0.0f;
    float maxMHz = 5000.0f;        // real: cpuinfo_max_freq (kHz->MHz); generic default until read
    float tempC = 0.0f;            // real: hwmon (k10temp/coretemp/zenpower), thermal_zone fallback
    float maxTempC = 95.0f;        // Tjmax for the detected CPU model (see metrics.cpp MaxTempForCpu); generic default otherwise
    float gpuPct = 0.0f;           // real via amdgpu gpu_busy_percent or nvidia-smi; 0 if neither present
    float memUsedGB = 0.0f;
    float memTotalGB = 1.0f;
    float diskPct = 0.0f;
    float networkKBs = 0.0f;
    float energyW = 0.0f;         // real when hwmon exposes package power; otherwise simulated
    float npuPct = 0.0f;          // simulated: no generic Linux source
    float gpuFanRpm = 0.0f;
    int logicalProcessors = 1;
    int processCount = 0;
    std::string gpuName = "GPU 0"; // set once from glGetString(GL_RENDERER)
    std::string gpuSource = "unavailable"; // "amdgpu" / "nvidia-smi" / "unavailable"

    // ---- System identity (System Info page) — doesn't change at runtime,
    // read once on the first Update() call; see ReadSystemIdentity() ----
    bool identityRead = false;
    std::string kernelVersion = "Unknown";
    std::string hostname = "Unknown";
    std::string userName = "Unknown";
    std::string osName = "Unknown";
    std::string platform = "Unknown";

    RingBuffer cpuHistory;
    RingBuffer tempHistory;
    RingBuffer kernelHistory;
    RingBuffer memHistory;
    RingBuffer gpuHistory;

    std::vector<ProcessRow> topProcesses;   // short list for Summary card
    std::vector<ProcessRow> allProcesses;   // full list for Processes page

    // ---- CPU detail (Performance > CPU) ----
    std::vector<CoreMetric> cores; // size == logicalProcessors
    std::string upTime = "0:00:00:00";
    int threadCount = 0;
    std::string baseSpeed = "Unavailable";
    std::string cpuModel = "Unknown CPU"; // /proc/cpuinfo "model name"
    int physicalCores = 1;
    int sockets = 1;
    std::string virtualization = "Unavailable";
    std::string virtualMachine = "Unavailable";
    std::string l1Cache = "Unavailable";
    std::string l2Cache = "Unavailable";
    std::string l3Cache = "Unavailable";
    std::string freqDriver = "Unavailable";
    std::string freqGovernor = "Unavailable";
    std::string powerPreference = "Unavailable";

    // ---- Memory detail ----
    float memAvailableGB = 0.0f;
    float memCommittedGB = 0.0f;
    float memCachedGB = 0.0f;
    float memBuffersGB = 0.0f;
    float memSwapUsedGB = 0.0f;
    float memSwapTotalGB = 0.0f;
    long memPageIns = 0;
    long memPageOuts = 0;
    float memPressurePct = 0.0f;          // simulated: needs /proc/pressure (PSI), may be absent
    std::string memPressureText = "Unavailable";

    // ---- Memory: DRAM/CPU telemetry, see dram_oc.h. dramOcSupported gates the
    // whole feature (daemon reachable at all); dramOc.smuSupported further gates
    // the AMD-only fields (voltages/PPT/clocks/JEDEC timings, needs root). Memory
    // speed/type, DIMM/SPD info, per-core temp/usage/freq, and package power
    // (RAPL) work on any CPU vendor whenever dramOcSupported is true. ----
    bool dramOcSupported = false;
    DramOcSummary dramOc{};

    // ---- GPU: gpu_ctl_daemon reachability + per-GPU caps, see gpu_ctl/gpu_ctl.h.
    // Live per-GPU config (GET_CONFIG/SET_CONFIG/CONFIRM/REVERT) is fetched
    // on demand from the GPU Controls section (gpu.cpp), not cached here. ----
    GpuCtlState gpuCtl{};

    // ---- Disk detail ----
    float diskActivePct = 0.0f;   // real %util, from /proc/diskstats "ms doing IO"
    float diskReadKBs = 0.0f;
    float diskWriteKBs = 0.0f;
    int diskCount = 0;
    RingBuffer diskActiveHistory;
    RingBuffer diskTransferHistory;

    // ---- Network detail ----
    RingBuffer netHistory;
    RingBuffer netSpikeHistory;  // left unpushed: no second real signal to overlay
    float netReceiveKBs = 0.0f;
    float netSendKBs = 0.0f;
    float netTotalReceivedGB = 0.0f;
    float netTotalSentGB = 0.0f;
    std::string netInterfaceCount = "0 network interfaces combined";

    // ---- Energy detail ----
    RingBuffer energyHistory;        // simulated: no generic Linux source
    std::string thermalState = "Unavailable";
    std::string powerMode = "Unavailable";
    std::string powerSource = "Unavailable";
    std::vector<EnergyProcessRow> topEnergyProcesses; // simulated: left empty, no generic source

    // ---- Laptop vs desktop: a battery under /sys/class/power_supply is the
    // signal (desktops have none). Drives whether Summary shows battery charge
    // or GPU power in the energy slot. ----
    bool hasBattery = false;
    float batteryPct = 0.0f;                     // 0..1, unused when hasBattery is false
    std::string batteryStatus = "Unavailable";    // "Charging" / "Discharging" / "Full" / ...
    RingBuffer batteryHistory;               // pushed only while hasBattery is true

    // ---- Thermals detail: real per-source temps via /sys/class/hwmon ----
    // (thermal_zone is a poor primary source — plenty of desktop boards,
    // this dev machine included, expose zero thermal_zone entries at all).
    RingBuffer hotspotHistory;                    // == tempHistory, CPU hotspot trace
    float gpuTempC = 0.0f;                             // 0 if no matching hwmon chip
    float boardTempC = 0.0f;                            // 0 if no Super I/O chip exposed
    float gpuCoreClockMHz = 0.0f;
    float gpuVramClockMHz = 0.0f;
    float gpuVoltageV = 0.0f;
    float gpuPowerLimitW = 0.0f;
    RingBuffer gpuTempHistory;
    RingBuffer boardTempHistory;
    std::vector<ThermalSensor> thermalSensors;
    std::unordered_map<std::string, RingBuffer> thermalSensorHistory; // keyed by ThermalSensor::label, normalized like gpuTempHistory
    float gpuVramUsedGB = 0.0f;                          // amdgpu only; 0 otherwise
    float gpuVramTotalGB = 0.0f;
    std::string thermalPressureText = "Unavailable";

    // ---- Disk Space page: real mounted volumes, /proc/mounts + statvfs ----
    std::vector<DiskVolume> diskVolumes;

    // ---- Users page: /etc/passwd + utmpx logged-in state ----
    std::vector<SystemUser> systemUsers;

    // ---- Services page: systemd units via `systemctl list-units` ----
    std::vector<ServiceUnit> services;

    // ---- Startup apps / Installed apps: XDG .desktop files ----
    std::vector<DesktopApp> startupApps;
    std::vector<DesktopApp> installedApps;

    // ---- Benchmarks page: all orchestration (spawning threads/subprocesses,
    // Run*/Stop* methods, results) lives on BenchmarkController, not here —
    // see benchmark_controller.h for why. Pages call m.bench.RunCpu(...) etc.
    BenchmarkController bench;

    double t = 0.0;

    void Update(double dt);
    void RescanApps(); // re-reads startupApps/installedApps after an add/enable/disable/remove

private:
    struct CpuTimes { uint64_t idle = 0, total = 0, kernel = 0; };
    CpuTimes prevTotal_;
    std::vector<CpuTimes> prevPerCore_;
    uint64_t prevRxBytes_ = 0, prevTxBytes_ = 0;
    uint64_t prevReadSectors_ = 0, prevWriteSectors_ = 0;
    uint64_t prevIoMs_ = 0;
    bool haveBaseline_ = false;
    std::unordered_map<int, uint64_t> prevProcJiffies_;
    std::unordered_map<int, uint64_t> prevProcGpuNs_;
    double diskVolumesRefreshT_ = -1000.0;
    double servicesRefreshT_ = -1000.0;
    double dramOcRefreshT_ = -1000.0; // ReadDramOc() now forks a subprocess; throttle like the readers above
    double gpuCtlRefreshT_ = -1000.0; // RefreshGpuCtl() is a socket round-trip; throttled like dramOcRefreshT_
    bool appsRead_ = false; // startupApps/installedApps/systemUsers: read once, rarely change
    void ScanApps(); // shared by Update()'s first-run read and RescanApps()
};
