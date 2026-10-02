#include "metrics/gpu_sysfs.h"
#include "metrics/proc_util.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sstream>
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>

// Skips "cardN-HDMI-A-1" connector subdirs, keeping only bare "cardN" dirs.
static bool IsBareCardDir(const std::string& n) {
    if (n.rfind("card", 0) != 0) return false;
    for (size_t i = 4; i < n.size(); ++i)
        if (!std::isdigit((unsigned char)n[i])) return false;
    return true;
}

// Walks every /sys/class/drm/cardN/device/hwmon/hwmonM dir, calling fn(dir)
// for each. fn returns true once it's found what it needed, which stops the
// whole walk — every caller below wants the first GPU hwmon with the file
// it's after, not all of them.
template <typename Fn>
static void ForEachAmdGpuHwmon(Fn fn) {
    DIR* d = opendir("/sys/class/drm");
    if (!d) return;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        std::string n = ent->d_name;
        if (!IsBareCardDir(n)) continue;
        std::string hwmonDir = "/sys/class/drm/" + n + "/device/hwmon";
        DIR* hw = opendir(hwmonDir.c_str());
        if (!hw) continue;
        struct dirent* hwent;
        bool stop = false;
        while ((hwent = readdir(hw)) != nullptr) {
            std::string h = hwent->d_name;
            if (h == "." || h == "..") continue;
            if (fn(hwmonDir + "/" + h)) { stop = true; break; }
        }
        closedir(hw);
        if (stop) break;
    }
    closedir(d);
}

void ReadGpuSysfsClocks(float* coreMhzOut, float* vramMhzOut, float* voltageOut) {
    float coreMhz = 0.0f, vramMhz = 0.0f, voltageV = 0.0f;
    ForEachAmdGpuHwmon([&](const std::string& hw) {
        std::string v = ReadTrimmed(hw + "/freq1_input");
        if (!v.empty()) {
            double f = std::strtod(v.c_str(), nullptr);
            if (f > 1000000.0) coreMhz = (float)(f / 1000000.0);
            else if (f > 1000.0) coreMhz = (float)(f / 1000.0);
            else coreMhz = (float)f;
        }
        v = ReadTrimmed(hw + "/freq2_input");
        if (!v.empty()) {
            double f = std::strtod(v.c_str(), nullptr);
            double scaled = f > 1000000.0 ? (f / 1000000.0) : (f > 1000.0 ? (f / 1000.0) : f);
            // AMD exposes the memory clock in raw DDR half-rate form in hwmon; lact
            // reports the effective memory clock, which is 2x the raw sysfs value.
            vramMhz = (float)(scaled * 2.0);
        }
        v = ReadTrimmed(hw + "/in0_input");
        if (!v.empty()) {
            double vIn = std::strtod(v.c_str(), nullptr);
            if (vIn > 0.0) voltageV = (float)(vIn / 1000.0); // hwmon in*_input is millivolts
        }
        return coreMhz > 0.0f || vramMhz > 0.0f || voltageV > 0.0f;
    });
    if (coreMhzOut) *coreMhzOut = coreMhz;
    if (vramMhzOut) *vramMhzOut = vramMhz;
    if (voltageOut) *voltageOut = voltageV;
}

float ReadAmdGpuFanRpm() {
    float rpm = 0.0f;
    ForEachAmdGpuHwmon([&](const std::string& hw) {
        std::string v = ReadTrimmed(hw + "/fan1_input");
        if (v.empty()) return false;
        rpm = std::strtof(v.c_str(), nullptr);
        return true;
    });
    return rpm;
}

float ReadAmdGpuPowerWatts() {
    float watts = 0.0f;
    ForEachAmdGpuHwmon([&](const std::string& hw) {
        std::string v = ReadTrimmed(hw + "/power1_average");
        if (v.empty()) return false;
        watts = std::strtof(v.c_str(), nullptr) / 1000000.0f;
        return true;
    });
    return watts;
}

float ReadAmdGpuPowerLimitWatts() {
    float watts = 0.0f;
    ForEachAmdGpuHwmon([&](const std::string& hw) {
        std::string v = ReadTrimmed(hw + "/power1_cap");
        if (v.empty()) v = ReadTrimmed(hw + "/power1_cap_max");
        if (v.empty()) return false;
        watts = std::strtof(v.c_str(), nullptr) / 1000000.0f;
        return true;
    });
    return watts;
}

float ReadAmdGpuBusyPercent(std::string* cardPathOut) {
    DIR* d = opendir("/sys/class/drm");
    if (!d) return -1.0f;
    struct dirent* ent;
    float result = -1.0f;
    while ((ent = readdir(d)) != nullptr) {
        std::string n = ent->d_name;
        if (!IsBareCardDir(n)) continue;
        std::string devPath = "/sys/class/drm/" + n + "/device";
        std::string raw = ReadTrimmed(devPath + "/gpu_busy_percent");
        if (raw.empty()) continue;
        result = std::strtof(raw.c_str(), nullptr) / 100.0f;
        if (cardPathOut) *cardPathOut = devPath;
        break;
    }
    closedir(d);
    return result;
}

bool ReadNvidiaSmi(float* pctOut, double* tempOut) {
    static int available = -1; // -1 unknown, 0 no, 1 yes
    if (available == 0) return false;
    FILE* p = popen("nvidia-smi --query-gpu=utilization.gpu,temperature.gpu "
                     "--format=csv,noheader,nounits 2>/dev/null", "r");
    if (!p) { available = 0; return false; }
    char buf[64] = {0};
    bool gotLine = std::fgets(buf, sizeof(buf), p) != nullptr;
    pclose(p);
    float pct = 0.0f; double temp = 0.0;
    bool ok = gotLine && std::sscanf(buf, "%f, %lf", &pct, &temp) == 2;
    available = ok ? 1 : 0;
    if (!ok) return false;
    *pctOut = pct / 100.0f;
    *tempOut = temp;
    return true;
}

static long PerfEventOpen(struct perf_event_attr* attr, pid_t pid, int cpu, int groupFd, unsigned long flags) {
    return syscall(SYS_perf_event_open, attr, pid, cpu, groupFd, flags);
}

// A perf dynamic-PMU event file's content is "config=0x...". The i915/xe
// PMU's per-engine busy events are single-field (see the "format" sysfs
// dir — one bitfield spanning the whole config), so this literal value is
// the complete perf_event_attr.config, no bit-packing needed.
static bool ParsePerfEventConfig(const std::string& path, uint64_t* configOut) {
    std::string s = ReadTrimmed(path);
    size_t eq = s.find('=');
    if (eq == std::string::npos) return false;
    *configOut = std::strtoull(s.c_str() + eq + 1, nullptr, 0);
    return true;
}

// Intel iGPU/dGPU busy % via the kernel's i915 (or newer "xe" driver) perf
// PMU — the same documented ABI `perf stat -e i915/rcs0-busy/` and
// intel_gpu_top use, not a reverse-engineered register. There is no sysfs
// gpu_busy_percent equivalent for Intel, so this is the only real source.
// Needs perf_event_open() to be allowed (root, CAP_PERFMON, or a
// permissive /proc/sys/kernel/perf_event_paranoid) — many hardened distro
// defaults block it for unprivileged processes, in which case this behaves
// exactly like "no source": returns -1 forever, same as ReadAmdGpuBusyPercent
// on non-AMD hardware.
float ReadIntelGpuBusyPercent() {
    static int available = -1; // -1 unknown, 0 no, 1 yes
    static int fd = -1;
    static uint64_t prevBusyNs = 0;
    static struct timespec prevTs;
    static bool havePrev = false;

    if (available == 0) return -1.0f;

    if (available < 0) {
        const char* pmus[] = {"i915", "xe"};
        for (const char* pmu : pmus) {
            std::string base = std::string("/sys/bus/event_source/devices/") + pmu;
            std::string typeStr = ReadTrimmed(base + "/type");
            uint64_t config = 0;
            if (typeStr.empty() || !ParsePerfEventConfig(base + "/events/rcs0-busy", &config)) continue;

            struct perf_event_attr pea;
            std::memset(&pea, 0, sizeof(pea));
            pea.type = (uint32_t)std::strtoul(typeStr.c_str(), nullptr, 10);
            pea.size = sizeof(pea);
            pea.config = config;

            // Uncore/device PMU convention: pid=-1 (not task-attached),
            // one concrete online CPU (0 always exists on x86).
            int f = (int)PerfEventOpen(&pea, -1, 0, -1, 0);
            if (f >= 0) { fd = f; break; }
        }
        available = (fd >= 0) ? 1 : 0;
        if (available == 0) return -1.0f;
    }

    uint64_t busyNs = 0;
    if (read(fd, &busyNs, sizeof(busyNs)) != (ssize_t)sizeof(busyNs)) return -1.0f;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    float pct = -1.0f; // first sample after (re)start: no delta yet
    if (havePrev && busyNs >= prevBusyNs) {
        double dtNs = (double)(now.tv_sec - prevTs.tv_sec) * 1e9 + (double)(now.tv_nsec - prevTs.tv_nsec);
        if (dtNs > 1e6) {
            double frac = (double)(busyNs - prevBusyNs) / dtNs;
            pct = (float)(frac < 0.0 ? 0.0 : frac > 1.0 ? 1.0 : frac);
        }
    }
    prevBusyNs = busyNs;
    prevTs = now;
    havePrev = true;
    return pct;
}

// ---- AMD overdrive/fan baseline (see gpu_sysfs.h) -------------------------
// Same line formats gpu_ctl_daemon/amd_backend.cpp parses — duplicated, not
// shared, since that daemon is a separate binary with its own sysfs helpers.

void ParseOdClkVoltage(const std::string& text, AmdOdState* s) {
    std::istringstream iss(text);
    std::string line, section;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == ':' && line.find(' ') == std::string::npos) { section = line; continue; }
        int idx, mhz, a, b;
        if (section == "OD_SCLK:" && std::sscanf(line.c_str(), " %d: %dMhz", &idx, &mhz) == 2) {
            s->hasSclk = true; (idx == 0 ? s->sclkMin : s->sclkMax) = mhz;
        } else if (section == "OD_MCLK:" && std::sscanf(line.c_str(), " %d: %d", &idx, &mhz) == 2) {
            s->hasMclk = true; (idx == 0 ? s->mclkMin : s->mclkMax) = mhz;
        } else if (section == "OD_VDDGFX_OFFSET:" && std::sscanf(line.c_str(), " %dmV", &mhz) == 1) {
            s->hasVoltOffset = true; s->voltOffsetMv = mhz;
        } else if (section == "OD_RANGE:") {
            if (std::sscanf(line.c_str(), " SCLK: %dMhz %dMhz", &a, &b) == 2) s->sclkRange = {a, b};
            else if (std::sscanf(line.c_str(), " MCLK: %dMhz %dMhz", &a, &b) == 2) s->mclkRange = {a, b};
            else if (std::sscanf(line.c_str(), " VDDGFX_OFFSET: %dmv %dmv", &a, &b) == 2) s->voltOffsetRange = {a, b};
        }
    }
}

bool ParseFanCtrlValue(const std::string& text, int* value, AmdRange* range) {
    std::istringstream iss(text);
    std::string line;
    bool got = false;
    while (std::getline(iss, line)) {
        int v, a, b;
        size_t colon = line.find(':');
        if (colon != std::string::npos && colon + 1 < line.size() &&
            std::sscanf(line.c_str() + colon + 1, " %d %d", &a, &b) == 2) { *range = {a, b}; continue; }
        if (!got && std::sscanf(line.c_str(), " %d", &v) == 1 && line.find(':') == std::string::npos) { *value = v; got = true; }
    }
    return got;
}

void ParseFanCurve(const std::string& text, AmdOdState* s) {
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        int idx, t, p, a, b;
        if (std::sscanf(line.c_str(), " %d: %dC %d%%", &idx, &t, &p) == 3 && idx >= 0 && idx < 16) {
            s->fanCurveTemp[idx] = t; s->fanCurvePct[idx] = p;
            s->fanCurvePoints = std::max(s->fanCurvePoints, idx + 1);
            s->hasFanCurve = true;
        } else if (std::sscanf(line.c_str(), " FAN_CURVE(hotspot temp): %dC %dC", &a, &b) == 2) s->fanCurveTempRange = {a, b};
        else if (std::sscanf(line.c_str(), " FAN_CURVE(fan speed): %d%% %d%%", &a, &b) == 2) s->fanCurvePctRange = {a, b};
    }
}

int ParseActiveProfile(const std::string& text) {
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        int idx; char name[32];
        if (std::sscanf(line.c_str(), " %d %31[A-Za-z0-9_]", &idx, name) == 2 && line.find('*') != std::string::npos) return idx;
    }
    return -1;
}

std::vector<int> ParseDpmLevels(const std::string& text, int* activeOut) {
    // "0: 500Mhz \n1: 2254Mhz *" — RDNA3 also prints an "S: 187Mhz" sleep row, skipped.
    std::vector<int> out;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        int idx, mhz;
        if (std::sscanf(line.c_str(), " %d: %d", &idx, &mhz) == 2 && idx == (int)out.size()) {
            if (activeOut && line.find('*') != std::string::npos) *activeOut = idx;
            out.push_back(mhz);
        }
    }
    return out;
}

AmdOdState ReadAmdOdState(const std::string& pciSlot) {
    AmdOdState s;
    if (pciSlot.empty() || pciSlot.find('/') != std::string::npos) return s; // slot comes from the daemon; never let it walk paths
    std::string dev = "/sys/bus/pci/devices/" + pciSlot;
    std::string vendor = ReadTrimmed(dev + "/vendor");
    if (vendor != "0x1002") return s;
    s.valid = true;

    std::string hwmon;
    if (DIR* d = opendir((dev + "/hwmon").c_str())) {
        while (struct dirent* e = readdir(d)) if (e->d_name[0] != '.') { hwmon = dev + "/hwmon/" + e->d_name; break; }
        closedir(d);
    }
    auto readLong = [](const std::string& p) { std::string v = ReadTrimmed(p); return v.empty() ? -1L : std::strtol(v.c_str(), nullptr, 10); };

    if (!hwmon.empty()) {
        long cap = readLong(hwmon + "/power1_cap");
        if (cap > 0) {
            s.hasPowerCap = true;
            s.powerCapW = (int)(cap / 1000000);
            s.powerCapRange = {(int)(readLong(hwmon + "/power1_cap_min") / 1000000), (int)(readLong(hwmon + "/power1_cap_max") / 1000000)};
            s.powerCapDefaultW = (int)(readLong(hwmon + "/power1_cap_default") / 1000000);
        }
        s.pwmEnable = (int)readLong(hwmon + "/pwm1_enable");
        long pwm = readLong(hwmon + "/pwm1"), pwmMax = readLong(hwmon + "/pwm1_max");
        if (pwm >= 0 && pwmMax > 0) s.fanPct = 100.0f * (float)pwm / (float)pwmMax;
        long rpm = readLong(hwmon + "/fan1_input");
        if (rpm >= 0) s.fanRpm = (float)rpm;
    }

    ParseOdClkVoltage(ReadFile(dev + "/pp_od_clk_voltage"), &s);
    s.perfLevel = ReadTrimmed(dev + "/power_dpm_force_performance_level");
    s.activeProfile = ParseActiveProfile(ReadFile(dev + "/pp_power_profile_mode"));
    s.sclkLevelsMhz = ParseDpmLevels(ReadFile(dev + "/pp_dpm_sclk"));
    s.mclkLevelsMhz = ParseDpmLevels(ReadFile(dev + "/pp_dpm_mclk"), &s.mclkActiveLevel);

    std::string fc = dev + "/gpu_od/fan_ctrl/";
    ParseFanCurve(ReadFile(fc + "fan_curve"), &s);
    s.hasTargetTemp = ParseFanCtrlValue(ReadFile(fc + "fan_target_temperature"), &s.targetTempC, &s.targetTempRange);
    s.hasAcousticLimit = ParseFanCtrlValue(ReadFile(fc + "acoustic_limit_rpm_threshold"), &s.acousticLimitRpm, &s.acousticLimitRange);
    s.hasAcousticTarget = ParseFanCtrlValue(ReadFile(fc + "acoustic_target_rpm_threshold"), &s.acousticTargetRpm, &s.acousticTargetRange);
    s.hasMinPwm = ParseFanCtrlValue(ReadFile(fc + "fan_minimum_pwm"), &s.minPwmPct, &s.minPwmRange);
    int zr = 0; AmdRange zrRange;
    s.hasZeroRpm = ParseFanCtrlValue(ReadFile(fc + "fan_zero_rpm_enable"), &zr, &zrRange);
    s.zeroRpm = zr != 0;
    s.hasZeroRpmStop = ParseFanCtrlValue(ReadFile(fc + "fan_zero_rpm_stop_temperature"), &s.zeroRpmStopC, &s.zeroRpmStopRange);

    long gttUsed = readLong(dev + "/mem_info_gtt_used"), gttTotal = readLong(dev + "/mem_info_gtt_total");
    if (gttUsed >= 0) s.gttUsedGB = (float)(gttUsed / 1073741824.0);
    if (gttTotal > 0) s.gttTotalGB = (float)(gttTotal / 1073741824.0);
    return s;
}
