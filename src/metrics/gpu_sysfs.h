#pragma once

#include <cstdint>
#include <string>
#include <vector>

// AMD GPU telemetry via /sys/class/drm (amdgpu) + nvidia-smi fallback for
// the non-AMD case. No app-specific types — plain out-params/return values.

void ReadGpuSysfsClocks(float* coreMhzOut, float* vramMhzOut, float* voltageOut);

float ReadAmdGpuFanRpm();
float ReadAmdGpuPowerWatts();
float ReadAmdGpuPowerLimitWatts();

// Real AMD GPU busy % from amdgpu's sysfs node (0..1), or -1 if no card
// exposes it (non-AMD GPU, or no amdgpu-driven card at all). Writes the
// matching card's /sys/class/drm/cardN/device path to *cardPathOut.
float ReadAmdGpuBusyPercent(std::string* cardPathOut);

// nvidia-smi fallback for the common non-AMD case; cached so a machine
// without it doesn't pay a shell-spawn every refresh forever.
bool ReadNvidiaSmi(float* pctOut, double* tempOut);

// Intel iGPU/dGPU busy % (0..1) via the kernel's i915/xe perf PMU, or -1 if
// unavailable (no Intel GPU, or perf_event_open blocked by this system's
// perf_event_paranoid). Cached like ReadNvidiaSmi — one attempt, then reused
// or never retried. Needs two calls ~1s apart to report a real value; the
// call right after (re)start returns -1 while it establishes a baseline.
float ReadIntelGpuBusyPercent();

// ---- AMD overdrive/fan state, read-only (the GPU page's "baseline") -------
// Everything here is user-readable sysfs under /sys/bus/pci/devices/<slot>/,
// so the UI can show the card's *real* current settings without asking the
// root daemon. A field whose node is missing stays at its has*=false/0
// default. Parsers are split out (string in, struct out) so
// tools/gpuctl_check.cpp can test them against captured RDNA3 text.
struct AmdRange { int min = 0, max = 0; };
struct AmdOdState {
    bool valid = false;               // device dir readable at all
    // power (W)
    bool hasPowerCap = false; int powerCapW = 0; AmdRange powerCapRange; int powerCapDefaultW = 0;
    // pp_od_clk_voltage
    bool hasSclk = false; int sclkMin = 0, sclkMax = 0; AmdRange sclkRange;
    bool hasMclk = false; int mclkMin = 0, mclkMax = 0; AmdRange mclkRange;
    bool hasVoltOffset = false; int voltOffsetMv = 0; AmdRange voltOffsetRange;
    // performance
    std::string perfLevel;            // "auto"/"low"/"high"/"manual"/... ("" if unreadable)
    int activeProfile = -1;           // '*' row of pp_power_profile_mode
    std::vector<int> sclkLevelsMhz, mclkLevelsMhz;   // pp_dpm_sclk / pp_dpm_mclk, in index order
    int mclkActiveLevel = -1;                       // '*' row of pp_dpm_mclk
    // PMFW fan (RDNA3+, gpu_od/fan_ctrl)
    bool hasFanCurve = false;
    int fanCurveTemp[16] = {}, fanCurvePct[16] = {}; int fanCurvePoints = 0;
    AmdRange fanCurveTempRange{25, 100}, fanCurvePctRange{15, 100};
    bool hasTargetTemp = false; int targetTempC = 0; AmdRange targetTempRange;
    bool hasAcousticLimit = false; int acousticLimitRpm = 0; AmdRange acousticLimitRange;
    bool hasAcousticTarget = false; int acousticTargetRpm = 0; AmdRange acousticTargetRange;
    bool hasMinPwm = false; int minPwmPct = 0; AmdRange minPwmRange;
    bool hasZeroRpm = false; bool zeroRpm = false;
    bool hasZeroRpmStop = false; int zeroRpmStopC = 0; AmdRange zeroRpmStopRange;
    // hwmon fan + memory
    int pwmEnable = -1; float fanPct = -1.0f; float fanRpm = -1.0f;
    float gttUsedGB = 0.0f, gttTotalGB = 0.0f;
};

AmdOdState ReadAmdOdState(const std::string& pciSlot);

// Pure parsers (exposed for tools/gpuctl_check.cpp).
void ParseOdClkVoltage(const std::string& text, AmdOdState* s);
// One gpu_od/fan_ctrl file ("FAN_TARGET_TEMPERATURE:\n80\nOD_RANGE:\nTARGET_TEMPERATURE: 25 110").
bool ParseFanCtrlValue(const std::string& text, int* value, AmdRange* range);
void ParseFanCurve(const std::string& text, AmdOdState* s);
int ParseActiveProfile(const std::string& text);
std::vector<int> ParseDpmLevels(const std::string& text, int* activeOut = nullptr);
