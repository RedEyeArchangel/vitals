#include "metrics.h"
#include "dram_oc/dram_oc.h"
#include "metrics/proc_util.h"
#include "metrics/thermal_sensors.h"
#include "metrics/gpu_sysfs.h"
#include "metrics/system_identity.h"
#include "metrics/disk_volumes.h"
#include "metrics/system_users.h"
#include "metrics/services.h"
#include "metrics/desktop_apps.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <set>
#include <unistd.h>
#include <vector>
#include <vector>

void Metrics::Update(double dt) {
    if (!identityRead) {
        ReadSystemIdentity(kernelVersion, hostname, userName, osName, platform);
        identityRead = true;
    }
    if (!appsRead_) {
        systemUsers = ReadSystemUsers();
        ScanApps();
        appsRead_ = true;
    }
    if (t - diskVolumesRefreshT_ > 2.0) {
        diskVolumes = ReadDiskVolumes();
        diskVolumesRefreshT_ = t;
    }
    if (t - servicesRefreshT_ > 3.0) {
        services = ReadServices();
        servicesRefreshT_ = t;
    }

    t += dt;
    logicalProcessors = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if ((int)prevPerCore_.size() != logicalProcessors) prevPerCore_.assign(logicalProcessors, {});
    if ((int)cores.size() != logicalProcessors) cores.assign(logicalProcessors, {});

    // --- CPU: "cpu  user nice system idle iowait irq softirq steal guest guest_nice" ---
    // and per-core "cpu0 ...", "cpu1 ..." lines right below it.
    {
        auto parseCpuLine = [](const std::string& line) -> CpuTimes {
            std::vector<std::string> parts = Split(line);
            std::vector<uint64_t> v;
            for (size_t i = 1; i < parts.size(); ++i) v.push_back(std::strtoull(parts[i].c_str(), nullptr, 10));
            CpuTimes ct;
            if (v.size() < 4) return ct;
            ct.idle = v[3] + (v.size() > 4 ? v[4] : 0);
            ct.kernel = v[2] + (v.size() > 5 ? v[5] : 0) + (v.size() > 6 ? v[6] : 0);
            for (uint64_t x : v) ct.total += x;
            return ct;
        };

        CpuTimes cur;
        int coreIdx = 0;
        for (const std::string& line : ReadLines("/proc/stat")) {
            if (line.rfind("cpu ", 0) == 0) {
                cur = parseCpuLine(line);
            } else if (line.rfind("cpu", 0) == 0 && line.size() > 3 && std::isdigit((unsigned char)line[3])) {
                if (coreIdx >= logicalProcessors) continue;
                CpuTimes c = parseCpuLine(line);
                if (haveBaseline_) {
                    const CpuTimes& p = prevPerCore_[coreIdx];
                    uint64_t dTotal = c.total - p.total;
                    uint64_t dIdle = c.idle - p.idle;
                    uint64_t dKernel = c.kernel - p.kernel;
                    float util = dTotal > 0 ? (float)(dTotal - dIdle) / (float)dTotal : 0.0f;
                    float kern = dTotal > 0 ? (float)dKernel / (float)dTotal : 0.0f;
                    cores[coreIdx].utilization = util;
                    cores[coreIdx].utilHistory.Push(util);
                    cores[coreIdx].kernelHistory.Push(kern);
                }
                prevPerCore_[coreIdx] = c;
                ++coreIdx;
            } else if (line.rfind("cpu", 0) != 0) {
                break; // cpu lines are always first in /proc/stat
            }
        }
        if (haveBaseline_) {
            uint64_t dTotal = cur.total - prevTotal_.total;
            uint64_t dIdle = cur.idle - prevTotal_.idle;
            uint64_t dKernel = cur.kernel - prevTotal_.kernel;
            cpuPct = dTotal > 0 ? (float)(dTotal - dIdle) / (float)dTotal : 0.0f;
            cpuKernelPct = dTotal > 0 ? (float)dKernel / (float)dTotal : 0.0f;
        }
        prevTotal_ = cur;
    }

    // --- Clock / cache / topology / virtualization: /proc/cpuinfo ---
    {
        double sum = 0.0;
        int count = 0;
        std::set<int> physIds, coreKeys;
        bool haveTopology = false;
        int curPhys = 0, curCore = 0;
        bool vmxSvm = false, hypervisor = false;
        std::string modelName;
        for (const std::string& line : ReadLines("/proc/cpuinfo")) {
            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string key = line.substr(0, colon);
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            std::string val = line.substr(colon + 1);
            if (key == "cpu MHz") { sum += std::strtod(val.c_str(), nullptr); ++count; }
            else if (key == "physical id") { curPhys = std::atoi(val.c_str()); physIds.insert(curPhys); haveTopology = true; }
            else if (key == "core id") { curCore = std::atoi(val.c_str()); coreKeys.insert(curPhys * 100000 + curCore); }
            else if (key == "model name" && modelName.empty()) { modelName = val; }
            else if (key == "flags" || key == "Features") {
                if (val.find("vmx") != std::string::npos || val.find("svm") != std::string::npos) vmxSvm = true;
                if (val.find("hypervisor") != std::string::npos) hypervisor = true;
            }
        }
        avgMHz = count > 0 ? (float)(sum / count) : 0.0f;
        sockets = haveTopology ? std::max(1, (int)physIds.size()) : 1;
        physicalCores = haveTopology ? std::max(1, (int)coreKeys.size()) : logicalProcessors;
        virtualization = vmxSvm ? "Supported" : "Not supported";
        virtualMachine = hypervisor ? "Yes" : "No";
        maxTempC = MaxTempForCpu(modelName);
        if (!modelName.empty()) cpuModel = modelName;
    }

    // --- Frequency scaling driver/governor/base speed: /sys/.../cpufreq ---
    {
        const char* cpu0 = "/sys/devices/system/cpu/cpu0/cpufreq";
        std::string driver = ReadTrimmed(std::string(cpu0) + "/scaling_driver");
        std::string governor = ReadTrimmed(std::string(cpu0) + "/scaling_governor");
        std::string epp = ReadTrimmed(std::string(cpu0) + "/energy_performance_preference");
        std::string maxFreq = ReadTrimmed(std::string(cpu0) + "/cpuinfo_max_freq");
        if (!driver.empty()) freqDriver = driver;
        if (!governor.empty()) freqGovernor = governor;
        if (!epp.empty()) powerPreference = epp;
        if (!maxFreq.empty()) {
            double maxKHz = std::strtod(maxFreq.c_str(), nullptr);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2f GHz", maxKHz / 1e6);
            baseSpeed = buf;
            if (maxKHz > 0.0) maxMHz = (float)(maxKHz / 1e3);
        }
    }

    // --- Cache sizes: /sys/devices/system/cpu/cpu0/cache/index{0,1,2,3} ---
    {
        auto cacheSize = [](int idx) -> std::string {
            char path[96];
            std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu0/cache/index%d/size", idx);
            std::string s = ReadTrimmed(path);
            return s;
        };
        std::string l1d = cacheSize(0);
        std::string l2 = cacheSize(2);
        std::string l3 = cacheSize(3);
        if (!l1d.empty()) l1Cache = l1d;
        if (!l2.empty()) l2Cache = l2;
        if (!l3.empty()) l3Cache = l3;
    }

    // --- Uptime ---
    {
        std::string raw = ReadFile("/proc/uptime");
        if (!raw.empty()) upTime = FormatUptime(std::strtod(raw.c_str(), nullptr));
    }

    // --- Temps: CPU / GPU / motherboard, all real via /sys/class/hwmon ---
    std::vector<HwmonChip> hwmon = EnumerateHwmon();
    {
        static const std::set<std::string> kCpuChips = {"k10temp", "coretemp", "zenpower", "cpu_thermal"};
        static const std::set<std::string> kGpuChips = {"amdgpu", "nouveau", "nvidia", "i915", "xe"};
        static const std::set<std::string> kBoardChips = {
            "nct6775", "nct6779", "nct6683", "nct6791", "it87", "w83627ehf", "w83627dhg",
            "f71882fg", "f71889fg", "w83795", "dme1737", "pc87360", "asus_wmi_sensors",
            "dell_smm", "applesmc", "atk0110"
        };

        double cpuT = HwmonTempByNames(hwmon, kCpuChips);
        tempC = cpuT >= 0.0 ? (float)cpuT : std::max(0.0f, (float)ReadFallbackThermalZoneTemp());

        double gpuT = HwmonTempByNames(hwmon, kGpuChips);
        gpuTempC = gpuT >= 0.0 ? (float)gpuT : 0.0f;

        double boardT = HwmonTempByNames(hwmon, kBoardChips);
        boardTempC = boardT >= 0.0 ? (float)boardT : 0.0f;

        thermalSensors = ReadThermalSensors();
        for (const ThermalSensor& s : thermalSensors) {
            auto it = thermalSensorHistory.try_emplace(s.label, kMaxHistorySamples).first;
            it->second.Push(std::min(1.0f, s.tempC / 110.0f));
        }
        gpuFanRpm = ReadAmdGpuFanRpm();
        ReadGpuSysfsClocks(&gpuCoreClockMHz, &gpuVramClockMHz, &gpuVoltageV);
        gpuPowerLimitW = ReadAmdGpuPowerLimitWatts();

        thermalPressureText = tempC >= 90.0f ? "Serious" : tempC >= 75.0f ? "Elevated" : "Nominal";
    }
    hotspotHistory.Push(std::min(1.0f, tempC / 110.0f));
    gpuTempHistory.Push(std::min(1.0f, gpuTempC / 110.0f));
    boardTempHistory.Push(std::min(1.0f, boardTempC / 110.0f));

    // --- Power source: AC vs battery, from /sys/class/power_supply ---
    // The presence of a Battery node is also how we tell laptop from desktop:
    // desktops normally expose none.
    {
        std::string src = "Unavailable";
        DIR* d = opendir("/sys/class/power_supply");
        if (d) {
            struct dirent* ent;
            bool onAc = false, sawBattery = false;
            std::string batteryBase;
            while ((ent = readdir(d)) != nullptr) {
                std::string name = ent->d_name;
                if (name == "." || name == "..") continue;
                std::string base = "/sys/class/power_supply/" + name;
                std::string type = ReadTrimmed(base + "/type");
                if (type == "Mains") {
                    std::string online = ReadTrimmed(base + "/online");
                    if (online == "1") onAc = true;
                } else if (type == "Battery") {
                    sawBattery = true;
                    batteryBase = base;
                }
            }
            closedir(d);
            if (onAc) src = "AC Power";
            else if (sawBattery) src = "Battery";

            hasBattery = sawBattery;
            if (sawBattery) {
                std::string capacity = ReadTrimmed(batteryBase + "/capacity");
                batteryPct = capacity.empty() ? 0.0f : std::clamp(std::strtof(capacity.c_str(), nullptr) / 100.0f, 0.0f, 1.0f);
                batteryStatus = ReadTrimmed(batteryBase + "/status");
                if (batteryStatus.empty()) batteryStatus = "Unavailable";
                batteryHistory.Push(batteryPct);
            } else {
                batteryPct = 0.0f;
                batteryStatus = "Unavailable";
            }
        }
        powerSource = src;
        thermalState = thermalPressureText == "Nominal" ? "Thermals nominal" : "Thermals " + thermalPressureText;
    }

    // --- Memory ---
    {
        uint64_t memTotal = 0, memAvail = 0, cached = 0, buffers = 0, committed = 0;
        uint64_t swapTotal = 0, swapFree = 0;
        for (const std::string& line : ReadLines("/proc/meminfo")) {
            std::vector<std::string> parts = Split(line);
            if (parts.size() < 2) continue;
            uint64_t v = std::strtoull(parts[1].c_str(), nullptr, 10);
            if (line.rfind("MemTotal:", 0) == 0) memTotal = v;
            else if (line.rfind("MemAvailable:", 0) == 0) memAvail = v;
            else if (line.rfind("Cached:", 0) == 0) cached = v;
            else if (line.rfind("Buffers:", 0) == 0) buffers = v;
            else if (line.rfind("Committed_AS:", 0) == 0) committed = v;
            else if (line.rfind("SwapTotal:", 0) == 0) swapTotal = v;
            else if (line.rfind("SwapFree:", 0) == 0) swapFree = v;
        }
        memTotalGB = memTotal / (1024.0f * 1024.0f);
        uint64_t used = memTotal > memAvail ? memTotal - memAvail : 0;
        memUsedGB = used / (1024.0f * 1024.0f);
        memAvailableGB = memAvail / (1024.0f * 1024.0f);
        memCachedGB = cached / (1024.0f * 1024.0f);
        memBuffersGB = buffers / (1024.0f * 1024.0f);
        memCommittedGB = committed / (1024.0f * 1024.0f);
        memSwapTotalGB = swapTotal / (1024.0f * 1024.0f);
        memSwapUsedGB = (swapTotal > swapFree ? swapTotal - swapFree : 0) / (1024.0f * 1024.0f);
    }

    // --- Memory pressure (PSI), page in/out counters: optional on older kernels ---
    {
        std::string psi = ReadFile("/proc/pressure/memory");
        if (!psi.empty()) {
            size_t pos = psi.find("some ");
            size_t avg10 = pos == std::string::npos ? std::string::npos : psi.find("avg10=", pos);
            if (avg10 != std::string::npos) {
                memPressurePct = (float)std::strtod(psi.c_str() + avg10 + 6, nullptr);
                memPressureText = memPressurePct < 1.0f ? "Normal" : memPressurePct < 10.0f ? "Elevated" : "Critical";
            }
        }
        for (const std::string& line : ReadLines("/proc/vmstat")) {
            if (line.rfind("pgpgin ", 0) == 0) memPageIns = std::strtol(line.c_str() + 7, nullptr, 10);
            else if (line.rfind("pgpgout ", 0) == 0) memPageOuts = std::strtol(line.c_str() + 8, nullptr, 10);
        }
    }

    // --- DRAM OC telemetry: ryzen_smu + aod_voltages (AMD only, needs root) ---
    // ReadDramOc() now forks the ram_oc_helper subprocess (see dram_oc.h), so
    // it's throttled instead of called every tick, same as disk volumes/services.
    if (t - dramOcRefreshT_ > 1.5) {
        dramOcSupported = ReadDramOc(dramOc);
        dramOcRefreshT_ = t;
        // aod_voltages (mem_vdd's normal source) needs its own kernel module,
        // separate from ryzen_smu, and often isn't built/loaded. Fall back to
        // the motherboard Super I/O's own "DRAM" voltage rail via hwmon (e.g.
        // nct6687 on boards with an NCT6687D chip) when SMU didn't give us one.
        if (dramOcSupported && dramOc.metrics.memVdd <= 0.0f) {
            double v = HwmonVoltageByLabel(hwmon, "DRAM");
            if (v > 0.0) dramOc.metrics.memVdd = (float)v;
        }
        // DDR4 ties VDD and VDDQ to the same physical rail (JEDEC DDR4 spec —
        // no separate VDDQ regulator), so reusing the DRAM reading here is
        // correct, not a guess. DDR5 splits them onto a per-DIMM PMIC, where
        // that wouldn't hold, so this stays scoped to DDR4 boards.
        if (dramOcSupported && dramOc.metrics.memVddq <= 0.0f && dramOc.metrics.memVdd > 0.0f &&
            dramOc.memoryType.find("DDR4") != std::string::npos) {
            dramOc.metrics.memVddq = dramOc.metrics.memVdd;
        }
        // CPU VDDIO and MEM VPP have no equivalent on nct6687-monitored
        // boards (its ADC channels don't cover either rail) — no fallback
        // exists for them short of the missing aod_voltages module, so they
        // stay an honest "N/A" rather than guessing at an unrelated channel.
    }

    // --- gpu_ctl_daemon reachability + per-GPU caps (see gpu_ctl/gpu_ctl.h) ---
    // Just a PING+LIST_GPUS round-trip, cheap even on failure, but still
    // throttled to the same cadence as the other daemon-backed checks above
    // rather than doing a socket connect() every frame.
    if (t - gpuCtlRefreshT_ > 1.5) {
        RefreshGpuCtl(gpuCtl);
        gpuCtlRefreshT_ = t;
    }

    // --- Network: sum of all interfaces except loopback ---
    {
        uint64_t rxBytes = 0, txBytes = 0;
        int ifaceCount = 0;
        for (const std::string& line : ReadLines("/proc/net/dev")) {
            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string iface = line.substr(0, colon);
            iface.erase(0, iface.find_first_not_of(" \t"));
            if (iface == "lo") continue;
            std::vector<std::string> fields = Split(line.substr(colon + 1));
            if (fields.size() < 9) continue;
            rxBytes += std::strtoull(fields[0].c_str(), nullptr, 10);
            txBytes += std::strtoull(fields[8].c_str(), nullptr, 10);
            ++ifaceCount;
        }
        if (haveBaseline_ && dt > 0) {
            netReceiveKBs = (float)(rxBytes - prevRxBytes_) / 1024.0f / (float)dt;
            netSendKBs = (float)(txBytes - prevTxBytes_) / 1024.0f / (float)dt;
            networkKBs = netReceiveKBs + netSendKBs;
        }
        prevRxBytes_ = rxBytes;
        prevTxBytes_ = txBytes;
        netTotalReceivedGB = rxBytes / (1024.0f * 1024.0f * 1024.0f);
        netTotalSentGB = txBytes / (1024.0f * 1024.0f * 1024.0f);
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%d network interface%s combined", ifaceCount, ifaceCount == 1 ? "" : "s");
        netInterfaceCount = buf;
    }
    netHistory.Push(std::min(1.0f, networkKBs / 2000.0f));

    // --- Disk: sum of sectors + IO-busy-ms across whole-disk block devices ---
    // ponytail: sums every non-loop/ram device, which can double-count
    // partitions alongside their parent disk; fine for a live aggregate.
    // diskPct is a heuristic against a 100 MB/s cap, not a real limit.
    {
        uint64_t readSectors = 0, writeSectors = 0, ioMs = 0;
        int devCount = 0;
        for (const std::string& line : ReadLines("/proc/diskstats")) {
            std::vector<std::string> f = Split(line);
            if (f.size() < 13) continue;
            const std::string& name = f[2];
            if (name.rfind("loop", 0) == 0 || name.rfind("ram", 0) == 0) continue;
            readSectors += std::strtoull(f[5].c_str(), nullptr, 10);
            writeSectors += std::strtoull(f[9].c_str(), nullptr, 10);
            ioMs += std::strtoull(f[12].c_str(), nullptr, 10);
            ++devCount;
        }
        if (haveBaseline_ && dt > 0) {
            double readBps = 512.0 * (readSectors - prevReadSectors_) / dt;
            double writeBps = 512.0 * (writeSectors - prevWriteSectors_) / dt;
            diskReadKBs = (float)(readBps / 1024.0);
            diskWriteKBs = (float)(writeBps / 1024.0);
            diskPct = std::min(1.0f, (float)((readBps + writeBps) / (100.0 * 1024 * 1024)));
            diskActivePct = std::min(1.0f, (float)((ioMs - prevIoMs_) / (dt * 1000.0)));
        }
        prevReadSectors_ = readSectors;
        prevWriteSectors_ = writeSectors;
        prevIoMs_ = ioMs;
        diskCount = devCount;
    }
    diskActiveHistory.Push(diskActivePct);
    diskTransferHistory.Push(diskPct);

    haveBaseline_ = true;

    cpuHistory.Push(cpuPct);
    tempHistory.Push(tempC);
    kernelHistory.Push(cpuKernelPct);
    memHistory.Push(memTotalGB > 0 ? memUsedGB / memTotalGB : 0.0f);

    // --- GPU usage: real via amdgpu sysfs (dGPU or APU iGPU alike — same
    // driver, same gpu_busy_percent node either way), else real via
    // nvidia-smi, else real via the Intel i915/xe perf PMU where permitted,
    // else honestly 0/"unavailable". No fake data for any vendor. ---
    {
        std::string amdDevPath;
        float amdBusy = ReadAmdGpuBusyPercent(&amdDevPath);
        if (amdBusy >= 0.0f) {
            gpuPct = amdBusy;
            gpuSource = "amdgpu";
            uint64_t vramUsed = std::strtoull(ReadTrimmed(amdDevPath + "/mem_info_vram_used").c_str(), nullptr, 10);
            uint64_t vramTotal = std::strtoull(ReadTrimmed(amdDevPath + "/mem_info_vram_total").c_str(), nullptr, 10);
            gpuVramUsedGB = (float)(vramUsed / (1024.0 * 1024.0 * 1024.0));
            gpuVramTotalGB = (float)(vramTotal / (1024.0 * 1024.0 * 1024.0));
        } else {
            float nvPct; double nvTemp;
            if (ReadNvidiaSmi(&nvPct, &nvTemp)) {
                gpuPct = nvPct;
                gpuTempC = (float)nvTemp; // nvidia-smi's own reading beats an unmatched hwmon scan
                gpuSource = "nvidia-smi";
            } else {
                float intelPct = ReadIntelGpuBusyPercent();
                if (intelPct >= 0.0f) {
                    gpuPct = intelPct;
                    gpuSource = "i915";
                } else {
                    gpuPct = 0.0f;
                    gpuSource = "unavailable";
                }
            }
        }
    }
    npuPct = 0.0f;
    float amdPower = ReadAmdGpuPowerWatts();
    energyW = amdPower > 0.0f ? amdPower : 0.0f;
    energyHistory.Push(std::min(1.0f, energyW / 200.0f));
    gpuHistory.Push(gpuPct);

    // --- Processes ---
    {
        long clkTck = sysconf(_SC_CLK_TCK);
        std::unordered_map<int, uint64_t> curJiffies;
        std::unordered_map<int, uint64_t> curGpuNs;
        std::vector<ProcessRow> rows;
        int total = 0;
        int threads = 0;

        DIR* d = opendir("/proc");
        if (d) {
            struct dirent* ent;
            while ((ent = readdir(d)) != nullptr) {
                char* endp = nullptr;
                long pid = std::strtol(ent->d_name, &endp, 10);
                if (*endp != '\0' || pid <= 0) continue;
                ++total;

                std::string base = "/proc/" + std::to_string(pid);
                std::string name = ReadFile(base + "/comm");
                while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
                if (name.empty()) continue;

                uint64_t vmRssKb = 0;
                for (const std::string& line : ReadLines(base + "/status")) {
                    if (line.rfind("VmRSS:", 0) == 0) {
                        std::vector<std::string> parts = Split(line);
                        if (parts.size() > 1) vmRssKb = std::strtoull(parts[1].c_str(), nullptr, 10);
                    } else if (line.rfind("Threads:", 0) == 0) {
                        std::vector<std::string> parts = Split(line);
                        if (parts.size() > 1) threads += std::atoi(parts[1].c_str());
                    }
                }

                float cpuPctProc = 0.0f;
                char state = '?';
                std::string stat = ReadFile(base + "/stat");
                size_t close = stat.rfind(')');
                if (close != std::string::npos) {
                    if (close + 2 < stat.size()) state = stat[close + 2];
                    std::vector<std::string> rest = Split(stat.substr(close + 2));
                    // rest[11]=utime, rest[12]=stime (0-indexed after pid/comm/state)
                    if (rest.size() > 12) {
                        uint64_t jiffies = std::strtoull(rest[11].c_str(), nullptr, 10) +
                                           std::strtoull(rest[12].c_str(), nullptr, 10);
                        auto prev = prevProcJiffies_.find((int)pid);
                        if (haveBaseline_ && dt > 0 && prev != prevProcJiffies_.end()) {
                            uint64_t delta = jiffies - prev->second;
                            float totalCpuScale = std::max(1.0f, (float)logicalProcessors);
                            cpuPctProc = 100.0f * ((float)delta / (float)clkTck) / ((float)dt * totalCpuScale);
                        }
                        curJiffies[(int)pid] = jiffies;
                    }
                }

                uint64_t engineNs = 0, vramKb = 0;
                ReadProcessDrmUsage((int)pid, engineNs, vramKb);
                float gpuPctProc = 0.0f;
                auto prevGpu = prevProcGpuNs_.find((int)pid);
                if (haveBaseline_ && dt > 0 && prevGpu != prevProcGpuNs_.end() && engineNs >= prevGpu->second) {
                    gpuPctProc = std::min(100.0f, 100.0f * (float)(engineNs - prevGpu->second) / 1e9f / (float)dt);
                }
                curGpuNs[(int)pid] = engineNs;

                ProcessRow row;
                row.pid = (int)pid;
                row.name = name;
                row.cpuPct = cpuPctProc;
                row.memory = FormatKb(vmRssKb);
                row.memoryKb = vmRssKb;
                row.user = UsernameForPid(base);
                row.status = StatusForChar(state);
                row.gpuPct = gpuPctProc;
                row.vram = FormatKb(vramKb);
                row.vramKb = vramKb;
                rows.push_back(std::move(row));
            }
            closedir(d);
        }
        prevProcJiffies_ = std::move(curJiffies);
        prevProcGpuNs_ = std::move(curGpuNs);
        processCount = total;
        threadCount = threads;

        std::sort(rows.begin(), rows.end(), [](const ProcessRow& a, const ProcessRow& b) {
            return a.cpuPct > b.cpuPct;
        });
        allProcesses = rows;
        if (rows.size() > 10) rows.resize(10);
        topProcesses = std::move(rows);
    }
}

void Metrics::ScanApps() {
    startupApps.clear();
    installedApps.clear();
    // Scan user dirs first: ScanDesktopDir skips a filename it's already
    // seen, and XDG precedence puts user config ahead of system dirs, so a
    // user override (e.g. our own disable-mask) must be scanned before the
    // system file it shadows.
    std::string home = RealHomeDir(); // resolves the real user, not root, under `sudo vitals`
    if (!home.empty()) ScanDesktopDir(home + "/.config/autostart", true, startupApps);
    ScanDesktopDir("/etc/xdg/autostart", true, startupApps);
    if (!home.empty()) ScanDesktopDir(home + "/.local/share/applications", false, installedApps);
    ScanDesktopDir("/usr/share/applications", false, installedApps);
    std::sort(startupApps.begin(), startupApps.end(), [](const DesktopApp& a, const DesktopApp& b) { return a.name < b.name; });
    std::sort(installedApps.begin(), installedApps.end(), [](const DesktopApp& a, const DesktopApp& b) { return a.name < b.name; });
}

void Metrics::RescanApps() { ScanApps(); }
