#include "amd_backend.h"
#include "sysfs_io.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

namespace {

bool IsBareCardDir(const std::string& n) {
    if (n.rfind("card", 0) != 0) return false;
    for (size_t i = 4; i < n.size(); ++i)
        if (!std::isdigit((unsigned char)n[i])) return false;
    return true;
}

std::string FirstHwmonDir(const std::string& devicePath) {
    std::string hwmonDir = devicePath + "/hwmon";
    DIR* d = opendir(hwmonDir.c_str());
    if (!d) return "";
    struct dirent* ent;
    std::string result;
    while ((ent = readdir(d)) != nullptr) {
        std::string n = ent->d_name;
        if (n == "." || n == "..") continue;
        result = hwmonDir + "/" + n;
        break;
    }
    closedir(d);
    return result;
}

// "0000:0b:00.0" from the devicePath's uevent (PCI_SLOT_NAME=...).
std::string ReadPciSlotProper(const std::string& devicePath) {
    FILE* f = std::fopen((devicePath + "/uevent").c_str(), "r");
    if (!f) return "";
    char line[256];
    std::string slot;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "PCI_SLOT_NAME=", 14) == 0) {
            slot = line + 14;
            while (!slot.empty() && (slot.back() == '\n' || slot.back() == '\r')) slot.pop_back();
            break;
        }
    }
    std::fclose(f);
    return slot;
}

long ParseMicro(const std::string& s) { return s.empty() ? -1 : std::strtol(s.c_str(), nullptr, 10); }

// Parses "SCLK: 300Mhz 2200Mhz" style lines from pp_od_clk_voltage's
// OD_RANGE section into (min,max) MHz. Returns false if the label isn't found.
bool ParseOdRangeLine(const std::string& content, const char* label, int32_t* minOut, int32_t* maxOut) {
    std::istringstream iss(content);
    std::string line;
    std::string wantPrefix = std::string(label) + ":";
    while (std::getline(iss, line)) {
        size_t pos = line.find(wantPrefix);
        if (pos == std::string::npos) continue;
        int a = 0, b = 0;
        if (std::sscanf(line.c_str() + pos + wantPrefix.size(), "%dMhz %dMhz", &a, &b) == 2) {
            *minOut = a; *maxOut = b;
            return true;
        }
    }
    return false;
}

// Parses the "0: 500Mhz" / "1: 2735Mhz" current-value lines under an
// "OD_SCLK:"/"OD_MCLK:" section header in pp_od_clk_voltage — these are
// today's actual clocks (what a slider should default to), distinct from
// ParseOdRangeLine's OD_RANGE section (how far the slider is allowed to
// move). Stops at the first non-matching line after entering the section,
// i.e. the next section header. Returns false if the header itself is
// never found (idx0/idx1 left untouched).
bool ParseOdCurrentSection(const std::string& content, const char* header, int32_t* idx0, int32_t* idx1) {
    std::istringstream iss(content);
    std::string line;
    bool inSection = false, got = false;
    while (std::getline(iss, line)) {
        if (line.compare(0, std::strlen(header), header) == 0) { inSection = true; continue; }
        if (!inSection) continue;
        int idx = -1, mhz = 0;
        if (std::sscanf(line.c_str(), " %d: %dMhz", &idx, &mhz) == 2) {
            if (idx == 0) { *idx0 = mhz; got = true; }
            else if (idx == 1) { *idx1 = mhz; got = true; }
            continue;
        }
        break;
    }
    return got;
}

int CountDpmLevels(const std::string& devicePath, const char* file) {
    std::string content = SysfsReadTrimmed(devicePath + "/" + file);
    int count = 0;
    for (char c : content) if (c == '\n') ++count;
    return content.empty() ? 0 : count + 1;
}

// Sends `cmd` (without trailing newline) as one write() to pp_od_clk_voltage
// — the driver parses one command per write() syscall, so each command must
// be its own SysfsWriteString call, never concatenated into one buffer.
bool WriteOdClkVoltageCmd(const std::string& devicePath, const std::string& cmd) {
    return SysfsWriteString(devicePath + "/pp_od_clk_voltage", cmd + "\n");
}

// ---- RDNA3+ PMFW fan nodes (device/gpu_od/fan_ctrl/*) ----------------------
// Same one-command-per-write protocol as pp_od_clk_voltage: write the value,
// then "c" to commit. pwm1 manual mode is ignored by the PMFW on these cards,
// which is why LACT drives fan_curve instead whenever it exists.

std::string FanCtrlPath(const AmdGpuHandle& gpu, const char* file) {
    return gpu.devicePath + "/gpu_od/fan_ctrl/" + file;
}

bool WriteFanCtrl(const AmdGpuHandle& gpu, const char* file, const std::string& value) {
    std::string path = FanCtrlPath(gpu, file);
    if (!SysfsFileExists(path)) return false;
    return SysfsWriteString(path, value + "\n") && SysfsWriteString(path, "c\n");
}

struct PmfwFanCurveInfo { int points = 0, tMin = 25, tMax = 100, sMin = 15, sMax = 100; };

// Parses fan_curve's "N: 29C 15%" lines and its OD_RANGE section.
PmfwFanCurveInfo ParsePmfwFanCurve(const std::string& content) {
    PmfwFanCurveInfo info;
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        int idx, t, s, a, b;
        if (std::sscanf(line.c_str(), " %d: %dC %d%%", &idx, &t, &s) == 3) info.points = std::max(info.points, idx + 1);
        else if (std::sscanf(line.c_str(), " FAN_CURVE(hotspot temp): %dC %dC", &a, &b) == 2) { info.tMin = a; info.tMax = b; }
        else if (std::sscanf(line.c_str(), " FAN_CURVE(fan speed): %d%% %d%%", &a, &b) == 2) { info.sMin = a; info.sMax = b; }
    }
    return info;
}

// ---- software fan-curve control ------------------------------------------
// AMD generations before RDNA3's PMFW curve nodes have no hardware fan-curve
// concept at all (LACT's own start_curve_fan_control_task does the same
// thing: a polling thread that reads a temp sensor and writes pwm1). Applies
// uniformly across AMD generations rather than branching hardware-curve vs
// software-curve — one code path, matches ladder rung "already established
// pattern" once written, and RDNA3+ owners who want the PMFW's own
// lower-power hardware curve can still get pmfw_options-only behavior by
// leaving fan.enabled=false.

struct FanCurveThread {
    std::thread th;
    std::atomic<bool> stop{false};
};

std::mutex g_fanThreadsMu;
std::map<std::string, std::unique_ptr<FanCurveThread>> g_fanThreads; // keyed by hwmonPath

float TempForKey(const std::string& hwmonPath, const std::string& key) {
    // Scan tempN_label files for a case-insensitive match on `key` (default
    // "edge" if key is empty), read the matching tempN_input (millidegrees C).
    std::string wantKey = key.empty() ? "edge" : key;
    std::transform(wantKey.begin(), wantKey.end(), wantKey.begin(), [](unsigned char c) { return std::tolower(c); });
    for (int i = 1; i <= 8; ++i) {
        std::string labelPath = hwmonPath + "/temp" + std::to_string(i) + "_label";
        std::string label = SysfsReadTrimmed(labelPath);
        if (label.empty()) {
            if (i == 1 && wantKey == "edge") {
                // Some cards expose temp1_input with no _label at all — that's the edge sensor.
                std::string v = SysfsReadTrimmed(hwmonPath + "/temp1_input");
                if (!v.empty()) return std::strtof(v.c_str(), nullptr) / 1000.0f;
            }
            continue;
        }
        std::string lower = label;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        if (lower.find(wantKey) != std::string::npos) {
            std::string v = SysfsReadTrimmed(hwmonPath + "/temp" + std::to_string(i) + "_input");
            if (!v.empty()) return std::strtof(v.c_str(), nullptr) / 1000.0f;
        }
    }
    return -1.0f;
}

float InterpolateCurve(const GpuCtlFanConfigPOD& fan, float tempC) {
    if (fan.curveCount == 0) return 0.5f;
    if (tempC <= fan.curve[0].tempC) return fan.curve[0].speed01;
    for (uint32_t i = 1; i < fan.curveCount; ++i) {
        if (tempC <= fan.curve[i].tempC) {
            const auto& a = fan.curve[i - 1];
            const auto& b = fan.curve[i];
            float span = b.tempC - a.tempC;
            float t = span > 0.0f ? (tempC - a.tempC) / span : 0.0f;
            return a.speed01 + (b.speed01 - a.speed01) * t;
        }
    }
    return fan.curve[fan.curveCount - 1].speed01;
}

void RunFanCurveLoop(std::string hwmonPath, GpuCtlFanConfigPOD fan, std::atomic<bool>* stopFlag) {
    SysfsWriteString(hwmonPath + "/pwm1_enable", "1"); // manual
    uint32_t intervalMs = fan.intervalMs > 0 ? fan.intervalMs : 500;
    float lastSpeed = -1.0f;
    while (!stopFlag->load()) {
        float t = TempForKey(hwmonPath, fan.temperatureKey);
        if (t > 0.0f) {
            float speed = InterpolateCurve(fan, t);
            speed = std::clamp(speed, 0.0f, 1.0f);
            // changeThresholdC: skip writes that don't meaningfully change speed
            // (avoid rewriting pwm1 every poll when the curve is flat here).
            if (lastSpeed < 0.0f || std::fabs(speed - lastSpeed) > 0.005f) {
                int pwm = (int)(speed * 255.0f + 0.5f);
                SysfsWriteString(hwmonPath + "/pwm1", std::to_string(pwm));
                lastSpeed = speed;
            }
        }
        // Sliced so AmdStopFanCurveThread's join() returns within ~50ms
        // instead of blocking the single-threaded daemon for intervalMs.
        for (uint32_t slept = 0; slept < intervalMs && !stopFlag->load(); slept += 50)
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min<uint32_t>(50, intervalMs - slept)));
    }
}

} // namespace

void AmdPmfwCurvePoints(const GpuCtlFanConfigPOD& fan, int n, int tMin, int tMax, int sMin, int sMax, int outTemp[], int outPct[]) {
    auto clampI = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    if (fan.mode == GPUCTL_FAN_MODE_STATIC) {
        // LACT's static-on-PMFW shape: flat line across the whole range.
        int pct = std::max(sMin, (int)(std::clamp(fan.staticSpeed01, 0.0f, 1.0f) * (float)sMax));
        for (int i = 0; i < n; ++i) { outTemp[i] = i == 0 ? tMin : tMax; outPct[i] = clampI(pct, sMin, sMax); }
        return;
    }
    // The node has a fixed point count (5 on RDNA3): use the user's points
    // when they match, otherwise resample the curve at evenly spaced temps.
    bool direct = fan.curveCount == (uint32_t)n;
    float lo = fan.curveCount ? fan.curve[0].tempC : (float)tMin;
    float hi = fan.curveCount ? fan.curve[fan.curveCount - 1].tempC : (float)tMax;
    for (int i = 0; i < n; ++i) {
        float t = direct ? fan.curve[i].tempC : (n > 1 ? lo + (hi - lo) * (float)i / (float)(n - 1) : lo);
        float speed = std::clamp(InterpolateCurve(fan, t), 0.0f, 1.0f);
        int ti = clampI((int)std::lround(t), tMin, tMax);
        outTemp[i] = i > 0 ? std::max(ti, outTemp[i - 1]) : ti; // keep non-decreasing
        outPct[i] = clampI((int)std::lround(speed * 100.0f), sMin, sMax);
    }
}

void AmdParseProfileModes(const std::string& content, GpuCtlCapsPOD* out) {
    // Header lines start with an index and a name (" 1 3D_FULL_SCREEN :",
    // " 5 COMPUTE*:", older " *3 VR"). RDNA3 also prints per-clock sub-rows
    // like "    0(       GFXCLK) ..." — the name charset below rejects '(' so
    // those don't become bogus modes. Names are stored at their real sysfs
    // index, since that's what gets written back to pp_power_profile_mode.
    out->profileModeCount = 0;
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        int idx = -1; char name[32] = {0};
        if (std::sscanf(line.c_str(), " %d %31[A-Za-z0-9_]", &idx, name) != 2 &&
            std::sscanf(line.c_str(), " *%d %31[A-Za-z0-9_]", &idx, name) != 2) continue;
        if (idx < 0 || idx >= GPUCTL_MAX_PROFILE_MODES) continue;
        std::snprintf(out->profileModeNames[idx], sizeof(out->profileModeNames[0]), "%s", name);
        out->profileModeCount = std::max(out->profileModeCount, (uint32_t)idx + 1);
    }
}

bool AmdDiscoverGpus(std::vector<AmdGpuHandle>& out) {
    DIR* d = opendir("/sys/class/drm");
    if (!d) return false;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        std::string n = ent->d_name;
        if (!IsBareCardDir(n)) continue;
        std::string devicePath = "/sys/class/drm/" + n + "/device";
        std::string vendor = SysfsReadTrimmed(devicePath + "/vendor");
        if (vendor != "0x1002") continue; // AMD PCI vendor id
        std::string hwmon = FirstHwmonDir(devicePath);
        if (hwmon.empty()) continue;

        AmdGpuHandle h;
        h.devicePath = devicePath;
        h.hwmonPath = hwmon;
        h.pciSlot = ReadPciSlotProper(devicePath);
        std::string namePath = "/sys/class/drm/" + n + "/device/product_name";
        h.name = SysfsReadTrimmed(namePath);
        if (h.name.empty()) h.name = "AMD GPU (" + n + ")";
        out.push_back(std::move(h));
    }
    closedir(d);
    return !out.empty();
}

bool AmdGetCaps(const AmdGpuHandle& gpu, GpuCtlCapsPOD* out) {
    std::memset(out, 0, sizeof(*out));
    out->vendor = GPUCTL_VENDOR_AMD;
    std::snprintf(out->pciSlot, sizeof(out->pciSlot), "%s", gpu.pciSlot.c_str());
    std::snprintf(out->name, sizeof(out->name), "%s", gpu.name.c_str());

    long capMin = ParseMicro(SysfsReadTrimmed(gpu.hwmonPath + "/power1_cap_min"));
    long capMax = ParseMicro(SysfsReadTrimmed(gpu.hwmonPath + "/power1_cap_max"));
    long capDef = ParseMicro(SysfsReadTrimmed(gpu.hwmonPath + "/power1_cap_default"));
    out->powerCapMinW = capMin > 0 ? (int32_t)(capMin / 1000000) : 0;
    out->powerCapMaxW = capMax > 0 ? (int32_t)(capMax / 1000000) : 0;
    out->powerCapDefaultW = capDef > 0 ? (int32_t)(capDef / 1000000) : 0;

    std::string odRange = SysfsReadTrimmed(gpu.devicePath + "/pp_od_clk_voltage");
    int32_t sclkMin = 0, sclkMax = 0, mclkMin = 0, mclkMax = 0;
    if (ParseOdRangeLine(odRange, "SCLK", &sclkMin, &sclkMax)) {
        out->coreClockMinMhz = sclkMin; out->coreClockMaxMhz = sclkMax;
    }
    if (ParseOdRangeLine(odRange, "MCLK", &mclkMin, &mclkMax)) {
        out->memClockMinMhz = mclkMin; out->memClockMaxMhz = mclkMax;
    }
    // Today's actual clocks (OD_SCLK/OD_MCLK), for sliders to default to
    // instead of the OD_RANGE extremes above. Missing "0:"/"1:" lines (e.g.
    // cards whose OD_MCLK only ever prints "1:") leave the matching field
    // at 0 — callers fall back to the OD_RANGE bound of the same name.
    ParseOdCurrentSection(odRange, "OD_SCLK:", &out->coreClockDefaultMinMhz, &out->coreClockDefaultMaxMhz);
    ParseOdCurrentSection(odRange, "OD_MCLK:", &out->memClockDefaultMinMhz, &out->memClockDefaultMaxMhz);
    out->supportsVfCurve = odRange.find("VDDC_CURVE") != std::string::npos;
    // Voltage-offset ("vo") range isn't published anywhere in sysfs. AMD's
    // curve-optimizer/PBO offset is undervolt-only (never positive) — -450mV
    // matches the window LACT/adjustor use in practice, replacing the
    // earlier placeholder +/-200mV guess.
    out->voltageOffsetMinMv = -450;
    out->voltageOffsetMaxMv = 0;

    out->corePstateCount = (uint32_t)CountDpmLevels(gpu.devicePath, "pp_dpm_sclk");
    out->memPstateCount = (uint32_t)CountDpmLevels(gpu.devicePath, "pp_dpm_mclk");

    AmdParseProfileModes(SysfsReadTrimmed(gpu.devicePath + "/pp_power_profile_mode"), out);
    return true;
}

void AmdStopFanCurveThread(const std::string& hwmonPath) {
    std::lock_guard<std::mutex> lock(g_fanThreadsMu);
    auto it = g_fanThreads.find(hwmonPath);
    if (it == g_fanThreads.end()) return;
    it->second->stop.store(true);
    if (it->second->th.joinable()) it->second->th.join();
    g_fanThreads.erase(it);
}

namespace {

void ApplyFanConfig(const AmdGpuHandle& gpu, const GpuCtlFanConfigPOD& fan) {
    AmdStopFanCurveThread(gpu.hwmonPath);

    std::string curvePath = FanCtrlPath(gpu, "fan_curve");
    PmfwFanCurveInfo info = ParsePmfwFanCurve(SysfsReadTrimmed(curvePath));
    if (info.points > 0) {
        if (!fan.enabled) {
            WriteFanCtrl(gpu, "fan_curve", "r"); // back to the PMFW's stock curve
            return;
        }
        // LACT disables zero-RPM for static speed so the fan actually spins.
        if (fan.mode == GPUCTL_FAN_MODE_STATIC) WriteFanCtrl(gpu, "fan_zero_rpm_enable", "0");
        int temps[16], pcts[16];
        int n = std::min(info.points, 16);
        AmdPmfwCurvePoints(fan, n, info.tMin, info.tMax, info.sMin, info.sMax, temps, pcts);
        for (int i = 0; i < n; ++i)
            SysfsWriteString(curvePath, std::to_string(i) + " " + std::to_string(temps[i]) + " " + std::to_string(pcts[i]) + "\n");
        SysfsWriteString(curvePath, "c\n");
        return;
    }

    if (!fan.enabled) {
        SysfsWriteString(gpu.hwmonPath + "/pwm1_enable", "2"); // auto
        return;
    }
    if (fan.mode == GPUCTL_FAN_MODE_STATIC) {
        SysfsWriteString(gpu.hwmonPath + "/pwm1_enable", "1");
        int pwm = (int)(std::clamp(fan.staticSpeed01, 0.0f, 1.0f) * 255.0f + 0.5f);
        SysfsWriteString(gpu.hwmonPath + "/pwm1", std::to_string(pwm));
        return;
    }
    // Curve mode: spawn the poll thread (see RunFanCurveLoop above).
    std::lock_guard<std::mutex> lock(g_fanThreadsMu);
    auto entry = std::make_unique<FanCurveThread>();
    std::atomic<bool>* stopFlag = &entry->stop;
    std::string hwmon = gpu.hwmonPath;
    entry->th = std::thread(RunFanCurveLoop, hwmon, fan, stopFlag);
    g_fanThreads[gpu.hwmonPath] = std::move(entry);
}

void ApplyPmfwOptions(const AmdGpuHandle& gpu, const GpuCtlPmfwPOD& pmfw) {
    if (!pmfw.set) return;
    if (pmfw.acousticLimitRpm != GPUCTL_UNSET)
        WriteFanCtrl(gpu, "acoustic_limit_rpm_threshold", std::to_string(pmfw.acousticLimitRpm));
    if (pmfw.acousticTargetRpm != GPUCTL_UNSET)
        WriteFanCtrl(gpu, "acoustic_target_rpm_threshold", std::to_string(pmfw.acousticTargetRpm));
    if (pmfw.minimumPwmPct != GPUCTL_UNSET)
        WriteFanCtrl(gpu, "fan_minimum_pwm", std::to_string(pmfw.minimumPwmPct));
    if (pmfw.targetTempC != GPUCTL_UNSET)
        WriteFanCtrl(gpu, "fan_target_temperature", std::to_string(pmfw.targetTempC));
    if (pmfw.zeroRpmSet)
        WriteFanCtrl(gpu, "fan_zero_rpm_enable", pmfw.zeroRpm ? "1" : "0");
    if (pmfw.zeroRpmSet && pmfw.zeroRpm && pmfw.zeroRpmThresholdC != GPUCTL_UNSET)
        WriteFanCtrl(gpu, "fan_zero_rpm_stop_temperature", std::to_string(pmfw.zeroRpmThresholdC));
}

bool ApplyClocks(const AmdGpuHandle& gpu, const GpuCtlClocksPOD& clocks, const GpuCtlCapsPOD& caps, std::string* err) {
    bool wroteAny = false;
    auto clampTo = [](int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); };

    if (clocks.minCoreClockMhz != GPUCTL_UNSET && caps.coreClockMaxMhz > 0) {
        int32_t v = clampTo(clocks.minCoreClockMhz, caps.coreClockMinMhz, caps.coreClockMaxMhz);
        wroteAny |= WriteOdClkVoltageCmd(gpu.devicePath, "s 0 " + std::to_string(v));
    }
    if (clocks.maxCoreClockMhz != GPUCTL_UNSET && caps.coreClockMaxMhz > 0) {
        int32_t v = clampTo(clocks.maxCoreClockMhz, caps.coreClockMinMhz, caps.coreClockMaxMhz);
        wroteAny |= WriteOdClkVoltageCmd(gpu.devicePath, "s 1 " + std::to_string(v));
    }
    if (clocks.minMemClockMhz != GPUCTL_UNSET && caps.memClockMaxMhz > 0) {
        int32_t v = clampTo(clocks.minMemClockMhz, caps.memClockMinMhz, caps.memClockMaxMhz);
        wroteAny |= WriteOdClkVoltageCmd(gpu.devicePath, "m 0 " + std::to_string(v));
    }
    if (clocks.maxMemClockMhz != GPUCTL_UNSET && caps.memClockMaxMhz > 0) {
        int32_t v = clampTo(clocks.maxMemClockMhz, caps.memClockMinMhz, caps.memClockMaxMhz);
        wroteAny |= WriteOdClkVoltageCmd(gpu.devicePath, "m 1 " + std::to_string(v));
    }
    if (clocks.voltageOffsetMv != GPUCTL_UNSET) {
        int32_t v = clampTo(clocks.voltageOffsetMv, caps.voltageOffsetMinMv, caps.voltageOffsetMaxMv);
        wroteAny |= WriteOdClkVoltageCmd(gpu.devicePath, "vo " + std::to_string(v));
    }
    for (uint32_t i = 0; i < clocks.gpuVfCurveCount && i < GPUCTL_MAX_VF_POINTS; ++i) {
        const auto& p = clocks.gpuVfCurve[i];
        wroteAny |= WriteOdClkVoltageCmd(gpu.devicePath,
            "vc " + std::to_string(p.point) + " " + std::to_string(p.v.clockspeedMhz) + " " + std::to_string(p.v.voltageMv));
    }
    // mem_vf_curve isn't covered by a stable documented "vc"-style command
    // across kernel versions — left unimplemented rather than guessing at a
    // write protocol that could apply the wrong value. gpuVfCurve above (the
    // one AMD's own driver docs cover) is fully implemented.
    if (wroteAny) {
        if (!WriteOdClkVoltageCmd(gpu.devicePath, "c")) {
            if (err) *err = "pp_od_clk_voltage commit ('c') failed";
            return false;
        }
    }
    return true;
}

} // namespace

bool AmdApplyConfig(const AmdGpuHandle& gpu, const GpuCtlConfigPOD& cfg, std::string* err) {
    GpuCtlCapsPOD caps;
    AmdGetCaps(gpu, &caps);

    if (cfg.performanceLevel != -1) {
        const char* levelStr = cfg.performanceLevel == GPUCTL_PERF_AUTO ? "auto"
                              : cfg.performanceLevel == GPUCTL_PERF_LOW ? "low"
                              : cfg.performanceLevel == GPUCTL_PERF_HIGH ? "high"
                              : "manual";
        if (!SysfsWriteString(gpu.devicePath + "/power_dpm_force_performance_level", levelStr)) {
            if (err) *err = "failed to write power_dpm_force_performance_level";
            return false;
        }
    }

    if (cfg.performanceLevel == GPUCTL_PERF_MANUAL) {
        if (cfg.corePstatesEnabledMask != 0) {
            std::string levels;
            for (uint32_t i = 0; i < caps.corePstateCount && i < 32; ++i)
                if (cfg.corePstatesEnabledMask & (1u << i)) levels += (levels.empty() ? "" : " ") + std::to_string(i);
            if (!levels.empty()) SysfsWriteString(gpu.devicePath + "/pp_dpm_sclk", levels);
        }
        if (cfg.memPstatesEnabledMask != 0) {
            std::string levels;
            for (uint32_t i = 0; i < caps.memPstateCount && i < 32; ++i)
                if (cfg.memPstatesEnabledMask & (1u << i)) levels += (levels.empty() ? "" : " ") + std::to_string(i);
            if (!levels.empty()) SysfsWriteString(gpu.devicePath + "/pp_dpm_mclk", levels);
        }
        if (cfg.powerProfileModeIndex >= 0) {
            SysfsWriteString(gpu.devicePath + "/pp_power_profile_mode", std::to_string(cfg.powerProfileModeIndex));
        }
    }

    if (!ApplyClocks(gpu, cfg.clocks, caps, err)) return false;

    ApplyPmfwOptions(gpu, cfg.pmfw);
    if (cfg.fan.mode != GPUCTL_UNSET) ApplyFanConfig(gpu, cfg.fan);

    if (cfg.powerCapWatts != GPUCTL_UNSET && caps.powerCapMaxW > 0) {
        int32_t w = std::clamp(cfg.powerCapWatts, caps.powerCapMinW, caps.powerCapMaxW);
        long microwatts = (long)w * 1000000;
        if (!SysfsWriteString(gpu.hwmonPath + "/power1_cap", std::to_string(microwatts))) {
            if (err) *err = "failed to write power1_cap";
            return false;
        }
    }

    return true;
}

void AmdResetToDefault(const AmdGpuHandle& gpu) {
    AmdStopFanCurveThread(gpu.hwmonPath);
    WriteOdClkVoltageCmd(gpu.devicePath, "r"); // reset clocks/voltage curve (on SMU13 also the PMFW fan settings)
    WriteOdClkVoltageCmd(gpu.devicePath, "c"); // older generations only stage "r" until committed
    SysfsWriteString(gpu.devicePath + "/power_dpm_force_performance_level", "auto");
    SysfsWriteString(gpu.hwmonPath + "/pwm1_enable", "2"); // auto fan
    long capDef = ParseMicro(SysfsReadTrimmed(gpu.hwmonPath + "/power1_cap_default"));
    if (capDef > 0) SysfsWriteString(gpu.hwmonPath + "/power1_cap", std::to_string(capDef));
}
